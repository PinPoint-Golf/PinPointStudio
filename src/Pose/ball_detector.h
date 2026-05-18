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

#include <QObject>
#include <QRectF>
#include <opencv2/core.hpp>

struct BallDetection {
    bool    found     = false;
    float   x         = 0.f;   // normalized [0,1] within the full frame (centre)
    float   y         = 0.f;
    float   radius    = 0.f;   // radius normalised to frame width
    qint64  detectMs  = 0;     // wall-clock duration of detect() in ms (0 = skipped)
};
Q_DECLARE_METATYPE(BallDetection)

// Detects golf balls using the OpenCV Hough circle transform.
//
// Receives preprocessed BGR frames from VideoPreprocessorOpenCV.
// Only searches within the ROI supplied via setRoi(); if no ROI is set
// the detector is silent (emits nothing).
//
// Wire into the pipeline and move to a dedicated QThread:
//
//   connect(preprocessor, &VideoPreprocessorOpenCV::framePreprocessed,
//           detector,     &BallDetector::detect,
//           Qt::QueuedConnection);

class BallDetector : public QObject
{
    Q_OBJECT

public:
    explicit BallDetector(QObject *parent = nullptr);

public slots:
    // Receives a BGR CV_8UC3 frame from VideoPreprocessorOpenCV.
    // No-op when no ROI is set.
    void detect(const cv::Mat &frame);

    // Update the search region. Safe to call via queued connection.
    void setRoi(QRectF roi);

signals:
    void ballDetected(const BallDetection &result);

private:
    // Only accessed on the detector's own thread (both detect() and setRoi()
    // arrive via queued connections, so they are serialised by the event loop).
    QRectF m_roi;
};

#endif // HAVE_OPENCV
