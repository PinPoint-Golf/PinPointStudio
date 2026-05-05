#pragma once

#include "video_input_base.h"

// Aravis-based backend for industrial cameras (GigE Vision / USB3 Vision).
//
// Uses the Aravis 0.8 C API to capture frames. Since industrial cameras often
// output raw Bayer data, this backend typically emits frames in Format_Grayscale8
// (representing the raw Bayer mosaic) and relies on a GPU shader for debayering.

class VideoInputAravis : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputAravis(QObject *parent = nullptr);
    ~VideoInputAravis() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()    const override;
    QVideoFrameFormat frameFormat() const override;

private:
    void captureLoop();

    void *m_camera    = nullptr; // ArvCamera*
    void *m_stream    = nullptr; // ArvStream*
    bool  m_streaming = false;
    bool  m_abort     = false;
};
