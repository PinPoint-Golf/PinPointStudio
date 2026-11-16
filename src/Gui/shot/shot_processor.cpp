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

#include "shot_processor.h"

#include "app_settings.h"
#include "camera_instance.h"
#include "capture_context.h"   // the live facts → the Recording, and its first document (step 1.5)
#include "camera_manager.h"
#include "device_enumerator.h"
#include "composite_payload_source.h"
#include "deferred_stitch.h"
#include "event_buffer.h"
#include "hm_instance.h"
#include "ram_payload_source.h"
#include "imu_instance.h"
#include "imu_manager.h"
#include "shot_list_model.h"
#include "shot_outcome.h"
#include "../Club/club_length_fusion.h"
#include "../IMU/imu_vision_fuser.h"
#include "../Timeline/phase_segmenter.h"
#include "../Analysis/swing_analysis.h"
#include "../Analysis/analysis_job_builder.h"   // the ONE job builder
#include "../Core/pp_debug.h"
#include "../Core/pp_os_metrics.h"
#include "../DomainModel/athlete.h"            // the current Athlete, whose sessions capture writes into
#include "../DomainModel/session.h"                // the Session capture writes into (step 3.4a-ii)
#include "../DomainModel/session_repository.h"   // the interim: a session no Athlete holds (NOTES-3.4a-ii N1)
#include "../Recording/recording.h"               // recording::ShotKind (Session::addShot)
#include "pp_version.h"

#include <QSysInfo>

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTime>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <variant>

namespace {

// Post-trigger capture continuation, per source — the buffer keeps capturing
// for this long after the trigger so the follow-through lands in the ring
// before it freezes. Auto detectors use 1250 ms (segmentation v3: the finish
// is follow-through ~0.67 s + decay-to-still, physically truncated at 500 ms
// — design A.6; impact then sits ~2.75 s into the 4 s ring, leaving ample
// pre-impact room). Manual stays 500 ms: the user presses after the swing and
// impact is back-dated, so the finish is already in the ring.
constexpr int kPostRollManualMs   = 500;
constexpr int kPostRollImuMs      = 1250;
constexpr int kPostRollPoseMs     = 1250;
constexpr int kPostRollBallMs     = 1250;
constexpr int kPostRollAcousticMs = 1250;

// ── The impact camera's keep band (impact_camera_design.md §10.2) ───────────
//
// The club crosses a 640 mm field in ~18 ms and the ball is gone in ~10 ms, so
// the frames that contain either sit within ~40 ms before and ~30 ms after
// impact. The band is wider than that for the anchor, not the physics: the
// arbiter's impactUs runs a uniform 13–22 ms EARLY against video truth
// (impact_geom.h), and on 3 of 14 truth swings the nearest-frame mapping was
// >230 ms late. 200 ms before / 100 ms after is ~180 frames at 591 fps — a
// 6 s clip in the 30 fps container instead of 79 s. If the band catches no
// frame at all (a gross anchor miss, or the lane simply not covering impact),
// it widens to ±500 ms; if THAT is empty the export keeps the whole lane and
// says so, rather than silently shipping a clip with no impact in it.
constexpr int64_t kImpactKeepBeforeUs   = 200'000;
constexpr int64_t kImpactKeepAfterUs    = 100'000;
constexpr int64_t kImpactKeepFallbackUs = 500'000;
// The clip's own encoder quality (x264 CRF; lower is better, 0 lossless):
// the library's "medium" 23 turned the 2026-09-15 clips, dark at 70 µs, into
// 16-px macroblocks at ~137 kbps. 12 is visually lossless and the clip is
// ~180 frames of 640×240, so it costs a few MB. A lossless library setting
// stays lossless (min).
constexpr int     kImpactClipCrf        = 12;

// ── Deferred gather (deferred_sources_design.md §4.1, brief Phase E) ────────
//
// A pull takes about as long as its window spans and the library serialises
// them, so a 3 s pre / 1.5 s post request costs ~4.5 s — and it cannot start
// until the window's last sample exists, at impact + 1.5 s. Reserving at
// DETECTION hides most of that inside the post-roll we were taking anyway.
//
// ⚠ THE TWO DEADLINES ARE ORDERED ON PURPOSE. The library's deadline sits just
// INSIDE ours, so a slow pull materialises its own block — carrying whatever
// arrived plus its coverage — rather than being cancelled by our timeout. A
// cancelled request still produces a block, but one that says CANCELLED where
// the honest answer was TIMED_OUT with 60 % coverage.
constexpr qint64 kHistoryDeadlineUs = 7'000'000;   // past impact: library gives up here
constexpr int    kGatherDeadlineMs  = 6'000;       // past pause:  we give up here
constexpr int    kGatherPollMs      = 50;

// The subset of a snapshot belonging to one source, ascending — the live half
// of a stitch. Cheap: this runs once per deferred lane per shot.
std::vector<pinpoint::IndexEntry> entriesForIn(
    const std::vector<pinpoint::IndexEntry> &all, pinpoint::SourceId id)
{
    std::vector<pinpoint::IndexEntry> out;
    for (const pinpoint::IndexEntry &e : all)
        if (e.source_id == id) out.push_back(e);
    return out;
}

int postRollMsFor(ShotController::Source s)
{
    switch (s) {
    case ShotController::Source::Manual:   return kPostRollManualMs;
    case ShotController::Source::Imu:      return kPostRollImuMs;
    case ShotController::Source::Pose:     return kPostRollPoseMs;
    case ShotController::Source::Ball:     return kPostRollBallMs;
    case ShotController::Source::Acoustic: return kPostRollAcousticMs;
    // ⚠ MANUAL'S VALUE, AND ON PURPOSE.  The post-roll exists to let a
    // back-dated estimate settle before the ring is frozen; a PPCP `t0` has
    // already waited out the arbiter's whole `issue_hold_ns` on the wire, plus
    // link latency and up to one pump tick, before it ever reaches this
    // process.  Adding the acoustic detector's 1250 ms on top of that would
    // push impact further from the window's trailing edge and eat the
    // pre-impact reach the analyzer needs, for no settling that has not already
    // happened.  Manual is the short value for exactly the same reason: it is
    // the one other Source whose instant is fully back-dated on arrival.
    case ShotController::Source::Ppcp:     return kPostRollManualMs;
    }
    return kPostRollManualMs;
}

// The entire trailing ring — every source's window_duration. Trimmed 5 s → 4 s
// (2026-07-19): the window is anchored at the pause instant, so it only reaches
// back from impact — post-impact room (~1.5 s: hold + back-date + post-roll) is
// unaffected and shrinking only trims pre-impact reach. The analyzer never reads
// further back than impact − 1.75 s (onset clamp bsMaxBeforeImpactUs = 1.6 s +
// 150 ms fill pad, shaft_track_assembly.h), so the floor is set by that clamp,
// not by tempo. 4 s leaves ~2.45 s pre-impact — ~0.7 s of margin over the floor —
// while cutting a fifth off the frozen window: ~190 MB of raw frame copy per
// camera at export and 20 % of the x264 encode. The per-source ring retention
// (SourceDescriptor::window_duration, still 5 s) is deliberately left larger so
// the captured window keeps drain/post-roll headroom inside the ring.
constexpr std::chrono::milliseconds kWindowDuration{4000};

// IMU roles come from ImuManager's role resolver (roleForDevice / instanceForRole) and
// are converted with segmentRoleForPlacementRole (segment_role.h). segmentRoleForSlot
// is deliberately NOT used here any more — it survives only for reading old swings.

// The session type's name for the session file and its folder's label: sessionTypeName(), beside
// the session lifecycle below (the label is SessionStore::folderTypeLabel's, step 2.2).

} // namespace

ShotProcessor::ShotProcessor(pinpoint::EventBuffer *buffer,
                             CameraManager         *cameraManager,
                             ImuManager            *imuManager,
                             AppSettings           *appSettings,
                             AthleteController     *athleteController,
                             SessionController     *sessionController,
                             ShotListModel         *shotModel,
                             QObject               *parent)
    : QObject(parent)
    , m_buffer(buffer)
    , m_cameraManager(cameraManager)
    , m_imuManager(imuManager)
    , m_appSettings(appSettings)
    , m_athlete(athleteController)
    , m_session(sessionController)
    , m_shotModel(shotModel)
{
    m_postRollTimer.setSingleShot(true);
    connect(&m_postRollTimer, &QTimer::timeout, this, &ShotProcessor::onPostRollExpired);
    // Repeating, not single-shot: the gather polls until every deferred source
    // has reported or the deadline passes.
    m_gatherTimer.setSingleShot(false);
    connect(&m_gatherTimer, &QTimer::timeout, this, &ShotProcessor::onGatherPoll);

    // Worker completion is delivered on this (UI) thread, strictly after the
    // worker lambda has returned — the join in maybeJoin()/finishShot() relies
    // on that ordering to destroy the SwingWindow safely.
    connect(&m_swingSaveWatcher, &QFutureWatcher<pinpoint::SwingExportResult>::finished,
            this, &ShotProcessor::onSwingSaveFinished);
    connect(&m_analysisWatcher, &QFutureWatcher<ShotAnalysisResult>::finished,
            this, &ShotProcessor::onAnalysisFinished);
    connect(&m_segmentationWatcher,
            &QFutureWatcher<pinpoint::analysis::Segmentation>::finished,
            this, &ShotProcessor::onSegmentationFinished);
    connect(&m_docWriteWatcher, &QFutureWatcher<SwingDocWriteResult>::finished,
            this, &ShotProcessor::onSwingDocWritten);

    // The UI-thread stall watchdog — see m_uiStallTimer. 50 ms ticks; anything later than 250 ms is a
    // stall the user can see (the video stops), so it is logged with how long the thread was gone.
    // Runs for the whole session, not just around a shot: the point is to catch it wherever it happens.
    m_uiStallClock.start();
    m_uiStallLastTickMs = m_uiStallClock.elapsed();
    m_uiStallTimer.setSingleShot(false);
    m_uiStallTimer.setInterval(50);
    connect(&m_uiStallTimer, &QTimer::timeout, this, [this]() {
        const qint64 now  = m_uiStallClock.elapsed();
        const qint64 gap  = now - m_uiStallLastTickMs;
        m_uiStallLastTickMs = now;
        if (gap >= 250)
            ppWarn() << "[UiStall] the GUI thread was blocked for" << gap
                     << "ms — state" << stateName();
    });
    m_uiStallTimer.start();
}

ShotProcessor::~ShotProcessor()
{
    // main.cpp declares the processor after CameraManager, so this runs first
    // in the stack unwind: join the workers and destroy the window before
    // ~CameraManager deregisters sources and ~EventBuffer frees ring memory.
    finishNowBlocking();
    if (m_cameraManager)
        m_cameraManager->setShotProcessor(nullptr);
    if (m_imuManager)
        m_imuManager->setShotProcessor(nullptr);
}

QString ShotProcessor::stateName() const
{
    switch (m_state) {
    case State::Idle:       return QStringLiteral("idle");
    case State::PostRoll:   return QStringLiteral("postroll");
    case State::Gathering:  return QStringLiteral("gathering");
    case State::Processing: return QStringLiteral("processing");
    case State::Replaying:  return QStringLiteral("replaying");
    }
    return QStringLiteral("unknown");
}

void ShotProcessor::setState(State s)
{
    if (m_state == s)
        return;
    const bool busyBefore   = busy();
    const bool replayBefore = isReplaying();
    m_state = s;
    emit stateChanged();
    if (busyBefore != busy())
        emit busyChanged();
    if (replayBefore != isReplaying())
        emit isReplayingChanged();
}

void ShotProcessor::setAnalysisProgress(double p)
{
    // Monotonic except for the explicit per-shot reset to 0: queued worker
    // updates can land after the completion 1.0 and must not drag the bar back.
    if (p == m_analysisProgress || (p < m_analysisProgress && p != 0.0))
        return;
    m_analysisProgress = p;
    emit analysisProgressChanged();
}

// ---------------------------------------------------------------------------
// Trigger → post-roll
// ---------------------------------------------------------------------------

void ShotProcessor::onShotDetected(ShotController::Source source,
                                   qint64 timestampUs, int sessionType)
{
    // ShotController's busy gate refuses triggers while we are non-Idle; this
    // guard is the belt-and-braces backstop.
    if (m_state != State::Idle) {
        ppWarn() << "[ShotProcessor] shot ignored — already processing (state"
                 << stateName() << ")";
        return;
    }
    if (!m_buffer || !m_buffer->isCapturing()) {
        ppWarn() << "[ShotProcessor] shot ignored — buffer not capturing";
        return;
    }

    m_shotSource     = source;
    m_impactUs       = timestampUs;
    m_sessionType    = sessionType;
    m_timestampLabel = QTime::currentTime().toString(QStringLiteral("hh:mm:ss"));

    // ⚠ RESERVE AT DETECTION, COLLECT AT THE GATHER (history.h C1). Retrieval
    // cannot begin until the window's last sample exists, and it then costs
    // about as long as the window spans — so telling the deferred sources NOW
    // what range we are going to want lets that cost hide inside the post-roll
    // instead of being added after it. Reserving does not block and issues no
    // radio traffic. A refusal is recorded by the source and changes nothing
    // here: a shot with no deferred data is the ordinary path, not an error.
    for (HmInstance *hm : deferredSources())
        hm->reserveHistory(timestampUs, timestampUs + kHistoryDeadlineUs);

    setAnalysisProgress(0.0);   // the ANALYSING bar starts empty for each shot
    setState(State::PostRoll);
    m_postRollTimer.start(postRollMsFor(source));
}

