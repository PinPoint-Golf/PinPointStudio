/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "qml_payload.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QHash>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "swing_window.h"
#include "shot_controller.h"
#include "hm_instance.h"
#include "../Club/club_length_fusion.h"
#include "../Analysis/shot_analyzer.h"
#include "../Analysis/analysis_service.h"   // the one run + the Recording's integrity verdicts (step 1.5)
#include "../Export/swing_exporter.h"
#include "../Export/swing_paths.h"   // SwingPaths::sanitise, for the camera files' names
#include "../Identity/identity.h"      // SessionId: the session swings are written into
#include "../DomainModel/recording_repository.h"   // WriteResult (the allocation's, J-22)
#include "shot_outcome.h"

namespace pinpoint { class Athlete; class EventBuffer; class RecordingRepository; class Session;
                     struct SwingAllocation; }
namespace pinpoint::capture { struct CaptureSnapshot; }
class AppSettings;
class AthleteController;
class CameraInstance;
class CameraManager;
class ImuManager;
class SessionController;
class ShotListModel;

// Orchestrates the post-shot pipeline (QML context property `shotProcessor`):
//
//   shotDetected ─► POSTROLL  per-source delay; the buffer keeps capturing so
//                             the follow-through lands in the ring
//                ─► pauseBuffer → captureSwingWindow(4 s trailing ring)
//                ─► the RECORDING of the shot, read once at the freeze
//                             (capture_context.h: the live facts → the Recording)
//                ─► PROCESSING analysis (the analysis service, on the ONE job
//                             builder) then export (SwingExporter) — QtConcurrent
//                             workers reading the frozen window (const, zero-copy)
//                ─► join      the first write through RecordingRepository::create;
//                             ShotListModel::addShot() always; then
//                ─► REPLAYING ¼× on-screen replay iff analysis AND export
//                             both succeeded
//                ─► finish    window destroyed → applyCaptureIntent() → Idle
//                             (bufferStateChanged re-arms ShotController)
//
// Owns the SwingWindow for its entire lifetime (migrated from CameraManager).
// The window is only valid while the buffer stays Paused: every resume path
// funnels through CameraManager::resumeBuffer(), which is blocked while
// EventBuffer::swingWindowLive() — and the window is destroyed only in
// finishShot() or after the blocking joins in finishNowBlocking().
class ShotProcessor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state       READ stateName   NOTIFY stateChanged)
    Q_PROPERTY(bool    busy        READ busy        NOTIFY busyChanged)
    Q_PROPERTY(bool    isReplaying READ isReplaying NOTIFY isReplayingChanged)
    // Analysis worker progress, 0..1 — drives the toolbar ANALYSING bar.
    // Reset on each shot; reported from the worker via the job's progress
    // callback (queued to the UI thread, throttled to ~1% steps).
    Q_PROPERTY(double analysisProgress READ analysisProgress NOTIFY analysisProgressChanged)
    // Replay playhead — the single source of truth both the video tile and the
    // synced metric graph bind to. Position fires per ~60 Hz tick; span/impact on start.
    // ⚠ THE ONE VIEW-BOUNDARY OFFSET (refactor step 1.3b). The replay runs on the ring clock
    // (EventBuffer::nowMicros(): the frozen window's frames, the pre-stage segmentation), but
    // replayAnalysisDetail is the one projection of the analysis block, in WINDOW time, like the
    // disk replay's. So these four are published in window time — µs from the capture window's
    // start, m_replayT0Us — and the overlays, the graph and the tiles read one domain on both
    // replays. Nothing in QML converts; nothing in C++ behind these getters does either.
    Q_PROPERTY(qint64  replayPositionUs READ replayPositionUs NOTIFY replayPositionChanged)
    Q_PROPERTY(qint64  replayStartUs    READ replayStartUs    NOTIFY replaySpanChanged)
    Q_PROPERTY(qint64  replayEndUs      READ replayEndUs      NOTIFY replaySpanChanged)
    Q_PROPERTY(qint64  replayImpactUs   READ replayImpactUs   NOTIFY replaySpanChanged)
    // The analyzed swing detail of the shot currently replaying — the ScreenWrist
    // in-replay graph binds to it (same shape as ShotListModel's analysisDetail role).
    // ⚠ QJSValue, NOT QVariantMap — ~650k nodes, converted to JavaScript once per shot and
    // handed to every reader by reference. As a QVariantMap it was copied on EVERY read
    // (~18 per shot) and froze the studio's GUI for 7.8 s per shot. See qml_payload.h.
    Q_PROPERTY(QJSValue replayAnalysisDetail READ replayAnalysisDetailJs NOTIFY replayAnalysisDetailChanged)
    // The active session's folder (absolute path), as SessionRepository spells it: the session started
    // or resumed by beginSessionFolder(). It changes only when a session starts or ends — a Start; the
    // library re-pointed mid-session, which ends the session and starts a new one (D7: DEC-154); the
    // first swing of a session whose folder could not be made at Start, which starts it then — and is
    // announced each time. "" when no session is in progress, unless a swing outside a session has had
    // a session started for it on demand, which it then names.
    Q_PROPERTY(QString activeSessionDir READ activeSessionDir NOTIFY activeSessionDirChanged)

