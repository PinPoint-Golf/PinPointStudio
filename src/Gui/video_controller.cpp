#include "video_controller.h"

#include "video_input_base.h"
#include "video_input_factory.h"
#include "video_preprocessor_base.h"

#ifdef HAVE_OPENCV
#include "video_preprocessor_opencv.h"
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
    m_preprocessThread = new QThread(this);
    m_preprocessThread->setObjectName(QStringLiteral("VideoPreprocessThread"));
    m_preprocessor = new VideoPreprocessorOpenCV();
    m_preprocessor->moveToThread(m_preprocessThread);

    // Frames branch: capture thread → preprocess thread (parallel to display path).
    connect(m_videoInput, &VideoInputBase::videoFrameReady,
            m_preprocessor, &VideoPreprocessorBase::processFrame,
            Qt::QueuedConnection);

    // Stats: preprocess thread → main thread.
    connect(m_preprocessor, &VideoPreprocessorBase::preprocessStatsUpdated,
            this, &VideoController::onPreprocessStats,
            Qt::QueuedConnection);

    m_preprocessThread->start();
    // TODO: connect VideoPreprocessorOpenCV::framePreprocessed to pose estimator here.
#endif

    startCapture();
}

VideoController::~VideoController()
{
    // Stop capture first so no further videoFrameReady signals are emitted.
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

    // Drain any frames already queued in the preprocess thread, then stop it.
    if (m_preprocessor && m_preprocessThread && m_preprocessThread->isRunning()) {
        QMetaObject::invokeMethod(m_preprocessor, [this]() {
            m_preprocessor->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        m_preprocessThread->quit();
        m_preprocessThread->wait();
    }
    delete m_preprocessor;
    m_preprocessor = nullptr;
}

bool VideoController::isRecording() const
{
    return m_recording;
}

bool VideoController::isAravis() const
{
    return VideoInputFactory::backendType(m_videoInput) == VideoInputFactory::Backend::Aravis;
}

bool VideoController::isSpinnaker() const
{
    return VideoInputFactory::backendType(m_videoInput) == VideoInputFactory::Backend::Spinnaker;
}

bool VideoController::needsDebayer() const
{
    return isAravis() || isSpinnaker();
}

VideoPreprocessorBase *VideoController::preprocessor() const
{
    return m_preprocessor;
}

double VideoController::preprocessAvgMs() const
{
    return m_preprocessAvgMs;
}

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
            // If an industrial camera backend failed, try falling back to standard VideoInput (Qt Multimedia)
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

void VideoController::startCapture()
{
    m_captureThread->start();
}
