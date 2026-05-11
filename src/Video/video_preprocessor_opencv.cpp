#ifdef HAVE_OPENCV

#include "video_preprocessor_opencv.h"

#include <QImage>
#include <opencv2/imgproc.hpp>

VideoPreprocessorOpenCV::VideoPreprocessorOpenCV(QObject *parent)
    : VideoPreprocessorBase(parent)
{
    qRegisterMetaType<cv::Mat>();
}

void VideoPreprocessorOpenCV::processFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    QImage img = frame.toImage();
    if (img.isNull())
        return;

    // Measure inter-frame interval for camera fps.
    if (m_frameTimer.isValid()) {
        const double intervalMs = m_frameTimer.nsecsElapsed() / 1e6;
        m_intervalSum -= m_intervalSamples[m_intervalIndex];
        m_intervalSamples[m_intervalIndex] = intervalMs;
        m_intervalSum += intervalMs;
        m_intervalIndex = (m_intervalIndex + 1) % kWindowSize;
        if (m_intervalCount < kWindowSize)
            ++m_intervalCount;
        if (m_intervalCount == kWindowSize) {
            const double avgInterval = m_intervalSum / kWindowSize;
            if (avgInterval > 0.0)
                emit cameraFpsUpdated(1000.0 / avgInterval);
        }
    }
    m_frameTimer.restart();

    QElapsedTimer timer;
    timer.start();

    // Normalise to packed RGB-triplet so the wrap below is always CV_8UC3.
    img = img.convertToFormat(QImage::Format_RGB888);

    // Wrap QImage pixels without copying, then cvtColor into a new BGR Mat
    // that owns its own buffer — safe to emit across thread boundaries.
    cv::Mat wrapped(img.height(), img.width(), CV_8UC3,
                    const_cast<uchar *>(img.constBits()),
                    static_cast<size_t>(img.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(wrapped, bgr, cv::COLOR_RGB2BGR);

    emit framePreprocessed(bgr);

    // Rolling average timing — circular buffer, O(1) per frame.
    const double ms = timer.nsecsElapsed() / 1e6;
    m_timingSum -= m_timingSamples[m_timingIndex];
    m_timingSamples[m_timingIndex] = ms;
    m_timingSum += ms;
    m_timingIndex = (m_timingIndex + 1) % kWindowSize;
    if (m_timingCount < kWindowSize)
        ++m_timingCount;

    if (m_timingCount == kWindowSize)
        emit preprocessStatsUpdated(m_timingSum / kWindowSize);
}

#endif // HAVE_OPENCV
