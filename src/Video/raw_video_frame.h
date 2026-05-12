#pragma once

#include <QByteArray>
#include <QMetaType>

// Lightweight container for a single raw (un-demosaiced) video frame.
// Used to pass Bayer data from the capture loop to the GPU display item
// and the CPU preprocessor without an intermediate demosaic step.
struct RawVideoFrame {
    enum class BayerPattern { RG = 0, BG = 1, GR = 2, GB = 3 };

    QByteArray   data;              // packed row-major bytes (stride == width)
    int          width   = 0;
    int          height  = 0;
    BayerPattern pattern = BayerPattern::RG;

    bool isNull() const { return data.isEmpty() || width <= 0 || height <= 0; }

    // OpenCV cvtColor code for Bayer→BGR conversion.
    int opencvBayerToBgrCode() const;
};
Q_DECLARE_METATYPE(RawVideoFrame)
