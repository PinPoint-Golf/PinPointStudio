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

// Frame-accurate random access into a recorded MP4 for re-analysis (a swing with
// no raw sidecar). Lifted out of swing_reanalyzer.cpp's anonymous namespace so the
// seek behaviour can be tested on a synthetic clip (mp4_frame_reader_test.cpp).
// Qt-free and log-free: the caller logs info() / stats().
//
// Why it matters (pose_inference_performance_plan.md step 5): after step 3 the
// serial MP4 decode is the floor of the studio's pose pass — 1.15 s face-on and
// 1.16 s DTL on 5 Oct swing_0013, against 0.54 + 0.61 s of inference. Part of the
// face-on figure is a second decode: the two-pass pose path reads the coarse grid
// to the end of the clip, then asks for the swing span, and a back-seek here used
// to rewind to frame 0 and decode forward again.
//
// Three switches, all OFF by default (= the reader as it was):
//   backend  — "ffmpeg" opens with cv::CAP_FFMPEG; otherwise OpenCV's priority list
//              picks, which on a Windows install WITHOUT opencv_videoio_ffmpeg*.dll
//              beside the exe (today's) is Media Foundation (MSMF) decoding on
//              D3D11 — ~1.0–1.3 ms a frame on the studio against FFmpeg software's
//              0.4. Fails soft to the default open when the FFmpeg plugin is absent.
//   threads  — the FFmpeg decoder's thread count (CAP_PROP_N_THREADS at open);
//              0 leaves OpenCV's own choice.
//   hwAccel  — CAP_PROP_HW_ACCELERATION = VIDEO_ACCELERATION_ANY (D3D11VA on the
//              studio, VideoToolbox where OpenCV's FFmpeg has it); fails soft to
//              software. Frames still arrive as host BGR.
//   seek     — a back-seek (or a forward jump that costs more to decode through
//              than to seek) goes to the keyframe before the target instead of
//              frame 0. The keyframe layout is probed once by demuxing packets
//              (no decode); every seek is verified against the frame's own
//              timestamp, and a reader whose seek ever lands wrong falls back to
//              rewind-and-decode for good. Only on the FFmpeg backend: the probe
//              needs its raw packet mode, and MSMF's seek measured SLOWER than its
//              rewind (DTL 1081 → 1506 ms on the studio).

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pinpoint::analysis {

class Mp4FrameDecoder {
public:
    struct Config {
        bool ffmpeg  = false;   // decode.backend   ("ffmpeg"; else OpenCV's priority list)
        int  threads = 0;       // decode.threads   (0 = OpenCV's default)
        bool hwAccel = false;   // decode.hwAccel   ("any")
        bool seek    = false;   // decode.seek
        bool any() const { return ffmpeg || threads > 0 || hwAccel || seek; }
        // threads and hwAccel are FFmpeg-backend properties: either one asks for it.
        bool wantsFfmpeg() const { return ffmpeg || threads > 0 || hwAccel; }
    };

    // What the backend reports once the clip is open (and the keyframe probe).
    struct Info {
        bool        opened      = false;
        std::string backend;            // cv::VideoCapture::getBackendName() ("FFMPEG", "MSMF", …)
        bool        ffmpegUnavailable = false; // asked for FFmpeg, could not open with it → OpenCV's default
        std::string codec;              // CAP_PROP_FOURCC as text ("h264", "avc1" …)
        std::string pixelFormat;        // CAP_PROP_CODEC_PIXEL_FORMAT as text
        int         threads     = -1;   // CAP_PROP_N_THREADS read back (-1 = not reported)
        int         hwAccel     = 0;    // CAP_PROP_HW_ACCELERATION read back (cv::VideoAccelerationType)
        bool        hwFellBack  = false;// requested, open with it failed → reopened in software
        int         width = 0, height = 0;
        double      fps         = 0.0;
        int64_t     frameCount  = 0;    // CAP_PROP_FRAME_COUNT (container's figure)
        double      openMs      = 0.0;
        // Keyframe probe (seek on only). keyframes empty = not probed / probe failed.
        std::vector<int64_t> keyframes; // frame indices of the key packets, ascending
        int64_t     maxGop      = -1;   // longest keyframe gap (or clip length when only frame 0 is key)
        double      probeMs     = 0.0;
        bool        seekUseless = false;// only frame 0 is a keyframe: a seek IS a rewind
    };

    // The reader's own counters, cumulative over its life.
    struct Stats {
        int64_t reads          = 0;   // frames returned
        int64_t decoded        = 0;   // frames this class grabbed/read itself
        int64_t seekDecodedEst = 0;   // frames OpenCV decodes inside its seeks (modelled, see .cpp)
        int64_t rewinds        = 0;   // back to frame 0, decode forward (today's back-seek)
        int64_t seeks          = 0;   // keyframe seeks taken (seek on)
        int64_t seekFailures   = 0;   // a seek landed on the wrong frame → rewound, seek disabled
        double  ms             = 0.0; // wall time inside read(), incl. seeks and conversion
    };

    Mp4FrameDecoder(std::string path, Config cfg);

    // Decode frame `index` (0-based, display order) to BGR into `out`. `out` refers
    // to this object's buffer and stays valid until the next read(). false on
    // open failure, past-the-end or a decode error.
    bool read(int64_t index, cv::Mat& out);

    const Info&   info()   const { return info_; }
    const Stats&  stats()  const { return stats_; }
    const Config& config() const { return cfg_; }

    // One line: what the backend reports + the counters (for the app log).
    std::string summary() const;

private:
    bool    ensureOpen();
    void    probeKeyframes();
    bool    rewindToZero();
    bool    seekTo(int64_t target);
    int64_t keyframeAtOrBefore(int64_t frame) const;
    int64_t seekCostEst(int64_t target) const;

    std::string      path_;
    Config           cfg_;
    Info             info_;
    Stats            stats_;
    cv::VideoCapture cap_;
    cv::Mat          frame_;
    int64_t          next_       = 0;     // index the next grab returns
    bool             tried_      = false;
    bool             seekOk_     = true;  // cleared for good on a mislanded seek
    bool             verifyNext_ = false; // check the next read's timestamp (just seeked)
};

} // namespace pinpoint::analysis
