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

#if defined(HAVE_OPENCV) && defined(HAVE_SEGMENTER) && defined(HAVE_ONNXRUNTIME)

#include <memory>
#include <QString>
#include <opencv2/core.hpp>

// Lightweight person segmentation using a dedicated ONNX model.
//
// Runs independently of MoveNet — use it to isolate the subject before
// pose estimation so background people don't interfere.
//
// Usage:
//   PersonSegmenter seg;
//   if (seg.load()) {
//       cv::Mat mask = seg.segment(bgrFrame); // CV_32F, [0,1], person=1
//       // blur background using mask, then run MoveNet on clean frame
//   }

class PersonSegmenter
{
public:
    PersonSegmenter();
    ~PersonSegmenter();

    static QString modelPath();
    static bool    isAvailable();

    bool load();
    bool isReady() const { return m_ready; }

    // Returns a CV_32F single-channel mask the same size as `bgr`.
    // Values close to 1.0 = person, close to 0.0 = background.
    // Returns an empty Mat on failure.
    cv::Mat segment(const cv::Mat &bgr) const;

private:
    struct OrtState;
    std::unique_ptr<OrtState> m_ort;

    bool m_ready  = false;
    int  m_inputH = 0;
    int  m_inputW = 0;
    bool m_isNHWC = false; // true: input [N,H,W,C], false: input [N,C,H,W]
};

#endif // HAVE_OPENCV && HAVE_SEGMENTER && HAVE_ONNXRUNTIME
