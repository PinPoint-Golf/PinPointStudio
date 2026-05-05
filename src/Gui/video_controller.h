#pragma once

#include <QObject>

class QThread;
class QVideoFrame;
class QVideoSink;
class VideoInputBase;

class VideoController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool isAravis READ isAravis NOTIFY isAravisChanged)

public:
    explicit VideoController(QObject *parent = nullptr);
    ~VideoController() override;

    bool isRecording() const;
    bool isAravis() const;

    // Called from QML: videoController.setVideoSink(videoOut.videoSink)
    Q_INVOKABLE void setVideoSink(QVideoSink *sink);

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void isRecordingChanged();
    void isAravisChanged();

private slots:
    void onVideoFrame(const QVideoFrame &frame);
    void onVideoError(const QString &message);

private:
    void startCapture();

    QThread        *m_captureThread = nullptr;
    VideoInputBase *m_videoInput    = nullptr;
    QVideoSink     *m_videoSink     = nullptr;
    bool            m_recording     = false;
};