public:
    // Gathering sits between PostRoll and Processing — deferred_sources_design.md
    // §4.1. The rings freeze at the SAME instant as before; what moves is when
    // the window is CONSTRUCTED, which now waits for any deferred source to
    // report or time out. busy() is already `m_state != Idle`, so ShotController
    // stays disarmed across it for free.
    enum class State { Idle, PostRoll, Gathering, Processing, Replaying };

    // What the swing-document worker did. The data-integrity checks and the write of swing.json all run
    // OFF the GUI thread (2026-09-16): the document is ~28 MB on a wrist swing and serialising it,
    // indented, to a library on a network share froze the UI at the end of every shot — with the replay
    // on screen, which is where it was most obvious.
    //
    // Nothing is announced until this comes back: the carousel row, the terminal statement and the
    // replay promotion all say what SURVIVES A RESTART, and a shot cannot be called saved before its
    // document is committed. In flight the UI says nothing at all; a failure is surfaced when it lands.
    struct SwingDocWriteResult {
        bool        wrote = false;      // a swing.json was actually committed
        QString     error;              // why not, when !wrote
        QJsonObject manifest;           // the manifest WITH the integrity blocks the checks added
        QVariantMap dataWarningDetail;  // dataWarningDetailFrom(manifest) — empty when nothing warns
        QVariantMap analysisDetail;     // the shot's detail: liveAnalysisDetail (swing_doc.h), the one
                                        // projection of the analysis block this document gets — window time
        QString     club;               // the club the document records (the seeded review.club)
        QString     documentPath;       // where it went (the log)
    };

    ShotProcessor(pinpoint::EventBuffer *buffer,
                  CameraManager         *cameraManager,
                  ImuManager            *imuManager,
                  AppSettings           *appSettings,
                  AthleteController     *athleteController,
                  SessionController     *sessionController,
                  ShotListModel         *shotModel,
                  QObject               *parent = nullptr);
    ~ShotProcessor() override;

    // THE ONE DOOR to the swing file (RecordingRepository, step 1.4): the live join's first write
    // goes through it (create). Held by the composition root (main.cpp). Without one, a shot's
    // document is not written and the join says so.
    void setRecordingRepository(pinpoint::RecordingRepository *repository) { m_repository = repository; }
    // THE SESSION CAPTURE WRITES INTO (refactor step 3.4a-ii; BRIEF §4.8): a Session of the current
    // Athlete, started, resumed and ended through the Athlete's and the Session's commands (DEC-78,
    // DEC-134, DEC-143), every shot allocated by Session::addShot (DEC-142), each shot's first document
    // reported to it (EVT-01's moment: D19, DEC-168). WITH NO GOLFER CHOSEN — or a library re-pointed mid-session
    // that does not list the golfer (D7; J-67) — the session is the library's UNASSIGNED OWNER's, a full Session like
    // any other (information_model.md §5.11; DEC-198; step 5.1 replaced N1's repository path). `unassigned(create,
    // result)` hands it over (the composition root's callable over the athlete repository; `create` writes its record
    // at the first need: J-67). Without it, and with no golfer, no session starts and no swing is saved. Also follows
    // the library setting (D7).
    using UnassignedAthlete = std::function<pinpoint::Athlete *(bool create, pinpoint::WriteResult *result)>;
    void setUnassignedAthlete(UnassignedAthlete unassigned);
    // A DEVICE-ONLY SHOT in the session a swing captured NOW is written into (the launch monitor's
    // standalone shot: one allocator, D10: DEC-157): Session::addShot of the running session — started
    // when a session is in progress whose folder could not be made at Start, or, outside a session, on
    // demand (today's behaviour). The allocation's ShotRef (null when none could be made), its folder and
    // names in `allocation`.
    pinpoint::ShotRef addDeviceOnlyShot(pinpoint::SwingAllocation *allocation);
    // A shot's first document has landed (EVT-01's moment, D19): the launch monitor's device-only write.
    // Its Session announces it (Session::shotDocumentWritten); nothing for a shot it does not hold.
    void shotDocumentWritten(const pinpoint::ShotRef &shot);

    QString stateName()   const;
    bool    busy()        const { return m_state != State::Idle; }
    bool    isReplaying() const { return m_state == State::Replaying; }
    qint64  replayPositionUs() const { return m_replayPositionUs    - m_replayT0Us; }   // window time
    qint64  replayStartUs()    const { return m_replayWindowStartUs - m_replayT0Us; }
    qint64  replayEndUs()      const { return m_replayWindowEndUs   - m_replayT0Us; }
    qint64  replayImpactUs()   const { return m_impactUs            - m_replayT0Us; }
    QVariantMap replayAnalysisDetail()   const { return m_replayAnalysisDetail.map(); }   // C++ readers
    QJSValue    replayAnalysisDetailJs() const { return m_replayAnalysisDetail.js(); } // the QML property
    double  analysisProgress() const { return m_analysisProgress; }
    QString activeSessionDir() const { return m_openSessionDir; }
    // False when beginSessionFolder() could not create the session folder — the
    // state a session used to start into silently.  Read by the session-start
    // preflight in main.cpp.
    bool    sessionFolderReady() const { return m_sessionFolderReady; }
    // The folder allocated for the shot being processed, or the last one processed.
    // Set BEFORE the export runs, so it survives a shot that produced no document at
    // all — which is exactly when a launch monitor reading has somewhere to go and the
    // rest of the pipeline does not. Empty when no shot has been processed.
    QString lastSwingDir() const { return m_swingDir; }
    // The carousel row id the last processed shot was given. addShot() runs even when
    // everything else failed, so a shot that produced no document still HAS a row —
    // in-memory, with no swingDir. A launch monitor filling that folder must point that
    // row at it rather than adding a second one. -1 when no shot has been processed.
    int lastShotId() const { return m_lastShotId; }

    // User-initiated skip (ESC). Only meaningful mid-replay: the shot is
    // already on the carousel and saved by the time the replay runs, so
    // cancelling is just the normal end-of-replay path taken early.
    Q_INVOKABLE void cancelReplay();

    // Teardown stop-barrier (camera deselect, destructors). Cancels any pending
    // post-roll, stops replay, BLOCKS until both workers return, destroys the
    // window. Deliberately does not touch buffer state — the caller owns the
    // pause/deregister/intent sequence around source registration.
    void finishNowBlocking();

    // ── Session folder lifecycle (QML) ──────────────────────────────────────
    // The wrist session screen decides the on-disk session folder up front (at
    // Capture / wizard start) rather than lazily at first save, so it can offer
    // "extend today vs new" and discard a session that captured nothing.

    // Most-recent existing session folder for TODAY (this athlete + session type),
    // or "" when none. Drives the Extend/New prompt and the today-scoped carousel.
    Q_INVOKABLE QString todaySessionDir(int sessionType);
    // Begin the session's folder: extend today's most-recent one, or allocate a
    // fresh "_NN". Every swing captured this session lands in it.
    Q_INVOKABLE void beginSessionFolder(int sessionType, bool extend);
    // End the session's folder: discard it (to the OS trash) when it captured no
    // new swings since beginSessionFolder(). Safe with no session in progress.
    Q_INVOKABLE void endSessionFolder();
    // The pending declared focus handed to the open session (refactor 4-d; AppState::handOverFocus's door): its
    // Session's own command (EVT-10), from the first shot, or its file by id for a session no Athlete holds. True only
    // when a session took it; false with none open (it stays pending) or a failed write (logged).
    bool declareStartFocus(const QString &conditionId);

