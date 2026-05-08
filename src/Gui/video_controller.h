#pragma once

#include <QObject>

class QThread;
class QVideoFrame;
class QVideoSink;
class VideoInputBase;
class VideoPreprocessorBase;

class VideoController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool isAravis READ isAravis NOTIFY isAravisChanged)
    Q_PROPERTY(bool isSpinnaker READ isSpinnaker NOTIFY isSpinnakerChanged)
    Q_PROPERTY(bool needsDebayer READ needsDebayer NOTIFY needsDebayerChanged)
    Q_PROPERTY(double preprocessAvgMs READ preprocessAvgMs NOTIFY preprocessAvgMsChanged)

public:
    explicit VideoController(QObject *parent = nullptr);
    ~VideoController() override;

    bool   isRecording() const;
    bool   isAravis() const;
    bool   isSpinnaker() const;
    bool   needsDebayer() const;
    double preprocessAvgMs() const;

    // Called from QML: videoController.setVideoSink(videoOut.videoSink)
    Q_INVOKABLE void setVideoSink(QVideoSink *sink);

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

    // Returns the active preprocessor (nullptr if OpenCV unavailable).
    // Cast to VideoPreprocessorOpenCV* to connect its framePreprocessed()
    // signal to the pose estimator once that class exists.
    VideoPreprocessorBase *preprocessor() const;

signals:
    void isRecordingChanged();
    void isAravisChanged();
    void isSpinnakerChanged();
    void needsDebayerChanged();
    void preprocessAvgMsChanged();

private slots:
    void onVideoFrame(const QVideoFrame &frame);
    void onVideoError(const QString &message);
    void onPreprocessStats(double avgMs);

private:
    void startCapture();

    QThread               *m_captureThread    = nullptr;
    VideoInputBase        *m_videoInput        = nullptr;
    QVideoSink            *m_videoSink         = nullptr;
    bool                   m_recording         = false;

    QThread               *m_preprocessThread  = nullptr;
    VideoPreprocessorBase *m_preprocessor      = nullptr;
    double                 m_preprocessAvgMs   = 0.0;
};