void ShotProcessor::onPostRollExpired()
{
    if (m_state != State::PostRoll)
        return;   // finishNowBlocking() raced the timer
    beginGather();
}

// ---------------------------------------------------------------------------
// Window capture → concurrent analysis + export
// ---------------------------------------------------------------------------

QVector<HmInstance *> ShotProcessor::deferredSources() const
{
    QVector<HmInstance *> out;
    if (!m_imuManager) return out;
    for (const QVariant &v : m_imuManager->instances())
        if (auto *hm = qobject_cast<HmInstance *>(v.value<QObject *>()))
            out.push_back(hm);
    return out;
}

void ShotProcessor::beginGather()
{
    // The user may have pressed Stop during the post-roll: the rings froze at
    // the pause instant, truncating the follow-through there — still a valid
    // shot, so proceed from Paused. Only buffer teardown aborts. The capture
    // check then sees a clip ending before the window does (earlyEnd) and the
    // shot carries a warning, but it is still assessed; only a clip with no
    // frame before impact is withheld (capture_integrity_check.h).
    m_cameraManager->pauseBuffer();
    if (!m_buffer || m_buffer->state() != pinpoint::BufferState::Paused) {
        ppWarn() << "[ShotProcessor] shot aborted — buffer unavailable at post-roll expiry";
        abortToIdle();
        return;
    }

    Q_ASSERT(!m_swingWindow);   // state machine forbids a second shot while busy

    // ⚠ THE WINDOW BOUNDS ARE FROZEN HERE, AT THE PAUSE. kWindowDuration is a
    // TRAILING span, so resolving it against a post-gather `now` would slide the
    // window several seconds past the swing it is meant to contain and the
    // snapshot would come back empty — with nothing reporting an error.
    m_historyProvenance.clear();   // per shot — never inherited from the last one
    m_stitchCounts.clear();

    m_windowEndUs   = pinpoint::EventBuffer::nowMicros();
    // The same instant on the wall clock, read back to back with the line above. This — not
    // the moment the save starts, which is after analysis and the pose pass, 2-37 s later —
    // is when "wallclock ≈ endTimestampUs()" is true, and it is what clock.wallclock is
    // computed from (buildSwingExportJob). Anchoring at save time put every swing's absolute
    // time seconds late (docs/design/foresight_premier_connector.md §2.6.6).
    m_windowEndUtc  = QDateTime::currentDateTimeUtc();
    m_windowStartUs = m_windowEndUs - kWindowDuration.count() * 1000LL;

    // ⚠ AND THE RESUME GUARD GOES UP HERE, for the same instant's reason (§3.5):
    // `resume_clear_rings` is true, so a resume arriving between the freeze and
    // the window's construction would clear the very rings we are about to
    // snapshot. CameraManager::resumeBuffer() is the hard backstop and it reads
    // exactly this flag, so raising it now is what makes the backstop cover the
    // whole gather.
    m_ringSource = m_buffer->makeRingPayloadSource();

    int pending = 0;
    for (HmInstance *hm : deferredSources())
        if (hm->historyPending()) ++pending;

    if (pending == 0) {
        // Nothing deferred — no HackMotion, no reservation, or it refused at the
        // call site. Construct immediately, exactly as this pipeline did before
        // deferred sources existed. ⚠ This is the ORDINARY path, not an error
        // path, and it must stay byte-identical to the undeferred behaviour.
        finishGatherAndLaunch();
        return;
    }

    ppInfo() << "[ShotProcessor] gathering —" << pending << "deferred source(s)";
    setState(State::Gathering);
    m_gatherDeadlineMs = QDateTime::currentMSecsSinceEpoch() + kGatherDeadlineMs;
    m_gatherTimer.start(kGatherPollMs);
}

void ShotProcessor::onGatherPoll()
{
    if (m_state != State::Gathering)
        return;   // finishNowBlocking() raced the timer

    int stillPending = 0;
    for (HmInstance *hm : deferredSources())
        if (hm->historyPending()) ++stillPending;

    const bool timedOut = QDateTime::currentMSecsSinceEpoch() >= m_gatherDeadlineMs;
    if (stillPending > 0 && !timedOut)
        return;

    if (timedOut && stillPending > 0) {
        // ⚠ Not an error, and NOT a cancel. The library's own deadline sits
        // inside ours, so a request still outstanding here has already
        // materialised its block with whatever arrived; we simply stop waiting.
        ppWarn() << "[ShotProcessor] gather deadline reached with"
                 << stillPending << "deferred source(s) outstanding";
    }

    m_gatherTimer.stop();
    finishGatherAndLaunch();
}

void ShotProcessor::finishGatherAndLaunch()
{
    m_gatherTimer.stop();

    // Compose the window's backing: the frozen ring for everything, overridden
    // by an in-RAM stitched lane for any source whose high-rate samples only
    // arrived after the freeze (design §3.2). With nothing deferred the
    // composite is a single catch-all route and the window is what it always was.
    auto entries   = m_buffer->snapshot(m_windowStartUs, m_windowEndUs);
    auto composite = std::make_unique<pinpoint::CompositePayloadSource>();
    auto ram       = std::make_unique<pinpoint::RamPayloadSource>();
    std::vector<pinpoint::SourceId> deferredIds;

    for (HmInstance *hm : deferredSources()) {
        HmInstance::HistoryResult hist = hm->takeHistoryResult();
        if (!hist.valid)
            continue;      // no block, or nothing usable in it — live lane stands

        HmUnit *const units[2] = { hm->unitLowerArm(), hm->unitPalm() };
        for (int u = 0; u < 2; ++u) {
            HmUnit *unit = units[u];
            if (!unit || unit->sourceId() == pinpoint::kInvalidSourceId)
                continue;
            const pinpoint::SourceId sid = unit->sourceId();

            pinpoint::DeferredStitchInput in;
            in.liveEntries     = entriesForIn(entries, sid);
            in.liveSample      = [this, sid](const pinpoint::IndexEntry &e)
                                     -> const pinpoint::ImuSample * {
                const auto h = m_ringSource->payloadOf(sid, e.source_sequence);
                if (!h.data || h.bytes != sizeof(pinpoint::ImuSample))
                    return nullptr;
                return reinterpret_cast<const pinpoint::ImuSample *>(h.data);
            };
            // ⚠ ELEMENT-WISE, NOT ASSIGNMENT, AND THE PLAIN `=` COMPILES ON TWO
            // OF THE THREE PLATFORMS. The Buffer layer is deliberately Qt-free
            // and spells these int64_t; HistoryResult is Qt-side and spells them
            // qint64. Identical width and representation everywhere, but on LP64
            // Linux int64_t is `long` and qint64 is `long long` — distinct types,
            // so the vectors do not convert. macOS and MSVC make both `long long`
            // and never see it. Do not "simplify" this back to `=`.
            in.deferredTUs.assign(hist.tUs.begin(), hist.tUs.end());
            in.deferredSamples = (u == 0) ? hist.lowerArm : hist.palm;
            in.delivered.assign(hist.delivered.begin(), hist.delivered.end());

            const pinpoint::DeferredStitchResult st =
                pinpoint::stitchDeferredLane(in);
            if (st.samples.empty())
                continue;

            // The stitched lane is served WHOLLY from RAM: one id cannot split
            // its sequence space across two backings, so its ring entries are
            // replaced rather than added to.
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                              [sid](const pinpoint::IndexEntry &e) {
                                  return e.source_id == sid;
                              }),
                          entries.end());
            for (size_t i = 0; i < st.tUs.size(); ++i)
                entries.push_back(pinpoint::IndexEntry{
                    st.tUs[i], sid, uint64_t(i), 0, 0 });

            ram->addImu(sid, m_ringSource->formatOf(sid), st.samples);
            deferredIds.push_back(sid);

            m_stitchCounts.insert(sid, { st.usedLive, st.usedDeferred });

            ppInfo() << "[ShotProcessor] stitched lane" << sid
                     << "— live" << st.usedLive
                     << "deferred" << st.usedDeferred
                     << "dropped" << st.droppedNonMonotonic;
        }
        m_historyProvenance.insert(hm->deviceId(), hist);
    }

    std::stable_sort(entries.begin(), entries.end(),
                     [](const pinpoint::IndexEntry &a, const pinpoint::IndexEntry &b) {
                         return a.timestamp_us < b.timestamp_us;
                     });

    if (!deferredIds.empty())
        composite->add(std::move(ram), deferredIds);   // claimed first
    composite->add(std::move(m_ringSource), {});       // catch-all: the ring

    m_swingWindow.emplace(std::move(composite), std::move(entries),
                          m_windowStartUs, m_windowEndUs);

    // One replay track per live camera with captured frames.
    m_replayTracks.clear();
    const std::vector<CameraInstance *> instances = m_cameraManager->liveCameraInstances();
    for (CameraInstance *ctrl : instances) {
        const pinpoint::SourceId sid = ctrl->sourceId();
        if (sid == pinpoint::kInvalidSourceId) continue;
        auto entries = m_swingWindow->entriesFor(sid);
        if (entries.empty()) continue;
        ReplayTrack track;
        track.ctrl     = ctrl;
        track.sourceId = sid;
        track.entries  = std::move(entries);
        track.loop     = (ctrl->perspective() == CameraInstance::Impact);
        // Freeze the ball-detector accumulator NOW (design §9): both job builders
        // run 12–37 s later (from onAnalysisFinished), by when the live deque has
        // scrolled to post-shot junk and a phantom re-launch may have overwritten
        // the launch. At this instant the 6 s accumulator still fully covers the
        // 4 s window. Absolute buffer-clock tUs kept (the analyzer consumes the
        // window's native domain; the exporter rebases).
        const auto &ballAccum = ctrl->ballSamples();
        track.ball.samples.reserve(ballAccum.size());
        for (const auto &s : ballAccum)
            track.ball.samples.push_back({s.tUs, s.found, s.x, s.y, s.r, s.conf});
        // CameraInstance's stored launch is never reset between shots, so when
        // the detector misses THIS shot's launch the stored one belongs to a
        // previous swing (observed: Wrist_02 sw4/5 exported sw3's launch).
        // Accept it only when it falls inside this window.
        qint64 lTUs = -1; double lx = 0.0, ly = 0.0;
        if (ctrl->ballLaunchInfo(lTUs, lx, ly)
            && lTUs >= m_swingWindow->startTimestampUs()
            && lTUs <= m_swingWindow->endTimestampUs()) {
            track.ball.hasLaunch = true;
            track.ball.launchTUs = lTUs;
            track.ball.launchX   = lx;
            track.ball.launchY   = ly;
        }
        m_replayTracks.push_back(std::move(track));
    }

    m_exportOutcome   = Outcome::Pending;
    m_analysisOutcome = Outcome::Pending;
    m_analysisResult  = {};
    m_integrity.reset();
    m_segmentation    = {};
    m_swingDir.clear();
    m_shot = {};
    m_shotSession = nullptr;
    m_allocResult = {};
    m_thumbnailPath.clear();
    setState(State::Processing);

    ppInfo() << "[ShotProcessor] window captured —"
             << static_cast<qint64>(m_swingWindow->entries().size()) << "entries,"
             << m_replayTracks.size() << "camera track(s)";

    // ── THE RECORDING, AT THE FREEZE (capture_context.h; refactor step 1.5) ──────────────────────
    // The athlete, the session and the settings are read ONCE, here, into the shot's Recording: the
    // analysis job, the export's athlete and club (12–37 s later), the seeded review club and the
    // integrity verdicts all come from this one value, so what is on disk is what was analysed.
    {
        std::vector<pinpoint::capture::CaptureCamera> cams;
        cams.reserve(m_replayTracks.size());
        for (const ReplayTrack &track : m_replayTracks) {
            pinpoint::capture::CaptureCamera c;
            c.ctrl   = track.ctrl;
            c.source = track.sourceId;
            // The accumulator snapshot frozen above (ReplayTrack::ball), on the ring clock.
            for (const BallSnapshot::Sample &b : track.ball.samples)
                c.ball.frames.push_back({ b.tUs, b.found, QPointF(b.x, b.y), b.r, b.conf });
            if (track.ball.hasLaunch) {
                c.ball.launchTUs    = track.ball.launchTUs;
                c.ball.launchCenter = QPointF(track.ball.launchX, track.ball.launchY);
            }
            cams.push_back(std::move(c));
        }
        m_capture = std::make_unique<pinpoint::capture::CaptureSnapshot>(pinpoint::capture::snapshotAtFreeze(
            *m_swingWindow, cams, m_imuManager, m_appSettings,
            pinpoint::capture::readCaptureAthlete(m_athlete, m_session),
            m_sessionType, static_cast<int>(m_shotSource), m_impactUs));
        // Per-shot club-length prior stash: empty/cold when this shot has no athlete/club/fixed-in-place
        // face-on camera, which onAnalysisFinished() reads as "nothing to persist".
        m_lengthPriorKey   = m_capture->lengthPriorKey;
        m_lengthPriorState = m_capture->lengthPriorState;
    }

    // Corpus capture: when saving raw frames with "skip analysis" on, export the
    // frames only — bypass segmentation + analysis and suppress replay (see
    // maybeJoin) — so each shot captures instantly and is re-analysed later. The
    // raw-only swing.json (analysis-skipped) still carries capture.impactUs, so
    // offline re-analysis has its impact reference.
    {
        AppSettings  fallback;
        AppSettings *s = m_appSettings ? m_appSettings : &fallback;
        m_skipAnalysisCapture = s->saveRawFrames() && s->skipAnalysisForRawCapture();
    }
    if (m_skipAnalysisCapture) {
        ppInfo() << "[ShotProcessor] skip-analysis corpus capture — export only";
        m_analysisOutcome = Outcome::Skipped;   // maybeJoin → raw-only swing.json
        startSwingSave();
        // ⚠ AND JOIN IT OURSELVES IF NOTHING ELSE WILL — shot_outcome.h,
        // JoinArrival::LaunchPathDirectly. startSwingSave() returns without
        // starting a worker when there is no swing folder (an unwritable
        // library) or no camera to encode (an IMU-only corpus shot), and
        // maybeJoin() otherwise only ever runs from a worker's completion. This
        // path used to return here and the shot never joined at all: Processing
        // for ever, the trigger never re-armed, the buffer never resumed, and
        // nothing on screen said so. The ordinary path is already covered —
        // onAnalysisFinished() calls maybeJoin() whatever the export did.
        if (!m_swingSaveInFlight)
            maybeJoin();
        return;
    }

    // Segmentation pre-stage (v3 G2): a milliseconds-cheap fuse + inertial
    // ladder over the frozen window, gating both heavy workers — its swing
    // bounds bound the heavy-stage scan windows and the in-window replay. The
    // job is resolved on the UI thread NOW (value types only); failure or
    // no-IMU yields a conf-0 result and everything below degrades to
    // full-window behaviour.
    //
    // ⚠ IT DOES NOT TRIM THE EXPORT, whatever this comment used to claim.
    // SwingExportJob carries no span and SwingExporter reads no bounds: the
    // encode is the whole frozen window, always. Playback trims instead —
    // DiskReplaySource on the persisted Finish event, startReplay() below on
    // the analysed one — which is why a swing.json can hold a 4 s clip and
    // still replay 2.7 s of swing.
    // THE ONE JOB BUILDER, on the Recording and its effective context: the recorded context with the
    // club capture seeds into review.club as the declared club (no correction exists yet) — exactly
    // what re-analysis reads back, so an uncorrected shot gets the same job either way.
    {
        pinpoint::analysis::ClubReview review;
        review.declared = m_capture->athlete.reviewClub;
        pinpoint::analysis::RunOptions run;
        run.origin = QStringLiteral("live");
        // Worker → UI progress marshalling: queued invoke with `this` as context (auto-cancelled if
        // the processor dies first), throttled to whole-percent steps. ATOMIC since the analysis can
        // run as a graph (analysis.parallel; analysis_dag.h): two stages may report at once, and a
        // compare-exchange keeps the bar monotonic.
        auto lastPct = std::make_shared<std::atomic<int>>(-1);
        run.progress = [this, lastPct](float p) {
            const int pct = static_cast<int>(p * 100.0f);
            int seen = lastPct->load(std::memory_order_relaxed);
            do {
                if (pct <= seen)
                    return;
            } while (!lastPct->compare_exchange_weak(seen, pct, std::memory_order_relaxed));
            QMetaObject::invokeMethod(this, [this, p] { setAnalysisProgress(p); }, Qt::QueuedConnection);
        };
        m_analysisJob = pinpoint::analysis::buildAnalysisJob(
            m_capture->recording,
            pinpoint::analysis::effectiveContext(m_capture->recording.context, review), run);
    }
    const pinpoint::SwingWindow *win = &*m_swingWindow;
    m_segmentationInFlight = true;
    m_segmentationWatcher.setFuture(QtConcurrent::run(
        [bindings = m_analysisJob.imuBindings, impactUs = m_impactUs, win] {
            try {
                const pinpoint::analysis::FusedStreams streams =
                    pinpoint::analysis::ImuVisionFuser::fuse(
                        *win, bindings,
                        pinpoint::analysis::ImuVisionFuser::gridHzForWindow(*win, bindings));
                return pinpoint::analysis::PhaseSegmenter::segment(streams, impactUs);
            } catch (...) {
                return pinpoint::analysis::Segmentation{};   // conf 0 → full window
            }
        }));
}

