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

#include "app_settings.h"
#include "event_buffer.h"
#include "source_descriptor.h"
#include <cstring>
#include <QVideoFrameFormat>
#include <QVideoSink>

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
#include <QVideoFrameFormat>
#include <QVideoSink>

#ifdef Q_OS_MACOS
#include "macos_permissions.h"
#endif

// Maps the backend-neutral PixelEncoding (from CameraCapabilities) to the
// pinpoint buffer type.  Used by both the constructor (pre-start estimate from
// enumerated capabilities) and updateBufferDescriptor() (post-start correction).
static pinpoint::PixelFormat bufferPixelFormat(PixelEncoding enc)
{
    switch (enc) {
    case PixelEncoding::YUV422_YUYV: return pinpoint::PixelFormat::YUYV;
    case PixelEncoding::YUV422_UYVY: return pinpoint::PixelFormat::UYVY;
    case PixelEncoding::YUV420_NV12: return pinpoint::PixelFormat::NV12;
    case PixelEncoding::YUV420_I420: return pinpoint::PixelFormat::YUV420P;
    case PixelEncoding::BGR8:        return pinpoint::PixelFormat::BGRA32;
    case PixelEncoding::BayerRG8:    return pinpoint::PixelFormat::BayerRG8;
    case PixelEncoding::BayerRG16:   return pinpoint::PixelFormat::BayerRG16;
    case PixelEncoding::BayerBG8:    return pinpoint::PixelFormat::BayerBG8;
    case PixelEncoding::BayerGR8:    return pinpoint::PixelFormat::BayerGR8;
    case PixelEncoding::BayerGB8:    return pinpoint::PixelFormat::BayerGB8;
    case PixelEncoding::BayerGB16:   return pinpoint::PixelFormat::BayerGB16;
    default:                          return pinpoint::PixelFormat::Unknown;
    }
}

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
    , m_deviceSerialNumber(device.capabilities.serialNumber)
    , m_deviceAlias(AppSettings().cameraAlias().value(
          device.description + QStringLiteral("|") +
          (device.capabilities.serialNumber.isEmpty() ? device.id : device.capabilities.serialNumber)
      ).toString())
    , m_videoInput(VideoInputFactory::create(device.backend))
    , m_eventBuffer(buffer)
{
    // Register the source before EventBuffer::start() is called in main().
    // Capabilities were queried at enumeration time (VideoInputFactory::enumerateDevices)
    // and stored in the Device struct — use them directly without re-opening the camera.
    if (m_eventBuffer) {
        const CameraCapabilities &caps = device.capabilities;

        pinpoint::SourceDescriptor desc;
        desc.name       = device.description.toStdString();
        desc.identifier = device.capabilities.serialNumber.toStdString();

        pinpoint::CameraFormat cfmt{};

        // --- Pixel format ---
        // queryCapabilities() now populates defaultFormat from Qt's preferred
        // (first) video format pre-start, so we have a real pixel format here
        // without needing the camera to be running.  updateBufferDescriptor()
        // will call updateSourceFormat() to correct it to the actual negotiated
        // format once the camera is active.
        cfmt.pixel_format = bufferPixelFormat(caps.pixelFormat.defaultFormat.encoding);

        // --- Resolution ---
        // Slot capacity must fit any frame Qt might negotiate, so size for the
        // LARGEST supported resolution (not the default).  The actual negotiated
        // resolution is written into the descriptor by updateSourceFormat() once
        // the camera is active.
        int w = 0, h = 0;
        for (const Resolution &r : caps.resolution.presets) {
            if (r.width * r.height > w * h) { w = r.width; h = r.height; }
        }
        if (w == 0) { w = caps.resolution.defaultResolution.width;
                      h = caps.resolution.defaultResolution.height; }
        cfmt.width  = static_cast<uint32_t>(w > 0 ? w : 1920);
        cfmt.height = static_cast<uint32_t>(h > 0 ? h : 1080);

        // --- Slot payload: worst-case bytes per pixel across all supported formats ---
        int maxBpp = 2; // NV12/YUYV baseline
        for (const PixelFormat &pf : caps.pixelFormat.supported) {
            int bpp = pf.bitsPerPixel > 0 ? (pf.bitsPerPixel + 7) / 8 : 0;
            if (bpp == 0) {
                switch (pf.encoding) {
                case PixelEncoding::RGBA8:
                case PixelEncoding::BGR8:          bpp = 4; break;
                case PixelEncoding::BayerRG16:
                case PixelEncoding::BayerGB16:
                case PixelEncoding::YUV422_YUYV:
                case PixelEncoding::YUV422_UYVY:   bpp = 2; break;
                default:                           bpp = 2; break;
                }
            }
            maxBpp = std::max(maxBpp, bpp);
        }
        cfmt.max_payload_bytes     = cfmt.width * cfmt.height * static_cast<uint32_t>(maxBpp);
        cfmt.typical_payload_bytes = cfmt.max_payload_bytes;

        // --- FPS: use the maximum supported rate for worst-case slot count ---
        double fps = caps.frameRate.range.max > 0.0 ? caps.frameRate.range.max : 60.0;
        cfmt.fps_numerator   = static_cast<uint32_t>(fps * 1000.0);
        cfmt.fps_denominator = 1000;

        switch (device.backend) {
        case VideoInputFactory::Backend::AppleAVFoundation:
            desc.format.device = pinpoint::DeviceKind::Camera_AVFoundation;
            break;
        case VideoInputFactory::Backend::Aravis:
        case VideoInputFactory::Backend::Spinnaker:
            desc.format.device     = pinpoint::DeviceKind::Camera_GenICam;
            // GenICam cameras (Spinnaker/Aravis) can run well above 60 fps.
            // Use 200 fps as the conservative upper bound so the ring is sized
            // for ceil(200*5)=1000 → 1024 slots, enough for 150 fps cameras.
            // At 60 fps the ring was 512 slots, causing constant overwrites at
            // 150 fps (150*5=750 frames > 512 ring capacity).
            cfmt.fps_numerator   = 200;
            cfmt.fps_denominator = 1;
            break;
        default:
            desc.format.device = pinpoint::DeviceKind::Camera_UVC;
            break;
        }
        desc.format.format            = cfmt;
        desc.window_duration          = std::chrono::milliseconds(5000);
        desc.expected_interarrival_us =
            std::chrono::microseconds(static_cast<int64_t>(1'000'000.0 / fps));
        desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;

        ppInfo() << "[VideoController] registering buffer source:"
                 << device.description
                 << (device.capabilities.serialNumber.isEmpty()
                     ? QString() : "(" + device.capabilities.serialNumber + ")")
                 << cfmt.width << "x" << cfmt.height
                 << "@ max" << fps << "fps"
                 << "slot_bytes:" << desc.computeSlotBytes()
                 << "slots:" << desc.computeSlotCount();

        // CameraManager::setSelected() ensures the buffer is Paused before
        // creating this controller, so registration is always safe.
        // Memory is held until deregisterFromBuffer() is called on deselection.
        m_sourceId = m_eventBuffer->registerSource(desc);
    }

    setupPipeline();
}

