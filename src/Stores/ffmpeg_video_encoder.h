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

#include <string>

#include "video_encoder.h"

// libav types are forward-declared so this header parses without the FFmpeg
// dev headers; only ffmpeg_video_encoder.cpp includes them.
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace pinpoint {

// H.264 / H.265 encoder backed by libx264 / libx265 via libavcodec/libavformat.
// The libav encoder is selected by name at construction (e.g. "libx264",
// "libx265"); the container muxer is guessed from the output path extension
// (mp4/mov/mkv all carry either codec). Output: yuv420p, BT.709 (limited range),
// +faststart, time_base {1, out_fps} with sequential pts — fixed-rate slow motion.
class FfmpegVideoEncoder final : public IVideoEncoder {
public:
    // codecName is a libavcodec encoder name (default "libx264").
    explicit FfmpegVideoEncoder(std::string codecName = "libx264");
    ~FfmpegVideoEncoder() override;

    FfmpegVideoEncoder(const FfmpegVideoEncoder&)            = delete;
    FfmpegVideoEncoder& operator=(const FfmpegVideoEncoder&) = delete;

    bool open(const VideoEncoderConfig& cfg) override;
    bool writeBgr(const cv::Mat& bgr) override;
    bool finish() override;

private:
    bool drainPackets(bool flushing);
    void cleanup();   // idempotent; safe mid-encode

    std::string m_codecName;               // libavcodec encoder name

    AVFormatContext* m_fmt    = nullptr;
    AVCodecContext*  m_enc    = nullptr;
    AVStream*        m_stream = nullptr;   // owned by m_fmt
    SwsContext*      m_sws    = nullptr;
    AVFrame*         m_frame  = nullptr;   // one reused YUV frame
    AVPacket*        m_pkt    = nullptr;   // reused

    int64_t m_pts           = 0;
    int     m_width         = 0;
    int     m_height        = 0;
    bool    m_headerWritten = false;
    bool    m_finished      = false;
};

} // namespace pinpoint
