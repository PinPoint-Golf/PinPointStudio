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

#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVideoFrame>
#include <array>
#include <atomic>
#include <deque>

#include "device_enumerator.h"
#include "raw_video_frame.h"
#include "ting_player.h"
#include "types.h"

namespace pinpoint { class EventBuffer; }

class AppSettings;
class QThread;
class QVideoFrame;
class QVideoSink;
class VideoInputBase;
class VideoPreprocessorBase;
class BayerVideoItem;

#ifdef HAVE_OPENCV
#include "pose_estimator_base.h"
#include "ball_detector.h"
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
    // moveNetModel is the active pose-model selector: 0=Lightning, 1=Thunder.
    Q_PROPERTY(int moveNetModel READ moveNetModel NOTIFY moveNetModelChanged)
    Q_PROPERTY(bool moveNetThunderAvailable READ moveNetThunderAvailable CONSTANT)
    Q_PROPERTY(QVariantList poseKeypoints READ poseKeypoints NOTIFY poseKeypointsChanged)
    Q_PROPERTY(QString deviceDescription  READ deviceDescription  CONSTANT)
    Q_PROPERTY(QString deviceSerialNumber READ deviceSerialNumber CONSTANT)
    Q_PROPERTY(QString deviceAlias        READ deviceAlias        NOTIFY deviceAliasChanged)
    Q_PROPERTY(int  perspective READ perspective NOTIFY perspectiveChanged)
    Q_PROPERTY(bool isMirrored  READ isMirrored  NOTIFY isMirroredChanged)
    Q_PROPERTY(QRectF roi     READ roi     NOTIFY roiChanged)
    Q_PROPERTY(QRectF cropRoi READ cropRoi NOTIFY cropRoiChanged)
    Q_PROPERTY(bool   ballDetected       READ ballDetected       NOTIFY ballDetectedChanged)
    Q_PROPERTY(double ballX              READ ballX              NOTIFY ballDetectedChanged)
    Q_PROPERTY(double ballY              READ ballY              NOTIFY ballDetectedChanged)
    Q_PROPERTY(double ballRadius         READ ballRadius         NOTIFY ballDetectedChanged)
    Q_PROPERTY(double ballPresencePercent READ ballPresencePercent NOTIFY ballPresencePercentChanged)
    Q_PROPERTY(bool   ballPresent        READ ballPresent        NOTIFY ballPresentChanged)
    Q_PROPERTY(double ballAvgMs          READ ballAvgMs          NOTIFY ballAvgMsChanged)
    Q_PROPERTY(double ballHoughConf    READ ballHoughConf    NOTIFY ballHoughConfChanged)
    Q_PROPERTY(int    ballWhiteSatCeil READ ballWhiteSatCeil NOTIFY ballWhiteSatCeilChanged)
    Q_PROPERTY(int    frameWidth         READ frameWidth          NOTIFY frameSizeChanged)
    Q_PROPERTY(int    frameHeight        READ frameHeight         NOTIFY frameSizeChanged)
    Q_PROPERTY(double configuredFps     READ configuredFps       NOTIFY frameSizeChanged)
    Q_PROPERTY(bool   isReplaying       READ isReplaying         NOTIFY isReplayingChanged)

public:
    // Perspective values — matches the selector in CameraView.qml.
    enum Perspective { None = 0, DownTheLine = 1, FaceOn = 2, Other = 3 };

    explicit VideoController(QObject *parent = nullptr);
    explicit VideoController(const Device &device,
                             pinpoint::EventBuffer *buffer = nullptr,
                             AppSettings *appSettings = nullptr,
                             QObject *parent = nullptr);
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
    QString deviceDescription()   const;
    QString deviceSerialNumber()  const;
    QString deviceAlias()         const;
    void    setDeviceAlias(const QString &alias);
    int     perspective() const;
    bool    isMirrored()  const;
    QRectF  roi()          const;
    QRectF  cropRoi()      const;
    bool    ballDetected()        const;
    double  ballX()               const;
    double  ballY()               const;
    double  ballRadius()          const;
    double  ballPresencePercent() const;
    bool    ballPresent()         const;
    double  ballAvgMs()           const;
    double  ballHoughConf()       const;
    int     ballWhiteSatCeil()    const;
    int     frameWidth()          const;
    int     frameHeight()         const;
    double  configuredFps()       const;
    bool    isReplaying()         const;
    pinpoint::SourceId sourceId() const;

    // Called by CameraManager only — not Q_INVOKABLE so QML cannot bypass.
    void stopCapture();       // Synchronously stops the capture thread; call before deregisterFromBuffer()
    void setPerspective(int p);
    void setIsMirrored(bool mirrored);
    void deregisterFromBuffer();
    void setReplaying(bool replaying);
    void displayReplayFrame(const std::byte *data, size_t bytes, int w, int h, pinpoint::PixelFormat fmt);

    Q_INVOKABLE void setVideoSink(QVideoSink *sink);
    Q_INVOKABLE void setSettingsSink(QVideoSink *sink);
    Q_INVOKABLE void setBayerItem(QObject *item);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void startPreview();  // start camera without ring-buffer capture (settings preview)
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void selectMoveNetModel(int variant);
    Q_INVOKABLE void setRoi(QRectF roi);    // hitting area for ball detection
    Q_INVOKABLE void clearRoi();
    Q_INVOKABLE void setBallHoughConf(double v);
    Q_INVOKABLE void setBallWhiteSatCeil(int v);
    Q_INVOKABLE void setCropRoi(QRectF roi); // frame crop for storage / ring-buffer sizing
    Q_INVOKABLE void clearCropRoi();

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
    void perspectiveChanged();
    void isMirroredChanged();
    void roiChanged();
    void cropRoiChanged();
    void ballDetectedChanged();
    void ballPresencePercentChanged();
    void ballPresentChanged(bool present);
    void ballAvgMsChanged();
    void ballHoughConfChanged();
    void ballWhiteSatCeilChanged();
    void frameSizeChanged();
    void isReplayingChanged();
    void deviceAliasChanged();

