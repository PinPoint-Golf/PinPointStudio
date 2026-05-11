#include "video_controller.h"

#include "video_input_base.h"
#include "video_input_factory.h"
#include "video_preprocessor_base.h"
#ifdef HAVE_OPENCV
#include "video_preprocessor_opencv.h"
#include "frame_throttle.h"
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
#include "pose_estimator_movenet.h"
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
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

VideoController::VideoController(const Device &device, QObject *parent)
    : QObject(parent)
    , m_captureThread(new QThread(this))
    , m_deviceId(device.id)
    , m_deviceDescription(device.description)
    , m_videoInput(VideoInputFactory::create(device.backend))
{
    setupPipeline();
}

void VideoController::setupPipeline()
{
    // Set to false to disable the entire preprocessor/pose/throttle pipeline
    // for capture-rate diagnostics.  Flip back to true to restore.
    static constexpr bool kPoseEnabled = true;

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
        m_frameThrottle->setSkipFactor(10);

        auto *cvPP = qobject_cast<VideoPreprocessorOpenCV *>(m_preprocessor);
        connect(cvPP, &VideoPreprocessorOpenCV::framePreprocessed,
                m_poseEstimator, &PoseEstimatorBase::estimatePose, Qt::QueuedConnection);
        connect(m_poseEstimator, &PoseEstimatorBase::estimationDone,
                m_frameThrottle, &FrameThrottle::clearBusy, Qt::DirectConnection);
        connect(m_poseEstimator, &PoseEstimatorBase::poseEstimated,
                this, &VideoController::onPoseEstimated, Qt::QueuedConnection);
        m_poseThread->start();
#endif // HAVE_MOVENET && HAVE_ONNXRUNTIME
    } // kPoseEnabled
#endif // HAVE_OPENCV

    connectVideoInput();
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
#endif
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void VideoController::connectVideoInput()
{
    // Gate the display path: the capture thread runs up to 100 fps per camera
    // but the main thread can only drain at screen refresh rate (~60 Hz).
    // A plain QueuedConnection would accumulate hundreds of QVideoFrame copies
    // in the main-thread event queue, causing unbounded memory growth.
    // Instead, use DirectConnection on the capture thread to store only the
    // freshest frame and post at most one drain event at a time.
    connect(m_videoInput, &VideoInputBase::videoFrameReady,
            this, [this](const QVideoFrame &frame) {
                // Capture-thread: count every frame for the fps stat.
                m_frameCaptureCount.fetch_add(1, std::memory_order_relaxed);
                {
                    QMutexLocker lk(&m_latestFrameMutex);
                    m_latestDisplayFrame = frame;
                }
                if (!m_displayFramePending.exchange(true, std::memory_order_acq_rel)) {
                    QMetaObject::invokeMethod(this, &VideoController::drainDisplayFrame,
                                             Qt::QueuedConnection);
                }
            }, Qt::DirectConnection);
    connect(m_videoInput, &VideoInputBase::errorOccurred,
            this, &VideoController::onVideoError);

#ifdef HAVE_OPENCV
    if (m_frameThrottle) {
        connect(m_videoInput, &VideoInputBase::videoFrameReady,
                m_frameThrottle, &FrameThrottle::offer,
                Qt::DirectConnection);
        connect(m_frameThrottle, &FrameThrottle::frameReady,
                m_preprocessor, &VideoPreprocessorBase::processFrame,
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
bool   VideoController::needsDebayer()   const { return isAravis() || isSpinnaker(); }
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

VideoPreprocessorBase *VideoController::preprocessor() const { return m_preprocessor; }

void VideoController::setVideoSink(QVideoSink *sink)
{
    m_videoSink = sink;
}

void VideoController::selectMoveNetModel(int variant)
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    if (variant == m_moveNetModel)
        return;
    m_moveNetModel = variant;
    emit moveNetModelChanged();
    m_poseAvgMs = 0.0; emit poseAvgMsChanged();
    m_poseFps   = 0.0; emit poseFpsChanged();
    m_poseBackendLabel = QString(); emit poseBackendLabelChanged();
    if (m_poseEstimator) {
        QMetaObject::invokeMethod(m_poseEstimator, "reloadModel",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, variant));
    }
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
                qWarning() << "[VideoController] Camera permission denied."
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
            qWarning() << "[VideoController] Failed to start primary video input. Attempting fallback...";
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
            qWarning() << "[VideoController] Failed to start camera:" << m_deviceDescription;
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
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void VideoController::drainDisplayFrame()
{
    QVideoFrame f;
    {
        QMutexLocker lk(&m_latestFrameMutex);
        f = m_latestDisplayFrame;
        m_latestDisplayFrame = QVideoFrame();
    }
    m_displayFramePending.store(false, std::memory_order_release);

    // Spinnaker emits BGR888 to avoid a full-image R/B swap on the capture thread
    // for every frame.  Convert to RGB888 here, at display rate (~30 fps), not at
    // capture rate (up to 100 fps per camera).
    if (f.isValid()) {
        const QImage img = f.toImage();
        if (img.format() == QImage::Format_BGR888)
            f = QVideoFrame(img.convertToFormat(QImage::Format_RGB888));
    }

    onVideoFrame(f);
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
    qWarning() << "[Video]" << message;
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