void ShotProcessor::onSegmentationFinished()
{
    if (!m_segmentationInFlight)
        return;   // already joined blockingly in finishNowBlocking()
    m_segmentationInFlight = false;
    if (m_state != State::Processing)
        return;   // aborted while the pre-stage ran
    m_segmentation = m_segmentationWatcher.result();

    // Sequence the two heavy workers rather than overlapping them. The ViTPose
    // pose pass dominates wall time and runs far faster with the cores to itself
    // (multi-threaded intra-op); the export's x264 encode threads otherwise
    // starved the pose pass, inflating per-frame inference roughly 5×. The
    // export is launched once analysis completes (onAnalysisFinished). Analysis
    // always launches a worker, so there is no synchronous-skip path to cover
    // with maybeJoin() here. Both workers read the same frozen window — const,
    // zero-copy reads over stable memory (producers stopped while Paused).
    startAnalysis();
}

void ShotProcessor::startAnalysis()
{
    // The analysis service runs the job the one builder made (finishGatherAndLaunch) on this shot's
    // Recording, and reaches its integrity verdicts with the one rule (integrityFor) — the same code
    // re-analysis and SwingLab run. The Recording reads the frozen window (stable optional storage).
    m_analysisInFlight = true;
    m_analysisWatcher.setFuture(QtConcurrent::run(
        [job = m_analysisJob, recording = m_capture->recording, service = m_analysisService] {
            // Name this pooled thread so it shows in the profiler's per-thread
            // CPU table for the duration of the analysis (RAII across all the
            // return paths below).
            pinpoint::osmetrics::ThreadScope _tscope("Analysis.Worker");
            // Exception barrier: anything escaping the worker would be rethrown by QtConcurrent on
            // the GUI thread at result()/waitForFinished() with no handler — std::terminate. The
            // service already degrades a throwing stage to a failed analysis; this covers the rest.
            // The join still adds the shot and resumes the buffer.
            try {
                return service.analyseJob(recording, job);
            } catch (const std::exception &e) {
                pinpoint::analysis::AnalysedRecording r;
                r.analysis.error = QString::fromUtf8(e.what());
                return r;
            } catch (...) {
                pinpoint::analysis::AnalysedRecording r;
                r.analysis.error = QStringLiteral("unknown exception in shot analysis");
                return r;
            }
        }));
}

void ShotProcessor::startSwingSave()
{
    // The allocation never moves a session's folder (step 2.2: SessionRepository fixes it at Start). A
    // session that STARTS here — the first swing of one whose folder could not be made at Start, or a
    // swing outside a session — is announced by captureSession() as any Start is, before this shot's
    // shotProcessed, so every panel bound to activeSessionDir is already on it (audit E-01).
    pinpoint::SwingExportJob job = buildSwingExportJob();
    // Cache the job + dir up front: even when there is nothing to encode (no
    // cameras, or an encode failure), an analysis-only swing.json is written
    // from these at the join so the shot still survives a restart.
    m_swingDir  = job.swingDir;
    m_exportJob = job;

    if (job.swingDir.isEmpty()) {
        ppWarn() << "[SwingExport] could not allocate a swing directory — not saving";
        m_exportOutcome = Outcome::Skipped;
        // A folder another writer holds (a tool, the other machine) says so in the lock's own words
        // (J-22); anything else is the library path, as before.
        if (m_allocResult.status == pinpoint::WriteResult::Status::Locked)
            emit swingSaveBlocked(m_allocResult.error);
        else
            emit swingSaveBlocked(tr("The swing folder could not be created — check the "
                                     "athlete library path in Settings"));
        return;
    }
    if (job.cameras.empty()) {
        ppWarn() << "[SwingExport] no exportable cameras — analysis-only swing";
        m_exportOutcome = Outcome::Skipped;
        return;
    }

    // The optional's storage is stable; the window is destroyed only in
    // finishShot()/finishNowBlocking(), strictly after the worker has returned.
    const pinpoint::SwingWindow *win = &*m_swingWindow;
    m_swingSaveInFlight = true;
    ppInfo() << "[SwingExport] saving swing to" << job.swingDir;
    m_swingSaveWatcher.setFuture(QtConcurrent::run(
        [job = std::move(job), win] {
            // Same exception barrier as the analysis worker above.
            try {
                return pinpoint::SwingExporter::run(*win, job);
            } catch (const std::exception &e) {
                pinpoint::SwingExportResult r;
                r.swingDir = job.swingDir;
                r.error = QString::fromUtf8(e.what());
                return r;
            } catch (...) {
                pinpoint::SwingExportResult r;
                r.swingDir = job.swingDir;
                r.error = QStringLiteral("unknown exception in swing export");
                return r;
            }
        }));
}

