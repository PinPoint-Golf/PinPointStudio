#pragma once

#include <QObject>

class QThread;
class QVideoFrame;
class QVideoSink;
class VideoInputBase;
class VideoPreprocessorBase;

#ifdef HAVE_OPENCV
class PoseEstimatorBase;
#endif

class VideoController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool isAravis READ isAravis NOTIFY isAravisChanged)
    Q_PROPERTY(bool isSpinnaker READ isSpinnaker NOTIFY isSpinnakerChanged)
    Q_PROPERTY(bool needsDebayer READ needsDebayer NOTIFY needsDebayerChanged)
    Q_PROPERTY(double preprocessAvgMs READ preprocessAvgMs NOTIFY preprocessAvgMsChanged)
    Q_PROPERTY(double poseAvgMs READ poseAvgMs NOTIFY poseAvgMsChanged)
    Q_PROPERTY(double poseFps   READ poseFps   NOTIFY poseFpsChanged)

public:
    explicit VideoController(QObject *parent = nullptr);
    ~VideoController() override;

    bool   isRecording() const;
    bool   isAravis() const;
    bool   isSpinnaker() const;
    bool   needsDebayer() const;
    double preprocessAvgMs() const;
    double poseAvgMs() const;
    double poseFps() const;

    // Called from QML: videoController.setVideoSink(videoOut.videoSink)
    Q_INVOKABLE void setVideoSink(QVideoSink *sink);

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

    // Returns the active preprocessor (nullptr if OpenCV unavailable).
    // Cast to VideoPreprocessorOpenCV* to connect its framePreprocessed() signal.
    VideoPreprocessorBase *preprocessor() const;

signals:
    void isRecordingChanged();
    void isAravisChanged();
    void isSpinnakerChanged();
    void needsDebayerChanged();
    void preprocessAvgMsChanged();
    void poseAvgMsChanged();
    void poseFpsChanged();

private slots:
    void onVideoFrame(const QVideoFrame &frame);
    void onVideoError(const QString &message);
    void onPreprocessStats(double avgMs);
    void onPoseStats(double avgMs, double fps);

private:
    void startCapture();

    QThread               *m_captureThread   = nullptr;
    VideoInputBase        *m_videoInput       = nullptr;
    QVideoSink            *m_videoSink        = nullptr;
    bool                   m_recording        = false;

    QThread               *m_preprocessThread = nullptr;
    VideoPreprocessorBase *m_preprocessor     = nullptr;
    double                 m_preprocessAvgMs  = 0.0;

#ifdef HAVE_OPENCV
    QThread           *m_poseThread    = nullptr;
    PoseEstimatorBase *m_poseEstimator = nullptr;
#endif
    double             m_poseAvgMs     = 0.0;
    double             m_poseFps       = 0.0;
};
