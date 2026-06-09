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

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <optional>
#include <vector>

#include "swing_window.h"
#include "shot_controller.h"
#include "../Analysis/shot_analyzer.h"
#include "../Export/swing_exporter.h"
#include "../Export/swing_paths.h"

namespace pinpoint { class EventBuffer; }
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
//                ─► pauseBuffer → captureSwingWindow(5 s trailing ring)
//                ─► PROCESSING analysis (ShotAnalyzer) ∥ export (SwingExporter)
//                             — both QtConcurrent workers reading the frozen
//                             window concurrently (const, zero-copy)
//                ─► join      ShotListModel::addShot() always; then
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
    // Replay playhead — the single source of truth both the video tile and the
    // synced metric graph bind to. EventBuffer::nowMicros() domain, == MetricSeries
    // t_us, so no conversion. Position fires per ~60 Hz tick; span/impact on start.
    Q_PROPERTY(qint64  replayPositionUs READ replayPositionUs NOTIFY replayPositionChanged)
    Q_PROPERTY(qint64  replayStartUs    READ replayStartUs    NOTIFY replaySpanChanged)
    Q_PROPERTY(qint64  replayEndUs      READ replayEndUs      NOTIFY replaySpanChanged)
    Q_PROPERTY(qint64  replayImpactUs   READ replayImpactUs   NOTIFY replaySpanChanged)
    // The analyzed swing detail of the shot currently replaying — the ScreenWrist
    // in-replay graph binds to it (same shape as ShotListModel's analysisDetail role).
    Q_PROPERTY(QVariantMap replayAnalysisDetail READ replayAnalysisDetail NOTIFY replayAnalysisDetailChanged)

public:
    enum class State { Idle, PostRoll, Processing, Replaying };

    ShotProcessor(pinpoint::EventBuffer *buffer,
                  CameraManager         *cameraManager,
                  ImuManager            *imuManager,
                  AppSettings           *appSettings,
                  AthleteController     *athleteController,
                  SessionController     *sessionController,
                  ShotListModel         *shotModel,
                  QObject               *parent = nullptr);
    ~ShotProcessor() override;

    QString stateName()   const;
    bool    busy()        const { return m_state != State::Idle; }
    bool    isReplaying() const { return m_state == State::Replaying; }
    qint64  replayPositionUs() const { return m_replayPositionUs; }
    qint64  replayStartUs()    const { return m_replayWindowStartUs; }
    qint64  replayEndUs()      const { return m_replayWindowEndUs; }
    qint64  replayImpactUs()   const { return m_impactUs; }
    QVariantMap replayAnalysisDetail() const { return m_replayAnalysisDetail; }

    // User-initiated skip (ESC). Only meaningful mid-replay: the shot is
    // already on the carousel and saved by the time the replay runs, so
    // cancelling is just the normal end-of-replay path taken early.
    Q_INVOKABLE void cancelReplay();

    // Teardown stop-barrier (camera deselect, destructors). Cancels any pending
    // post-roll, stops replay, BLOCKS until both workers return, destroys the
    // window. Deliberately does not touch buffer state — the caller owns the
    // pause/deregister/intent sequence around source registration.
    void finishNowBlocking();

public slots:
    void onShotDetected(ShotController::Source source, qint64 timestampUs, int sessionType);

signals:
    void stateChanged();
    void busyChanged();
    void isReplayingChanged();
    void replayPositionChanged();
    void replaySpanChanged();
    void replayAnalysisDetailChanged();
    void shotProcessed(const QString &swingDir);   // analysis+export join reached, all ok
    void shotFailed(const QString &error);
    void swingSaved(const QString &path);
    void swingSaveFailed(const QString &error);

private slots:
    void onPostRollExpired();
    void onAnalysisFinished();
    void onSwingSaveFinished();
    void onReplayTick();

private:
    enum class Outcome { Pending, Succeeded, Failed, Skipped };

    struct ReplayTrack {
        CameraInstance                    *ctrl     = nullptr;
        pinpoint::SourceId                 sourceId = pinpoint::kInvalidSourceId;
        std::vector<pinpoint::IndexEntry>  entries;
        size_t                             idx      = 0;
    };

    void setState(State s);
    void captureWindowAndLaunch();
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

    State m_state = State::Idle;

    // Pending shot, captured at trigger time.
    ShotController::Source m_shotSource = ShotController::Source::Manual;
    qint64  m_impactUs    = -1;
    int     m_sessionType = -1;
    QString m_timestampLabel;   // wallclock "hh:mm:ss" at trigger
    QTimer  m_postRollTimer;    // single-shot post-trigger capture continuation

    // Window + replay (migrated from CameraManager).
    std::optional<pinpoint::SwingWindow> m_swingWindow;
    std::vector<ReplayTrack> m_replayTracks;
    int64_t       m_replayWindowStartUs = 0;
    int64_t       m_replayWindowEndUs   = 0;
    int64_t       m_replayPositionUs    = 0;   // published playhead (window µs)
    QVariantMap   m_replayAnalysisDetail;      // detail of the shot being replayed
    QElapsedTimer m_replayElapsed;
    QTimer       *m_replayTimer = nullptr;

    // Workers. Both borrow &*m_swingWindow; the window may only be destroyed
    // once BOTH have returned (and replay has stopped) — see maybeJoin()/
    // finishShot()/finishNowBlocking().
    QFutureWatcher<pinpoint::SwingExportResult> m_swingSaveWatcher;
    QFutureWatcher<ShotAnalysisResult>          m_analysisWatcher;
    bool    m_swingSaveInFlight = false;
    bool    m_analysisInFlight  = false;
    Outcome m_exportOutcome     = Outcome::Pending;
    Outcome m_analysisOutcome   = Outcome::Pending;
    ShotAnalysisResult m_analysisResult;
    QString m_swingDir;          // cached from the export job for the join
    QString m_thumbnailPath;     // from SwingExportResult
    QJsonObject m_exportManifest; // raw manifest from the exporter, for the unified write
    pinpoint::SwingPaths m_swingPaths;   // per-app-run session-folder cache
};
