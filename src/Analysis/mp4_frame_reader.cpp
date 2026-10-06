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

#include "mp4_frame_reader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

namespace pinpoint::analysis {

namespace {

// OpenCV 4.x's FFmpeg seek (cap_ffmpeg_impl.hpp, CvCapture_FFMPEG::seek) aims at
// the timestamp 16 frames BEFORE the target, lets av_seek_frame(BACKWARD) land on
// the keyframe at or before that, then decodes forward to the target. So a seek to
// frame n decodes n − keyframe(n − 16) frames inside OpenCV, where this class
// cannot count them — that is the modelled figure in Stats::seekDecodedEst.
constexpr int64_t kOpenCvSeekBackoff = 16;
// A forward jump is decoded through unless a seek is estimated cheaper by this many
// frames: a seek also flushes the decoder, and a frame-threaded H.264 decoder then
// refills its pipeline (one frame per thread) before the first frame comes out.
constexpr int64_t kForwardSeekMargin = 8;

double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::string fourccText(double v)
{
    if (!(v > 0.0))
        return v < 0.0 ? std::string("n/a") : std::string("0");
    const auto u = static_cast<uint32_t>(static_cast<int64_t>(v));
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((u >> (8 * i)) & 0xFF);
        if (c < 32 || c > 126)
            return std::to_string(u);
        s.push_back(c);
    }
    return s;
}

const char* hwName(int t)
{
    switch (t) {
    case cv::VIDEO_ACCELERATION_NONE:  return "none";
    case cv::VIDEO_ACCELERATION_ANY:   return "any";
    case cv::VIDEO_ACCELERATION_D3D11: return "d3d11";
    case cv::VIDEO_ACCELERATION_VAAPI: return "vaapi";
    case cv::VIDEO_ACCELERATION_MFX:   return "mfx";
    default:                           return "other";
    }
}

} // namespace

Mp4FrameDecoder::Mp4FrameDecoder(std::string path, Config cfg)
    : path_(std::move(path)), cfg_(cfg)
{
}