public slots:
    void onShotDetected(ShotController::Source source, qint64 timestampUs, int sessionType);

signals:
    void stateChanged();
    void busyChanged();
    void isReplayingChanged();
    void analysisProgressChanged();
    void replayPositionChanged();
    void replaySpanChanged();
    void replayAnalysisDetailChanged();
    void activeSessionDirChanged();
    // analysis+export join reached, all ok — carries the reviewable on-disk swing
    // (swingDir) and its carousel row id so the UI can promote it straight into Review.
    void shotProcessed(int shotId, const QString &swingDir);
    // NOT a domain event (refactor 3.7-I): the shot's Session has just announced EVT-01 (Session::shotAdded) for a
    // shot this processor reported. A relay for the app log only: main.cpp writes a [GpuMem] line on it
    // (ProfilerController::logGpuMem). Nothing recomputes on it; nothing else may connect to it.
    void shotDocumentAnnounced(const pinpoint::ShotRef &shot);
    void shotFailed(const QString &error);
    void swingSaved(const QString &path);
    void swingSaveFailed(const QString &error);
    // ⚠ A CONDITION, NOT AN EVENT, AND A DIFFERENT FAULT FROM THE ABOVE.
    // swingSaveFailed says one export failed; this says swings are not being
    // saved AT ALL, because the swing folder could not be created — a state of
    // the machine that does not stop being true when a timer expires, and that
    // every subsequent shot will hit again.  They used to share one signal and
    // one toast, which is why ten occurrences of this on 1 September 2026 read
    // as one piece of news.
    void swingSaveBlocked(const QString &detail);
    // The session's own file (session.json) could not be written at End, or at D7's end of the old
    // session while its folder is still there (DEC-126): the session stays in progress on disk until the
    // athlete's next Start. Posted by main.cpp as a notice.
    void sessionFileFailed(const QString &detail);
    // "Continue today's session" was asked for and refused or could not be written (DEC-126): a new
    // session started instead. Posted by main.cpp as a notice.
    void sessionResumeFailed(const QString &detail);
    // D7 (DEC-154; refactor 4-d): the ended session's declared focus, carried to the restart. Emitted after its End and
    // before the new start; main.cpp makes it SessionController's pending pick, which AppState::handOverFocus hands to
    // the first Session that opens (the restart's, or the next swing's when the restart fails).
    void focusCarried(const QString &conditionId);
    // Analyzer returned !ok — the shot degrades to no-score/no-metrics but the
    // pipeline continues. Surfaced as a window-level toast (Main.qml), not a
    // log line: the user must see why a shot has no analysis.
    void analysisFailed(const QString &error);
    // §7.5 R4 — what the shot BECAME, once, at the join.  `ordinal` is the
    // carousel row's 1-based number (0 when it never reached the carousel).
    // The three stage signals above stay, because the log still wants them;
    // this is the one the user is told.
    void shotOutcome(int ordinal, bool exportOk, bool analysisOk);

