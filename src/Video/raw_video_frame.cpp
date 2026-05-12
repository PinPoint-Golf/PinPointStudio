#include "raw_video_frame.h"

#ifdef HAVE_OPENCV
#include <opencv2/imgproc.hpp>
#endif

int RawVideoFrame::opencvBayerToBgrCode() const
{
#ifdef HAVE_OPENCV
    switch (pattern) {
    case BayerPattern::RG: return cv::COLOR_BayerRGGB2BGR;
    case BayerPattern::BG: return cv::COLOR_BayerBGGR2BGR;
    case BayerPattern::GR: return cv::COLOR_BayerGRBG2BGR;
    case BayerPattern::GB: return cv::COLOR_BayerGBRG2BGR;
    }
#endif
    return -1;
}
