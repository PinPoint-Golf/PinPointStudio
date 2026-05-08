#include "video_controller.h"

#include "video_input_base.h"
#include "video_input_factory.h"
#include "video_preprocessor_base.h"

#ifdef HAVE_OPENCV
#include "video_preprocessor_opencv.h"
#include "pose_estimator_base.h"
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

    connect(m_videoInput, &VideoInputBase::videoFrameReady,
            this, &VideoController::onVideoFrame,
            Qt::QueuedConnection);
    connect(m_videoInput, &VideoInputBase::errorOccurred,
            this, &VideoController::onVideoError);

#ifdef HAVE_OPENCV
    // ── Preprocess thread ────────────────────────────────────────────────────
    m_preprocessThread = new QThread(this);
    m_preprocessThread->setObjectName(QStringLiteral("VideoPreprocessThread"));
    m_preprocessor = new VideoPreprocessorOpenCV();
    m_preprocessor->moveToThread(m_preprocessThread);

    connect(m_videoInput, &VideoInputBase::videoFrameReady,
            m_preprocessor, &VideoPreprocessorBase::processFrame,
            Qt::QueuedConnection);
    connect(m_preprocessor, &VideoPreprocessorBase::preprocessStatsUpdated,
            this, &VideoController::onPreprocessStats,
            Qt::QueuedConnection);

    m_preprocessThread->start();

#if defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)
    // ── Pose estimator thread ────────────────────────────────────────────────
    m_poseThread    = new QThread(this);
    m_poseThread->setObjectName(QStringLiteral("PoseEstimatorThread"));
    auto *mn = new PoseEstimatorMoveNet();
    m_poseEstimator = mn;
    m_poseEstimator->moveToThread(m_poseThread);

    // Load the model on the pose thread as soon as it starts.
    // Use the concrete pointer — Qt's template deduction needs the exact type.
    connect(m_poseThread, &QThread::started, mn, &PoseEstimatorMoveNet::load);

    // preprocessor → pose estimator (preprocess thread → pose thread)
    auto *cvPP = qobject_cast<VideoPreprocessorOpenCV *>(m_preprocessor);
    connect(cvPP, &VideoPreprocessorOpenCV::framePreprocessed,
            m_poseEstimator, &PoseEstimatorBase::estimatePose,
            Qt::QueuedConnection);

    // Stats: pose thread → main thread
    connect(m_poseEstimator, &PoseEstimatorBase::poseStatsUpdated,
            this, &VideoController::onPoseStats,
            Qt::QueuedConnection);

    m_poseThread->start();
#endif // HAVE_MOVENET && HAVE_ONNXRUNTIME
#endif // HAVE_OPENCV

    startCapture();
}

VideoController::~VideoController()
{
    // 1. Stop capture — no further videoFrameReady signals after this.
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

    // 2. Stop preprocess — no further framePreprocessed signals after this.
    if (m_preprocessor && m_preprocessThread && m_preprocessThread->isRunning()) {
        QMetaObject::invokeMethod(m_preprocessor, [this]() {
            m_preprocessor->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_preprocessThread->quit();
        m_preprocessThread->wait();
    }
    delete m_preprocessor;
    m_preprocessor = nullptr;

#ifdef HAVE_OPENCV
    // 3. Stop pose estimator — drain any queued estimatePose calls first.
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

bool VideoController::isRecording() const  { return m_recording; }
bool VideoController::isAravis() const
{
    return VideoInputFactory::backendType(m_videoInput) == VideoInputFactory::Backend::Aravis;
}
bool VideoController::isSpinnaker() const
{
    return VideoInputFactory::backendType(m_videoInput) == VideoInputFactory::Backend::Spinnaker;
}
bool VideoController::needsDebayer() const { return isAravis() || isSpinnaker(); }

VideoPreprocessorBase *VideoController::preprocessor() const { return m_preprocessor; }

double VideoController::preprocessAvgMs() const { return m_preprocessAvgMs; }
double VideoController::poseAvgMs() const        { return m_poseAvgMs; }
double VideoController::poseFps() const          { return m_poseFps; }

void VideoController::setVideoSink(QVideoSink *sink)
{
    m_videoSink = sink;
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
                    connect(m_videoInput, &VideoInputBase::videoFrameReady,
                            this, &VideoController::onVideoFrame, Qt::QueuedConnection);
                    connect(m_videoInput, &VideoInputBase::errorOccurred,
                            this, &VideoController::onVideoError);
#ifdef HAVE_OPENCV
                    if (m_preprocessor) {
                        connect(m_videoInput, &VideoInputBase::videoFrameReady,
                                m_preprocessor, &VideoPreprocessorBase::processFrame,
                                Qt::QueuedConnection);
                    }
#endif
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

void VideoController::onVideoFrame(const QVideoFrame &frame)
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

void VideoController::onPoseStats(double avgMs, double fps)
{
    bool changed = false;
    if (!qFuzzyCompare(m_poseAvgMs, avgMs)) { m_poseAvgMs = avgMs; changed = true; emit poseAvgMsChanged(); }
    if (!qFuzzyCompare(m_poseFps,   fps))   { m_poseFps   = fps;   changed = true; emit poseFpsChanged();   }
    Q_UNUSED(changed);
}

void VideoController::startCapture()
{
    m_captureThread->start();
}
