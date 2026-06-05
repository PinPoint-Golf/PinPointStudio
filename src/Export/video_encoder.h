/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <memory>
#include <string>

#include <opencv2/core/mat.hpp>

namespace pinpoint {

// Configuration for a single output clip.  All captured frames are written
// sequentially; the container framerate is fixed at out_fps so a 150 fps
// capture plays back as ~5x slow motion.
struct VideoEncoderConfig {
    int width  = 0;
    int height = 0;
    int out_fps = 30;            // fixed slow-mo playback rate; all frames kept
    int crf     = 23;            // from AppSettings videoQuality
    std::string preset = "medium";
    std::string path;            // output .mp4
};

// Abstract encoder. Codec backends are product-specific; obtain a concrete
// instance through makeVideoEncoder().
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool open(const VideoEncoderConfig&) = 0;
    virtual bool writeBgr(const cv::Mat& bgr) = 0;   // one frame; encoder owns pts (sequential @out_fps)
    virtual bool finish() = 0;                       // flush + write trailer
};

// Factory keyed by AppSettings storage/videoCodec. "h264" -> FfmpegH264Encoder
// (when built with HAVE_FFMPEG); unknown codecs / no-FFmpeg builds -> nullptr.
std::unique_ptr<IVideoEncoder> makeVideoEncoder(const std::string& codec);

} // namespace pinpoint
