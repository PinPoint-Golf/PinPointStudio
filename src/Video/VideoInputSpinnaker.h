#pragma once

#include "video_input_base.h"

// Spinnaker-based backend for Teledyne/FLIR industrial cameras.
// Only supported on Windows.
//
// Uses the Spinnaker C++ API to capture frames.

class VideoInputSpinnaker : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputSpinnaker(QObject *parent = nullptr);
    ~VideoInputSpinnaker() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()    const override;
    QVideoFrameFormat frameFormat() const override;

private:
    void captureLoop();

    void *m_system    = nullptr; // Spinnaker::SystemPtr*
    void *m_camera    = nullptr; // Spinnaker::CameraPtr*
    bool  m_streaming = false;
    bool  m_abort     = false;
};