bool Mp4FrameDecoder::ensureOpen()
{
    if (tried_)
        return info_.opened;
    tried_ = true;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        std::vector<int> params;
        if (cfg_.hwAccel) {
            // OpenCL interop off: the frame has to come back to host memory as BGR
            // for everything downstream, and the UMat path would only add a copy.
            params.insert(params.end(), { cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_ANY,
                                          cv::CAP_PROP_HW_ACCELERATION_USE_OPENCL, 0 });
        }
        if (cfg_.threads > 0)
            params.insert(params.end(), { cv::CAP_PROP_N_THREADS, cfg_.threads });

        if (cfg_.wantsFfmpeg()) {
            info_.opened = params.empty() ? cap_.open(path_, cv::CAP_FFMPEG)
                                          : cap_.open(path_, cv::CAP_FFMPEG, params);
            if (!info_.opened && cfg_.hwAccel) {
                // The FFmpeg backend is there but would not open with a hardware
                // device: software FFmpeg, same threads.
                info_.hwFellBack = true;
                std::vector<int> sw;
                if (cfg_.threads > 0)
                    sw = { cv::CAP_PROP_N_THREADS, cfg_.threads };
                info_.opened = sw.empty() ? cap_.open(path_, cv::CAP_FFMPEG)
                                          : cap_.open(path_, cv::CAP_FFMPEG, sw);
            }
            if (!info_.opened) {
                // No FFmpeg backend at all — a Windows build without the OpenCV
                // FFmpeg plugin DLL on its search path. Never fail the re-analysis
                // over a speed switch: open the way the reader always did.
                info_.ffmpegUnavailable = true;
                info_.opened = cap_.open(path_);
            }
        } else {
            // Exactly the pre-step-5 open (backend chosen by OpenCV's priority list).
            info_.opened = cap_.open(path_);
        }
        info_.opened = info_.opened && cap_.isOpened();
        if (info_.opened) {
            info_.backend     = cap_.getBackendName();
            info_.codec       = fourccText(cap_.get(cv::CAP_PROP_FOURCC));
            info_.pixelFormat = fourccText(cap_.get(cv::CAP_PROP_CODEC_PIXEL_FORMAT));
            info_.threads     = int(cap_.get(cv::CAP_PROP_N_THREADS));
            info_.hwAccel     = int(cap_.get(cv::CAP_PROP_HW_ACCELERATION));
            info_.width       = int(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
            info_.height      = int(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
            info_.fps         = cap_.get(cv::CAP_PROP_FPS);
            info_.frameCount  = int64_t(cap_.get(cv::CAP_PROP_FRAME_COUNT));
        }
    } catch (const std::exception&) {
        info_.opened = false;
    }
    next_ = 0;
    info_.openMs = msSince(t0);
    if (info_.opened && cfg_.seek && info_.backend == "FFMPEG")
        probeKeyframes();
    return info_.opened;
}

// One pass over the PACKETS (OpenCV raw mode: demux only, nothing decoded) to learn
// where the keyframes are. The exporter writes GOP 10 (ffmpeg_video_encoder.cpp,
// video_encoder.h gop = 10) precisely so replay can seek; older clips written with
// x264's default keyint ~250 are one GOP per swing, and there a seek is a rewind.
void Mp4FrameDecoder::probeKeyframes()
{
    const auto t0 = std::chrono::steady_clock::now();
    try {
        cv::VideoCapture raw;
        if (raw.open(path_, cv::CAP_FFMPEG) && raw.set(cv::CAP_PROP_FORMAT, -1)) {
            int64_t i = 0;
            while (raw.grab()) {
                if (raw.get(cv::CAP_PROP_LRF_HAS_KEY_FRAME) != 0.0)
                    info_.keyframes.push_back(i);
                ++i;
            }
            if (!info_.keyframes.empty()) {
                int64_t gap = 0;
                for (size_t k = 1; k < info_.keyframes.size(); ++k)
                    gap = std::max(gap, info_.keyframes[k] - info_.keyframes[k - 1]);
                gap = std::max(gap, i - info_.keyframes.back());
                info_.maxGop      = gap;
                info_.seekUseless = info_.keyframes.size() <= 1;
            }
        }
    } catch (const std::exception&) {
        info_.keyframes.clear();
    }
    info_.probeMs = msSince(t0);
}

int64_t Mp4FrameDecoder::keyframeAtOrBefore(int64_t frame) const
{
    const auto& kf = info_.keyframes;
    if (kf.empty() || frame <= 0)
        return 0;
    auto it = std::upper_bound(kf.begin(), kf.end(), frame);
    return it == kf.begin() ? 0 : *(it - 1);
}

// Frames OpenCV decodes to land a seek on `target` (see kOpenCvSeekBackoff). Only
// called with the keyframes probed.
int64_t Mp4FrameDecoder::seekCostEst(int64_t target) const
{
    return target - keyframeAtOrBefore(std::max<int64_t>(0, target - kOpenCvSeekBackoff));
}

bool Mp4FrameDecoder::rewindToZero()
{
    ++stats_.rewinds;
    cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
    next_ = 0;
    return true;
}

bool Mp4FrameDecoder::seekTo(int64_t target)
{
    ++stats_.seeks;
    stats_.seekDecodedEst += seekCostEst(target);
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, double(target))) {
        ++stats_.seekFailures;
        seekOk_ = false;
        return rewindToZero();
    }
    next_       = target;
    verifyNext_ = true;
    return true;
}