void VideoController::deregisterFromBuffer()
{
    if (!m_eventBuffer || m_sourceId == pinpoint::kInvalidSourceId) return;
    m_eventBuffer->deregisterSource(m_sourceId);
    m_sourceId = pinpoint::kInvalidSourceId;
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
    if (kPoseEnabled && m_eventBuffer) {
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
        m_frameThrottle->setConsumerCount(2);  // pose estimator + ball detector

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
        connect(m_ballDetector, &BallDetector::ballDetected,
                m_frameThrottle, &FrameThrottle::clearBusy, Qt::DirectConnection);

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

                    // queryCapabilities() must run on the capture thread where
                    // m_videoInput (and its camera handle) lives.  Post it there,
                    // then deliver the result back to the main thread.
                    // By the time this queued call runs, frames are flowing and
                    // m_sink->videoFrame() is valid — so queryCapabilities() returns
                    // the true negotiated format.  Re-run updateBufferDescriptor()
                    // here to correct any mismatch from the immediate call above.
                    QMetaObject::invokeMethod(m_videoInput, [this]() {
                        CameraCapabilities caps = m_videoInput->queryCapabilities();
                        QMetaObject::invokeMethod(this, [this, caps = std::move(caps)]() {
                            int w     = caps.resolution.defaultResolution.width;
                            int h     = caps.resolution.defaultResolution.height;
                            double fps = caps.frameRate.range.defaultValue;
                            if (w > 0 && h > 0) {
                                m_frameWidth    = w;
                                m_frameHeight   = h;
                                m_configuredFps = fps;
                                emit frameSizeChanged();
                            }
                            updateBufferDescriptor();
                        });
                    }, Qt::QueuedConnection);
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

void VideoController::stopCapture()
{
    if (m_captureThread->isRunning()) {
        QMetaObject::invokeMethod(m_videoInput, [this]() {
            m_videoInput->stop();
            m_videoInput->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_captureThread->quit();
        m_captureThread->wait();
    }
}

VideoController::~VideoController()
{
    // Shutdown in pipeline order so no stage receives frames after it is torn down.

    // 1. Stop capture — no more videoFrameReady signals after this.
    stopCapture();
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
        // Suppressed in preview-only mode so the pose/ball pipeline never runs.
        connect(m_videoInput, &VideoInputBase::videoFrameReady,
                this, [this](const QVideoFrame &frame) {
                    if (!m_previewOnly.load(std::memory_order_relaxed))
                        m_frameThrottle->offer(frame);
                }, Qt::DirectConnection);
        connect(m_frameThrottle, &FrameThrottle::frameReady,
                m_preprocessor, &VideoPreprocessorBase::processFrame,
                Qt::QueuedConnection);

        // Raw Bayer throttle path (Spinnaker).
        connect(m_videoInput, &VideoInputBase::rawVideoFrameReady,
                this, [this](const RawVideoFrame &frame) {
                    if (!m_previewOnly.load(std::memory_order_relaxed))
                        m_frameThrottle->offerRaw(frame);
                }, Qt::DirectConnection);
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
int     VideoController::frameWidth()             const { return m_frameWidth; }
int     VideoController::frameHeight()            const { return m_frameHeight; }
double  VideoController::configuredFps()          const { return m_configuredFps; }
double  VideoController::poseAvgMs()              const { return m_poseAvgMs; }
double  VideoController::poseFps()                const { return m_poseFps; }
double  VideoController::ballAvgMs()              const { return m_ballAvgMs; }
QString VideoController::poseBackendLabel()       const { return m_poseBackendLabel; }
int     VideoController::moveNetModel()           const { return m_moveNetModel; }
QString VideoController::deviceDescription()      const { return m_deviceDescription; }
QString VideoController::deviceSerialNumber()     const { return m_deviceSerialNumber; }
QString VideoController::deviceAlias()            const { return m_deviceAlias; }

void VideoController::setDeviceAlias(const QString &alias)
{
    if (m_deviceAlias == alias) return;
    m_deviceAlias = alias;
    emit deviceAliasChanged();
}

int     VideoController::perspective()            const { return m_perspective; }
bool    VideoController::isMirrored()             const { return m_isMirrored; }

void VideoController::setPerspective(int p)
{
    if (m_perspective == p)
        return;
    m_perspective = p;
    emit perspectiveChanged();
}

void VideoController::setIsMirrored(bool mirrored)
{
    if (m_isMirrored == mirrored)
        return;
    m_isMirrored = mirrored;
    emit isMirroredChanged();
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

QRectF VideoController::cropRoi() const { return m_cropRoi; }

void VideoController::setCropRoi(QRectF roi)
{
    roi = roi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (m_cropRoi == roi) return;
    m_cropRoi = roi;
    emit cropRoiChanged();
}

void VideoController::clearCropRoi()
{
    if (m_cropRoi.isEmpty()) return;
    m_cropRoi = QRectF();
    emit cropRoiChanged();
}

bool   VideoController::ballDetected()        const { return m_ballDetected; }
double VideoController::ballX()               const { return m_ballX; }
double VideoController::ballY()               const { return m_ballY; }
double VideoController::ballRadius()          const { return m_ballRadius; }
double VideoController::ballPresencePercent() const { return m_ballPresencePercent; }
bool   VideoController::ballPresent()         const { return m_ballPresent; }
double VideoController::ballHoughConf()       const { return m_ballHoughConf; }
int    VideoController::ballWhiteSatCeil()    const { return m_ballWhiteSatCeil; }

#ifdef HAVE_OPENCV
void VideoController::setBallHoughConf(double v)
{
    v = qBound(0.3, v, 1.0);
    if (qFuzzyCompare(v, m_ballHoughConf)) return;
    m_ballHoughConf = v;
    if (m_ballDetector)
        QMetaObject::invokeMethod(m_ballDetector, [this]() {
            m_ballDetector->setParams(m_ballHoughConf, m_ballWhiteSatCeil);
        }, Qt::QueuedConnection);
    emit ballHoughConfChanged();
}

void VideoController::setBallWhiteSatCeil(int v)
{
    v = qBound(20, v, 120);
    if (v == m_ballWhiteSatCeil) return;
    m_ballWhiteSatCeil = v;
    if (m_ballDetector)
        QMetaObject::invokeMethod(m_ballDetector, [this]() {
            m_ballDetector->setParams(m_ballHoughConf, m_ballWhiteSatCeil);
        }, Qt::QueuedConnection);
    emit ballWhiteSatCeilChanged();
}
#else
void VideoController::setBallHoughConf(double)    {}
void VideoController::setBallWhiteSatCeil(int)    {}
#endif

#ifdef HAVE_OPENCV
void VideoController::onBallDetected(const BallDetection &result)
{
    if (m_replaying) return;

    // EMA of detection latency — only when detect() actually ran (detectMs > 0).
    if (result.detectMs > 0) {
        constexpr double kAlpha = 0.1;
        const double ms = m_ballAvgMs == 0.0
                        ? static_cast<double>(result.detectMs)
                        : kAlpha * static_cast<double>(result.detectMs) + (1.0 - kAlpha) * m_ballAvgMs;
        if (!qFuzzyCompare(m_ballAvgMs, ms)) {
            m_ballAvgMs = ms;
            emit ballAvgMsChanged();
        }
    }

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

    // Fire ting and notify CameraManager when threshold-based presence flips.
    const bool nowPresent = (m_ballPresencePercent > kBallPresentThreshold);
    if (m_ballPresent != nowPresent) {
        if (!m_ballPresent && nowPresent)
            m_tingPlayer->play();
        m_ballPresent = nowPresent;
        emit ballPresentChanged(nowPresent);
    }

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

void VideoController::setSettingsSink(QVideoSink *sink)
{
    m_settingsSink = sink;
}

void VideoController::setBayerItem(QObject *item)
{
    m_bayerItem = qobject_cast<BayerVideoItem *>(item);
    if (item && !m_bayerItem)
        ppError() << "[VideoController] setBayerItem: cast failed — item is" << item->metaObject()->className();
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

void VideoController::startPreview()
{
    if (m_previewing || !m_captureThread->isRunning()) return;
    m_previewing = true;
    if (m_recording) return; // camera already running; pipeline already active
    m_previewOnly.store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(m_videoInput, [this]() {
        if (!m_videoInput->start(m_deviceId)) {
            QMetaObject::invokeMethod(this, [this]() {
                m_previewing = false;
                m_previewOnly.store(false, std::memory_order_relaxed);
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}

void VideoController::stopPreview()
{
    if (!m_previewing) return;
    m_previewing = false;
    m_previewOnly.store(false, std::memory_order_relaxed);
    if (m_recording) return; // recording keeps camera alive
    QMetaObject::invokeMethod(m_videoInput, [this]() {
        m_videoInput->stop();
    }, Qt::QueuedConnection);
}

void VideoController::startRecording()
{
    if (m_recording || !m_captureThread->isRunning())
        return;

#ifdef Q_OS_MACOS
    auto *self = this;
    requestCameraPermission([self](bool granted) {
        QMetaObject::invokeMethod(self, [self, granted]() {
            if (!granted) {
                ppError() << "[VideoController] Camera permission denied."
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

    if (m_previewing) {
        // Camera already running — promote to full recording by enabling the pipeline.
        m_previewOnly.store(false, std::memory_order_relaxed);
        return;
    }

    QMetaObject::invokeMethod(m_videoInput, [this]() {
        if (m_videoInput->start(m_deviceId))
            return;

        if (m_deviceId.isEmpty()) {
            // Auto-mode only: fall back from industrial camera to Qt Multimedia webcam.
            ppError() << "[VideoController] Failed to start primary video input. Attempting fallback...";
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
            ppError() << "[VideoController] Failed to start camera:" << m_deviceDescription;
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
    if (!m_previewing) {
        QMetaObject::invokeMethod(m_videoInput, [this]() {
            m_videoInput->stop();
        }, Qt::QueuedConnection);
    } else {
        // Demote back to preview-only — camera stays running but pipeline suppressed.
        m_previewOnly.store(true, std::memory_order_relaxed);
    }
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
    if (m_ballPresent) {
        m_ballPresent = false;
        emit ballPresentChanged(false);
    }
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

    if (m_replaying)  // replay frames are pushed directly; suppress live feed
        return;

    // Fallback: populate frame size from the actual pixel dimensions if the
    // capabilities query hasn't delivered a result yet.
    if (f.isValid() && m_frameWidth == 0) {
        int w = f.width(), h = f.height();
        if (w > 0 && h > 0) {
            m_frameWidth  = w;
            m_frameHeight = h;
            emit frameSizeChanged();
        }
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

    if (m_replaying || raw.isNull())  // suppress live feed during replay
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

    // Deliver greyscale Y8 preview to the settings sink (Bayer pattern as luma).
    if (m_settingsSink && !raw.isNull()) {
        QVideoFrameFormat fmt(QSize(raw.width, raw.height), QVideoFrameFormat::Format_Y8);
        QVideoFrame yFrame(fmt);
        if (yFrame.map(QVideoFrame::WriteOnly)) {
            const auto *src = reinterpret_cast<const uchar *>(raw.data.constData());
            uchar *dst = yFrame.bits(0);
            const int pixels = raw.width * raw.height;
            // Raw data is always 8-bit packed (BayerRG8 etc.) from the ring buffer.
            memcpy(dst, src, pixels);
            yFrame.unmap();
            m_settingsSink->setVideoFrame(yFrame);
        }
    }
}

void VideoController::onVideoFrame(const QVideoFrame &frame)
{
    if (m_videoSink && frame.isValid())
        m_videoSink->setVideoFrame(frame);
    if (m_settingsSink && frame.isValid())
        m_settingsSink->setVideoFrame(frame);
}

#ifdef HAVE_OPENCV
void VideoController::onPoseEstimated(const PoseResult &result)
{
    if (m_replaying) return;

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
// Replay support
// ---------------------------------------------------------------------------

static QVideoFrameFormat::PixelFormat toQtPixelFormat(pinpoint::PixelFormat fmt)
{
    switch (fmt) {
    case pinpoint::PixelFormat::NV12:    return QVideoFrameFormat::Format_NV12;
    case pinpoint::PixelFormat::YUYV:    return QVideoFrameFormat::Format_YUYV;
    case pinpoint::PixelFormat::UYVY:    return QVideoFrameFormat::Format_UYVY;
    case pinpoint::PixelFormat::YUV420P: return QVideoFrameFormat::Format_YUV420P;
    case pinpoint::PixelFormat::BGRA32:  return QVideoFrameFormat::Format_BGRA8888;
    default:                              return QVideoFrameFormat::Format_Invalid;
    }
}

pinpoint::SourceId VideoController::sourceId() const { return m_sourceId; }
bool               VideoController::isReplaying() const { return m_replaying; }

void VideoController::setReplaying(bool replaying)
{
    if (m_replaying == replaying) return;
    m_replaying = replaying;
    if (replaying) {
        m_poseKeypoints.clear();
        emit poseKeypointsChanged();
    }
    emit isReplayingChanged();
}

void VideoController::displayReplayFrame(const std::byte *data, size_t bytes,
                                          int w, int h, pinpoint::PixelFormat fmt)
{
    if (!data || bytes == 0) return;

    const bool isBayer = (fmt == pinpoint::PixelFormat::BayerRG8  ||
                          fmt == pinpoint::PixelFormat::BayerRG12 ||
                          fmt == pinpoint::PixelFormat::BayerRG16 ||
                          fmt == pinpoint::PixelFormat::BayerBG8  ||
                          fmt == pinpoint::PixelFormat::BayerGR8  ||
                          fmt == pinpoint::PixelFormat::BayerGB8  ||
                          fmt == pinpoint::PixelFormat::BayerGB16);

    if (isBayer) {
        if (!m_bayerItem) return;
        RawVideoFrame raw;
        raw.width   = w;
        raw.height  = h;
        raw.pattern = (fmt == pinpoint::PixelFormat::BayerBG8)
                    ? RawVideoFrame::BayerPattern::BG
                    : (fmt == pinpoint::PixelFormat::BayerGR8)
                    ? RawVideoFrame::BayerPattern::GR
                    : (fmt == pinpoint::PixelFormat::BayerGB8  ||
                       fmt == pinpoint::PixelFormat::BayerGB16)
                    ? RawVideoFrame::BayerPattern::GB
                    : RawVideoFrame::BayerPattern::RG;
        raw.data    = QByteArray(reinterpret_cast<const char *>(data),
                                 static_cast<qsizetype>(bytes));
        m_bayerItem->updateFrame(raw);
    } else {
        if (!m_videoSink) return;
        const auto qtFmt = toQtPixelFormat(fmt);
        if (qtFmt == QVideoFrameFormat::Format_Invalid) return;

        QVideoFrameFormat frameFormat(QSize(w, h), qtFmt);
        QVideoFrame replayFrame(frameFormat);
        if (!replayFrame.map(QVideoFrame::WriteOnly)) return;

        // Restore all planes from the sequentially-stored buffer.
        // publishFrameToBuffer stores planes in order: plane0 bytes, plane1 bytes, …
        const auto *src = reinterpret_cast<const uint8_t *>(data);
        size_t offset = 0;
        for (int p = 0; p < replayFrame.planeCount(); ++p) {
            const size_t planeBytes = static_cast<size_t>(replayFrame.mappedBytes(p));
            const size_t available  = offset < bytes ? bytes - offset : 0;
            std::memcpy(replayFrame.bits(p), src + offset,
                        std::min(planeBytes, available));
            offset += planeBytes;
        }

        replayFrame.unmap();
        m_videoSink->setVideoFrame(replayFrame);
    }
}

// ---------------------------------------------------------------------------
// EventBuffer descriptor registration (called once on first Active state)
// ---------------------------------------------------------------------------

void VideoController::updateBufferDescriptor()
{
    if (!m_eventBuffer || m_sourceId == pinpoint::kInvalidSourceId) return;
    if (!m_videoInput || !m_videoInput->isActive()) return;

    // queryCapabilities() returns accurate values for all backends once the
    // camera is active — no Qt-specific format mapping needed here.
    const CameraCapabilities caps = m_videoInput->queryCapabilities();
    const PixelFormat        &pf  = caps.pixelFormat.defaultFormat;
    const Resolution         &res = caps.resolution.defaultResolution;

    const auto backend = VideoInputFactory::backendType(m_videoInput);
    pinpoint::FormatDescriptor fd;
    fd.device = (backend == VideoInputFactory::Backend::Aravis ||
                 backend == VideoInputFactory::Backend::Spinnaker)
                ? pinpoint::DeviceKind::Camera_GenICam
                : (backend == VideoInputFactory::Backend::AppleAVFoundation
                   ? pinpoint::DeviceKind::Camera_AVFoundation
                   : pinpoint::DeviceKind::Camera_UVC);

    pinpoint::CameraFormat cfmt{};
    cfmt.pixel_format = bufferPixelFormat(pf.encoding);
    cfmt.width        = static_cast<uint32_t>(res.width  > 0 ? res.width  : 1920);
    cfmt.height       = static_cast<uint32_t>(res.height > 0 ? res.height : 1080);
    const int bpp     = pf.bitsPerPixel > 0 ? pf.bitsPerPixel : 16;
    cfmt.max_payload_bytes     = cfmt.width * cfmt.height * static_cast<uint32_t>(bpp) / 8;
    cfmt.typical_payload_bytes = cfmt.max_payload_bytes;
    const double fps           = caps.frameRate.range.defaultValue > 0.0
                                 ? caps.frameRate.range.defaultValue : 30.0;
    cfmt.fps_numerator   = static_cast<uint32_t>(fps * 1000.0);
    cfmt.fps_denominator = 1000;
    fd.format = cfmt;

    ppInfo() << "[VideoController] camera active:" << m_deviceDescription
             << cfmt.width << "x" << cfmt.height << "@" << fps << "fps"
             << "fmt:" << pf.nativeKey;

    m_eventBuffer->updateSourceFormat(m_sourceId, fd);
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

    // Sum all planes — multi-plane formats (NV12, YUV420P) have separate luma
    // and chroma planes; storing only bits(0) loses the chroma plane and causes
    // green frames on replay.
    size_t totalBytes = 0;
    for (int p = 0; p < mutable_frame.planeCount(); ++p)
        totalBytes += static_cast<size_t>(mutable_frame.mappedBytes(p));

    auto slot = m_eventBuffer->acquireWriteSlot(m_sourceId);
    if (slot.valid && totalBytes > 0 && totalBytes <= slot.capacity) {
        auto *dst = reinterpret_cast<uint8_t *>(slot.data);
        size_t offset = 0;
        for (int p = 0; p < mutable_frame.planeCount(); ++p) {
            const size_t planeBytes = static_cast<size_t>(mutable_frame.mappedBytes(p));
            std::memcpy(dst + offset, mutable_frame.bits(p), planeBytes);
            offset += planeBytes;
        }
        *slot.bytes_written = static_cast<uint32_t>(totalBytes);
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