private slots:
    void onVideoFrame(const QVideoFrame &frame);
    void drainDisplayFrame();
    void drainRawFrame();
    void onVideoError(const QString &message);
    void onPreprocessStats(double avgMs);
    void onPoseStats(double avgMs, double fps);
    void onPoseBackendReady(const QString &label);
#ifdef HAVE_OPENCV
    void onPoseEstimated(const PoseResult &result);
    void onBallDetected(const BallDetection &result);
#endif

private:
    void setupPipeline();
    void connectVideoInput();
    void updateBufferDescriptor();
    void publishFrameToBuffer(const QVideoFrame &frame);
    void publishRawFrameToBuffer(const RawVideoFrame &frame);

    pinpoint::EventBuffer *m_eventBuffer      = nullptr;
    pinpoint::SourceId     m_sourceId         = pinpoint::kInvalidSourceId;

    QThread               *m_captureThread   = nullptr;
    VideoInputBase        *m_videoInput       = nullptr;
    QVideoSink            *m_videoSink         = nullptr;
    QVideoSink            *m_settingsSink      = nullptr;
    BayerVideoItem        *m_bayerItem        = nullptr;
    bool                   m_recording        = false;
    bool                   m_previewing       = false;
    std::atomic<bool>      m_previewOnly{false}; // true = camera running but pipeline suppressed
    QString                m_deviceId;
    QString                m_deviceDescription;
    QString                m_deviceSerialNumber;
    QString                m_deviceAlias;

    // Display-path throttle: at most one frame is ever queued to the main thread
    // at a time (shared between the QVideoFrame and RawVideoFrame paths, which are
    // mutually exclusive per camera type).
    std::atomic<bool>      m_displayFramePending{false};

    // Latched after the first time publishFrameToBuffer() drops a frame that won't
    // fit the ring slot, so the error is logged once rather than every frame (which
    // would flood the bounded log at the capture rate and hide everything else).
    std::atomic<bool>      m_publishDropLogged{false};

    // Set once the source descriptor's pixel format + resolution have been stamped
    // from the first real delivered frame (main-thread only — see drainDisplayFrame).
    bool                   m_bufferDescriptorStamped = false;

    QMutex                 m_latestFrameMutex;
    QVideoFrame            m_latestDisplayFrame;

    QMutex                 m_latestRawFrameMutex;
    RawVideoFrame          m_latestRawFrame;

    QThread               *m_preprocessThread = nullptr;
    VideoPreprocessorBase *m_preprocessor     = nullptr;
    double                 m_preprocessAvgMs  = 0.0;

#ifdef HAVE_OPENCV
    QThread           *m_poseThread    = nullptr;
    PoseEstimatorBase *m_poseEstimator = nullptr;
    FrameThrottle     *m_frameThrottle = nullptr;
    QThread           *m_ballThread    = nullptr;
    BallDetector      *m_ballDetector  = nullptr;
#endif
    bool   m_ballDetected = false;
    double m_ballX        = 0.0;
    double m_ballY        = 0.0;
    double m_ballRadius   = 0.0;

    static constexpr int kBallWindowSize      = 50;
    static constexpr double kBallPresentThreshold = 30.0;
    std::deque<bool> m_ballWindow;
    int              m_ballPresentCount     = 0;
    double           m_ballPresencePercent  = 0.0;
    bool             m_ballPresent          = false;
    double           m_ballAvgMs            = 0.0;
    double           m_ballHoughConf        = 0.7;
    int              m_ballWhiteSatCeil     = 50;
    TingPlayer      *m_tingPlayer           = nullptr;
    bool             m_replaying            = false;
    // Capture-rate FPS: counted on the capture thread, sampled on a timer.
    std::atomic<int>   m_frameCaptureCount{0};
    QTimer            *m_fpsSampleTimer    = nullptr;
    QElapsedTimer      m_fpsSampleElapsed;
    double             m_cameraFps         = 0.0;
    double             m_poseAvgMs          = 0.0;
    double             m_poseFps            = 0.0;
    QString            m_poseBackendLabel;
    int                m_moveNetModel       = 0;
    QVariantList       m_poseKeypoints;
    int                m_perspective        = 0;
    bool               m_isMirrored         = false;
    QRectF             m_roi;
    QRectF             m_cropRoi;
    int                m_frameWidth  = 0;
    int                m_frameHeight = 0;
    double             m_configuredFps = 0.0;
};
