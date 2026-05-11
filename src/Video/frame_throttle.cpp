#ifdef HAVE_OPENCV

#include "frame_throttle.h"

FrameThrottle::FrameThrottle(QObject *parent)
    : QObject(parent)
{}

void FrameThrottle::offer(const cv::Mat &frame)
{
    if (m_busy)
        return;
    m_busy = true;
    emit frameReady(frame);
}

void FrameThrottle::clearBusy()
{
    m_busy = false;
}

#endif // HAVE_OPENCV
