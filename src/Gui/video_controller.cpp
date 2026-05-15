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

#include "video_controller.h"

#include "event_buffer.h"
#include "source_descriptor.h"
#include <cstring>

#include "ting_player.h"
#include "video_input_base.h"
#include "video_input_factory.h"
#include "video_preprocessor_base.h"
#include "bayer_video_item.h"
#ifdef HAVE_OPENCV
#include "video_preprocessor_opencv.h"
#include "frame_throttle.h"
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
#include "pose_estimator_movenet.h"
#endif

#ifdef HAVE_OPENCV
#include "ball_detector.h"
#endif

#include <QCoreApplication>
#include <QMetaObject>
#include "pp_debug.h"
#include <QThread>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#ifdef Q_OS_MACOS
#include "macos_permissions.h"
#endif

// ---------------------------------------------------------------------------
// Constructors / destructor
// ---------------------------------------------------------------------------

VideoController::VideoController(QObject *parent)
    : QObject(parent)
    , m_captureThread(new QThread(this))
    , m_videoInput(VideoInputFactory::create(VideoInputFactory::Backend::Auto))
{
    setupPipeline();
}

VideoController::VideoController(const Device &device, pinpoint::EventBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_captureThread(new QThread(this))
    , m_deviceId(device.id)
    , m_deviceDescription(device.description)
    , m_videoInput(VideoInputFactory::create(device.backend))
    , m_eventBuffer(buffer)
{
    // Register source now with conservative defaults so the source ID is valid
    // before EventBuffer::start() is called in main(). updateBufferDescriptor()
    // fires on first Active and logs the actual format; re-registration is not
    // supported in EventBuffer v1 (TODO Phase 5).
    if (m_eventBuffer) {
        pinpoint::SourceDescriptor desc;
        desc.name = device.description.toStdString();

        pinpoint::CameraFormat cfmt{};
        cfmt.pixel_format          = pinpoint::PixelFormat::Unknown;
        cfmt.width                 = 0;
        cfmt.height                = 0;
        cfmt.fps_numerator         = 60;
        cfmt.fps_denominator       = 1;
        cfmt.max_payload_bytes     = 1920 * 1080 * 2;
        cfmt.typical_payload_bytes = 1920 * 1080;

        switch (device.backend) {
        case VideoInputFactory::Backend::AppleAVFoundation:
            desc.format.device = pinpoint::DeviceKind::Camera_AVFoundation;
            break;
        case VideoInputFactory::Backend::Aravis:
        case VideoInputFactory::Backend::Spinnaker:
            desc.format.device = pinpoint::DeviceKind::Camera_GenICam;
            break;
        default:
            desc.format.device = pinpoint::DeviceKind::Camera_UVC;
            break;
        }
        desc.format.format            = cfmt;
        desc.window_duration          = std::chrono::milliseconds(5000);
        desc.expected_interarrival_us = std::chrono::microseconds(16667);
        desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;

        m_sourceId = m_eventBuffer->registerSource(desc);
    }

    setupPipeline();
}

void VideoController::setupPipeline()
{
    // Set to false to disable the entire preprocessor/pose/throttle pipeline
    // for capture-rate diagnostics.  Flip back to true to restore.
    static constexpr bool kPoseEnabled = true;

    m_tingPlayer = new TingPlayer(this);

    m_captureThread->setObjectName(QStringLiteral("VideoCaptureThread"));
    m_videoInput->moveToThread(m_captureThread);

#ifdef HAVE_OPENCV
    if (kPoseEnabled) {
        m_preprocessThread = new QThread(this);
        m_preprocessThread->setObjectName(QStringLiteral("VideoPreprocessThread"));
        m_preprocessor = new VideoPreprocessorOpenCV();
        m_preprocessor->moveToThread(m_preprocessThread);
        connect(m_preprocessor, &VideoPreprocessorBase::preprocessStatsUpdated,
                this, &VideoController::onPreprocessStats, Qt::QueuedConnection);
        m_preprocessThread->start();

        auto *cvPP = qobject_cast<VideoPreprocessorOpenCV *>(m_preprocessor);

#if defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
        m_poseThread = new QThread(this);
        m_poseThread->setObjectName(QStringLiteral("PoseEstimatorThread"));
        auto *mn = new PoseEstimatorMoveNet();
        m_poseEstimator = mn;
        m_poseEstimator->moveToThread(m_poseThread);
        connect(m_poseThread, &QThread::started, mn, &PoseEstimatorMoveNet::load);
        connect(m_poseEstimator, &PoseEstimatorBase::poseStatsUpdated,
                this, &VideoController::onPoseStats, Qt::QueuedConnection);
        connect(m_poseEstimator, &PoseEstimatorBase::poseBackendReady,
                this, &VideoController::onPoseBackendReady, Qt::QueuedConnection);

        m_frameThrottle = new FrameThrottle();
        m_frameThrottle->setSkipFactor(2);

        connect(cvPP, &VideoPreprocessorOpenCV::framePreprocessed,
                m_poseEstimator, &PoseEstimatorBase::estimatePose, Qt::QueuedConnection);
        connect(m_poseEstimator, &PoseEstimatorBase::estimationDone,
                m_frameThrottle, &FrameThrottle::clearBusy, Qt::DirectConnection);
        connect(m_poseEstimator, &PoseEstimatorBase::estimationDone,
                m_frameThrottle, &FrameThrottle::clearRawBusy, Qt::DirectConnection);
        connect(m_poseEstimator, &PoseEstimatorBase::poseEstimated,
                this, &VideoController::onPoseEstimated, Qt::QueuedConnection);
        m_poseThread->start();
#endif // HAVE_MOVENET && HAVE_ONNXRUNTIME

        // Ball detector — connects to the same preprocessed frame signal,
        // runs on its own thread so it never stalls the pose estimator.
        m_ballThread = new QThread(this);
        m_ballThread->setObjectName(QStringLiteral("BallDetectorThread"));
        m_ballDetector = new BallDetector();
        m_ballDetector->moveToThread(m_ballThread);

        connect(cvPP, &VideoPreprocessorOpenCV::framePreprocessed,
                m_ballDetector, &BallDetector::detect, Qt::QueuedConnection);
        connect(m_ballDetector, &BallDetector::ballDetected,
                this, &VideoController::onBallDetected, Qt::QueuedConnection);

        // Forward ROI changes to the detector thread.  The lambda captures the
        // current roi value on the main thread before posting it to the detector.
        connect(this, &VideoController::roiChanged, this, [this]() {
            QMetaObject::invokeMethod(m_ballDetector, "setRoi",
                Qt::QueuedConnection, Q_ARG(QRectF, m_roi));
        }, Qt::DirectConnection);

        m_ballThread->start();

    } // kPoseEnabled
#endif // HAVE_OPENCV

    connectVideoInput();

    // On first Active state: register the buffer source with actual format/fps,
    // and re-evaluate needsDebayer (only meaningful after Active).
    connect(m_videoInput, &VideoInputBase::stateChanged,
            this, [this](VideoInputBase::State s) {
                if (s == VideoInputBase::State::Active) {
                    updateBufferDescriptor();
                    emit needsDebayerChanged();
                }
            }, Qt::QueuedConnection);

    m_captureThread->start();

    // Sample the capture-thread frame counter every 500 ms to compute actual fps.
    m_fpsSampleTimer = new QTimer(this);
    m_fpsSampleTimer->setInterval(500);
    connect(m_fpsSampleTimer, &QTimer::timeout, this, [this]() {
        const int count   = m_frameCaptureCount.exchange(0, std::memory_order_relaxed);
        const double secs = m_fpsSampleElapsed.restart() / 1000.0;
        if (secs > 0.0) {
            const double fps = count / secs;
            if (!qFuzzyCompare(m_cameraFps, fps)) {
                m_cameraFps = fps;
                emit cameraFpsChanged();
            }
        }
    });
    m_fpsSampleElapsed.start();
    m_fpsSampleTimer->start();
}

VideoController::~VideoController()
{
    // Shutdown in pipeline order so no stage receives frames after it is torn down.

    // 1. Stop capture — no more videoFrameReady signals after this.
    if (m_captureThread->isRunning()) {
        QMetaObject::invokeMethod(m_videoInput, [this]() {
            m_videoInput->stop();
            m_videoInput->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_captureThread->quit();
        m_captureThread->wait();
    }
    delete m_videoInput;
    m_videoInput = nullptr;

    // 2. Stop preprocess — no more framePreprocessed signals after this.
    if (m_preprocessor && m_preprocessThread && m_preprocessThread->isRunning()) {
        QMetaObject::invokeMethod(m_preprocessor, [this]() {
            m_preprocessor->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_preprocessThread->quit();
        m_preprocessThread->wait();
    }
    delete m_preprocessor;
    m_preprocessor = nullptr;
    delete m_frameThrottle;
    m_frameThrottle = nullptr;

#ifdef HAVE_OPENCV
    // 3. Stop pose estimator — drain queued estimatePose calls first.
    if (m_poseEstimator && m_poseThread && m_poseThread->isRunning()) {
        QMetaObject::invokeMethod(m_poseEstimator, [this]() {
            m_poseEstimator->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_poseThread->quit();
        m_poseThread->wait();
    }
    delete m_poseEstimator;
    m_poseEstimator = nullptr;

    // 4. Stop ball detector.
    if (m_ballDetector && m_ballThread && m_ballThread->isRunning()) {
        QMetaObject::invokeMethod(m_ballDetector, [this]() {
            m_ballDetector->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_ballThread->quit();
        m_ballThread->wait();
    }
    delete m_ballDetector;
    m_ballDetector = nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void VideoController::connectVideoInput()
{
    // Standard QVideoFrame display path — used by Qt Multimedia and Aravis.
    // DirectConnection so only the freshest frame is ever queued to the main thread.
    connect(m_videoInput, &VideoInputBase::videoFrameReady,
            this, [this](const QVideoFrame &frame) {
                m_frameCaptureCount.fetch_add(1, std::memory_order_relaxed);

                // EventBuffer publish (zero-copy into pre-allocated ring slot)
                if (m_eventBuffer && m_sourceId != pinpoint::kInvalidSourceId
                        && m_eventBuffer->isCapturing()) {
                    publishFrameToBuffer(frame);
                }

                {
                    QMutexLocker lk(&m_latestFrameMutex);
                    m_latestDisplayFrame = frame;
                }
                if (!m_displayFramePending.exchange(true, std::memory_order_acq_rel)) {
                    QMetaObject::invokeMethod(this, &VideoController::drainDisplayFrame,
                                             Qt::QueuedConnection);
                }
            }, Qt::DirectConnection);

    // Raw Bayer display path — used by Spinnaker (and future Bayer backends).
    // Replaces the QVideoFrame path for cameras that emit rawVideoFrameReady.
    connect(m_videoInput, &VideoInputBase::rawVideoFrameReady,
            this, [this](const RawVideoFrame &frame) {
                m_frameCaptureCount.fetch_add(1, std::memory_order_relaxed);

                // EventBuffer publish for raw Bayer frames
                if (m_eventBuffer && m_sourceId != pinpoint::kInvalidSourceId
                        && m_eventBuffer->isCapturing()) {
                    publishRawFrameToBuffer(frame);
                }

                {
                    QMutexLocker lk(&m_latestRawFrameMutex);
                    m_latestRawFrame = frame;
                }
                if (!m_displayFramePending.exchange(true, std::memory_order_acq_rel)) {
                    QMetaObject::invokeMethod(this, &VideoController::drainRawFrame,
                                             Qt::QueuedConnection);
                }
            }, Qt::DirectConnection);

    connect(m_videoInput, &VideoInputBase::errorOccurred,
            this, &VideoController::onVideoError);

#ifdef HAVE_OPENCV
    if (m_frameThrottle) {
        // QVideoFrame throttle path (Qt Multimedia, Aravis).
        connect(m_videoInput, &VideoInputBase::videoFrameReady,
                m_frameThrottle, &FrameThrottle::offer,
                Qt::DirectConnection);
        connect(m_frameThrottle, &FrameThrottle::frameReady,
                m_preprocessor, &VideoPreprocessorBase::processFrame,
                Qt::QueuedConnection);

        // Raw Bayer throttle path (Spinnaker).
        connect(m_videoInput, &VideoInputBase::rawVideoFrameReady,
                m_frameThrottle, &FrameThrottle::offerRaw,
                Qt::DirectConnection);
        connect(m_frameThrottle, &FrameThrottle::rawFrameReady,
                m_preprocessor, &VideoPreprocessorBase::processRawFrame,
                Qt::QueuedConnection);
    }
#endif
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

bool   VideoController::isRecording()    const { return m_recording; }
bool   VideoController::isAravis()       const { return VideoInputFactory::backendType(m_videoInput) == VideoInputFactory::Backend::Aravis; }
bool   VideoController::isSpinnaker()    const { return VideoInputFactory::backendType(m_videoInput) == VideoInputFactory::Backend::Spinnaker; }
bool   VideoController::needsDebayer()   const { return isSpinnaker() && m_videoInput->emitsRawBayer(); }
double  VideoController::preprocessAvgMs()        const { return m_preprocessAvgMs; }
double  VideoController::cameraFps()              const { return m_cameraFps; }
double  VideoController::poseAvgMs()              const { return m_poseAvgMs; }
double  VideoController::poseFps()                const { return m_poseFps; }
QString VideoController::poseBackendLabel()       const { return m_poseBackendLabel; }
int     VideoController::moveNetModel()           const { return m_moveNetModel; }
QString VideoController::deviceDescription()      const { return m_deviceDescription; }
int     VideoController::perspective()            const { return m_perspective; }

void VideoController::setPerspective(int p)
{
    if (m_perspective == p)
        return;
    m_perspective = p;
    emit perspectiveChanged();
}

bool    VideoController::moveNetThunderAvailable() const
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    return PoseEstimatorMoveNet::isVariantAvailable(PoseEstimatorMoveNet::ModelVariant::Thunder);
#else
    return false;
#endif
}

QVariantList VideoController::poseKeypoints() const { return m_poseKeypoints; }

QRectF VideoController::roi() const { return m_roi; }

void VideoController::setRoi(QRectF roi)
{
    roi = roi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (roi.isEmpty() || m_roi == roi)
        return;
    m_roi = roi;
    emit roiChanged();
}

void VideoController::clearRoi()
{
    if (m_roi.isEmpty())
        return;
    m_roi = QRectF();
    emit roiChanged();
    if (m_ballDetected) {
        m_ballDetected = false;
        m_ballX = m_ballY = m_ballRadius = 0.0;
        emit ballDetectedChanged();
    }
    m_ballWindow.clear();
    m_ballPresentCount = 0;
    m_ballPresent = false;
    if (m_ballPresencePercent != 0.0) {
        m_ballPresencePercent = 0.0;
        emit ballPresencePercentChanged();
    }
}

bool   VideoController::ballDetected()        const { return m_ballDetected; }
double VideoController::ballX()               const { return m_ballX; }
double VideoController::ballY()               const { return m_ballY; }
double VideoController::ballRadius()          const { return m_ballRadius; }
double VideoController::ballPresencePercent() const { return m_ballPresencePercent; }

#ifdef HAVE_OPENCV
void VideoController::onBallDetected(const BallDetection &result)
{
    // Update the rolling 50-frame window.
    m_ballWindow.push_back(result.found);
    if (result.found)
        ++m_ballPresentCount;
    if (static_cast<int>(m_ballWindow.size()) > kBallWindowSize) {
        if (m_ballWindow.front())
            --m_ballPresentCount;
        m_ballWindow.pop_front();
    }
    const int pct = m_ballWindow.empty() ? 0
                  : static_cast<int>(std::round(m_ballPresentCount * 100.0 / static_cast<int>(m_ballWindow.size())));
    if (pct != static_cast<int>(std::round(m_ballPresencePercent))) {
        m_ballPresencePercent = static_cast<double>(pct);
        emit ballPresencePercentChanged();
    }

    // Fire ting when the threshold-based presence state flips false → true.
    const bool nowPresent = (m_ballPresencePercent > kBallPresentThreshold);
    if (!m_ballPresent && nowPresent)
        m_tingPlayer->play();
    m_ballPresent = nowPresent;

    // Only emit ballDetectedChanged when the present/absent state flips.
    if (!result.found && !m_ballDetected)
        return;

    m_ballDetected = result.found;
    m_ballX        = result.x;
    m_ballY        = result.y;
    m_ballRadius   = result.radius;
    emit ballDetectedChanged();
}
#endif

VideoPreprocessorBase *VideoController::preprocessor() const { return m_preprocessor; }

void VideoController::setVideoSink(QVideoSink *sink)
{
    m_videoSink = sink;
}

void VideoController::setBayerItem(QObject *item)
{
    m_bayerItem = qobject_cast<BayerVideoItem *>(item);
    if (item && !m_bayerItem)
        ppWarn() << "[VideoController] setBayerItem: cast failed — item is" << item->metaObject()->className();
    else
        ppDebug() << "[VideoController] setBayerItem:" << (m_bayerItem ? "ok" : "null");
}

void VideoController::selectMoveNetModel(int variant)
{
#if defined(HAVE_OPENCV) && defined(HAVE_ONNXRUNTIME)
    if (variant == m_moveNetModel || !m_poseThread)
        return;

    m_moveNetModel = variant;
    emit moveNetModelChanged();
    m_poseAvgMs = 0.0; emit poseAvgMsChanged();
    m_poseFps   = 0.0; emit poseFpsChanged();
    m_poseBackendLabel = QString(); emit poseBackendLabelChanged();

#if defined(HAVE_MOVENET)
    if (m_poseEstimator) {
        QMetaObject::invokeMethod(m_poseEstimator, "reloadModel",
                                  Qt::QueuedConnection, Q_ARG(int, variant));
    }
#endif
#else
    Q_UNUSED(variant)
#endif
}

// ---------------------------------------------------------------------------
// Recording control
// ---------------------------------------------------------------------------

void VideoController::startRecording()
{
    if (m_recording || !m_captureThread->isRunning())
        return;

#ifdef Q_OS_MACOS
    auto *self = this;
    requestCameraPermission([self](bool granted) {
        QMetaObject::invokeMethod(self, [self, granted]() {
            if (!granted) {
                ppWarn() << "[VideoController] Camera permission denied."
                           << "Grant access in System Settings → Privacy & Security → Camera.";
                return;
            }
            QMetaObject::invokeMethod(self->m_videoInput, [self]() {
                self->m_videoInput->start(self->m_deviceId);
            }, Qt::QueuedConnection);
            self->m_recording = true;
            emit self->isRecordingChanged();
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(self);
#else
    m_recording = true;
    emit isRecordingChanged();

    QMetaObject::invokeMethod(m_videoInput, [this]() {
        if (m_videoInput->start(m_deviceId))
            return;

        if (m_deviceId.isEmpty()) {
            // Auto-mode only: fall back from industrial camera to Qt Multimedia webcam.
            ppWarn() << "[VideoController] Failed to start primary video input. Attempting fallback...";
            VideoInputFactory::Backend currentBackend = VideoInputFactory::backendType(m_videoInput);
            if (currentBackend == VideoInputFactory::Backend::Aravis ||
                currentBackend == VideoInputFactory::Backend::Spinnaker)
            {
                QMetaObject::invokeMethod(this, [this]() {
                    delete m_videoInput;
                    m_videoInput = VideoInputFactory::create(VideoInputFactory::Backend::QtMultimedia);
                    m_videoInput->moveToThread(m_captureThread);
                    connectVideoInput();
                    emit isAravisChanged();
                    emit isSpinnakerChanged();
                    emit needsDebayerChanged();
                    QMetaObject::invokeMethod(m_videoInput, [this]() {
                        if (!m_videoInput->start()) {
                            QMetaObject::invokeMethod(this, [this]() {
                                m_recording = false;
                                emit isRecordingChanged();
                            }, Qt::QueuedConnection);
                        }
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, [this]() {
                    m_recording = false;
                    emit isRecordingChanged();
                }, Qt::QueuedConnection);
            }
        } else {
            ppWarn() << "[VideoController] Failed to start camera:" << m_deviceDescription;
            QMetaObject::invokeMethod(this, [this]() {
                m_recording = false;
                emit isRecordingChanged();
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
#endif
}

void VideoController::stopRecording()
{
    if (!m_recording)
        return;
    QMetaObject::invokeMethod(m_videoInput, [this]() {
        m_videoInput->stop();
    }, Qt::QueuedConnection);
    m_recording = false;
    emit isRecordingChanged();
    m_poseKeypoints.clear();
    emit poseKeypointsChanged();
    if (m_ballDetected) {
        m_ballDetected = false;
        m_ballX = m_ballY = m_ballRadius = 0.0;
        emit ballDetectedChanged();
    }
    m_ballWindow.clear();
    m_ballPresentCount = 0;
    m_ballPresent = false;
    if (m_ballPresencePercent != 0.0) {
        m_ballPresencePercent = 0.0;
        emit ballPresencePercentChanged();
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void VideoController::drainDisplayFrame()
{
    // Reset pending flag BEFORE consuming the frame so that any videoFrameReady
    // arriving in this window can schedule a fresh drain (prevents frames getting
    // permanently stuck when they arrive mid-drain).
    m_displayFramePending.store(false, std::memory_order_release);

    QVideoFrame f;
    {
        QMutexLocker lk(&m_latestFrameMutex);
        f = m_latestDisplayFrame;
        m_latestDisplayFrame = QVideoFrame();
    }
    onVideoFrame(f);
}

void VideoController::drainRawFrame()
{
    // Same ordering as drainDisplayFrame: reset pending flag first.
    m_displayFramePending.store(false, std::memory_order_release);

    RawVideoFrame raw;
    {
        QMutexLocker lk(&m_latestRawFrameMutex);
        raw              = m_latestRawFrame;
        m_latestRawFrame = RawVideoFrame();
    }

    if (raw.isNull())
        return;

    static bool logged = false;
    if (!logged) {
        ppDebug() << "[VideoController] first raw Bayer frame:" << raw.width << "x" << raw.height
                 << "pattern" << static_cast<int>(raw.pattern)
                 << "bayerItem" << (m_bayerItem ? "set" : "NULL");
        logged = true;
    }

    if (m_bayerItem)
        m_bayerItem->updateFrame(raw);
}

void VideoController::onVideoFrame(const QVideoFrame &frame)
{
    if (m_videoSink && frame.isValid())
        m_videoSink->setVideoFrame(frame);
}

#ifdef HAVE_OPENCV
void VideoController::onPoseEstimated(const PoseResult &result)
{
    QVariantList kps;
    kps.reserve(PoseResult::kNumKeypoints);
    for (const auto &kp : result.keypoints) {
        QVariantMap m;
        m[QStringLiteral("x")]     = static_cast<double>(kp.x);
        m[QStringLiteral("y")]     = static_cast<double>(kp.y);
        m[QStringLiteral("score")] = static_cast<double>(kp.score);
        kps.append(m);
    }
    m_poseKeypoints = kps;
    emit poseKeypointsChanged();
}
#endif

void VideoController::onVideoError(const QString &message)
{
    ppWarn() << "[Video]" << message;
}

void VideoController::onPreprocessStats(double avgMs)
{
    if (qFuzzyCompare(m_preprocessAvgMs, avgMs))
        return;
    m_preprocessAvgMs = avgMs;
    emit preprocessAvgMsChanged();
}

void VideoController::onPoseStats(double avgMs, double fps)
{
    if (!qFuzzyCompare(m_poseAvgMs, avgMs)) { m_poseAvgMs = avgMs; emit poseAvgMsChanged(); }
    if (!qFuzzyCompare(m_poseFps,   fps))   { m_poseFps   = fps;   emit poseFpsChanged();   }
}

void VideoController::onPoseBackendReady(const QString &label)
{
    if (m_poseBackendLabel == label)
        return;
    m_poseBackendLabel = label;
    emit poseBackendLabelChanged();
}

// ---------------------------------------------------------------------------
// EventBuffer descriptor registration (called once on first Active state)
// ---------------------------------------------------------------------------

void VideoController::updateBufferDescriptor()
{
    if (!m_eventBuffer) return;
    if (!m_videoInput || !m_videoInput->isActive()) return;

    QVideoFrameFormat fmt = m_videoInput->frameFormat();
    bool isRaw = m_videoInput->emitsRawBayer();

    pinpoint::SourceDescriptor desc;
    desc.name            = m_deviceDescription.toStdString();
    desc.window_duration = std::chrono::milliseconds(5000);
    desc.sync_source     = pinpoint::SyncSource::SoftwareTimestamp;

    pinpoint::CameraFormat cfmt{};

    if (isRaw) {
        cfmt.width  = static_cast<uint32_t>(fmt.frameWidth()  > 0 ? fmt.frameWidth()  : 1920);
        cfmt.height = static_cast<uint32_t>(fmt.frameHeight() > 0 ? fmt.frameHeight() : 1080);
        cfmt.pixel_format          = pinpoint::PixelFormat::BayerRG8;
        cfmt.max_payload_bytes     = cfmt.width * cfmt.height;
        cfmt.typical_payload_bytes = cfmt.max_payload_bytes;
        desc.format.device         = pinpoint::DeviceKind::Camera_GenICam;
    } else {
        cfmt.width  = static_cast<uint32_t>(fmt.frameWidth());
        cfmt.height = static_cast<uint32_t>(fmt.frameHeight());
        switch (fmt.pixelFormat()) {
        case QVideoFrameFormat::Format_NV12:
            cfmt.pixel_format      = pinpoint::PixelFormat::NV12;
            cfmt.max_payload_bytes = cfmt.width * cfmt.height * 3 / 2;
            break;
        case QVideoFrameFormat::Format_YUYV:
            cfmt.pixel_format      = pinpoint::PixelFormat::YUYV;
            cfmt.max_payload_bytes = cfmt.width * cfmt.height * 2;
            break;
        case QVideoFrameFormat::Format_YUV420P:
            cfmt.pixel_format      = pinpoint::PixelFormat::YUV420P;
            cfmt.max_payload_bytes = cfmt.width * cfmt.height * 3 / 2;
            break;
        case QVideoFrameFormat::Format_BGRA8888:
            cfmt.pixel_format      = pinpoint::PixelFormat::BGRA32;
            cfmt.max_payload_bytes = cfmt.width * cfmt.height * 4;
            break;
        default:
            cfmt.pixel_format      = pinpoint::PixelFormat::Unknown;
            cfmt.max_payload_bytes = cfmt.width * cfmt.height * 4;
            break;
        }
        cfmt.typical_payload_bytes = cfmt.max_payload_bytes;
        desc.format.device         = pinpoint::DeviceKind::Camera_UVC;
    }

    float fps = fmt.streamFrameRate() > 0.0f ? fmt.streamFrameRate() : 30.0f;
    cfmt.fps_numerator   = static_cast<uint32_t>(fps * 1000);
    cfmt.fps_denominator = 1000;
    desc.expected_interarrival_us =
        std::chrono::microseconds(static_cast<int64_t>(1'000'000.0f / fps));
    desc.format.format = cfmt;

    // Always log actual format — useful for verifying correct sizing vs the
    // conservative defaults used at construction time.
    ppWarn() << "[VideoController] camera active:" << QString::fromStdString(desc.name)
             << cfmt.width << "x" << cfmt.height << "@" << fps << "fps"
             << "ideal slots:" << desc.computeSlotCount()
             << "ideal slot_bytes:" << desc.computeSlotBytes();

    // Source was registered in the constructor with conservative defaults.
    // EventBuffer v1 does not support re-registration after start().
    // TODO Phase 5: support source re-registration for resolution/fps changes.
    if (m_sourceId != pinpoint::kInvalidSourceId) return;

    // Safety guard — buffer must be Idle to register (should not reach here
    // in normal operation since the constructor always registers first).
    if (m_eventBuffer->state() != pinpoint::BufferState::Idle) return;

    m_sourceId = m_eventBuffer->registerSource(desc);
}

// ---------------------------------------------------------------------------
// EventBuffer publish helpers (called on the capture thread via DirectConnection)
// ---------------------------------------------------------------------------

void VideoController::publishFrameToBuffer(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    QVideoFrame mutable_frame = frame;  // QVideoFrame is ref-counted; copy is cheap
    if (!mutable_frame.map(QVideoFrame::ReadOnly))
        return;

    const uint8_t *src  = mutable_frame.bits(0);
    const size_t   size = static_cast<size_t>(mutable_frame.mappedBytes(0));

    auto slot = m_eventBuffer->acquireWriteSlot(m_sourceId);
    if (slot.valid && size <= slot.capacity) {
        std::memcpy(slot.data, src, size);
        *slot.bytes_written = static_cast<uint32_t>(size);
        *slot.timestamp_us  = pinpoint::EventBuffer::nowMicros();
        m_eventBuffer->publish(m_sourceId, slot.sequence);
    }

    mutable_frame.unmap();
}

void VideoController::publishRawFrameToBuffer(const RawVideoFrame &frame)
{
    if (frame.isNull())
        return;

    const size_t size = static_cast<size_t>(frame.data.size());
    auto slot = m_eventBuffer->acquireWriteSlot(m_sourceId);
    if (slot.valid && size <= slot.capacity) {
        std::memcpy(slot.data, frame.data.constData(), size);
        *slot.bytes_written = static_cast<uint32_t>(size);
        *slot.timestamp_us  = pinpoint::EventBuffer::nowMicros();
        m_eventBuffer->publish(m_sourceId, slot.sequence);
    }
}
