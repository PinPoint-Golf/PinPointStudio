#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QVideoFrame>
#include <array>
#include <atomic>

#include "device_enumerator.h"

class QThread;
class QVideoFrame;
class QVideoSink;
class VideoInputBase;
class VideoPreprocessorBase;

#ifdef HAVE_OPENCV
#include "pose_estimator_base.h"
class FrameThrottle;
#endif

class VideoController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool isAravis READ isAravis NOTIFY isAravisChanged)
    Q_PROPERTY(bool isSpinnaker READ isSpinnaker NOTIFY isSpinnakerChanged)
    Q_PROPERTY(bool needsDebayer READ needsDebayer NOTIFY needsDebayerChanged)
    Q_PROPERTY(double preprocessAvgMs READ preprocessAvgMs NOTIFY preprocessAvgMsChanged)
    Q_PROPERTY(double cameraFps READ cameraFps NOTIFY cameraFpsChanged)
    Q_PROPERTY(double poseAvgMs READ poseAvgMs NOTIFY poseAvgMsChanged)
    Q_PROPERTY(double poseFps   READ poseFps   NOTIFY poseFpsChanged)
    Q_PROPERTY(QString poseBackendLabel READ poseBackendLabel NOTIFY poseBackendLabelChanged)
    Q_PROPERTY(int moveNetModel READ moveNetModel NOTIFY moveNetModelChanged)
    Q_PROPERTY(bool moveNetThunderAvailable READ moveNetThunderAvailable CONSTANT)
    Q_PROPERTY(QVariantList poseKeypoints READ poseKeypoints NOTIFY poseKeypointsChanged)
    Q_PROPERTY(QString deviceDescription READ deviceDescription CONSTANT)

public:
    explicit VideoController(QObject *parent = nullptr);
    explicit VideoController(const Device &device, QObject *parent = nullptr);
    ~VideoController() override;

    bool   isRecording() const;
    bool   isAravis() const;
    bool   isSpinnaker() const;
    bool   needsDebayer() const;
    double  preprocessAvgMs() const;
    double  cameraFps() const;
    double  poseAvgMs() const;
    double  poseFps() const;
    QString poseBackendLabel() const;
    int     moveNetModel() const;
    bool    moveNetThunderAvailable() const;
    QVariantList poseKeypoints() const;
    QString deviceDescription() const;

    Q_INVOKABLE void setVideoSink(QVideoSink *sink);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void selectMoveNetModel(int variant);

    VideoPreprocessorBase *preprocessor() const;

signals:
    void isRecordingChanged();
    void isAravisChanged();
    void isSpinnakerChanged();
    void needsDebayerChanged();
    void preprocessAvgMsChanged();
    void cameraFpsChanged();
    void poseAvgMsChanged();
    void poseFpsChanged();
    void poseBackendLabelChanged();
    void moveNetModelChanged();
    void poseKeypointsChanged();

private slots:
    void onVideoFrame(const QVideoFrame &frame);
    void drainDisplayFrame();
    void onVideoError(const QString &message);
    void onPreprocessStats(double avgMs);
    void onPoseStats(double avgMs, double fps);
    void onPoseBackendReady(const QString &label);
#ifdef HAVE_OPENCV
    void onPoseEstimated(const PoseResult &result);
#endif

private:
    void setupPipeline();
    void connectVideoInput();

    QThread               *m_captureThread   = nullptr;
    VideoInputBase        *m_videoInput       = nullptr;
    QVideoSink            *m_videoSink        = nullptr;
    bool                   m_recording        = false;
    QString                m_deviceId;
    QString                m_deviceDescription;

    // Display-path throttle: at most one QVideoFrame is ever queued to the
    // main thread at a time, preventing unbounded queue growth at high frame rates.
    std::atomic<bool>      m_displayFramePending{false};
    QMutex                 m_latestFrameMutex;
    QVideoFrame            m_latestDisplayFrame;

    QThread               *m_preprocessThread = nullptr;
    VideoPreprocessorBase *m_preprocessor     = nullptr;
    double                 m_preprocessAvgMs  = 0.0;

#ifdef HAVE_OPENCV
    QThread           *m_poseThread    = nullptr;
    PoseEstimatorBase *m_poseEstimator = nullptr;
    FrameThrottle     *m_frameThrottle = nullptr;
#endif
    static constexpr int kCamFpsWindow = 30;
    QElapsedTimer                        m_camFpsTimer;
    std::array<double, kCamFpsWindow>    m_camFpsIntervals{};
    double                               m_camFpsSum   = 0.0;
    int                                  m_camFpsIndex = 0;
    int                                  m_camFpsCount = 0;
    double             m_cameraFps          = 0.0;
    double             m_poseAvgMs          = 0.0;
    double             m_poseFps            = 0.0;
    QString            m_poseBackendLabel;
    int                m_moveNetModel       = 0;
    QVariantList       m_poseKeypoints;
};
