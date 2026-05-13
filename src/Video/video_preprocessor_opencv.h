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

#ifdef HAVE_OPENCV

#include "video_preprocessor_base.h"
#include <QElapsedTimer>
#include <opencv2/core.hpp>
#include <array>

// Concrete preprocessor that converts incoming QVideoFrames to BGR cv::Mat.
//
// cv::Mat is registered as a Qt metatype at runtime in the constructor via
// qRegisterMetaType<cv::Mat>() — sufficient for function-pointer QueuedConnection
// in Qt 6.  Q_DECLARE_METATYPE is intentionally omitted: MOC compiles all moc
// files into one translation unit and any prior implicit instantiation of
// QMetaTypeId<cv::Mat> (e.g. from PoseEstimatorBase's slot signature) would
// cause a "explicit specialisation after instantiation" error.
//
// Wire the output signal to the pose estimator:
//   connect(preprocessor, &VideoPreprocessorOpenCV::framePreprocessed,
//           poseEstimator, &PoseEstimatorBase::estimatePose,
//           Qt::QueuedConnection);

class VideoPreprocessorOpenCV : public VideoPreprocessorBase
{
    Q_OBJECT

public:
    explicit VideoPreprocessorOpenCV(QObject *parent = nullptr);

public slots:
    void processFrame(const QVideoFrame &frame) override;
    void processRawFrame(const RawVideoFrame &frame) override;

signals:
    void framePreprocessed(const cv::Mat &mat);

private:
    static constexpr int kWindowSize = 30;

    // Preprocessing duration samples.
    std::array<double, kWindowSize> m_timingSamples{};
    double m_timingSum   = 0.0;
    int    m_timingIndex = 0;
    int    m_timingCount = 0;

    // Inter-frame interval samples for camera fps.
    QElapsedTimer m_frameTimer;
    std::array<double, kWindowSize> m_intervalSamples{};
    double m_intervalSum   = 0.0;
    int    m_intervalIndex = 0;
    int    m_intervalCount = 0;
};

#endif // HAVE_OPENCV
