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
    bool              isActive()      const override;
    QVideoFrameFormat frameFormat()   const override;
    bool              emitsRawBayer() const override { return m_emitRaw; }

private:
    void captureLoop();

    void *m_system    = nullptr; // Spinnaker::SystemPtr*
    void *m_camera    = nullptr; // Spinnaker::CameraPtr*
    bool  m_streaming = false;
    bool  m_abort     = false;
    int   m_bayerPattern = 0;   // RawVideoFrame::BayerPattern int, valid when Bayer format selected
    bool  m_emitRaw      = false; // true when camera runs a Bayer pixel format
};
