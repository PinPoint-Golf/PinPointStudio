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
#include "athlete_controller.h"
#include "camera_instance.h"
#include "camera_manager.h"
#include "device_enumerator.h"
#include "event_buffer.h"
#include "imu_instance.h"
#include "imu_manager.h"
#include "session_controller.h"
#include "shot_list_model.h"
#include "../Analysis/imu_refusion_check.h"
#include "../Analysis/imu_vision_fuser.h"
#include "../Analysis/phase_segmenter.h"
#include "../Analysis/swing_analysis.h"
#include "../Export/swing_doc.h"
#include "../Core/pp_debug.h"
#include "pp_version.h"

#include <QSysInfo>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTime>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <variant>

namespace {

// Post-trigger capture continuation, per source — the buffer keeps capturing
// for this long after the trigger so the follow-through lands in the ring
// before it freezes. Auto detectors use 1250 ms (segmentation v3: the finish
// is follow-through ~0.67 s + decay-to-still, physically truncated at 500 ms
// — design A.6; impact then sits ~3.75 s into the 5 s ring, leaving ample
// pre-impact room). Manual stays 500 ms: the user presses after the swing and
// impact is back-dated, so the finish is already in the ring.
constexpr int kPostRollManualMs   = 500;
constexpr int kPostRollImuMs      = 1250;
constexpr int kPostRollPoseMs     = 1250;
constexpr int kPostRollBallMs     = 1250;
constexpr int kPostRollAcousticMs = 1250;

int postRollMsFor(ShotController::Source s)
{
    switch (s) {
    case ShotController::Source::Manual:   return kPostRollManualMs;
    case ShotController::Source::Imu:      return kPostRollImuMs;
    case ShotController::Source::Pose:     return kPostRollPoseMs;
    case ShotController::Source::Ball:     return kPostRollBallMs;
    case ShotController::Source::Acoustic: return kPostRollAcousticMs;
    }
    return kPostRollManualMs;
}

// The entire trailing ring — every source's window_duration is 5 s.
constexpr std::chrono::milliseconds kWindowDuration{5000};

// Convert the analyzer's rich SwingAnalysis into QML-friendly data for the shot's
// analysisDetail role (the future scrubbable metric graph reads series + phases).
QVariantMap toAnalysisDetail(const pinpoint::analysis::SwingAnalysis &a)
{
    using namespace pinpoint::analysis;
    QVariantList series;
    for (const MetricSeries &m : a.series) {
        QVariantList ts, vs, samples;
        for (const int64_t t : m.t_us) ts.append(static_cast<qlonglong>(t));
        for (const double v : m.value) vs.append(v);
        for (const PhaseSample &ps : m.phaseSamples)
            samples.append(QVariantMap{ { QStringLiteral("phase"), int(ps.phase) },
                                        { QStringLiteral("t_us"),  static_cast<qlonglong>(ps.t_us) },
                                        { QStringLiteral("value"), ps.value },
                                        { QStringLiteral("band"),  ps.band } });
        series.append(QVariantMap{ { QStringLiteral("key"),   m.key },
                                   { QStringLiteral("label"), m.label },
                                   { QStringLiteral("unit"),  m.unit },
                                   { QStringLiteral("t_us"),  ts },
                                   { QStringLiteral("value"), vs },
                                   { QStringLiteral("phaseSamples"), samples } });
    }
    QVariantList phases;
    for (const PhaseEvent &e : a.phases)
        phases.append(QVariantMap{ { QStringLiteral("phase"),   int(e.phase) },
                                   { QStringLiteral("t_us"),    static_cast<qlonglong>(e.t_us) },
                                   { QStringLiteral("conf"),    e.conf },
                                   { QStringLiteral("segment"), int(e.provenance) } });
    QVariantMap detail{ { QStringLiteral("tier"),    a.tier },
                        { QStringLiteral("overall"), a.score.overall },
                        { QStringLiteral("series"),  series },
                        { QStringLiteral("phases"),  phases } };

    // Resemblance estimand + uncertainty interval (design §B.0a/§B.7) — same sibling-key
    // shape SwingDocReader reloads, so live and reloaded swings expose identical detail.
    if (a.score.kind == ScoreKind::Resemblance) {
        QVariantMap res;
        for (auto it = a.score.resemblance.constBegin(); it != a.score.resemblance.constEnd(); ++it)
            res.insert(it.key(), it.value());
        detail.insert(QStringLiteral("resemblance"), res);
        detail.insert(QStringLiteral("pattern"), a.score.patternLabel);
        detail.insert(QStringLiteral("blended"), a.score.blended);
    }
    if (a.score.interval.valid())
        detail.insert(QStringLiteral("interval"),
                      QVariantMap{ { QStringLiteral("halfWidth"), a.score.interval.halfWidth },
                                   { QStringLiteral("lo"),        a.score.interval.lo },
                                   { QStringLiteral("hi"),        a.score.interval.hi } });

    // Swing bounds + ladder meta (v3 G2) — same shape the doc reader reloads.
    if (a.segmentation.swingEndUs > a.segmentation.swingStartUs)
        detail.insert(QStringLiteral("segmentation"),
                      QVariantMap{
                          { QStringLiteral("swingStartUs"),
                            static_cast<qlonglong>(a.segmentation.swingStartUs) },
                          { QStringLiteral("swingEndUs"),
                            static_cast<qlonglong>(a.segmentation.swingEndUs) },
                          { QStringLiteral("conf"),    double(a.segmentation.conf) },
                          { QStringLiteral("version"), a.segmentation.version } });

    // ShaftTracker blocks for the replay overlay — IDENTICAL shapes to the
    // swing.json blocks SwingDocReader reloads (swing_doc.cpp), keypoints and
    // club grip/head normalized 0..1 so QML never sees pixel spaces.
    if (!a.pose2d.frames.empty()) {
        QVariantList frames;
        for (const PoseFrame2D &f : a.pose2d.frames) {
            QVariantList kp;
            kp.reserve(17 * 3);
            for (int j = 0; j < 17; ++j) {
                kp.append(f.kp[size_t(j)].x());
                kp.append(f.kp[size_t(j)].y());
                kp.append(double(f.conf[size_t(j)]));
            }
            frames.append(QVariantMap{
                { QStringLiteral("t_us"), static_cast<qlonglong>(f.t_us) },
                { QStringLiteral("kp"),   kp },
                { QStringLiteral("lead"),  QVariantList{ f.leadHand.x(),  f.leadHand.y() } },
                { QStringLiteral("trail"), QVariantList{ f.trailHand.x(), f.trailHand.y() } },
                { QStringLiteral("handConf"), double(f.handConf) } });
        }
        detail.insert(QStringLiteral("pose2d"),
                      QVariantMap{ { QStringLiteral("camera"), int(a.pose2d.camera) },
                                   { QStringLiteral("frames"), frames } });
    }
    if (a.shaft.valid && !a.shaft.samples.empty()
        && a.shaft.frameWidth > 0 && a.shaft.frameHeight > 0) {
        const double iw = 1.0 / a.shaft.frameWidth, ih = 1.0 / a.shaft.frameHeight;
        QVariantList samples;
        for (const ShaftSample2D &s : a.shaft.samples)
            samples.append(QVariantMap{
                { QStringLiteral("t_us"),  static_cast<qlonglong>(s.t_us) },
                { QStringLiteral("grip"),  QVariantList{ s.gripPx.x() * iw, s.gripPx.y() * ih } },
                { QStringLiteral("head"),  QVariantList{ s.headPx.x() * iw, s.headPx.y() * ih } },
                { QStringLiteral("theta"), s.thetaRad },
                { QStringLiteral("thetaDot"), s.thetaDotRadS },
                { QStringLiteral("lenPx"), s.visibleLenPx },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("flags"), int(s.flags) } });
        // R7 predicted series (pure R6 model) for the ghost overlay — same
        // normalized shape as `samples`; σ_β recoverable from conf for the cone.
        QVariantList predicted;
        for (const ShaftSample2D &s : a.shaft.predicted)
            predicted.append(QVariantMap{
                { QStringLiteral("t_us"),  static_cast<qlonglong>(s.t_us) },
                { QStringLiteral("grip"),  QVariantList{ s.gripPx.x() * iw, s.gripPx.y() * ih } },
                { QStringLiteral("head"),  QVariantList{ s.headPx.x() * iw, s.headPx.y() * ih } },
                { QStringLiteral("theta"), s.thetaRad },
                { QStringLiteral("lenPx"), s.visibleLenPx },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("flags"), int(s.flags) } });
        detail.insert(QStringLiteral("club"),
                      QVariantMap{
                          { QStringLiteral("camera"),        int(a.shaft.camera) },
                          { QStringLiteral("valid"),         a.shaft.valid },
                          { QStringLiteral("coverage"),      double(a.shaft.coverage) },
                          { QStringLiteral("imuVisionCorr"), double(a.shaft.imuVisionCorr) },
                          { QStringLiteral("modelVisionResidualDeg"), double(a.shaft.modelVisionResidualDeg) },
                          { QStringLiteral("frameWidth"),    a.shaft.frameWidth },
                          { QStringLiteral("frameHeight"),   a.shaft.frameHeight },
                          { QStringLiteral("samples"),       samples },
                          { QStringLiteral("predicted"),     predicted } });
    }
    return detail;
}

