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

#include "motion_capture_probe.h"

#include <QMetaObject>
#include <QUrl>
#include <cmath>

#include "pose_backend.h"
#include "pp_gpu_metrics.h"
#include "pp_debug.h"
#include "session_controller.h"
#include "../TTS/ModelDownloader.h"   // generic URL->path downloader (reused in place)

#ifdef HAVE_OPENCV
#  include <opencv2/core.hpp>
#endif

#if defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)
#  include "pose_estimator_vitpose.h"
#endif

namespace {

// ── Tunable constants — Mark validates these against real swings ────────────
// [seam] All three are provisional and flagged for empirical tuning.
constexpr uint64_t kHighTierMinVramBytes      = 4ull * 1024 * 1024 * 1024; // 4 GB floor for ViTPose++-L
constexpr double   kHighToMediumComputeFactor = 3.5;   // ViT-L ~60 GFLOPs / ViT-B ~17 GFLOPs; provisional
constexpr qint64   kHighModelApproxBytes      = 1234579166; // exact vitpose-l-wholebody.onnx size, for the prompt
constexpr int      kRepresentativeSwingFrames = 120;   // approx analysed frames/swing — [seam] use real count later

// Frames pushed through each estimator to warm its 30-sample rolling window
// (kWindowSize = 30) so poseStatsUpdated() reports a stable average.
constexpr int      kBenchmarkFrames           = 35;

// Whole seconds for one swing at a given per-frame inference time (ms). -1 in →
// -1 out (unknown propagates).
int secondsForInferenceMs(double avgMs)
{
    if (avgMs <= 0.0)
        return -1;
    return static_cast<int>(std::lround(avgMs * kRepresentativeSwingFrames / 1000.0));
}

} // namespace

MotionCaptureProbe::MotionCaptureProbe(SessionController *session, QObject *parent)
    : QObject(parent), m_session(session)
{
    // Deliberately lazy: no probing at construction. gpumetrics init + two ORT
    // session loads are too costly for launch — refresh() is kicked from the
    // General panel's Component.onCompleted instead.
    m_secondsPerSwing[QStringLiteral("Medium")] = -1;
    m_secondsPerSwing[QStringLiteral("High")]   = -1;

    refreshHighModelPresent();   // reflect a previously-downloaded High model at launch
}

MotionCaptureProbe::~MotionCaptureProbe()
{
    // Break the benchmark loop and wait for the worker to unwind before any
    // member it captures dies. A queued result post to a now-dead `this` is
    // purged by ~QObject, so this join is the only barrier needed.
    m_cancel.store(true, std::memory_order_relaxed);
    if (m_worker.joinable())
        m_worker.join();
}

void MotionCaptureProbe::refresh()
{
    if (m_probing)
        return;                 // a benchmark is already in flight

    runSuitabilityProbe();      // cheap; refreshes backend/VRAM gate every call

    if (m_ready)
        return;                 // timing already measured this launch

    kickTimingBenchmark();      // async, at most once per launch
}

void MotionCaptureProbe::runSuitabilityProbe()
{
    refreshHighModelPresent();   // reflect an out-of-band download/placement

    const QString backend = pinpoint::pose::bestAvailableAcceleratedBackend();

    pinpoint::gpumetrics::init();
    const pinpoint::gpumetrics::GpuSample g = pinpoint::gpumetrics::sample();

    const bool accel  = pinpoint::pose::isAccelerated(backend);
    // Apple-Silicon unified memory has no discrete VRAM budget to gate on.
    const bool vramOk = g.unified || g.deviceTotalBytes >= kHighTierMinVramBytes;

    m_backend           = backend;
    m_deviceName        = QString::fromStdString(g.deviceName);
    m_highTierSupported = accel && vramOk;
    m_highTierBlockReason = !accel  ? tr("Needs a supported graphics card")
                          : !vramOk ? tr("Not enough graphics memory")
                                    : QString();

    recomputeSecondsPerSwing();
    emit stateChanged();
}

void MotionCaptureProbe::kickTimingBenchmark()
{
    // Don't contend with a live capture/analysis for the GPU — the reanalysis
    // controller already serialises ViTPose one swing at a time. Leave the
    // per-tier seconds unknown (-1); a later refresh() (panel re-open) retries.
    if (m_session && m_session->running()) {
        ppInfo() << "[MotionCaptureProbe] session active — deferring benchmark";
        return;
    }
    if (m_worker.joinable())
        return;                 // backstop: a run already owns the worker

    m_probing = true;
    m_cancel.store(false, std::memory_order_relaxed);
    emit stateChanged();

    m_worker = std::thread([this]() {
        double mediumMs = -1.0, highMs = -1.0;
        runBenchmark(mediumMs, highMs);

        // Marshal back to the GUI thread. If `this` is being destroyed the dtor
        // joins us first, then ~QObject purges this posted event — no dangling.
        QMetaObject::invokeMethod(this, [this, mediumMs, highMs]() {
            m_benchMediumMs = mediumMs;
            m_benchHighMs   = highMs;
            m_probing = false;
            m_ready   = true;
            recomputeSecondsPerSwing();
            emit stateChanged();
        }, Qt::QueuedConnection);
    });
}

// Worker thread. Reuses each estimator's own rolling poseStatsUpdated() average
// rather than timing here, so the value matches what a live analysis pass reports.
void MotionCaptureProbe::runBenchmark(double &mediumMs, double &highMs)
{
#ifdef HAVE_OPENCV
    // Timing is content-independent for these CNNs — a flat frame is enough. The
    // estimators resize to their own input size, so any sane resolution works.
    const cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(120, 120, 120));

    const auto benchmark = [&](PoseEstimatorBase &est) -> double {
        double last = -1.0;
        QObject::connect(&est, &PoseEstimatorBase::poseStatsUpdated, &est,
                         [&last](double avgMs, double /*fps*/) { last = avgMs; });
        for (int i = 0; i < kBenchmarkFrames && !m_cancel.load(std::memory_order_relaxed); ++i)
            est.estimatePose(frame);
        return last;
    };