pinpoint::SwingExportJob ShotProcessor::buildSwingExportJob()
{
    AppSettings  fallback;
    AppSettings *s = m_appSettings ? m_appSettings : &fallback;

    pinpoint::SwingExportJob job;

    // CRF from videoQuality (storage/videoQuality).
    const QString quality = s->videoQuality();
    job.crf = quality == QLatin1String("low")      ? 28
            : quality == QLatin1String("high")     ? 18
            : quality == QLatin1String("lossless") ? 0
                                                   : 23;   // "medium"
    job.codec   = s->videoCodec();
    job.saveImu = s->saveImuStreams();
    job.resolutionMode = s->videoResolutionMode();
    job.saveRaw        = s->saveRawFrames();
    job.savePose       = s->savePoseKeypoints();
    // job.poseStreams intentionally left empty: pose production (analyzer / pose
    // buffering) is a separate scope. The exporter serialises whatever is here,
    // so this is forward-compatible — populate it upstream once a producer lands.

    // ⚠ EVERY RECORDING INPUT THIS JOB WRITES COMES FROM THE SNAPSHOT TAKEN AT THE FREEZE — the
    // athlete, the club and its prior, the analysis context, each camera's placement / search box /
    // empty-mat baseline, the pose-quality tier, the host IMU filter, each lane's rate and calibration
    // (capture_context.h, fillExportFromSnapshot below). This runs after the analysis, 12–37 s later:
    // by then the ball detector has re-learned its baseline over the empty mat and a setting may have
    // changed, and the header must record what the analysis consumed (H-02, audit E-01).
    const pinpoint::capture::CaptureSnapshot *snap = m_capture.get();

    // Wallclock anchor: the UTC instant taken when the window end was frozen (beginGather), so
    // wallclock == monotonic endTimestampUs() by construction. NOT currentDateTimeUtc() here:
    // this runs from startSwingSave(), after analysis, and "now" is seconds past the window.
    job.wallclockAnchorUtc = m_windowEndUtc.isValid() ? m_windowEndUtc
                                                      : QDateTime::currentDateTimeUtc();

    // Session context + host provenance for the manifest's "capture" block.
    job.sessionType = m_sessionType;
    job.shotSource  = static_cast<int>(m_shotSource);
    job.swingDetectionSensitivity = s->swingDetectionSensitivity();
    job.motionCaptureQuality      = snap ? snap->recording.capture.motionCaptureQuality : s->motionCaptureQuality();
    job.imuBleLatencyUs      = ImuInstance::kImuBleLatencyUs;
    job.audioDeviceLatencyUs = s->audioDeviceLatencyUs();
    job.micTravelUs          = s->micTravelUs();
    job.host.appVersion = QStringLiteral(PP_APP_VERSION);
    job.host.gitSha     = QStringLiteral(PP_GIT_SHA);
    job.host.hostname   = QSysInfo::machineHostName();
    job.host.platform   = QSysInfo::prettyProductName();
    for (const ReplayTrack &track : m_replayTracks) {
        if (!track.ctrl->poseBackendLabel().isEmpty()) {
            job.host.poseBackend = track.ctrl->poseBackendLabel();
            break;
        }
    }

    // Cameras: every replay track, with its alias resolved and sanitised.
    // Filename = alias (live-updated on the instance), falling back to the
    // device description, then serial.
    QSet<QString> usedNames;
    for (const ReplayTrack &track : m_replayTracks) {
        QString alias = track.ctrl->deviceAlias().trimmed();
        if (alias.isEmpty()) alias = track.ctrl->deviceDescription();
        if (alias.isEmpty()) alias = QStringLiteral("camera-%1")
                                         .arg(track.ctrl->deviceSerialNumber());

        QString base = pinpoint::SwingPaths::sanitise(alias);
        QString name = base;
        for (int n = 2; usedNames.contains(name); ++n)
            name = base + QStringLiteral("-%1").arg(n);
        usedNames.insert(name);

        pinpoint::SwingExportCamera cam;
        cam.sourceId = track.sourceId;
        cam.alias    = name;
        cam.fileName = name + QStringLiteral(".mp4");   // the extension picks FFmpeg's muxer
        // Placement, mirroring, fixed-in-place, search box, baseline: the frozen facts.
        const pinpoint::CaptureCameraFacts *facts =
            snap ? pinpoint::captureCameraFactsFor(snap->cameraFacts, track.sourceId) : nullptr;
        if (facts) {
            pinpoint::applyCaptureCameraFacts(cam, *facts);
        } else {   // no snapshot (cannot happen once a window exists): today's reads
            cam.perspective   = track.ctrl->perspective();
            cam.mirrored      = track.ctrl->isMirrored();
            cam.fixedInPlace  = s->cameraFixedInPlace().value(track.ctrl->cameraKey()).toBool();
            cam.ballSearchRoi = track.ctrl->roi();
        }
        // Timestamp provenance, for EVERY camera (event_buffer_design.md §9): which clock stamped these
        // frames and how the mapping onto ours was doing. A backend with no mapping leaves both out, and
        // the stream reads as arrival-stamped.
        {
            pinpoint::DeviceClockStats cs;
            if (track.ctrl->clockStats(&cs) && cs.method != pinpoint::ClockMapMethod::None) {
                cam.hasClock = true;
                cam.clock    = cs;
                switch (cs.method) {
                case pinpoint::ClockMapMethod::DevicePts:
                    cam.timestampSource = QStringLiteral("devicePts"); break;
                case pinpoint::ClockMapMethod::HostArrival:
                    cam.timestampSource = QStringLiteral("hostArrival"); break;
                default:
                    cam.timestampSource = QStringLiteral("device"); break;
                }
            }
            // No mapping at all (a PPCP peer's camera maps its own clock elsewhere): the field is left
            // out rather than asserted, and an absent field reads as arrival-stamped.
        }
        // The impact camera's keep band — see kImpactKeepBeforeUs. Absolute
        // buffer-clock, the domain of both m_impactUs and the entries.
        if (cam.perspective == CameraInstance::Impact && m_impactUs >= 0) {
            auto countIn = [&](int64_t lo, int64_t hi) {
                size_t n = 0;
                for (const auto &e : track.entries)
                    if (e.timestamp_us >= lo && e.timestamp_us <= hi) ++n;
                return n;
            };
            int64_t lo = m_impactUs - kImpactKeepBeforeUs;
            int64_t hi = m_impactUs + kImpactKeepAfterUs;
            if (countIn(lo, hi) == 0) {
                lo = m_impactUs - kImpactKeepFallbackUs;
                hi = m_impactUs + kImpactKeepFallbackUs;
                if (countIn(lo, hi) == 0) {
                    ppWarn() << "[ShotProcessor] impact camera" << name
                             << "has no frame within ±" << kImpactKeepFallbackUs / 1000
                             << "ms of impact — exporting its whole lane";
                    lo = hi = -1;
                } else {
                    ppWarn() << "[ShotProcessor] impact camera" << name
                             << "has no frame in the impact band — widened to ±"
                             << kImpactKeepFallbackUs / 1000 << "ms";
                }
            }
            cam.keepStartUs = lo;
            cam.keepEndUs   = hi;
        }
        // Gain provenance (impact_camera_design.md §10.3): what the camera
        // held if it told us, else what it was asked for. Every camera whose
        // gain was locked — the impact camera always, any other when its role
        // has a lock (flir_image_clarity.md §6.4); auto gain writes nothing.
        if (cam.perspective == CameraInstance::Impact || track.ctrl->requestedGainDb() >= 0.0) {
            const double heldGain  = track.ctrl->appliedGainDb();
            cam.gainDb     = heldGain >= 0.0 ? heldGain : track.ctrl->requestedGainDb();
            cam.gainSource = (heldGain >= 0.0) ? QStringLiteral("applied")
                                               : QStringLiteral("requested");
        }
        if (cam.perspective == CameraInstance::Impact) {
            cam.blackLevelLift = CameraInstance::kImpactBlackLevelLiftPct;
            const QVariantMap tuning = s->cameraTuning().value(track.ctrl->cameraKey()).toMap();
            cam.strobe   = tuning.value(QStringLiteral("strobe"), false).toBool();
            cam.viewGain = tuning.value(QStringLiteral("viewGain"),
                                        CameraInstance::kImpactDefaultViewGain).toDouble();
            cam.note     = tuning.value(QStringLiteral("note")).toString().trimmed();
            // Near-lossless for the clip: the library's quality is for a 4 s
            // full-frame swing, and it turned a dark 640×240 strip into 16-px
            // blocks (2026-09-15). ~180 frames — a few MB at most. Lossless
            // (0) stays lossless.
            cam.crf = std::min(job.crf, kImpactClipCrf);
        }
        // The v2 temporal detector carries no calibration profile, so the
        // CamRecord ball-calibration fields keep their defaults (uncalibrated).
        // Recording the v2 auto-detected ball position (locked centre + satFrac)
        // into swing.json is the additive "Provenance v2" follow-up.
        // The hitting area (the re-analysis ball search box) and the learned empty-mat baseline are
        // the FROZEN ones (applyCaptureCameraFacts above): the detector re-learns its baseline over the
        // empty mat after impact, during the analysis, so the camera's cache now is the post-shot one,
        // not the one the live analysis ran on.
        job.cameras.push_back(std::move(cam));

        // v3.4 (design §9.7): this camera's ball stream, window-relative —
        // additive, empty when ball detection never ran on this camera. Read
        // from the snapshot frozen at window capture (ReplayTrack::ball), NOT
        // live controller state: buildSwingExportJob runs 12–37 s after impact,
        // by when the live accumulator had scrolled to post-shot junk and any
        // re-launch had overwritten the launch — the archived stream must cover
        // the swing, not the post-shot accumulation.
        const auto &ballSamples = track.ball.samples;
        if (!ballSamples.empty() && m_swingWindow) {
            const int64_t t0 = m_swingWindow->startTimestampUs();
            pinpoint::SwingBallStream bs;
            bs.alias  = name;
            bs.serial = track.ctrl->deviceSerialNumber();
            bs.tUs.reserve(ballSamples.size());
            bs.data.reserve(ballSamples.size() * 5);
            for (const auto &s : ballSamples) {
                bs.tUs.push_back(s.tUs - t0);
                bs.data.push_back(s.found ? 1.f : 0.f);
                bs.data.push_back(s.x);
                bs.data.push_back(s.y);
                bs.data.push_back(s.r);
                bs.data.push_back(s.conf);
            }
            if (track.ball.hasLaunch) {
                bs.launchTUs = track.ball.launchTUs - t0;
                bs.launchX   = float(track.ball.launchX);
                bs.launchY   = float(track.ball.launchY);
            }
            job.ballStreams.push_back(std::move(bs));
        }
    }

    // Impact thumbnail from the face-on camera, else the exporter falls back
    // to the first exported stream.
    job.thumbnailTimestampUs = m_impactUs;
    // Window-relative impact for swing.json's capture.impactUs — the re-analysis
    // impact reference (survives analysis-skipped corpus captures).
    job.impactUs = (m_impactUs >= 0 && m_swingWindow)
                       ? m_impactUs - m_swingWindow->startTimestampUs()
                       : -1;
    for (const ReplayTrack &track : m_replayTracks) {
        if (track.ctrl->perspective() == CameraInstance::FaceOn) {
            job.thumbnailSourceId = track.sourceId;
            break;
        }
    }

    // IMU aliases keyed by the same identifier the sources registered with
    // (serial when present, else device id — mirrors ImuInstance).
    const QVariantMap imuAliases   = s->imuAlias();
    const QVariantMap imuFusion    = s->imuFusionMode();
    const QVariantMap imuRates     = s->imuOutputRateHz();
    const QList<Device> imus = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    for (const Device &dev : imus) {
        const QString serial = dev.imuCapabilities.serialNumber.isEmpty()
                                   ? dev.id
                                   : dev.imuCapabilities.serialNumber;
        const QString imuKey = dev.description + QStringLiteral("|") + dev.id;
        const QString alias  = imuAliases.value(imuKey).toString().trimmed();
        if (!alias.isEmpty())
            job.imuAliasBySerial.insert(serial, alias);

        // Per-device config for the stream "device" object. The live
        // instance's rate is authoritative; settings are the fallback when
        // the device disconnected between capture and export.
        pinpoint::SwingImuDeviceInfo info;
        info.outputRateHz      = imuRates.value(dev.id, 200).toInt();
        info.fusionMode        = imuFusion.value(dev.id, s->imuDefaultFusionMode()).toString();
        // The host filter at the freeze (the integrity rule judges the lanes against it).
        info.orientationFilter = snap ? snap->imuOrientationFilter : s->imuOrientationFilter();
        // ⚠ placementSlot IS NO LONGER WRITTEN (design §4.13): placement is role-keyed,
        // and the letter carried nothing the role does not. The field and every reader
        // stay — old swings have it — and the exporter omits the key when it is empty.
        //
        // The anatomical role comes from the same resolver as analysis.bindings, with no
        // sessionType gate, so it is baked into the stream itself — needed by the data
        // viewer, SwingLab and future post-hoc analysis. ⚠ Witmotion only, as before: a
        // wG3's `info` is keyed by the bare device id, which no lane carries (see below),
        // and its unit lanes deliberately get no role here.
        const pinpoint::analysis::SegmentRole role =
            (m_imuManager && dev.imuVendor != ImuVendor::HackMotion)
                ? pinpoint::analysis::segmentRoleForPlacementRole(m_imuManager->roleForDevice(dev.id))
                : pinpoint::analysis::SegmentRole::Unknown;
        info.role     = int(role);
        info.roleName = pinpoint::analysis::segmentRoleName(role);
        if (m_imuManager) {
            const QVariantList insts = m_imuManager->instances();
            for (const QVariant &v : insts) {
                auto *imu = qobject_cast<ImuInstance *>(v.value<QObject *>());
                if (imu && imu->deviceId() == dev.id) {
                    info.outputRateHz = imu->outputRateHz();
                    // A/M calibration snapshot baked into the stream — lets an
                    // analysis-skipped corpus swing be re-analysed (no
                    // analysis.bindings). The frozen binding replaces these below.
                    info.hasCalibration = true;
                    info.alignA         = imu->alignA();
                    info.mountM         = imu->mountM();
                    info.anatCalibrated = imu->anatCalibrated();
                    info.calibrated     = imu->fullyCalibrated();
                    info.mountDeviationDeg    = imu->mountDeviationDeg();
                    info.mountGravityErrorDeg = imu->mountGravityErrorDeg();
                    if (imu->calibratedAtUtc().isValid()) {
                        info.calibratedAtUtc = imu->calibratedAtUtc().toString(Qt::ISODateWithMs);
                        info.calibAgeSec     = imu->calibratedAtUtc()
                                                   .msecsTo(QDateTime::currentDateTimeUtc()) / 1000.0;
                    }
                    info.trunkCalib = imu->trunkCalibProvenance();   // trunk design §6.8
                    // A lane in the window: its rate, calibration and role AS THEY STOOD AT THE
                    // FREEZE (the Recording's binding) — the reads above are only for a device the
                    // window holds no lane of, which no stream carries.
                    if (snap) {
                        for (const pinpoint::analysis::ImuSegmentBinding &bnd : snap->recording.imuBindings)
                            if (bnd.source == imu->sourceId() && !bnd.hackMotion) {
                                pinpoint::applyCaptureImuFacts(info, snap->imuOutputRateHz.value(imu->sourceId(), 0), bnd);
                                break;
                            }
                    }
                    break;
                }
            }

            // A HackMotion wG3 is ONE peripheral that registers TWO EventBuffer
            // sources, "<deviceId>#lowerArm" / "<deviceId>#palm" (hm_instance.h) —
            // neither equals `serial` above (the bare device id), so the alias
            // and `info` this loop iteration built reach no stream at all: a
            // captured wG3's two lanes would export with the raw unit id as
            // their only label and no device object. Insert under each real
            // unit id instead; additive, and unreachable for a Witmotion
            // (qobject_cast below returns null).
            for (const QVariant &v : insts) {
                auto *hm = qobject_cast<HmInstance *>(v.value<QObject *>());
                if (!hm || hm->deviceId() != dev.id)
                    continue;
                const QString baseAlias = alias.isEmpty() ? serial : alias;
                HmUnit *const units[2] = { hm->unitLowerArm(), hm->unitPalm() };
                for (HmUnit *unit : units) {
                    // sourceId() is kInvalidSourceId if this unit's own
                    // registerSource() call failed — BLE registration is
                    // allowed to fail per unit, and a lane with no source
                    // never appears in the export window, so an alias/device
                    // entry for it would just be dead.
                    if (!unit || unit->sourceId() == pinpoint::kInvalidSourceId)
                        continue;
                    const QString unitId = unit->unitId();
                    job.imuAliasBySerial.insert(
                        unitId, baseAlias + QStringLiteral(" · ") + unit->unitLabel());

                    // ⚠ Deliberately NOT a copy of `info` above: hasCalibration/
                    // alignA/mountM must stay unset (SwingImuDeviceInfo's
                    // defaults) — the re-analyzer treats a device object
                    // carrying a 4-element alignA + mountM pair as a fallback
                    // IMU->segment binding (swing_reanalyzer.cpp:471), so
                    // writing them here would silently bind a HackMotion lane
                    // into the Witmotion wrist maths in a frame nobody has
                    // reconciled — that reconciliation is Phase D.
                    // fusionMode/orientationFilter stay empty too, rather than
                    // inheriting the app's Witmotion defaults: this device
                    // fuses on-device, and a persisted "madgwick" against a
                    // HackMotion lane would be a fabricated provenance record.
                    // role/roleName stay unset (and placementSlot is no longer
                    // written for any device) — unchanged by role-keyed placement;
                    // the binding in buildAnalysisJob carries the unit's role.
                    pinpoint::SwingImuDeviceInfo hmInfo;
                    // Measured average of an ADAPTIVE rate (25 Hz at rest, 100 Hz
                    // in motion, dense bursts to ~799 Hz) — not a configured
                    // output rate like Witmotion's outputRateHz, so a reader must
                    // not treat this single number as a period. Rounded to the
                    // nearest Hz; the settings default of 200 never applies here.
                    hmInfo.outputRateHz = int(std::lround(hm->dataRateHz()));
                    // Palm-minus-lower-arm skew: the two blocks are NOT paired as
                    // simultaneous (see SwingImuDeviceInfo::skewUs). NaN until a
                    // sample has been seen — leave 0.0 (the "absent" value the
                    // exporter checks) rather than writing a fake measurement.
                    // ⚠ The MEDIAN: a single record's difference is mostly ±½-sample
                    // pairing jitter, so a mean would bake those outliers in.
                    const double skew = hm->skewUsMedian();
                    if (std::isfinite(skew))
                        hmInfo.skewUs = skew;

                    // ── Capture provenance, scoped to THIS window (Phase B′) ──
                    // ⚠ Queried per unit, not per device: pinning is a property of
                    // one board — the palm sits at the larger radius and saturates
                    // first — so folding both units into one count would report a
                    // clipped palm as a fault of the lower arm as well.
                    //
                    // ⚠ Left entirely at its "not measured" defaults when there is no
                    // window. A swing exported without one cannot say what happened
                    // inside it, and inventing a clean answer is the failure this
                    // whole block exists to prevent.
                    if (m_swingWindow) {
                        const HmInstance::CaptureProvenance prov =
                            hm->captureProvenance(m_swingWindow->startTimestampUs(),
                                                  m_swingWindow->endTimestampUs());
                        hmInfo.hmCalibrationStateAtStart = prov.calibration.stateAtStart;
                        hmInfo.hmCalibrationStateAtEnd   = prov.calibration.stateAtEnd;
                        hmInfo.hmCalibrationSpansTransition =
                            prov.calibration.spansTransition;
                        hmInfo.hmConfigBits           = prov.configBits;
                        hmInfo.hmProvenanceDropped    = int(prov.exceptionsDropped);
                        hmInfo.hmNoFitSkippedSession  = int(prov.noFitSkipped);

                        const quint8 thisUnit =
                            quint8(unit == hm->unitPalm() ? WR_UNIT_PALM
                                                          : WR_UNIT_LOWER_ARM);
                        for (const HmInstance::SampleException &e : prov.exceptions) {
                            switch (e.reason) {
                            case HmInstance::SampleException::Pinned:
                                if (e.unit == thisUnit)
                                    ++hmInfo.hmPinnedSamples;
                                break;
                            case HmInstance::SampleException::QuatNormSuspect:
                                // ⚠ Record-level, so it counts for BOTH units rather
                                // than being assigned to one. A misaligned decode is
                                // not something one board did.
                                ++hmInfo.hmQuatNormSuspect;
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    // ── Deferred history provenance (Phase E) ───────────
                    // ⚠ Keyed by DEVICE, applied to BOTH units: one wG3 is one
                    // peripheral with one stream and one pull, so the coverage,
                    // the gaps and the fit describe both lanes equally. The
                    // stitch COUNTS differ per lane and are set below.
                    const auto hIt = m_historyProvenance.constFind(hm->deviceId());
                    if (hIt != m_historyProvenance.constEnd() && hIt->valid) {
                        const HmInstance::HistoryResult &h = *hIt;
                        hmInfo.hmHistoryStatus      = h.status;
                        hmInfo.hmHistoryAttempts    = h.attempts;
                        hmInfo.hmCoverageOverflowed = h.coverageOverflowed;
                        hmInfo.hmCoverageFraction   = h.coverageFraction;
                        hmInfo.hmDensity            = h.density;
                        hmInfo.hmAchievedHz         = h.achievedHz;
                        hmInfo.hmLargestGapUs       = qint64(h.largestGapUs);
                        hmInfo.hmLiveOverlapSamples    = int(h.liveOverlapSamples);
                        hmInfo.hmLiveOverlapMismatches = int(h.liveOverlapMismatches);
                        hmInfo.hmSelfRecordingGapStartUs = h.selfRecordingGapStartUs;
                        hmInfo.hmSelfRecordingGapEndUs   = h.selfRecordingGapEndUs;
                        hmInfo.hmDelivered = h.delivered;
                        for (const auto &g : h.gaps)
                            hmInfo.hmGaps.emplace_back(g.startUs, g.endUs, g.kind);

                        hmInfo.hmFitValid            = true;
                        hmInfo.hmFitFlags            = h.fit.flags;
                        hmInfo.hmFitObservations     = h.fit.observations;
                        hmInfo.hmFitRateHz           = h.fit.fitted_rate_hz;
                        hmInfo.hmFitAnchorHostUs     = qint64(h.fit.anchor_host_us);
                        hmInfo.hmFitAnchorIndex      = h.fit.anchor_index;
                        hmInfo.hmFitSlopeUsPerIndex  = h.fit.slope_us_per_index;
                        hmInfo.hmFitOffsetUs         = qint64(h.fit.offset_us);
                        hmInfo.hmFitSpanUs           = qint64(h.fit.span_us);
                        hmInfo.hmFitResidualMedianUs = h.fit.residual_median_us;
                        hmInfo.hmFitResidualP90Us    = h.fit.residual_p90_us;
                        hmInfo.hmFitResidualMaxUs    = h.fit.residual_max_us;
                        hmInfo.hmFitDriftUsPerS      = h.fit.accuracy_drift_us_per_s;

                        const auto sIt = m_stitchCounts.constFind(unit->sourceId());
                        if (sIt != m_stitchCounts.constEnd()) {
                            hmInfo.hmStitchedFromLive     = sIt->first;
                            hmInfo.hmStitchedFromDeferred = sIt->second;
                        }
                    }

                    job.imuDeviceBySerial.insert(unitId, hmInfo);
                }
                break;
            }
        }
        job.imuDeviceBySerial.insert(serial, info);
    }

    // The athlete, the club (its geometry and the length prior the live fuse used), the analysis context
    // and the capture-wide inputs — from the snapshot, over everything listed above (the cameras' and
    // devices' facts are re-applied from it, which is idempotent).
    if (snap)
        pinpoint::capture::fillExportFromSnapshot(job, *snap);

    // One allocator (D10: DEC-157): the next swing number of the session this swing is written into —
    // Session::addShot (allocate → the Shot → returned: DEC-142), the allocation's folder handed back for
    // the media; for a session no Athlete holds, the repository's allocator itself (the interim).
    const pinpoint::SessionId session = captureSession();
    pinpoint::SwingAllocation alloc;
    m_shotSession = m_openSessionObject;
    if (m_openSessionObject)
        m_openSessionObject->addShot(pinpoint::recording::ShotKind::Captured, &alloc);
    else if (m_sessions && !session.isNull())
        alloc = m_sessions->allocateSwing(session);
    if (!alloc.ok())
        ppWarn() << "[SwingExport] no swing folder:"
                 << (session.isNull() ? QStringLiteral("no session could be started") : alloc.result.error);
    job.swingDir   = alloc.ok() ? alloc.swingFolder : QString();
    m_shot         = alloc.ok() ? alloc.shot : pinpoint::ShotRef();
    m_allocResult  = alloc.result;
    job.swingId    = alloc.swingName;
    job.swingIndex = alloc.shot.swingNumber();
    job.sessionId  = alloc.sessionFolderName;
    return job;
}

// ── Session folder lifecycle (QML) ──────────────────────────────────────────
// Through SessionRepository (step 2.2), with the same library / athlete / naming-pattern settings for
// Start, the extend prompt and every swing, so the session chosen at Start is the one swings save into.

namespace {

// The session type by name (DEC-88), as the session file records it; "" for none.
QString sessionTypeName(int sessionType)
{
    switch (sessionType) {
    case 0:  return QStringLiteral("swing");
    case 1:  return QStringLiteral("wrist");
    case 2:  return QStringLiteral("grf");
    case 3:  return QStringLiteral("coach");
    default: return QString();
    }
}

// The library as D7 compares it: cleaned, so "lib/" and "lib" are one library.
QString libraryKey(const QString &root)
{
    const QString r = root.trimmed();
    return r.isEmpty() ? QString() : QDir::cleanPath(r);
}

pinpoint::SessionOwner ownerOf(const AthleteController *athlete, const AppSettings *settings)
{
    const pinpoint::capture::AthleteIdentity who = pinpoint::capture::athleteIdentity(athlete);
    return { pinpoint::AthleteId::fromString(who.uuid), who.name,
             settings ? settings->athleteLibraryPath() : QString() };
}

} // namespace

void ShotProcessor::setSessionRepository(pinpoint::SessionRepository *sessions)
{
    m_sessions = sessions;
    // D7 (DEC-154): a library re-pointed mid-session ends the session and starts a new one there.
    if (m_appSettings)
        connect(m_appSettings, &AppSettings::athleteLibraryPathChanged, this, &ShotProcessor::onLibraryChanged,
                Qt::UniqueConnection);
}

void ShotProcessor::setOpenSession(pinpoint::Session *session)
{
    m_openSessionObject = session;
    m_openStartedHere   = false;   // the interim's flag; an Athlete's Session keeps its own
    const auto entry = session ? session->entry() : std::nullopt;
    m_openSession    = session ? session->id() : pinpoint::SessionId();
    m_openSessionDir = entry ? entry->folder : QString();
    if (!session)
        m_openedOnDemand = false;
}

void ShotProcessor::setOpenSessionId(const pinpoint::SessionId &id)
{
    m_openSessionObject = nullptr;
    m_openStartedHere   = false;
    m_openSession = id;
    const auto entry = (m_sessions && !id.isNull()) ? m_sessions->open(id) : std::nullopt;
    m_openSessionDir = entry ? entry->folder : QString();
    if (id.isNull())
        m_openedOnDemand = false;
}

pinpoint::Athlete *ShotProcessor::captureAthlete() const
{
    pinpoint::Athlete *a = pinpoint::capture::currentAthlete(m_athlete);
    if (!a || !m_appSettings)
        return nullptr;
    // Its sessions live where capture writes (today's composition: the athlete's token under SET-03). A golfer
    // kept current while the library was re-pointed under a session (D7), and not listed by the new library,
    // is not: that session no Athlete holds (the interim).
    return libraryKey(a->sessionOwner().libraryRoot) == libraryKey(m_appSettings->athleteLibraryPath()) ? a : nullptr;
}

bool ShotProcessor::startOpenSession(const QString &type, bool onDemand, pinpoint::WriteResult *result)
{
    const QString pattern = m_appSettings->sessionNamingPattern();
    if (pinpoint::Athlete *a = captureAthlete()) {
        // DEC-78: the Athlete ends the session running here first (never the empty discard), then starts.
        pinpoint::WriteResult r;
        pinpoint::Session *s = a->startSession(type, pattern, &r,
                                               onDemand ? pinpoint::SessionStart::OnDemand : pinpoint::SessionStart::Deliberate);
        if (result)
            *result = r;
        setOpenSession(s);
        return s != nullptr;
    }
    // THE INTERIM (NOTES-3.4a-ii N1): no Athlete holds this session, so the repository starts it directly, as
    // before step 3.4a-ii; the empty discard at End is made here, by the same rule (m_openStartedHere).
    const pinpoint::SessionStartResult s = m_sessions->start(ownerOf(m_athlete, m_appSettings), type, pattern);
    if (result)
        *result = s.result;
    setOpenSessionId(s.ok() ? s.id : pinpoint::SessionId());
    m_openStartedHere = s.ok() && !onDemand;
    return s.ok();
}

pinpoint::WriteResult ShotProcessor::endOpenSession(bool golferEnd)
{
    pinpoint::WriteResult r{ pinpoint::WriteResult::Status::Unchanged, {} };
    if (m_openSessionObject) {
        // The Session's End: Ended, then — an empty session the golfer started here — trashed by its Athlete
        // (EVT-20), else EVT-09 (DEC-143). A session started on demand never is, whatever ends it. Only End
        // itself, or the end of one started on demand, is asked for here: a start or a resume of the same
        // Athlete ends its running session itself, never discarding (beginSessionFolder).
        // ⚠ `golferEnd` is not read on this path: a non-End end (golferEnd false) of an Athlete's Session is
        // safe only because it can only be an on-demand one (never discarded) — a deliberate session is ended
        // by its own Athlete's start/resume, and an athlete switch is refused while it runs (Stage 0.8g).
        r = m_openSessionObject->end();
    } else if (m_sessions && !m_openSession.isNull()) {
        // THE INTERIM: the same end and the same discard (DEC-93), through the repository.
        r = m_sessions->end(m_openSession);
        if (r.ok() && golferEnd && m_openStartedHere && m_sessions->shots(m_openSession).isEmpty()) {
            QString where;
            if (const pinpoint::WriteResult t = m_sessions->trash(m_openSession, &where); t.ok())
                ppInfo() << "[SwingExport] discarded the empty session (to" << where << ")";
            else
                ppWarn() << "[SwingExport] could not trash the empty session:" << t.error;
        }
    }
    setOpenSession(nullptr);
    return r;
}

pinpoint::SessionId ShotProcessor::captureSession()
{
    if (!m_sessions || !m_appSettings)
        return {};
    const pinpoint::SessionOwner owner = ownerOf(m_athlete, m_appSettings);
    const QString type = sessionTypeName(m_sessionType);

    if (m_sessionBegun) {
        // A session whose folder could not be made at Start: its first swing makes it (as before).
        if (m_openSession.isNull()) {
            if (startOpenSession(type, /*onDemand=*/false)) {
                m_openLibraryKey     = libraryKey(owner.libraryRoot);
                m_sessionFolderReady = true;
                emit activeSessionDirChanged();
            }
        }
        // ⚠ WITHIN A SESSION THE FOLDER IS FIXED (E-01): the athlete's name, the date, the naming
        // pattern and the type may move on; the session keeps its folder.
        return m_openSession;
    }

    // Outside a session (no beginSessionFolder()): a session is started on demand and kept while the
    // athlete, the library, the day and the type stay the same — as the folder was before step 2.2.
    const QString key = owner.athlete.toString() + QLatin1Char('|') + libraryKey(owner.libraryRoot)
                        + QLatin1Char('|') + QDate::currentDate().toString(Qt::ISODate) + QLatin1Char('|') + type;
    if (!m_openSession.isNull() && key != m_onDemandKey) {
        endOpenSession(/*golferEnd=*/false);   // started on demand: never discarded
        emit activeSessionDirChanged();
    }
    if (m_openSession.isNull()) {
        if (startOpenSession(type, /*onDemand=*/true)) {
            m_openedOnDemand = true;
            m_onDemandKey    = key;
            m_openLibraryKey = libraryKey(owner.libraryRoot);
            emit activeSessionDirChanged();
        }
    }
    return m_openSession;
}

pinpoint::ShotRef ShotProcessor::addDeviceOnlyShot(pinpoint::SwingAllocation *allocation)
{
    pinpoint::SwingAllocation none;
    pinpoint::SwingAllocation &out = allocation ? *allocation : none;
    out = {};
    const pinpoint::SessionId session = captureSession();
    if (m_openSessionObject)
        m_openSessionObject->addShot(pinpoint::recording::ShotKind::DeviceOnly, &out);   // DEC-142
    else if (m_sessions && !session.isNull())
        out = m_sessions->allocateSwing(session);   // the interim
    else
        out.result = { pinpoint::WriteResult::Status::Failed, QStringLiteral("no session to record it in") };
    return out.ok() ? out.shot : pinpoint::ShotRef();
}

void ShotProcessor::shotDocumentWritten(const pinpoint::ShotRef &shot)
{
    for (pinpoint::Session *s : { m_openSessionObject.data(), m_shotSession.data() })
        if (s && s->id() == shot.session()) {
            s->shotDocumentWritten(shot);   // EVT-01, once per shot (D19)
            return;
        }
}

void ShotProcessor::onLibraryChanged()
{
    if (!m_sessions || !m_appSettings || m_openSession.isNull())
        return;
    const pinpoint::SessionOwner owner = ownerOf(m_athlete, m_appSettings);
    if (libraryKey(owner.libraryRoot) == m_openLibraryKey)
        return;   // the same library, spelled differently

    if (!m_sessionBegun) {
        // A session started on demand for a swing outside a session: ended; the next swing starts one
        // in the new library.
        endOpenSession(/*golferEnd=*/false);
        emit activeSessionDirChanged();
        return;
    }
    // ⚠ D7 (DEC-154): the session ENDS — the Session's End, its empty discard included (as the store's
    // restart made it before step 3.4a-ii), written if the old library is still reachable; if not it stays
    // in progress until the athlete's next Start there, DEC-73 — and a NEW one of the same type starts in
    // the new library. The club and the declared focus are SessionController's and carry over
    // untouched; the focus applies from the new session's first shot. The views bound to
    // activeSessionDir follow it as they follow any Start. (This replaces step 0.3's stopgap.)
    const pinpoint::SessionId oldId = m_openSession;
    const auto oldEntry = m_openSessionObject ? m_openSessionObject->entry() : m_sessions->open(m_openSession);
    const QString type  = oldEntry ? oldEntry->identity.type() : QString();
    const QString was   = oldEntry ? oldEntry->folder : QString();
    const pinpoint::WriteResult ended = endOpenSession(/*golferEnd=*/true);
    if (!ended.ok()) {
        ppWarn() << "[SessionStore] the session in the old library could not be ended (it stays in progress"
                    " until the athlete's next session there):" << was << "-" << ended.error;
        // A folder still there (a held lock, a read-only file) is a failure to report; a library that has
        // gone (a dropped share) is the expected case, and the "not being saved" notices cover it. Still
        // there: the folder, whatever its session file's state (the repository's read; nothing here touches the
        // disk) — so an unreadable session file in a present folder is still shown (DEC-140).
        if (!oldId.isNull() && m_sessions->folderPresent(oldId))
            emit sessionFileFailed(ended.error);   // the old library is still there, but its end was not saved
    }
    const bool started = startOpenSession(type, /*onDemand=*/false);
    ppWarn() << "[SwingExport] the library changed during a session: the session" << was
             << "ended, and a new one started in" << m_openSessionDir;
    m_openedOnDemand     = false;
    m_openLibraryKey     = libraryKey(owner.libraryRoot);
    m_sessionFolderReady = started;   // if not, the next swing tries again (captureSession)
    emit sessionRestartedForLibrary();   // first: the focus's "from shot" is reset before the views re-point
    emit activeSessionDirChanged();
}

QString ShotProcessor::todaySessionDir(int sessionType)
{
    if (!m_appSettings || !m_athlete || !m_sessions)
        return {};
    const QString type = sessionTypeName(sessionType);
    if (pinpoint::Athlete *a = captureAthlete()) {
        const pinpoint::Session *s = a->session(a->todaysSession(type));
        const auto entry = s ? s->entry() : std::nullopt;
        return entry ? entry->folder : QString();
    }
    const pinpoint::SessionId id = m_sessions->todaysSession(ownerOf(m_athlete, m_appSettings), type);
    const auto entry = id.isNull() ? std::nullopt : m_sessions->open(id);
    return entry ? entry->folder : QString();
}

void ShotProcessor::beginSessionFolder(int sessionType, bool extend)
{
    m_sessionType = sessionType;   // so a shot's session and its folder name use the right type
    // ⚠ THE OUTCOME IS THE POINT.  A session that could not create its folder used to start happily on
    // an unwritable library, and the first thing the golfer learned was a toast after a swing had
    // already been lost — precisely what happened on 1 September 2026 with /mnt/swingdata unmounted.
    // The session-start preflight reads it through sessionFolderReady().
    m_sessionFolderReady = false;
    pinpoint::Athlete *athlete = captureAthlete();
    // One still open (on demand): ended, never discarded. The current Athlete's own running session is
    // ended by its startSession / resumeSession below (DEC-78, DEC-134), never discarded either, as before;
    // any other — another athlete's on-demand session, or one no Athlete holds — is ended here.
    if (!m_openSession.isNull() && !(m_openSessionObject && m_openSessionObject->athlete() == athlete))
        endOpenSession(/*golferEnd=*/false);
    setOpenSession(nullptr);
    m_sessionBegun = true;
    if (m_sessions && m_appSettings && m_athlete) {
        const pinpoint::SessionOwner owner = ownerOf(m_athlete, m_appSettings);
        const QString type = sessionTypeName(sessionType);
        bool started = false;
        // Extend: today's latest session of this type, resumed with its own SessionId (MD-05: DEC-134).
        // A resume that is refused or cannot be written starts a new session instead.
        if (extend) {
            if (athlete) {
                const pinpoint::SessionId id = athlete->todaysSession(type);
                if (!id.isNull()) {
                    pinpoint::WriteResult r;
                    if (pinpoint::Session *s = athlete->resumeSession(id, type, &r)) {
                        setOpenSession(s);
                        started = true;
                    } else {
                        ppWarn() << "[SwingExport] today's session could not be continued - a new one starts:" << r.error;
                        emit sessionResumeFailed(r.error);   // the golfer asked to continue: say why they did not
                    }
                }
            } else {
                const pinpoint::SessionId id = m_sessions->todaysSession(owner, type);   // the interim
                if (!id.isNull()) {
                    if (const pinpoint::WriteResult r = m_sessions->resume(id, type); !r.ok()) {
                        ppWarn() << "[SwingExport] today's session could not be continued - a new one starts:" << r.error;
                        emit sessionResumeFailed(r.error);
                    } else {
                        setOpenSessionId(id);
                        started = true;
                    }
                }
            }
        }
        if (!started) {
            pinpoint::WriteResult r;
            started = startOpenSession(type, /*onDemand=*/false, &r);
            if (!started)
                ppError() << "[SwingExport] no session started:" << r.error;
        }
        m_openLibraryKey     = libraryKey(owner.libraryRoot);
        m_sessionFolderReady = started;
    }
    emit activeSessionDirChanged();
}

void ShotProcessor::endSessionFolder()
{
    if (!m_openSession.isNull()) {
        // THE END (DEC-143): the Session's own, with its empty discard (DEC-93) — a session the golfer
        // started here that captured nothing goes to the trash; one started on demand never does.
        const pinpoint::WriteResult r = endOpenSession(/*golferEnd=*/true);
        if (!r.ok())
            emit sessionFileFailed(r.error);
    }
    setOpenSession(nullptr);
    m_sessionBegun = false;
    emit activeSessionDirChanged();
}

// ---------------------------------------------------------------------------
// Worker completion → join
// ---------------------------------------------------------------------------

void ShotProcessor::onAnalysisFinished()
{
    if (!m_analysisInFlight)
        return;   // already joined blockingly in finishNowBlocking()
    m_analysisInFlight = false;
    {
        pinpoint::analysis::AnalysedRecording run = m_analysisWatcher.result();
        m_analysisResult = std::move(run.analysis);
        m_integrity      = std::move(run.integrity);
    }
    m_analysisOutcome  = m_analysisResult.ok ? Outcome::Succeeded : Outcome::Failed;
    setAnalysisProgress(1.0);   // beat any still-queued worker updates to full
    if (m_analysisResult.ok)
        ppInfo() << "[ShotProcessor] analysis done — score" << m_analysisResult.score;
    else
        emit analysisFailed(m_analysisResult.error);   // toast, not a log line

    // Persistent club-length prior update (club_length_fusion.h / plan: robust
    // club length — starry-shimmying-wind) — LIVE path only. m_lengthPriorKey was
    // stashed from the capture snapshot (empty ⇒ no athlete/club/fixed-in-place
    // face-on camera this shot, nothing to persist). Folds ONLY the PRIOR-FREE
    // fusedInstant* value into the prior — never the WITH-prior fusedPx, which
    // would self-reinforce (club_length_fusion.h updateLengthPrior comment).
    if (m_analysisResult.ok && m_analysisResult.detail && !m_lengthPriorKey.isEmpty()) {
        const pinpoint::analysis::ClubLengthEstimate &lengths = m_analysisResult.detail->shaft.lengths;
        const pinpoint::analysis::LengthFusionConfig cfg;   // defaults — live path doesn't sweep fusion.*
        if (lengths.fusedInstantPx > 0.0 && lengths.fusedInstantConf >= cfg.updateConfMin) {
            pinpoint::analysis::updateLengthPrior(m_lengthPriorState, lengths.fusedInstantPx,
                                                  lengths.fusedInstantConf, cfg);
            AppSettings  fallback;
            AppSettings *s = m_appSettings ? m_appSettings : &fallback;
            QVariantMap all   = s->clubLenPrior();
            QVariantMap entry = all.value(m_lengthPriorKey).toMap();
            entry[QStringLiteral("emaPx")]          = m_lengthPriorState.emaPx;
            entry[QStringLiteral("varPx")]          = m_lengthPriorState.varPx;
            entry[QStringLiteral("n")]              = m_lengthPriorState.n;
            entry[QStringLiteral("disagreeRun")]    = m_lengthPriorState.disagreeRun;
            entry[QStringLiteral("lengthMm")]       = static_cast<int>(std::lround(m_analysisJob.clubLengthM * 1000.0));
            entry[QStringLiteral("frameW")]         = m_analysisResult.detail->shaft.frameWidth;
            entry[QStringLiteral("frameH")]         = m_analysisResult.detail->shaft.frameHeight;
            entry[QStringLiteral("lastUpdatedUtc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            all[m_lengthPriorKey] = entry;
            s->setClubLenPrior(all);
        }
    }

    // Pose pass is done — only now launch the x264 export, so it has the cores
    // to itself and never contends with the (just-finished) inference. maybeJoin()
    // still covers the export-skipped path completing synchronously.
    startSwingSave();
    maybeJoin();
}

void ShotProcessor::onSwingSaveFinished()
{
    if (!m_swingSaveInFlight)
        return;   // already joined blockingly in finishNowBlocking()
    m_swingSaveInFlight = false;
    const pinpoint::SwingExportResult result = m_swingSaveWatcher.result();
    if (result.ok) {
        ppInfo() << "[SwingExport] media saved:" << result.swingDir;
        m_exportOutcome  = Outcome::Succeeded;
        m_thumbnailPath  = result.thumbnailPath;
        m_exportManifest = result.manifest;   // unified swing.json written at the join
        emit swingSaved(result.swingDir);
    } else {
        ppError() << "[SwingExport] swing save failed:" << result.error;
        m_exportOutcome = Outcome::Failed;
        emit swingSaveFailed(result.error);
    }
    maybeJoin();   // failure joins identically — the buffer must resume
}

void ShotProcessor::maybeJoin()
{
    if (m_state != State::Processing)
        return;   // aborted via finishNowBlocking()
    // The decision logic lives in shot_outcome.h so it can be tested without the
    // camera stack behind it (shot_outcome_test). Filled in twice: once now, for
    // the persist path, and again below once the write has been attempted.
    pinpoint::ShotJoinInputs join;
    join.analysis            = m_analysisOutcome;
    join.mediaExport         = m_exportOutcome;
    join.hasAnalysisDetail   = static_cast<bool>(m_analysisResult.detail);
    join.swingDirAllocated   = !m_swingDir.isEmpty();
    join.skipAnalysisCapture = m_skipAnalysisCapture;
    join.hasReplayTracks     = !m_replayTracks.empty();

    const pinpoint::ShotJoinDecision gate = pinpoint::decideJoin(join);
    if (!gate.ready)
        return;   // wait for BOTH workers

    // ── Everything below runs OFF the GUI thread ───────────────────────────────────────────────────
    //
    // The document write used to run right here, on the UI thread, and the user watched the app stop:
    // the document is ~28 MB on a wrist swing (analysis.pose2d alone is ~13 MB), it is serialised
    // INDENTED, and the library is a network share. None of it needs the GUI thread, and the freeze
    // landed with the replay on screen, which is the worst moment to stop moving.
    //
    // Nothing is announced until it returns (onSwingDocWritten): the carousel row, the terminal
    // statement and the replay promotion are all claims about what survives a restart. In flight the UI
    // says nothing; a failed write is surfaced when it lands, as the shot's terminal statement.
    //
    // The worker borrows the Recording (and through it the window) and m_analysisResult.detail (the
    // write). Both stay alive because the processor remains in State::Processing until the handler
    // runs — a new shot clears them and cannot start from here — and finishNowBlocking() joins this
    // watcher on teardown.
    const pinpoint::PersistPath persist = pinpoint::persistPathFor(join);

    // The header the document gets: the exporter's manifest, or — a degraded persist (export failed
    // or skipped, analysis did not) — the ONE header writer's analysis-only header (recording_header.h),
    // as the exporter calls it minus the media, so a reloaded analysis-only shot is indistinguishable
    // from an exported one apart from hasVideo=false.
    QJsonObject header = m_exportManifest;
    if (persist == pinpoint::PersistPath::AnalysisOnlyDocument) {
        const int64_t t0 = m_swingWindow->startTimestampUs();
        header = pinpoint::SwingExporter::analysisOnlyHeader(m_exportJob, t0, m_swingWindow->endTimestampUs() - t0);
    }

    // Storage → Save pose keypoints. Read here, with the other settings, not on the worker.
    const bool savePose = m_appSettings ? m_appSettings->savePoseKeypoints() : true;

    m_docWriteInFlight = true;
    m_docWriteWatcher.setFuture(QtConcurrent::run(
        [this, persist, header, savePose]() -> SwingDocWriteResult {
            // The Recording's integrity verdicts — the IMU re-fusion parity and the frame-timestamp
            // holes / coverage of each camera lane — by the ONE rule (integrityFor, the analysis
            // service): reached with the analysis, or here, alone, for a capture that ran none. A
            // mismatch or a hole flags the carousel item (⚠); the blocks are persisted.
            pinpoint::analysis::IntegrityVerdicts verdicts;
            if (m_integrity)
                verdicts = *m_integrity;
            else if (m_capture)
                verdicts = m_analysisService.integrity(m_capture->recording);

            pinpoint::capture::LiveDocumentInput in;
            in.repository = m_repository;
            in.shot       = m_shot;
            in.swingDir   = m_swingDir;
            in.persist    = persist;
            in.header     = header;
            in.integrity  = &verdicts;
            in.analysis   = m_analysisResult.detail ? m_analysisResult.detail.get() : nullptr;
            in.windowT0Us = m_swingWindow ? m_swingWindow->startTimestampUs() : 0;
            in.reviewClub = m_capture ? m_capture->athlete.reviewClub : QString();
            in.savePose   = savePose;
            const pinpoint::capture::LiveDocument doc = pinpoint::capture::writeLiveDocument(in);

            SwingDocWriteResult out;
            out.wrote             = doc.wrote;
            out.error             = doc.error;
            out.manifest          = doc.manifest;
            out.dataWarningDetail = doc.dataWarningDetail;
            out.analysisDetail    = doc.analysisDetail;
            out.club              = doc.club;
            out.documentPath      = doc.documentPath;
            return out;
        }));
}

// The document is written (or it is not). Everything the shot is ANNOUNCED as happens here — see
// SwingDocWriteResult. Runs on the GUI thread, strictly after the worker lambda returned.
void ShotProcessor::onSwingDocWritten()
{
    if (!m_docWriteInFlight)
        return;                     // a queued delivery after finishNowBlocking() — no-op
    m_docWriteInFlight = false;
    if (m_state != State::Processing)
        return;                     // aborted underneath us

    const SwingDocWriteResult res = m_docWriteWatcher.result();
    // The manifest now carries the integrity blocks the checks added; later readers (the degraded
    // persist path, re-analysis write-back) must see them.
    m_exportManifest = res.manifest;

    pinpoint::ShotJoinInputs join;
    join.analysis            = m_analysisOutcome;
    join.mediaExport         = m_exportOutcome;
    join.hasAnalysisDetail   = static_cast<bool>(m_analysisResult.detail);
    join.swingDirAllocated   = !m_swingDir.isEmpty();
    join.skipAnalysisCapture = m_skipAnalysisCapture;
    join.hasReplayTracks     = !m_replayTracks.empty();

    const bool analysisOk = m_analysisOutcome == Outcome::Succeeded;
    const bool exportOk   = m_exportOutcome   == Outcome::Succeeded;
    const QVariantMap dataWarningDetail = res.dataWarningDetail;
    const bool        dataWarning       = !dataWarningDetail.isEmpty();

    // savedSwingDir is set only when a swing.json was actually written, so the carousel row links to a
    // real file (rating/note write-through, reload) and an unwritten shot stays in-memory only.
    QString savedSwingDir;
    if (res.wrote) {
        savedSwingDir = m_swingDir;
        ppInfo() << "[SwingDoc] wrote" << res.documentPath
                 << (analysisOk ? "(with analysis)" : "(raw only)");
        // EVT-01's moment (D19: DEC-168): the shot's first document has landed — with or without its
        // analysis (an analysis-skipped corpus shot fires too, from what it has: BRIEF-NOTES U12). Its
        // Session announces it, once; a session no Athlete holds has no Session to (the interim).
        if (m_shotSession)
            m_shotSession->shotDocumentWritten(m_shot);
    } else if (!res.error.isEmpty()) {
        ppError() << "[SwingDoc]" << res.error;
    }

    // The club the document records (its seeded review.club, settled at the freeze), not re-resolved:
    // the row and the file must name the same club.
    const QString shotClub = res.club;

    // ⏱ What this handler costs, per phase, one line per shot. `notify` is the emit below and everything
    // QML does synchronously inside it: the bindings re-evaluate and, on the first read, the detail is
    // converted to JavaScript ONCE (qml_payload.h). Before that type existed the conversion ran on every
    // read — ~18 of them — and this line read `notify 7773 ms` on the studio (16 Sept 2026). If it ever
    // climbs back into the seconds, a new QVariantMap-sized reader has appeared; find it, don't retune.
    QElapsedTimer phase;
    phase.start();
    const qint64 tStart = phase.elapsed();

    // The shot happened — it always lands on the carousel, with whatever the pipeline
    // produced, carrying the same shotClub the document above records. The user can still
    // change it per-shot via the swing-edit popover (persisted to review.club).
    //
    // Publish the analyzed detail of the shot about to replay (the ScreenWrist in-replay
    // graph binds to it) before addShot, so it's ready when REPLAYING begins.
    // The map is the one projection of the analysis block the document got (liveAnalysisDetail),
    // built by the worker beside the write: window time, the reloaded shot's keys.
    m_replayAnalysisDetail.set(analysisOk ? res.analysisDetail : QVariantMap{});
    const qint64 tDetail = phase.elapsed();
    emit replayAnalysisDetailChanged();
    const qint64 tEmit = phase.elapsed();

    int newShotId = -1;
    if (m_shotModel) {
        const QUrl thumbUrl = m_thumbnailPath.isEmpty()
                                  ? QUrl()
                                  : QUrl::fromLocalFile(m_thumbnailPath);
        newShotId = m_shotModel->addShot(savedSwingDir,
                             m_timestampLabel,
                             shotClub,
                             exportOk,
                             thumbUrl,
                             analysisOk ? m_analysisResult.tracePoints : QVariantList{},
                             analysisOk ? m_analysisResult.score : 0,
                             analysisOk ? m_analysisResult.metrics : QVariantMap{},
                             m_replayAnalysisDetail.map(),
                             dataWarning, dataWarningDetail);
    }

    const qint64 tRow = phase.elapsed();
    ppInfo() << "[ShotProcessor] join tail on the GUI thread:"
             << "analysisDetail" << (tDetail - tStart) << "ms,"
             << "notify" << (tEmit - tDetail) << "ms,"
             << "addShot" << (tRow - tEmit) << "ms,"
             << "total" << tRow << "ms";

    m_lastShotId = newShotId;

    // "Reviewable on disk": analysis + export both succeeded AND a swing.json was
    // actually written — that is the swing the UI promotes straight into Review for
    // instant playback (the disk replay reads the just-written MP4(s), not the ring).
    join.documentWritten = !savedSwingDir.isEmpty();
    const pinpoint::ShotJoinDecision decision = pinpoint::decideJoin(join);
    const bool reviewableOnDisk = decision.reviewableOnDisk;

    if (decision.terminal == pinpoint::Terminal::Processed)
        emit shotProcessed(newShotId, savedSwingDir);
    else
        emit shotFailed(decision.analysisFaulted ? m_analysisResult.error
                      : decision.exportFaulted   ? tr("The video could not be saved")
                                                 : tr("Nothing was saved for this shot"));

    // ⭐ §7.5 R4 — ONE TERMINAL STATEMENT PER SHOT, NOT ONE PER STAGE.
    // The stages above each announced themselves and the user had to assemble
    // the verdict; worse, the one that means "this shot produced nothing at
    // all" — shotFailed — reached a controller that discarded its message and
    // raised nothing on screen, so it has never once been said out loud.  The
    // fold is already computed here; this is only saying what it came to.
    // Stage detail keeps going to the app log, which is where it belongs.
    // The carousel's number, not the model's id — they diverge once a shot is
    // trashed, and "Shot 7" has to mean the row the user can point at.
    const int ordinal = (m_shotModel && newShotId >= 0) ? m_shotModel->ordinalForId(newShotId) : 0;
    // ⚠ FAULTED, NOT "DID NOT SUCCEED". A stage skipped on request went wrong
    // with nothing, and this signal is the ONE thing the golfer is told about
    // the shot: a corpus capture with analysis deliberately off announced
    // "analysis incomplete" on every ball, and an IMU-only shot whose
    // analysis-only swing.json was written exactly as designed announced "not
    // saved". Both were the pipeline doing as it was asked.
    emit shotOutcome(ordinal, !decision.exportFaulted, !decision.analysisFaulted);

    // Post-shot playback now lives on the Review stage: a reviewable shot is auto-
    // promoted into Review (disk replay) by the UI from shotProcessed(), so skip the
    // on-screen ¼× window transient and resume capture immediately. Only when there
    // is no reviewable disk shot do we fall back to the in-window transient (reading
    // the frozen window's frames directly) so the user still sees the swing they
    // just made.
    // Auto-replay after capture is a user setting (View menu). Off → the shot
    // still lands on the carousel, but neither the disk-replay auto-promotion (gated
    // in Main.qml.onShotProcessed) nor this in-window fallback transient plays. Handy
    // for corpus capture, where uninterrupted back-to-back hitting matters.
    const bool autoReplay = !m_appSettings || m_appSettings->autoReplayAfterCapture();
    join.autoReplay = autoReplay;

    if (pinpoint::decideJoin(join).after == pinpoint::AfterJoin::Replay) {
        startReplay();
    } else if (reviewableOnDisk) {
        finishShot();
    } else {
        if (!autoReplay)
            ppInfo() << "[ShotProcessor] replay skipped — auto-replay disabled (View menu)";
        else if (m_skipAnalysisCapture)
            ppInfo() << "[ShotProcessor] replay skipped — skip-analysis corpus capture";
        else
            ppInfo() << "[ShotProcessor] replay skipped — no captured camera tracks"
                     << "(analysisOk" << analysisOk << "exportOk" << exportOk << ")";
        finishShot();
    }
}

// ---------------------------------------------------------------------------
// ¼× replay (migrated from CameraManager)
// ---------------------------------------------------------------------------

// Breathing room after the located Finish, so the club is seen to arrive rather than the
// picture cutting on the frame the detector settled on. Matches the segmenter's own
// boundPadUs in spirit; kept local because this is a viewing choice, not a measurement.
static constexpr int64_t kReplayFinishPadUs = 250000;   // 250 ms

void ShotProcessor::startReplay()
{
    // Anchor to the actual first/last captured entry so replay starts
    // immediately rather than waiting for the (potentially empty) leading
    // portion of the window.
    // The view boundary: the detail is window time (liveAnalysisDetail against this same start),
    // so the published playhead, span and impact are offset by it (shot_processor.h).
    m_replayT0Us          = m_swingWindow->startTimestampUs();
    m_replayWindowStartUs = m_swingWindow->endTimestampUs();
    m_replayWindowEndUs   = m_swingWindow->startTimestampUs();
    for (const ReplayTrack &track : m_replayTracks) {
        if (!track.entries.empty()) {
            m_replayWindowStartUs = std::min(m_replayWindowStartUs,
                                             track.entries.front().timestamp_us);
            m_replayWindowEndUs   = std::max(m_replayWindowEndUs,
                                             track.entries.back().timestamp_us);
        }
    }

    // Clamp to the detected swing (v3 G2): replay starts at address, not
    // mid-fidget. conf 0 (no IMU / failed pre-stage) keeps the full span.
    if (m_segmentation.conf > 0.f) {
        const int64_t lo = std::max(m_replayWindowStartUs, m_segmentation.swingStartUs);
        // END ON THE ANALYSED FINISH WHERE THERE IS ONE. m_segmentation is the PRE-STAGE
        // ladder — fused from the IMU before the club track exists — so its swingEndUs
        // carries the segmenter's window-edge fallback whenever the quiet-run detector
        // never fired, which on this corpus was every swing: the replay then ran ~1.3 s
        // past a finish the camera had already located. The analysed ladder has been
        // arbitrated against the club track by then (timeline_fusion.h), so its Finish is
        // the real one. Bounds are deliberately NOT rewritten by fusion — it moves labels,
        // never spans — so the correction belongs here, at the point of use.
        int64_t endUs = m_segmentation.swingEndUs;
        if (m_analysisResult.ok && m_analysisResult.detail) {
            using pinpoint::analysis::Phase;
            const auto &seg = m_analysisResult.detail->segmentation;
            // The analysis leaves the pipeline in WINDOW time (its one rebase, step 1.5); the replay
            // runs on the ring clock, so the window's start is added back here, at the view boundary.
            const int64_t toRing = m_analysisResult.detail->timeBase == pinpoint::analysis::TimeBase::Window
                                       ? m_replayT0Us : 0;
            if (const auto *fin = seg.eventFor(Phase::Finish))
                endUs = fin->t_us + toRing + kReplayFinishPadUs;
        }
        const int64_t hi = std::min(m_replayWindowEndUs, endUs);
        if (hi > lo) {
            m_replayWindowStartUs = lo;
            m_replayWindowEndUs   = hi;
        }
    }

    for (ReplayTrack &track : m_replayTracks) {
        track.idx = 0;
        track.ctrl->setReplaying(true);
    }

    m_replayPositionUs = m_replayWindowStartUs;   // playhead at the window start
    emit replaySpanChanged();
    emit replayPositionChanged();

    setState(State::Replaying);

    m_replayElapsed.start();
    m_replayTimer = new QTimer(this);
    m_replayTimer->setInterval(16);   // ~60 Hz drive
    connect(m_replayTimer, &QTimer::timeout, this, &ShotProcessor::onReplayTick);
    m_replayTimer->start();
}

void ShotProcessor::onReplayTick()
{
    // Quarter speed: divide real elapsed time by 4 to get virtual footage time.
    const int64_t realElapsedUs   = m_replayElapsed.elapsed() * 1000LL;
    const int64_t virtualTimeUs   = realElapsedUs / 4;
    const int64_t footageDuration = m_replayWindowEndUs - m_replayWindowStartUs;

    // Publish the playhead (window µs) — the graph and video both follow this one clock.
    const int64_t pos = m_replayWindowStartUs + virtualTimeUs;
    if (pos != m_replayPositionUs) {
        m_replayPositionUs = pos;
        emit replayPositionChanged();
    }

    if (virtualTimeUs >= footageDuration) {
        stopReplay(true);
        return;
    }

    for (ReplayTrack &track : m_replayTracks) {
        if (track.loop && track.entries.size() > 1) {
            // The impact camera loops its keep band on its own clock, in phase
            // with the playhead: the loop's local time is the playhead's
            // distance from the band's first frame, wrapped at the band's
            // length, so the band's impact frame is on screen exactly when the
            // playhead crosses impact and the loop runs around it otherwise.
            const int64_t first  = track.entries.front().timestamp_us;
            const int64_t last   = track.entries.back().timestamp_us;
            const int64_t period = std::max<int64_t>(1, (last - first) / int64_t(track.entries.size() - 1));
            const int64_t dur    = std::max<int64_t>(1, last - first + period);
            const int64_t local  = ((pos - first) % dur + dur) % dur;
            const int64_t want   = first + local;
            auto it = std::upper_bound(track.entries.begin(), track.entries.end(), want,
                                       [](int64_t t, const pinpoint::IndexEntry &e) {
                                           return t < e.timestamp_us;
                                       });
            track.idx = (it == track.entries.begin()) ? 0 : size_t(it - track.entries.begin()) - 1;
        } else {
        // Advance to the newest frame whose offset from the first entry <= virtual time.
        while (track.idx + 1 < track.entries.size()) {
            const int64_t nextOffset =
                track.entries[track.idx + 1].timestamp_us - m_replayWindowStartUs;
            if (nextOffset <= virtualTimeUs)
                ++track.idx;
            else
                break;
        }
        }

        const auto &entry  = track.entries[track.idx];
        const auto  handle = m_swingWindow->payloadOf(entry);
        if (!handle.data) continue;

        const auto &fd = m_swingWindow->formatOf(track.sourceId);
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format)) {
            track.ctrl->displayReplayFrame(
                handle.data,
                handle.bytes,
                static_cast<int>(cfmt->width),
                static_cast<int>(cfmt->height),
                cfmt->pixel_format,
                cfmt->plane_strides);
        }
    }
}

void ShotProcessor::cancelReplay()
{
    if (m_state != State::Replaying)
        return;
    ppInfo() << "[ShotProcessor] replay cancelled by user";
    stopReplay(true);   // normal end-of-replay path, taken early
}

void ShotProcessor::stopReplay(bool thenFinish)
{
    if (m_replayTimer) {
        m_replayTimer->stop();
        m_replayTimer->deleteLater();
        m_replayTimer = nullptr;
    }

    for (ReplayTrack &track : m_replayTracks)
        track.ctrl->setReplaying(false);

    if (thenFinish)
        finishShot();
}

// ---------------------------------------------------------------------------
// Finish / teardown
// ---------------------------------------------------------------------------

void ShotProcessor::finishShot()
{
    m_capture.reset();       // its Recording points into the window
    m_swingWindow.reset();   // both workers returned, replay stopped
    m_replayTracks.clear();

    // The window held the buffer Paused; now that it is gone, return the
    // buffer to whatever the user last chose (Capture/Stop). The unconditional
    // bufferStateChanged it emits re-arms ShotController.
    m_cameraManager->applyCaptureIntent();

    setState(State::Idle);
}

void ShotProcessor::abortToIdle()
{
    m_postRollTimer.stop();
    m_gatherTimer.stop();
    // ⚠ RELEASING THIS IS NOT OPTIONAL. It holds the resume guard from the pause
    // instant, and CameraManager::resumeBuffer() refuses while the guard is up —
    // so an abandoned gather that kept it would leave the buffer unable to
    // resume for the rest of the session, with capture silently dead.
    m_ringSource.reset();
    setState(State::Idle);
}

void ShotProcessor::finishNowBlocking()
{
    m_postRollTimer.stop();   // a pending shot is forfeited — acceptable on teardown
    m_gatherTimer.stop();

    if (m_state == State::Replaying)
        stopReplay(false);

    // Block until the pre-stage and both workers have returned: they read ring
    // memory through the window, which the caller is about to invalidate
    // (deregister/teardown).
    if (m_segmentationInFlight)
        m_segmentationWatcher.waitForFinished();
    if (m_analysisInFlight)
        m_analysisWatcher.waitForFinished();
    if (m_swingSaveInFlight)
        m_swingSaveWatcher.waitForFinished();
    // ⚠ AND THE DOCUMENT WRITER. It borrows the window (integrity checks) and the analysis detail
    // (the write itself), both of which the caller is about to invalidate — and a shot whose write was
    // abandoned here would vanish at the next restart having been recorded, encoded and analysed.
    if (m_docWriteInFlight)
        m_docWriteWatcher.waitForFinished();
    // The queued finished() handlers will still be delivered later; flag-off
    // makes them no-ops.
    m_segmentationInFlight = false;
    m_analysisInFlight  = false;
    m_swingSaveInFlight = false;
    m_docWriteInFlight  = false;

    m_capture.reset();
    m_swingWindow.reset();
    m_replayTracks.clear();
    // A gather abandoned on teardown: release the guard the window never adopted
    // (a no-op once the window exists, which took ownership of it).
    m_ringSource.reset();

    // Deliberately no applyCaptureIntent(): callers (setSelected, destructors)
    // own the buffer-state sequence around source registration.
    setState(State::Idle);
}
