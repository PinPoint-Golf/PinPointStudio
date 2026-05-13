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

    // Build a BGR cv::Mat that owns its own buffer (safe to emit across threads).
    // Spinnaker frames arrive as BGR888; other sources arrive as RGB888.
    cv::Mat bgr;
    if (img.format() == QImage::Format_BGR888) {
        // Already BGR — clone directly, no channel-swap needed.
        cv::Mat wrapped(img.height(), img.width(), CV_8UC3,
                        const_cast<uchar *>(img.constBits()),
                        static_cast<size_t>(img.bytesPerLine()));
        bgr = wrapped.clone();
    } else {
        img = img.convertToFormat(QImage::Format_RGB888);
        cv::Mat wrapped(img.height(), img.width(), CV_8UC3,
                        const_cast<uchar *>(img.constBits()),
                        static_cast<size_t>(img.bytesPerLine()));
        cv::cvtColor(wrapped, bgr, cv::COLOR_RGB2BGR);
    }

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

void VideoPreprocessorOpenCV::processRawFrame(const RawVideoFrame &rawFrame)
{
    if (rawFrame.isNull())
        return;

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

    const int cvtCode = rawFrame.opencvBayerToBgrCode();
    cv::Mat bgr;
    if (cvtCode >= 0) {
        cv::Mat bayer(rawFrame.height, rawFrame.width, CV_8UC1,
                      const_cast<char *>(rawFrame.data.constData()),
                      static_cast<size_t>(rawFrame.width));
        cv::cvtColor(bayer, bgr, cvtCode);
    } else {
        // Fallback for unknown patterns: treat as grayscale.
        cv::Mat gray(rawFrame.height, rawFrame.width, CV_8UC1,
                     const_cast<char *>(rawFrame.data.constData()),
                     static_cast<size_t>(rawFrame.width));
        cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
    }

    emit framePreprocessed(bgr);

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