#  if defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)
    if (!m_cancel.load(std::memory_order_relaxed)) {          // Medium = ViTPose-B
        PoseEstimatorViTPose est;
        est.load();
        if (est.isReady())
            mediumMs = benchmark(est);
    }

    // High = ViTPose++-L — measured only once the user has downloaded the model
    // (m_highModelPresent, set on the GUI thread before this worker spawns). Until
    // then High stays projected from Medium (recomputeSecondsPerSwing).
    if (!m_cancel.load(std::memory_order_relaxed) && m_highModelPresent) {
        PoseEstimatorViTPose est(PoseEstimatorViTPose::ModelVariant::WholeBodyLarge);
        est.load();
        if (est.isReady())
            highMs = benchmark(est);
    }
#  endif
#else
    Q_UNUSED(mediumMs);
    Q_UNUSED(highMs);
#endif
}

void MotionCaptureProbe::reportLivePoseStats(double avgMs, double /*fps*/)
{
    // A real analysis pass of the Medium (ViTPose) model — always preferred over
    // the micro-benchmark. Fold it in and refresh the derived High estimate.
    if (avgMs <= 0.0)
        return;
    m_liveMediumMs = avgMs;
    recomputeSecondsPerSwing();
    emit stateChanged();
}

void MotionCaptureProbe::recomputeSecondsPerSwing()
{
    // Live Medium measurement wins over the micro-benchmark when present.
    const double mediumMs = (m_liveMediumMs > 0.0) ? m_liveMediumMs : m_benchMediumMs;

    m_secondsPerSwing[QStringLiteral("Medium")] = secondsForInferenceMs(mediumMs);

    // High (ViTPose++-L): use a REAL benchmark once the model is downloaded;
    // otherwise project from Medium and flag it provisional. Only expose it at all
    // when the hardware can actually run the tier.
    if (m_highTierSupported && m_benchHighMs > 0.0) {
        m_secondsPerSwing[QStringLiteral("High")] = secondsForInferenceMs(m_benchHighMs);
        m_highEstimateProvisional = false;
    } else if (m_highTierSupported && mediumMs > 0.0) {
        m_secondsPerSwing[QStringLiteral("High")] =
            secondsForInferenceMs(mediumMs * kHighToMediumComputeFactor);
        m_highEstimateProvisional = true;
    } else {
        m_secondsPerSwing[QStringLiteral("High")] = -1;
        m_highEstimateProvisional = false;
    }
}

// ── On-demand High-tier model (ViTPose++-L) download ─────────────────────────

qint64 MotionCaptureProbe::highModelSizeBytes() const
{
    return kHighModelApproxBytes;
}

void MotionCaptureProbe::refreshHighModelPresent()
{
    bool present = false;
#if defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)
    present = PoseEstimatorViTPose::isVariantAvailable(
        PoseEstimatorViTPose::ModelVariant::WholeBodyLarge);
#endif
    if (present != m_highModelPresent) {
        m_highModelPresent = present;
        emit highModelChanged();
    }
}

void MotionCaptureProbe::downloadHighModel()
{
#if defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)
    if (m_highModelPresent || m_highModelDownloading)
        return;   // already have it, or a download is in flight

    if (!m_downloader) {
        m_downloader = new ModelDownloader(this);
        connect(m_downloader, &ModelDownloader::progress, this,
                [this](int, int, qint64 received, qint64 total) {
                    m_highModelDownloadProgress =
                        (total > 0) ? double(received) / double(total) : -1.0;
                    emit highModelChanged();
                });
        connect(m_downloader, &ModelDownloader::finished, this, [this]() {
            m_highModelDownloading      = false;
            m_highModelDownloadProgress = 1.0;
            refreshHighModelPresent();   // flips highModelPresent, emits highModelChanged
            // Re-measure High now that the real model exists (was projected before).
            m_ready       = false;
            m_benchHighMs = -1.0;
            kickTimingBenchmark();
            emit highModelChanged();
        });
        connect(m_downloader, &ModelDownloader::failed, this,
                [this](const QString &err) {
                    m_highModelDownloading      = false;
                    m_highModelDownloadProgress = -1.0;
                    m_highModelDownloadError    = err;
                    ppWarn() << "[MotionCaptureProbe] High model download failed:" << err;
                    emit highModelChanged();
                });
    }

    m_highModelDownloadError.clear();
    m_highModelDownloading      = true;
    m_highModelDownloadProgress = -1.0;
    emit highModelChanged();

    const QString dest = PoseEstimatorViTPose::largeModelDir()
                       + PoseEstimatorViTPose::largeModelFileName();
    ppInfo() << "[MotionCaptureProbe] downloading High-tier model to" << dest;
    m_downloader->download({ ModelDownloader::Item{
        QUrl(PoseEstimatorViTPose::largeModelUrl()), dest } });
#endif
}

void MotionCaptureProbe::cancelHighModelDownload()
{
    if (m_downloader)
        m_downloader->abort();
    if (m_highModelDownloading) {
        m_highModelDownloading      = false;
        m_highModelDownloadProgress = -1.0;
        emit highModelChanged();
    }
}

void MotionCaptureProbe::clearHighModelDownloadError()
{
    if (!m_highModelDownloadError.isEmpty()) {
        m_highModelDownloadError.clear();
        emit highModelChanged();
    }
}