// Placement-slot → SegmentRole mapping lives in swing_analysis.h
// (pinpoint::analysis::segmentRoleForSlot) — one source of truth shared with the
// stream device.role export and the data viewer's settings fallback.
using pinpoint::analysis::segmentRoleForSlot;

// Human label for a SessionController::Type, used in session-folder naming.
QString sessionTypeLabel(int sessionType)
{
    switch (sessionType) {
    case 0:  return QStringLiteral("Swing");
    case 1:  return QStringLiteral("Wrist");
    case 2:  return QStringLiteral("GRF");
    case 3:  return QStringLiteral("Coach");
    default: return QString();
    }
}

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

    setAnalysisProgress(0.0);   // the ANALYSING bar starts empty for each shot
    setState(State::PostRoll);
    m_postRollTimer.start(postRollMsFor(source));
}

void ShotProcessor::onPostRollExpired()
{
    if (m_state != State::PostRoll)
        return;   // finishNowBlocking() raced the timer
    captureWindowAndLaunch();
}

// ---------------------------------------------------------------------------
// Window capture → concurrent analysis + export
// ---------------------------------------------------------------------------

void ShotProcessor::captureWindowAndLaunch()
{
    // The user may have pressed Stop during the post-roll: the rings froze at
    // the pause instant, truncating the follow-through there — still a valid
    // shot, so proceed from Paused. Only buffer teardown aborts.
    m_cameraManager->pauseBuffer();
    if (!m_buffer || m_buffer->state() != pinpoint::BufferState::Paused) {
        ppWarn() << "[ShotProcessor] shot aborted — buffer unavailable at post-roll expiry";
        abortToIdle();
        return;
    }

    Q_ASSERT(!m_swingWindow);   // state machine forbids a second shot while busy
    m_swingWindow.emplace(m_buffer->captureSwingWindow(kWindowDuration));

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
        m_replayTracks.push_back(std::move(track));
    }

    m_exportOutcome   = Outcome::Pending;
    m_analysisOutcome = Outcome::Pending;
    m_analysisResult  = {};
    m_segmentation    = {};
    m_swingDir.clear();
    m_thumbnailPath.clear();
    setState(State::Processing);

    ppInfo() << "[ShotProcessor] window captured —"
             << static_cast<qint64>(m_swingWindow->entries().size()) << "entries,"
             << m_replayTracks.size() << "camera track(s)";

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
        startSwingSave();                        // onSwingSaveFinished() joins
        return;
    }

    // Segmentation pre-stage (v3 G2): a milliseconds-cheap fuse + inertial
    // ladder over the frozen window, gating both heavy workers — its swing
    // bounds trim the export encode span and the replay. The job is resolved
    // on the UI thread NOW (value types only); failure or no-IMU yields a
    // conf-0 result and everything below degrades to full-window behaviour.
    m_analysisJob = buildAnalysisJob();
    const pinpoint::SwingWindow *win = &*m_swingWindow;
    m_segmentationInFlight = true;
    m_segmentationWatcher.setFuture(QtConcurrent::run(
        [bindings = m_analysisJob.imuBindings, impactUs = m_impactUs, win] {
            try {
                const pinpoint::analysis::FusedStreams streams =
                    pinpoint::analysis::ImuVisionFuser::fuse(*win, bindings);
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

ShotAnalysisJob ShotProcessor::buildAnalysisJob()
{
    ShotAnalysisJob job;
    job.sessionType = m_sessionType;
    job.shotSource  = static_cast<int>(m_shotSource);
    job.impactUs    = m_impactUs;

    // Face-on first so analyzers can prefer it without re-sorting; the count
    // makes "face-on first" verifiable from the worker (0 = none captured).
    for (const ReplayTrack &track : m_replayTracks) {
        if (track.ctrl->perspective() == CameraInstance::FaceOn) {
            job.cameraSources.insert(job.cameraSources.begin(), track.sourceId);
            ++job.faceOnCameraCount;
        } else {
            job.cameraSources.push_back(track.sourceId);
        }
    }

    // IMU and marker sources discovered from the window's own formats.
    QSet<pinpoint::SourceId> seen;
    for (const pinpoint::IndexEntry &e : m_swingWindow->entries()) {
        if (seen.contains(e.source_id)) continue;
        seen.insert(e.source_id);
        const pinpoint::FormatDescriptor &fd = m_swingWindow->formatOf(e.source_id);
        if (fd.device == pinpoint::DeviceKind::Marker_App)
            job.markerSourceId = e.source_id;
        else if (std::holds_alternative<pinpoint::ImuFormat>(fd.format))
            job.imuSources.push_back(e.source_id);
    }

    // Athlete handedness (lead-arm sign) and IMU -> segment bindings, resolved
    // here on the UI thread — the worker can read neither the athlete controller
    // nor the live ImuInstance calibration (alignA/mountM are session-lifetime).
    const QString hand = m_athlete ? m_athlete->currentHandedness() : QString();
    job.handedness = hand.compare(QLatin1String("Left"),  Qt::CaseInsensitive) == 0 ? 2
                   : hand.compare(QLatin1String("Right"), Qt::CaseInsensitive) == 0 ? 1 : 0;
    if (m_imuManager) {
        const QVariantMap placement = m_appSettings ? m_appSettings->imuPlacement() : QVariantMap{};
        const QVariantList insts = m_imuManager->instances();
        for (const QVariant &v : insts) {
            auto *imu = qobject_cast<ImuInstance *>(v.value<QObject *>());
            if (!imu) continue;
            const pinpoint::SourceId sid = imu->sourceId();
            if (std::find(job.imuSources.begin(), job.imuSources.end(), sid) == job.imuSources.end())
                continue;   // this IMU is not a source in the captured window
            pinpoint::analysis::ImuSegmentBinding b;
            b.source = sid;
            b.role   = segmentRoleForSlot(m_sessionType, placement.value(imu->deviceId()).toString());
            b.alignA = imu->alignA();
            b.mountM = imu->mountM();
            // Calibration status snapshot — persisted into swing.json's
            // analysis.bindings so SwingLab can filter by provenance.
            b.anatCalibrated       = imu->anatCalibrated();
            b.calibrated           = imu->fullyCalibrated();
            b.mountDeviationDeg    = imu->mountDeviationDeg();
            b.mountGravityErrorDeg = imu->mountGravityErrorDeg();
            if (imu->calibratedAtUtc().isValid()) {
                b.calibratedAtUtc = imu->calibratedAtUtc().toString(Qt::ISODateWithMs);
                b.calibAgeSec     = imu->calibratedAtUtc()
                                        .msecsTo(QDateTime::currentDateTimeUtc()) / 1000.0;
            }
            job.imuBindings.push_back(b);
        }
    }

    // Worker → UI progress marshalling: queued invoke with `this` as context
    // (auto-cancelled if the processor dies first), throttled to whole-percent
    // steps so per-frame reporting stays a handful of events per second.
    auto lastPct = std::make_shared<int>(-1);
    job.progress = [this, lastPct](float p) {
        const int pct = static_cast<int>(p * 100.0f);
        if (pct <= *lastPct)
            return;            // single worker thread — no synchronisation needed
        *lastPct = pct;
        QMetaObject::invokeMethod(this, [this, p] { setAnalysisProgress(p); },
                                  Qt::QueuedConnection);
    };
    return job;
}

void ShotProcessor::startAnalysis()
{
    ShotAnalysisJob job = m_analysisJob;   // resolved in captureWindowAndLaunch

    const pinpoint::SwingWindow *win = &*m_swingWindow;   // stable optional storage
    m_analysisInFlight = true;
    m_analysisWatcher.setFuture(QtConcurrent::run(
        [job = std::move(job), win] {
            // Exception barrier: anything escaping the worker (e.g. a
            // cv::Exception on malformed frame geometry) would be rethrown by
            // QtConcurrent on the GUI thread at result()/waitForFinished()
            // with no handler — std::terminate. Degrade to a failed analysis
            // instead; the join still adds the shot and resumes the buffer.
            try {
                auto analyzer = makeShotAnalyzer(job.sessionType);
                return analyzer->analyze(*win, job);
            } catch (const std::exception &e) {
                ShotAnalysisResult r;
                r.error = QString::fromUtf8(e.what());
                return r;
            } catch (...) {
                ShotAnalysisResult r;
                r.error = QStringLiteral("unknown exception in shot analysis");
                return r;
            }
        }));
}

void ShotProcessor::startSwingSave()
{
    pinpoint::SwingExportJob job = buildSwingExportJob();
    // Cache the job + dir up front: even when there is nothing to encode (no
    // cameras, or an encode failure), an analysis-only swing.json is written
    // from these at the join so the shot still survives a restart.
    m_swingDir  = job.swingDir;
    m_exportJob = job;

    if (job.swingDir.isEmpty()) {
        ppWarn() << "[SwingExport] could not allocate a swing directory — not saving";
        m_exportOutcome = Outcome::Skipped;
        emit swingSaveFailed(tr("could not create the swing folder — check the athlete "
                                "library path in Settings"));
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
    job.imuFormat      = s->imuDataFormat();
    job.savePose       = s->savePoseKeypoints();
    // job.poseStreams intentionally left empty: pose production (analyzer / pose
    // buffering) is a separate scope. The exporter serialises whatever is here,
    // so this is forward-compatible — populate it upstream once a producer lands.

    // Container extension drives the FFmpeg muxer (avformat guesses from the
    // output path). mp4/mov/mkv all carry H.264/H.265; fall back to mp4 for
    // anything else so a stale setting can never break the export.
    QString container = s->videoContainer().toLower();
    if (container != QLatin1String("mp4") && container != QLatin1String("mov")
        && container != QLatin1String("mkv"))
        container = QStringLiteral("mp4");

    if (m_athlete) {
        job.athleteName = m_athlete->currentName();
        job.athleteUuid = m_athlete->currentUuid();
        job.handedness  = m_athlete->currentHandedness();
    }

    // Wallclock anchor: right now, wallclock ~= monotonic endTimestampUs().
    job.wallclockAnchorUtc = QDateTime::currentDateTimeUtc();

    // Session context + host provenance for the manifest's "capture" block.
    job.sessionType = m_sessionType;
    job.shotSource  = static_cast<int>(m_shotSource);
    job.swingDetectionSensitivity = s->swingDetectionSensitivity();
    job.imuBleLatencyUs      = ImuInstance::kImuBleLatencyUs;
    job.audioDeviceLatencyUs = s->audioDeviceLatencyUs();
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
        cam.fileName = name + QLatin1Char('.') + container;
        cam.perspective  = track.ctrl->perspective();
        cam.mirrored     = track.ctrl->isMirrored();
        cam.fixedInPlace = s->cameraFixedInPlace()
                               .value(track.ctrl->cameraKey()).toBool();
        cam.ballCalibrated     = track.ctrl->ballCalibrated();
        cam.ballMargin         = track.ctrl->ballCalMargin();
        cam.ballCalibratedAtMs = track.ctrl->ballCalibratedAtMs();
        cam.ballDriftAtCapture = track.ctrl->ballDriftSeverity();
        job.cameras.push_back(std::move(cam));
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
    const QVariantMap imuPlacement = s->imuPlacement();
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
        info.orientationFilter = s->imuOrientationFilter();
        info.placementSlot     = imuPlacement.value(dev.id).toString();
        // Resolve the anatomical body role from slot+sessionType (same canonical
        // mapping as analysis.bindings) so it is baked into the stream itself —
        // needed by the data viewer, SwingLab and future post-hoc analysis.
        const pinpoint::analysis::SegmentRole role =
            segmentRoleForSlot(m_sessionType, info.placementSlot);
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
                    // analysis.bindings). Mirrors buildAnalysisJob's resolution.
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
                    break;
                }
            }
        }
        job.imuDeviceBySerial.insert(serial, info);
    }

    const auto alloc = m_swingPaths.allocateSwingDir(s->athleteLibraryPath(),
                                                     job.athleteName,
                                                     job.athleteUuid,
                                                     s->sessionNamingPattern(),
                                                     sessionTypeLabel(m_sessionType));
    job.swingDir   = alloc.swingDir;
    job.swingId    = alloc.swingId;
    job.swingIndex = alloc.swingIndex;
    job.sessionId  = alloc.sessionId;
    return job;
}

QJsonObject ShotProcessor::buildSynthManifest() const
{
    // Mirrors the exporter's header exactly (swing_exporter.cpp), minus the
    // media streams — so a reloaded analysis-only shot is indistinguishable
    // from an exported one apart from hasVideo=false. writeSwingJson stamps the
    // schema and appends the "analysis" block.
    const int64_t t0         = m_swingWindow->startTimestampUs();
    const int64_t durationUs = m_swingWindow->endTimestampUs() - t0;
    const QDateTime wallclock = m_exportJob.wallclockAnchorUtc.addMSecs(-durationUs / 1000);

    QJsonObject root;
    root[QStringLiteral("swing")] = QJsonObject{
        { QStringLiteral("index"), m_exportJob.swingIndex },
        { QStringLiteral("id"),    m_exportJob.swingId },
    };
    root[QStringLiteral("athlete")] = QJsonObject{
        { QStringLiteral("name"),       m_exportJob.athleteName },
        { QStringLiteral("uuid"),       m_exportJob.athleteUuid },
        { QStringLiteral("handedness"), m_exportJob.handedness },
    };
    root[QStringLiteral("session")] = QJsonObject{{ QStringLiteral("dir"), m_exportJob.sessionId }};
    root[QStringLiteral("clock")] = QJsonObject{
        { QStringLiteral("t0_us"),     static_cast<qint64>(t0) },
        { QStringLiteral("unit"),      QStringLiteral("us") },
        { QStringLiteral("wallclock"), wallclock.toString(Qt::ISODateWithMs) },
    };
    root[QStringLiteral("window")] = QJsonObject{
        { QStringLiteral("start_us"), 0 },
        { QStringLiteral("end_us"),   static_cast<qint64>(durationUs) },
    };
    root[QStringLiteral("capture")] = pinpoint::SwingExporter::captureBlock(m_exportJob);
    root[QStringLiteral("streams")] = QJsonArray{};   // analysis-only — no media
    return root;
}

// ---------------------------------------------------------------------------
// Worker completion → join
// ---------------------------------------------------------------------------

void ShotProcessor::onAnalysisFinished()
{
    if (!m_analysisInFlight)
        return;   // already joined blockingly in finishNowBlocking()
    m_analysisInFlight = false;
    m_analysisResult   = m_analysisWatcher.result();
    m_analysisOutcome  = m_analysisResult.ok ? Outcome::Succeeded : Outcome::Failed;
    setAnalysisProgress(1.0);   // beat any still-queued worker updates to full
    if (m_analysisResult.ok)
        ppInfo() << "[ShotProcessor] analysis done — score" << m_analysisResult.score;
    else
        emit analysisFailed(m_analysisResult.error);   // toast, not a log line

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
    if (m_analysisOutcome == Outcome::Pending || m_exportOutcome == Outcome::Pending)
        return;   // wait for BOTH workers

    const bool analysisOk = m_analysisOutcome == Outcome::Succeeded;
    const bool exportOk   = m_exportOutcome   == Outcome::Succeeded;

    // IMU data-integrity (offline re-fusion parity): re-fuse each IMU source from its
    // recorded raw accel+gyro and confirm it reproduces the stored quaternion. A
    // mismatch means the IMU record is internally inconsistent — the shot can't be
    // re-analysed offline — so the carousel item is flagged (⚠) and an "imuIntegrity"
    // block is persisted to swing.json. Only meaningful for the Madgwick default (the
    // only exactly-warm-startable filter); skip under ESKF to avoid a false warning.
    bool imuDataWarning = false;
    if (m_swingWindow
        && (!m_appSettings
            || m_appSettings->imuOrientationFilter().compare(QStringLiteral("ESKF"),
                                                             Qt::CaseInsensitive) != 0)) {
        const pinpoint::ImuRefusionVerdict v = pinpoint::checkImuRefusion(*m_swingWindow);
        if (v.sourcesChecked > 0) {
            imuDataWarning = v.warns();
            m_exportManifest[QStringLiteral("imuIntegrity")] = QJsonObject{
                { QStringLiteral("refusionOk"),     v.ok },
                { QStringLiteral("worstMaxDeg"),    v.worstMaxDeg },
                { QStringLiteral("sourcesChecked"), v.sourcesChecked },
                { QStringLiteral("thresholdDeg"),   v.thresholdDeg },
                { QStringLiteral("filter"),         QStringLiteral("madgwick") } };
            if (imuDataWarning)
                ppInfo() << "[ShotProcessor] IMU re-fusion parity FAILED — worst"
                         << v.worstMaxDeg << "deg over" << v.sourcesChecked
                         << "source(s); shot flagged not re-analysable";
        }
    }

    // The ONE unified swing.json (raw manifest + inline "analysis"), written here on the
    // GUI thread now that both workers have finished — no parallel-write race (the workers
    // wrote only media + returned values). savedSwingDir is set only when a swing.json was
    // actually written, so the carousel row links to a real file (rating/note write-through,
    // reload) and an unwritten shot stays in-memory only.
    QString savedSwingDir;
    if (exportOk) {
        QString werr;
        if (pinpoint::SwingDocWriter::writeSwingJson(
                m_swingDir, m_exportManifest,
                analysisOk && m_analysisResult.detail ? m_analysisResult.detail.get() : nullptr,
                &werr)) {
            savedSwingDir = m_swingDir;
            ppInfo() << "[SwingDoc] wrote" << m_swingDir + QStringLiteral("/swing.json")
                     << (analysisOk ? "(with analysis)" : "(raw only)");
        } else {
            ppError() << "[SwingDoc]" << werr;
        }
    } else if (analysisOk && m_analysisResult.detail && !m_swingDir.isEmpty()) {
        // Degraded persist: export failed/skipped but analysis succeeded — write a
        // minimal, analysis-only swing.json so the shot reloads after a restart.
        QString werr;
        QJsonObject synthManifest = buildSynthManifest();
        if (m_exportManifest.contains(QStringLiteral("imuIntegrity")))
            synthManifest[QStringLiteral("imuIntegrity")] = m_exportManifest[QStringLiteral("imuIntegrity")];
        if (pinpoint::SwingDocWriter::writeSwingJson(
                m_swingDir, synthManifest, m_analysisResult.detail.get(), &werr)) {
            savedSwingDir = m_swingDir;
            ppInfo() << "[SwingDoc] wrote analysis-only" << m_swingDir + QStringLiteral("/swing.json");
        } else {
            ppError() << "[SwingDoc] (degraded)" << werr;
        }
    }

    // The shot happened — it always lands on the carousel, with whatever the
    // pipeline produced. Club is a stub until club selection exists.
    // Publish the analyzed detail of the shot about to replay (the ScreenWrist in-replay
    // graph binds to it) before addShot, so it's ready when REPLAYING begins.
    m_replayAnalysisDetail = (analysisOk && m_analysisResult.detail)
                                 ? toAnalysisDetail(*m_analysisResult.detail)
                                 : QVariantMap{};
    emit replayAnalysisDetailChanged();

    int newShotId = -1;
    if (m_shotModel) {
        const QUrl thumbUrl = m_thumbnailPath.isEmpty()
                                  ? QUrl()
                                  : QUrl::fromLocalFile(m_thumbnailPath);
        newShotId = m_shotModel->addShot(savedSwingDir,
                             m_timestampLabel,
                             QStringLiteral("DRIVER"),
                             exportOk,
                             thumbUrl,
                             analysisOk ? m_analysisResult.tracePoints : QVariantList{},
                             analysisOk ? m_analysisResult.score : 0,
                             analysisOk ? m_analysisResult.metrics : QVariantMap{},
                             m_replayAnalysisDetail,
                             imuDataWarning);
    }

    // "Reviewable on disk": analysis + export both succeeded AND a swing.json was
    // actually written — that is the swing the UI promotes straight into Review for
    // instant playback (the disk replay reads the just-written MP4(s), not the ring).
    const bool reviewableOnDisk = analysisOk && exportOk && !savedSwingDir.isEmpty();

    if (analysisOk && exportOk)
        emit shotProcessed(newShotId, savedSwingDir);
    else
        emit shotFailed(!analysisOk ? m_analysisResult.error
                                    : QStringLiteral("export failed or skipped"));

    // Post-shot playback now lives on the Review stage: a reviewable shot is auto-
    // promoted into Review (disk replay) by the UI from shotProcessed(), so skip the
    // on-screen ¼× window transient and resume capture immediately. Only when there
    // is no reviewable disk shot do we fall back to the in-window transient (reading
    // the frozen window's frames directly) so the user still sees the swing they
    // just made.
    if (reviewableOnDisk) {
        finishShot();
    } else if (!m_skipAnalysisCapture && !m_replayTracks.empty()) {
        startReplay();
    } else {
        if (m_skipAnalysisCapture)
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

void ShotProcessor::startReplay()
{
    // Anchor to the actual first/last captured entry so replay starts
    // immediately rather than waiting for the (potentially empty) leading
    // portion of the window.
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
        const int64_t hi = std::min(m_replayWindowEndUs,   m_segmentation.swingEndUs);
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
        // Advance to the newest frame whose offset from the first entry <= virtual time.
        while (track.idx + 1 < track.entries.size()) {
            const int64_t nextOffset =
                track.entries[track.idx + 1].timestamp_us - m_replayWindowStartUs;
            if (nextOffset <= virtualTimeUs)
                ++track.idx;
            else
                break;
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
    setState(State::Idle);
}

void ShotProcessor::finishNowBlocking()
{
    m_postRollTimer.stop();   // a pending shot is forfeited — acceptable on teardown

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
    // The queued finished() handlers will still be delivered later; flag-off
    // makes them no-ops.
    m_segmentationInFlight = false;
    m_analysisInFlight  = false;
    m_swingSaveInFlight = false;

    m_swingWindow.reset();
    m_replayTracks.clear();

    // Deliberately no applyCaptureIntent(): callers (setSelected, destructors)
    // own the buffer-state sequence around source registration.
    setState(State::Idle);
}
