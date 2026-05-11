#include "video_controller.h"

#include "video_input_base.h"
#include "video_input_factory.h"
#include "video_preprocessor_base.h"
#include "video_overlay_base.h"

#ifdef HAVE_OPENCV
#include "video_preprocessor_opencv.h"
#include "video_overlay_pose.h"
#include "pose_estimator_base.h"
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
    connect(m_preprocessor, &VideoPreprocessorBase::cameraFpsUpdated,
            this, &VideoController::onCameraFps, Qt::QueuedConnection);
    m_preprocessThread->start();

    // ── Overlay thread ───────────────────────────────────────────────────────
    m_overlayThread = new QThread(this);
    m_overlayThread->setObjectName(QStringLiteral("VideoOverlayThread"));
    m_overlay = new VideoOverlayPose();
    m_overlay->moveToThread(m_overlayThread);
    connect(m_overlay, &VideoOverlayBase::frameReady,
            this, &VideoController::onAnnotatedFrame, Qt::QueuedConnection);
    m_overlayThread->start();

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

    // Throttle: preprocessor → throttle (direct, same thread) →
    //           pose estimator (queued, cross-thread).
    // clearBusy() fires via queued connection from the pose thread back to
    // the preprocessor thread, keeping m_busy single-threaded.
    m_frameThrottle = new FrameThrottle();
    m_frameThrottle->moveToThread(m_preprocessThread);

    auto *cvPP = qobject_cast<VideoPreprocessorOpenCV *>(m_preprocessor);
    connect(cvPP, &VideoPreprocessorOpenCV::framePreprocessed,
            m_frameThrottle, &FrameThrottle::offer,
            Qt::DirectConnection);
    connect(m_frameThrottle, &FrameThrottle::frameReady,
            m_poseEstimator, &PoseEstimatorBase::estimatePose,
            Qt::QueuedConnection);
    connect(m_poseEstimator, &PoseEstimatorBase::estimationDone,
            m_frameThrottle, &FrameThrottle::clearBusy,
            Qt::QueuedConnection);

    // Pose result → overlay (so skeleton is drawn on the live feed).
    auto *poseOverlay = static_cast<VideoOverlayPose *>(m_overlay);
    connect(m_poseEstimator, &PoseEstimatorBase::poseEstimated,
            poseOverlay, &VideoOverlayPose::updatePose,
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
            if (m_frameThrottle)
                m_frameThrottle->moveToThread(QCoreApplication::instance()->thread());
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

    // 4. Stop overlay — drain any queued overlayFrame calls first.
    if (m_overlay && m_overlayThread && m_overlayThread->isRunning()) {
        QMetaObject::invokeMethod(m_overlay, [this]() {
            m_overlay->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_overlayThread->quit();
        m_overlayThread->wait();
    }
    delete m_overlay;
    m_overlay = nullptr;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void VideoController::connectVideoInput()
{
    // Route video frames through the overlay when available; otherwise direct.
    if (m_overlay) {
        connect(m_videoInput, &VideoInputBase::videoFrameReady,
                m_overlay, &VideoOverlayBase::overlayFrame,
                Qt::QueuedConnection);
    } else {
        connect(m_videoInput, &VideoInputBase::videoFrameReady,
                this, &VideoController::onVideoFrame,
                Qt::QueuedConnection);
    }
    connect(m_videoInput, &VideoInputBase::errorOccurred,
            this, &VideoController::onVideoError);

#ifdef HAVE_OPENCV
    if (m_preprocessor) {
        connect(m_videoInput, &VideoInputBase::videoFrameReady,
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
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void VideoController::onVideoFrame(const QVideoFrame &frame)
{
    if (m_videoSink && frame.isValid())
        m_videoSink->setVideoFrame(frame);
}

void VideoController::onAnnotatedFrame(const QVideoFrame &frame)
{
    if (m_videoSink && frame.isValid())
        m_videoSink->setVideoFrame(frame);
}

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

void VideoController::onCameraFps(double fps)
{
    if (qFuzzyCompare(m_cameraFps, fps))
        return;
    m_cameraFps = fps;
    emit cameraFpsChanged();
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