private slots:
    void onPostRollExpired();
    void onSegmentationFinished();
    void onAnalysisFinished();
    void onSwingSaveFinished();
    // The swing document has been written (or failed to be). Everything the shot is announced as —
    // the carousel row, shotProcessed/shotFailed, the outcome toast, the replay promotion — happens
    // here, because until this returns none of it is true yet.
    void onSwingDocWritten();
    void onReplayTick();

private:
    // The per-stage result vocabulary lives in shot_outcome.h alongside the
    // join rules that read it, so the two cannot drift.
    using Outcome = pinpoint::StageOutcome;

    // Per-camera ball-detector state frozen at window-capture time. Both job
    // builders run 12–37 s after impact (from onAnalysisFinished), by when the
    // live CameraInstance accumulator has scrolled to post-shot junk (ball
    // detection never stops) and a phantom re-launch — the struck ball rolling —
    // may have overwritten ballLaunchInfo(). So we snapshot the instant the
    // SwingWindow is frozen, when the 6 s accumulator still fully covers the 4 s
    // window; both builders read this, never live controller state. Samples
    // mirror CameraInstance::BallAccumSample (POD, avoids a header dependency).
    struct BallSnapshot {
        struct Sample { qint64 tUs = 0; bool found = false; float x = 0.f, y = 0.f, r = 0.f, conf = 0.f; };
        std::vector<Sample> samples;
        bool   hasLaunch = false;
        qint64 launchTUs = -1;
        double launchX = 0.0, launchY = 0.0;
    };

    struct ReplayTrack {
        CameraInstance                    *ctrl     = nullptr;
        pinpoint::SourceId                 sourceId = pinpoint::kInvalidSourceId;
        std::vector<pinpoint::IndexEntry>  entries;
        size_t                             idx      = 0;
        BallSnapshot                       ball;   // frozen at window capture
        // The impact camera (impact_camera_design.md §10.2): its entries are
        // the keep band around impact, and in the post-shot replay it LOOPS
        // on its own clock rather than following the playhead — phased so
        // its impact frame shows as the playhead crosses impact.
        bool                               loop     = false;
    };

    int  m_lastShotId = -1;

    void setState(State s);
    void setAnalysisProgress(double p);
    // The two halves of what captureWindowAndLaunch() used to do in one breath.
    // Every deferred source bound to this capture. One wG3 is ONE peripheral
    // with one reservation, even though it feeds two lanes.
    QVector<HmInstance *> deferredSources() const;
    void beginGather();             // pause the rings, raise the guard, start the wait
    void onGatherPoll();            // deferred sources reported, or the deadline passed
    void finishGatherAndLaunch();   // compose the backing, construct the window, take the Recording, launch
    void startAnalysis();
    void startSwingSave();
    pinpoint::SwingExportJob buildSwingExportJob();
    void maybeJoin();
    void startReplay();
    void stopReplay(bool thenFinish);
    void finishShot();
    void abortToIdle();

    // Dependencies — UI-thread only; workers see value-type jobs.
    pinpoint::EventBuffer *m_buffer        = nullptr;
    CameraManager         *m_cameraManager = nullptr;
    ImuManager            *m_imuManager    = nullptr;
    AppSettings           *m_appSettings   = nullptr;
    AthleteController     *m_athlete       = nullptr;
    SessionController     *m_session       = nullptr;
    ShotListModel         *m_shotModel     = nullptr;

    State  m_state = State::Idle;
    double m_analysisProgress = 0.0;

    // Pending shot, captured at trigger time.
    ShotController::Source m_shotSource = ShotController::Source::Manual;
    qint64  m_impactUs    = -1;
    int     m_sessionType = -1;
    bool    m_sessionFolderReady = false;
    QString m_timestampLabel;   // wallclock "hh:mm:ss" at trigger
    QTimer  m_postRollTimer;    // single-shot post-trigger capture continuation

    // ── Deferred gather (design §4.1) ───────────────────────────────────────
    // ⚠ m_ringSource HOLDS THE RESUME GUARD from the pause instant until the
    // window that adopts it dies. It is built at pause rather than at window
    // construction because `resume_clear_rings` is true, so a resume landing in
    // between would CLEAR THE RINGS THIS SHOT IS ABOUT TO SNAPSHOT (§3.5). Any
    // path that abandons a gather must reset it, or the buffer can never resume.
    std::unique_ptr<const pinpoint::SwingPayloadSource> m_ringSource;
    QTimer  m_gatherTimer;          // polls the deferred sources during Gathering

    // ⏱ THE UI-THREAD STALL WATCHDOG (2026-09-16). A repeating timer on the GUI thread: if a tick
    // arrives late, the thread was blocked for the difference, and the log says so with a timestamp.
    //
    // It exists because three rounds of "this code looks expensive" were wrong — the pose rebuilds it
    // blamed measure 64 ms together on the real 28 MB document, nowhere near the freeze Mark sees. This
    // measures the SYMPTOM instead of a suspect: whatever blocks the thread, the stall is recorded, and
    // the surrounding log lines say what was happening at that moment.
    QTimer        m_uiStallTimer;
    QElapsedTimer m_uiStallClock;
    qint64        m_uiStallLastTickMs = 0;
    qint64  m_gatherDeadlineMs = 0; // QDateTime msecs; the wait is bounded
    // ⚠ FROZEN AT THE PAUSE INSTANT, NOT READ AFTER THE GATHER. The window is a
    // TRAILING span, so computing it from a post-gather `now` would slide it
    // several seconds forward — past the swing it is supposed to contain — and
    // the snapshot would come back empty with nothing reporting an error.
    int64_t m_windowStartUs = 0;
    int64_t m_windowEndUs   = 0;
    QDateTime m_windowEndUtc;   // m_windowEndUs on the wall clock, taken in the same breath
    // What each pull actually delivered, keyed by device id — coverage, gaps,
    // the fit and the overlap counters. Read by the exporter into swing.json's
    // per-stream provenance, so a swing says a year later what it was built from.
    QHash<QString, HmInstance::HistoryResult> m_historyProvenance;
    // Per stitched lane: {live samples used, deferred samples used}. Keyed by
    // source because the two units of one peripheral stitch independently — the
    // palm can come back denser than the lower arm.
    QHash<pinpoint::SourceId, std::pair<int, int>> m_stitchCounts;

    // Window + replay (migrated from CameraManager).
    std::optional<pinpoint::SwingWindow> m_swingWindow;
    std::vector<ReplayTrack> m_replayTracks;
    int64_t       m_replayWindowStartUs = 0;
    int64_t       m_replayWindowEndUs   = 0;
    int64_t       m_replayPositionUs    = 0;   // the playhead, ring clock (published minus m_replayT0Us)
    // The capture window's start on the ring clock: the one view-boundary offset between the replay's
    // ring-clock playhead and the window-time detail (see the replay Q_PROPERTYs). Set by startReplay.
    int64_t       m_replayT0Us          = 0;
    QmlPayload    m_replayAnalysisDetail;      // detail of the shot being replayed (see qml_payload.h)
    QElapsedTimer m_replayElapsed;
    QTimer       *m_replayTimer = nullptr;

    // Segmentation pre-stage (v3 G2): a cheap fuse+segment pass on the frozen
    // window whose future GATES both heavy workers — its swing bounds trim the
    // export encode span and the replay, and ride the jobs as values. Borrows
    // &*m_swingWindow like the workers, so finishNowBlocking() must join it.
    QFutureWatcher<pinpoint::analysis::Segmentation> m_segmentationWatcher;
    bool                            m_segmentationInFlight = false;
    pinpoint::analysis::Segmentation m_segmentation;   // result for THIS shot
    // THE RECORDING of this shot (capture_context.h): made once at the freeze from the live facts,
    // the athlete and session read once with it. The analysis job, the export's athlete and club,
    // the seeded review club and the integrity verdicts all come from it. Its window points into
    // m_swingWindow, so it is reset with it (finishShot / finishNowBlocking).
    std::unique_ptr<pinpoint::capture::CaptureSnapshot> m_capture;
    // The job the ONE builder made from that Recording (buildAnalysisJob): the pre-stage reads its
    // bindings; the analysis service runs it.
    ShotAnalysisJob                 m_analysisJob;
    pinpoint::analysis::AnalysisService m_analysisService;   // stateless (analysis_service.h)
    pinpoint::RecordingRepository  *m_repository = nullptr;  // the one door (setRecordingRepository)

    // Persistent club-length prior (club_length_fusion.h / plan: robust club
    // length — starry-shimmying-wind). Stashed from the capture snapshot when the
    // face-on camera is cameraFixedInPlace (else left empty/cold — nothing to
    // update); onAnalysisFinished() folds the recorded fuse's fusedInstant* into
    // m_lengthPriorState and persists it to AppSettings under m_lengthPriorKey.
    // Cleared at the start of every shot alongside m_analysisResult.
    QString                            m_lengthPriorKey;
    pinpoint::analysis::LengthPriorState m_lengthPriorState;

    // Workers. Both borrow &*m_swingWindow; the window may only be destroyed
    // once BOTH have returned (and replay has stopped) — see maybeJoin()/
    // finishShot()/finishNowBlocking().
    QFutureWatcher<pinpoint::SwingExportResult> m_swingSaveWatcher;
    QFutureWatcher<pinpoint::analysis::AnalysedRecording> m_analysisWatcher;
    bool    m_swingSaveInFlight = false;
    bool    m_analysisInFlight  = false;
    // The document writer (SwingDocWriteResult). It borrows &*m_swingWindow for the integrity checks
    // and m_analysisResult.detail for the write, so — like the two above — finishNowBlocking() must
    // join it, and the processor stays in State::Processing until it returns, which is what keeps both
    // of those alive (a new shot clears them, and it cannot start from Processing).
    QFutureWatcher<SwingDocWriteResult> m_docWriteWatcher;
    bool    m_docWriteInFlight = false;
    Outcome m_exportOutcome     = Outcome::Pending;
    Outcome m_analysisOutcome   = Outcome::Pending;
    // Corpus capture: saveRawFrames && skipAnalysisForRawCapture, resolved per shot
    // at window capture. When set, the pipeline exports frames only — no analysis,
    // no segmentation, no on-screen replay — leaving the swing to be re-analysed
    // later from the Shots view.
    bool    m_skipAnalysisCapture = false;
    ShotAnalysisResult m_analysisResult;
    // The Recording's integrity verdicts, as the analysis service reached them with the analysis
    // (integrityFor). Unset when no analysis ran (a skip-analysis corpus capture): the document
    // writer then asks the service for them alone.
    std::optional<pinpoint::analysis::IntegrityVerdicts> m_integrity;
    QString m_swingDir;          // cached from the export job for the join
    pinpoint::ShotRef   m_shot;          // the allocation's ShotRef: the join's write is keyed by it (step 2.4)
    pinpoint::WriteResult m_allocResult;   // why the allocation failed, when it did (J-22: a held lock's words)
    pinpoint::SwingExportJob m_exportJob;  // cached job — synth header on export fail/skip
    QString m_thumbnailPath;     // from SwingExportResult
    QJsonObject m_exportManifest; // raw manifest from the exporter, for the unified write

    // ── The session swings are written into (SessionRepository's; step 2.2) ──
    // Its folder is fixed for its life: never re-resolved per shot (E-01). A rename, midnight or a
    // changed naming pattern or type keep it; a re-pointed library ends it and starts another (D7).
    void setOpenSession(pinpoint::Session *session);          // an Athlete's Session (or none)
    void onLibraryChanged();
    // The session a swing captured NOW is written into (D10): started when a session is in progress
    // whose folder could not be made at Start, or, outside a session, on demand; null when none can be.
    pinpoint::SessionId captureSession();
    // The Athlete capture starts its sessions through: the current one, when its sessions live in the
    // library capture writes to (SET-03); else the library's Unassigned owner (§5.11; J-67), its record written first
    // when `create` (a Start) and it has none yet. Null when neither can be had (`*result` says why).
    pinpoint::Athlete *captureAthlete(bool create, pinpoint::WriteResult *result = nullptr) const;
    // A new session of `type`: through captureAthlete() (DEC-78). False when none started.
    bool startOpenSession(const QString &type, bool onDemand, pinpoint::WriteResult *result = nullptr);
    // The open session ended (null after). `golferEnd`: the End (the empty discard may take it, DEC-93,
    // DEC-143); else an end before another start (never discards, as today).
    pinpoint::WriteResult endOpenSession(bool golferEnd);
    UnassignedAthlete            m_unassigned;
    QPointer<pinpoint::Session>  m_openSessionObject;   // the Athlete's Session capture writes into; null: none
    QPointer<pinpoint::Session>  m_shotSession;         // the Session the shot being processed was added to
    pinpoint::SessionId m_openSession;     // null: none
    QString m_openSessionDir;              // its folder, as the repository spells it
    QString m_openLibraryKey;              // the library (cleaned) it was started in, for D7
    bool    m_sessionBegun = false;        // beginSessionFolder() .. endSessionFolder()
    bool    m_openedOnDemand = false;      // started for a swing outside a session: never discarded
    QString m_onDemandKey;                 // athlete | library | day | type of that session
};
