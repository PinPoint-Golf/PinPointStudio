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
#include <QVideoFrame>
#include <QVideoSink>

#ifdef Q_OS_MACOS
#include "macos_permissions.h"
#endif

VideoController::VideoController(QObject *parent)
    : QObject(parent)
    , m_captureThread(new QThread(this))
    , m_videoInput(VideoInputFactory::create(VideoInputFactory::Backend::Auto))
{
    m_captureThread->setObjectName(QStringLiteral("VideoCaptureThread"));
    m_videoInput->moveToThread(m_captureThread);

#ifdef HAVE_OPENCV
    // ── Preprocess thread ────────────────────────────────────────────────────
    m_preprocessThread = new QThread(this);
    m_preprocessThread->setObjectName(QStringLiteral("VideoPreprocessThread"));
    m_preprocessor = new VideoPreprocessorOpenCV();
    m_preprocessor->moveToThread(m_preprocessThread);
    connect(m_preprocessor, &VideoPreprocessorBase::preprocessStatsUpdated,
            this, &VideoController::onPreprocessStats, Qt::QueuedConnection);
    m_preprocessThread->start();

#if defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    // ── Pose estimator thread ────────────────────────────────────────────────
    m_poseThread    = new QThread(this);
    m_poseThread->setObjectName(QStringLiteral("PoseEstimatorThread"));
    auto *mn = new PoseEstimatorMoveNet();
    m_poseEstimator = mn;
    m_poseEstimator->moveToThread(m_poseThread);
    connect(m_poseThread, &QThread::started, mn, &PoseEstimatorMoveNet::load);
    connect(m_poseEstimator, &PoseEstimatorBase::poseStatsUpdated,
            this, &VideoController::onPoseStats, Qt::QueuedConnection);
    connect(m_poseEstimator, &PoseEstimatorBase::poseBackendReady,
            this, &VideoController::onPoseBackendReady, Qt::QueuedConnection);

    // Throttle sits before the preprocessor: the capture thread offers raw
    // QVideoFrames; only the accepted frame reaches processFrame, so the
    // preprocessor queue never builds a stale-frame backlog.
    // clearBusy fires immediately on the pose thread (DirectConnection) and
    // re-emits the latest frame captured during the previous inference cycle.
    m_frameThrottle = new FrameThrottle();

    auto *cvPP = qobject_cast<VideoPreprocessorOpenCV *>(m_preprocessor);
    connect(cvPP, &VideoPreprocessorOpenCV::framePreprocessed,
            m_poseEstimator, &PoseEstimatorBase::estimatePose,
            Qt::QueuedConnection);
    connect(m_poseEstimator, &PoseEstimatorBase::estimationDone,
            m_frameThrottle, &FrameThrottle::clearBusy,
            Qt::DirectConnection);

    // Pose result → keypoints property (drawn by QML Canvas, independent of frame display).
    connect(m_poseEstimator, &PoseEstimatorBase::poseEstimated,
            this, &VideoController::onPoseEstimated,
            Qt::QueuedConnection);

    m_poseThread->start();
#endif // HAVE_MOVENET && HAVE_ONNXRUNTIME
#endif // HAVE_OPENCV

    connectVideoInput();
    startCapture();
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
    connect(m_videoInput, &VideoInputBase::videoFrameReady,
            this, &VideoController::onVideoFrame,
            Qt::QueuedConnection);
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
bool    VideoController::moveNetThunderAvailable() const
{
#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    return PoseEstimatorMoveNet::isVariantAvailable(PoseEstimatorMoveNet::ModelVariant::Thunder);
#else
    return false;
#endif
}

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
    // Reset stats so stale numbers from the old model don't persist.
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
                self->m_videoInput->start();
            }, Qt::QueuedConnection);
            self->m_recording = true;
            emit self->isRecordingChanged();
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(self);
#else
    QMetaObject::invokeMethod(m_videoInput, [this]() {
        if (m_videoInput->start()) {
            QMetaObject::invokeMethod(this, [this]() {
                m_recording = true;
                emit isRecordingChanged();
            }, Qt::QueuedConnection);
        } else {
            qWarning() << "[VideoController] Failed to start primary video input. Attempting fallback...";
            VideoInputFactory::Backend currentBackend = VideoInputFactory::backendType(m_videoInput);
            if (currentBackend == VideoInputFactory::Backend::Aravis ||
                currentBackend == VideoInputFactory::Backend::Spinnaker)
            {
                QMetaObject::invokeMethod(this, [this]() {
                    delete m_videoInput;
                    m_videoInput = VideoInputFactory::create(VideoInputFactory::Backend::QtMultimedia);
                    m_videoInput->moveToThread(m_captureThread);
                    connectVideoInput();     // re-wires all connections for the new input

                    emit isAravisChanged();
                    emit isSpinnakerChanged();
                    emit needsDebayerChanged();

                    QMetaObject::invokeMethod(m_videoInput, [this]() {
                        if (m_videoInput->start()) {
                            QMetaObject::invokeMethod(this, [this]() {
                                m_recording = true;
                                emit isRecordingChanged();
                            }, Qt::QueuedConnection);
                        }
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            }
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

QVariantList VideoController::poseKeypoints() const { return m_poseKeypoints; }

void VideoController::onVideoFrame(const QVideoFrame &frame)
{
    if (m_videoSink && frame.isValid())
        m_videoSink->setVideoFrame(frame);

    if (m_camFpsTimer.isValid()) {
        const double ms = m_camFpsTimer.nsecsElapsed() / 1e6;
        m_camFpsSum -= m_camFpsIntervals[m_camFpsIndex];
        m_camFpsIntervals[m_camFpsIndex] = ms;
        m_camFpsSum += ms;
        m_camFpsIndex = (m_camFpsIndex + 1) % kCamFpsWindow;
        if (m_camFpsCount < kCamFpsWindow)
            ++m_camFpsCount;
        if (m_camFpsCount == kCamFpsWindow) {
            const double avg = m_camFpsSum / kCamFpsWindow;
            if (avg > 0.0) {
                const double fps = 1000.0 / avg;
                if (!qFuzzyCompare(m_cameraFps, fps)) {
                    m_cameraFps = fps;
                    emit cameraFpsChanged();
                }
            }
        }
    }
    m_camFpsTimer.restart();
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

void VideoController::startCapture()
{
    m_captureThread->start();
}