bool Mp4FrameDecoder::read(int64_t index, cv::Mat& out)
{
    const auto t0 = std::chrono::steady_clock::now();
    struct Clock {
        Stats& s; std::chrono::steady_clock::time_point t0;
        ~Clock() { s.ms += msSince(t0); }
    } clock{ stats_, t0 };

    try {
        if (index < 0 || !ensureOpen())
            return false;

        // Seek only where the keyframes are known (FFmpeg backend, probe done): the
        // cost model needs them, and on MSMF a seek measured dearer than the rewind.
        const bool canSeek = cfg_.seek && seekOk_ && !info_.seekUseless && !info_.keyframes.empty();
        if (index < next_) {
            // A back-seek. Before step 5 this always rewound to frame 0 — on the
            // face-on two-pass path that decoded the clip a second time up to the
            // swing span. A keyframe seek decodes ≤ backoff + one GOP instead.
            if (canSeek && index >= 2 && seekCostEst(index) < index)
                seekTo(index);
            else
                rewindToZero();
        } else if (canSeek && index >= 2
                   && seekCostEst(index) + kForwardSeekMargin < index - next_) {
            seekTo(index);   // a long forward jump: cheaper to seek than to decode through
        }

        for (int attempt = 0; attempt < 2; ++attempt) {
            while (next_ < index) {
                if (!cap_.grab())
                    return false;
                ++stats_.decoded;
                ++next_;
            }
            if (!cap_.read(frame_) || frame_.empty())
                return false;
            ++stats_.decoded;
            ++next_;

            if (!verifyNext_)
                break;
            verifyNext_ = false;
            // The frame a seek lands on is checked against its own timestamp: the
            // exporter stamps frame i at pts i / fps (CFR), so the index is
            // recoverable exactly. A mislanded seek (a container with an offset, an
            // edit list, VFR) turns seeking off for this reader and the frame is
            // re-read the pre-step-5 way.
            const double ms = cap_.get(cv::CAP_PROP_POS_MSEC);
            const int64_t got = (info_.fps > 0.0) ? int64_t(std::llround(ms * info_.fps / 1000.0)) : -1;
            if (got == index)
                break;
            ++stats_.seekFailures;
            seekOk_ = false;
            rewindToZero();
        }
        if (!frame_.isContinuous())
            frame_ = frame_.clone();
        ++stats_.reads;
        out = frame_;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string Mp4FrameDecoder::summary() const
{
    char buf[512];
    const int64_t dec = stats_.decoded + stats_.seekDecodedEst;
    std::snprintf(buf, sizeof buf,
        "%s backend=%s%s codec=%s pixfmt=%s %dx%d fps=%.3g frames=%lld threads=%d(req %d) hw=%s(req %s%s) "
        "open=%.1fms | seek=%s gop=%lld keyframes=%zu probe=%.1fms | reads=%lld decoded=%lld "
        "(+~%lld in seeks = %lld) rewinds=%lld seeks=%lld seekFailures=%lld | %.0f ms, %.2f ms/decoded frame",
        info_.opened ? "open" : "NOT OPEN",
        info_.backend.empty() ? "?" : info_.backend.c_str(),
        info_.ffmpegUnavailable ? "(FFmpeg asked for, unavailable)" : "",
        info_.codec.c_str(), info_.pixelFormat.c_str(), info_.width, info_.height, info_.fps,
        static_cast<long long>(info_.frameCount), info_.threads, cfg_.threads,
        hwName(info_.hwAccel), cfg_.hwAccel ? "any" : "off", info_.hwFellBack ? ", fell back to software" : "",
        info_.openMs,
        !cfg_.seek ? "off"
            : info_.keyframes.empty() ? "unavailable(no keyframe probe: not FFmpeg)"
            : info_.seekUseless ? "useless(one GOP)" : (seekOk_ ? "on" : "disabled"),
        static_cast<long long>(info_.maxGop), info_.keyframes.size(), info_.probeMs,
        static_cast<long long>(stats_.reads), static_cast<long long>(stats_.decoded),
        static_cast<long long>(stats_.seekDecodedEst), static_cast<long long>(dec),
        static_cast<long long>(stats_.rewinds), static_cast<long long>(stats_.seeks),
        static_cast<long long>(stats_.seekFailures),
        stats_.ms, dec > 0 ? stats_.ms / double(dec) : 0.0);
    return buf;
}

} // namespace pinpoint::analysis
