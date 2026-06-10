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
#include "../Analysis/swing_analysis.h"
#include "../Export/swing_doc.h"
#include "../Core/pp_debug.h"

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
// before it freezes. All 500 ms for now; per-source constants so each
// detector can be tuned independently once its latency is characterised.
constexpr int kPostRollManualMs   = 500;
constexpr int kPostRollImuMs      = 500;
constexpr int kPostRollPoseMs     = 500;
constexpr int kPostRollBallMs     = 500;
constexpr int kPostRollAcousticMs = 500;

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
        phases.append(QVariantMap{ { QStringLiteral("phase"), int(e.phase) },
                                   { QStringLiteral("t_us"),  static_cast<qlonglong>(e.t_us) },
                                   { QStringLiteral("conf"),  e.conf } });
    return QVariantMap{ { QStringLiteral("tier"),    a.tier },
                        { QStringLiteral("overall"), a.score.overall },
                        { QStringLiteral("series"),  series },
                        { QStringLiteral("phases"),  phases } };
}

// Map a placement slot ("A"/"B"/"C") to an anatomical SegmentRole per session
// type. Wrist (1): A=forearm, B=hand, C=upper arm. Other types resolve as their
// analyzers land — Unknown until then (the binding's A/M are still captured).
pinpoint::analysis::SegmentRole segmentRoleForSlot(int sessionType, const QString &slot)
{
    using R = pinpoint::analysis::SegmentRole;
    if (sessionType == 1) {            // Wrist Motion
        if (slot == QLatin1String("A")) return R::LeadForearm;
        if (slot == QLatin1String("B")) return R::LeadHand;
        if (slot == QLatin1String("C")) return R::LeadUpperArm;
    }
    return R::Unknown;
}

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
    m_swingDir.clear();
    m_thumbnailPath.clear();
    setState(State::Processing);

    ppInfo() << "[ShotProcessor] window captured —"
             << static_cast<qint64>(m_swingWindow->entries().size()) << "entries,"
             << m_replayTracks.size() << "camera track(s)";

    // Both workers read the same frozen window concurrently — const, zero-copy
    // reads over stable memory (producers stopped while Paused).
    startAnalysis();
    startSwingSave();
    maybeJoin();   // covers the export-skipped path completing synchronously
}

void ShotProcessor::startAnalysis()
{
    ShotAnalysisJob job;
    job.sessionType = m_sessionType;
    job.shotSource  = static_cast<int>(m_shotSource);
    job.impactUs    = m_impactUs;

    // Face-on first so analyzers can prefer it without re-sorting.
    for (const ReplayTrack &track : m_replayTracks) {
        if (track.ctrl->perspective() == CameraInstance::FaceOn)
            job.cameraSources.insert(job.cameraSources.begin(), track.sourceId);
        else
            job.cameraSources.push_back(track.sourceId);
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
            job.imuBindings.push_back(b);
        }
    }

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
        job.cameras.push_back(std::move(cam));
    }

    // Impact thumbnail from the face-on camera, else the exporter falls back
    // to the first exported stream.
    job.thumbnailTimestampUs = m_impactUs;
    for (const ReplayTrack &track : m_replayTracks) {
        if (track.ctrl->perspective() == CameraInstance::FaceOn) {
            job.thumbnailSourceId = track.sourceId;
            break;
        }
    }

    // IMU aliases keyed by the same identifier the sources registered with
    // (serial when present, else device id — mirrors ImuInstance).
    const QVariantMap imuAliases = s->imuAlias();
    const QList<Device> imus = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    for (const Device &dev : imus) {
        const QString serial = dev.imuCapabilities.serialNumber.isEmpty()
                                   ? dev.id
                                   : dev.imuCapabilities.serialNumber;
        const QString imuKey = dev.description + QStringLiteral("|") + dev.id;
        const QString alias  = imuAliases.value(imuKey).toString().trimmed();
        if (!alias.isEmpty())
            job.imuAliasBySerial.insert(serial, alias);
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
    if (m_analysisResult.ok)
        ppInfo() << "[ShotProcessor] analysis done — score" << m_analysisResult.score;
    else
        ppError() << "[ShotProcessor] analysis failed:" << m_analysisResult.error;
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
        if (pinpoint::SwingDocWriter::writeSwingJson(
                m_swingDir, buildSynthManifest(), m_analysisResult.detail.get(), &werr)) {
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

    if (m_shotModel) {
        const QUrl thumbUrl = m_thumbnailPath.isEmpty()
                                  ? QUrl()
                                  : QUrl::fromLocalFile(m_thumbnailPath);
        m_shotModel->addShot(savedSwingDir,
                             m_timestampLabel,
                             QStringLiteral("DRIVER"),
                             exportOk,
                             thumbUrl,
                             analysisOk ? m_analysisResult.tracePoints : QVariantList{},
                             analysisOk ? m_analysisResult.score : 0,
                             analysisOk ? m_analysisResult.metrics : QVariantMap{},
                             m_replayAnalysisDetail);
    }

    if (analysisOk && exportOk)
        emit shotProcessed(m_swingDir);
    else
        emit shotFailed(!analysisOk ? m_analysisResult.error
                                    : QStringLiteral("export failed or skipped"));

    // Replay gating: fire whenever there is video to show. The on-screen ¼×
    // replay reads the frozen window's camera frames directly (m_replayTracks),
    // not the exported MP4 or the analysis result — so neither analysisOk nor
    // exportOk is a precondition. A shot with captured footage always replays,
    // even if analysis was absent or the disk export failed.
    if (!m_replayTracks.empty()) {
        startReplay();
    } else {
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
                cfmt->pixel_format);
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

    // Block until both workers have returned: they read ring memory through
    // the window, which the caller is about to invalidate (deregister/teardown).
    if (m_analysisInFlight)
        m_analysisWatcher.waitForFinished();
    if (m_swingSaveInFlight)
        m_swingSaveWatcher.waitForFinished();
    // The queued finished() handlers will still be delivered later; flag-off
    // makes them no-ops.
    m_analysisInFlight  = false;
    m_swingSaveInFlight = false;

    m_swingWindow.reset();
    m_replayTracks.clear();

    // Deliberately no applyCaptureIntent(): callers (setSelected, destructors)
    // own the buffer-state sequence around source registration.
    setState(State::Idle);
}
