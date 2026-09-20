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

// The window→pixels layer of a shaft tracker: pose interpolation, grey decode,
// and the decode-once parallel frame cache. Promoted VERBATIM out of
// shaft_tracker.cpp's anonymous namespace so the down-the-line tracker reads its
// stream through the same code (dtl_shaft_tracker_design.md §5.1) rather than a
// second copy that could drift on the payload contract — which is the part that
// is easy to get subtly, silently wrong:
//
//   payload bytes are valid only until the next payloadOf() on this source, and
//   the disk backing keeps ONE frame resident, so the FETCH must stay
//   single-threaded. Only the DECODE is parallel, and it writes owned Mats.
//
// Header-only: these are a handful of small functions over the frozen window,
// and every caller is in the same library.

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QPointF>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "shaft_track_assembly.h"   // FrameSource
#include "shaft_track_shared.h"     // kFrameCacheCapBytes (shared with the assembly's own cache)
#include "swing_analysis.h"         // PoseTrack2D / PoseFrame2D / kWholeBodyJoints
#include "swing_window.h"
#include "types.h"
#include "../Export/frame_decode.h"

namespace pinpoint::analysis {

// Linear-interpolate a pose frame between two samples at time t.
inline PoseFrame2D lerpPoseFrame(const PoseTrack2D& pose, size_t lo, size_t hi, int64_t t)
{
    const PoseFrame2D& a = pose.frames[lo];
    const PoseFrame2D& b = pose.frames[hi];
    const double f = (hi == lo || b.t_us == a.t_us)
                         ? 0.0
                         : std::clamp(double(t - a.t_us) / double(b.t_us - a.t_us), 0.0, 1.0);
    PoseFrame2D o; o.t_us = t;
    // Interpolate ALL 133 COCO-WholeBody channels (not just the 17 body joints):
    // the WB4 hand-axis prior reads the hand keypoints (91–132) at the grip's
    // sub-frame time. Output-identical on the existing path — nothing but the new
    // prior reads channels 17+.
    for (size_t i = 0; i < size_t(kWholeBodyJoints); ++i) {
        o.kp[i] = QPointF(a.kp[i].x() + (b.kp[i].x() - a.kp[i].x()) * f,
                          a.kp[i].y() + (b.kp[i].y() - a.kp[i].y()) * f);
        o.conf[i] = a.conf[i] + (b.conf[i] - a.conf[i]) * float(f);
    }
    o.leadHand  = QPointF(a.leadHand.x() + (b.leadHand.x() - a.leadHand.x()) * f,
                          a.leadHand.y() + (b.leadHand.y() - a.leadHand.y()) * f);
    o.trailHand = QPointF(a.trailHand.x() + (b.trailHand.x() - a.trailHand.x()) * f,
                          a.trailHand.y() + (b.trailHand.y() - a.trailHand.y()) * f);
    o.handConf  = a.handConf + (b.handConf - a.handConf) * float(f);
    return o;
}

// Decode raw payload bytes to CV_8UC1 grey, matching the Python path (demosaic →
// BGR2GRAY). Falls back to the luma fast path. Empty on undecodable formats. The
// result never aliases `data` (cvtColor/clone), so it stays valid after the
// payload buffer is reused — the invariant the parallel-decode cache relies on.
inline cv::Mat decodeGrayFromBytes(const pinpoint::CameraFormat& cfmt,
                                   const std::byte* data, size_t bytes)
{
    cv::Mat bgr;
    if (pinpoint::decodeToBgr(cfmt, data, bytes, bgr) && !bgr.empty()) {
        if (bgr.channels() == 1) return bgr.clone();
        cv::Mat g; cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
        return g;
    }
    cv::Mat luma;
    if (pinpoint::decodeToLuma(cfmt, data, bytes, luma) && !luma.empty())
        return luma.clone();
    return {};
}

// Fetch + decode one frozen-ring payload (the serial fall-back frameAt path).
// payloadOf() is single-threaded by contract; the handle is consumed at once.
inline cv::Mat decodeGray(const pinpoint::SwingWindow& window, const pinpoint::IndexEntry& e,
                          const pinpoint::CameraFormat& cfmt)
{
    const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
    return decodeGrayFromBytes(cfmt, handle.data, handle.bytes);
}

// ── decode-once span cache with parallel decode ──────────────────────────────
// Fills `cacheOut` with one grey Mat per entry of `cov` and returns the
// FrameSource that reads it. Capped by bytes; over the cap `cacheOut` is left
// empty and the returned callable falls back to the per-call decode path, which
// keeps the serial contract and reproduces the pre-cache behaviour exactly.
// The cap is shaftshared::kFrameCacheCapBytes — ONE constant, because decideTrack
// runs its own cache under the same rule and the two must make the same decision
// on the same span. Two copies of the number is how they stop doing that.
//
// ⚠ `window`, `cov`, `cfmt` and `cacheOut` must all outlive the returned
// callable — it closes over them by reference, exactly as the in-line version
// did. `cacheOut` is CLEARED first: a caller that reuses one vector across spans
// would otherwise keep the previous span's Mats when this span is over the cap,
// and every frame would come back from the wrong swing.
inline FrameSource buildFrameCache(const pinpoint::SwingWindow& window,
                                   const std::vector<pinpoint::IndexEntry>& cov,
                                   const pinpoint::CameraFormat& cfmt, int w, int h,
                                   std::vector<cv::Mat>& cacheOut)
{
    cacheOut.clear();
    const int nf = int(cov.size());
    const size_t cacheBytes = size_t(nf) * size_t(w) * size_t(h);   // CV_8UC1: 1 byte/px
    if (cacheBytes > 0 && cacheBytes <= shaftshared::kFrameCacheCapBytes) {
        cacheOut.assign(size_t(nf), cv::Mat());
        constexpr int kChunk = 16;
        std::vector<std::vector<std::byte>> chunkBuf(kChunk);
        for (int base = 0; base < nf; base += kChunk) {
            const int cnt = std::min(kChunk, nf - base);
            for (int j = 0; j < cnt; ++j) {                 // serial fetch (payload contract)
                const pinpoint::SourceRing::ReadHandle hnd = window.payloadOf(cov[size_t(base + j)]);
                if (hnd.data && hnd.bytes) chunkBuf[size_t(j)].assign(hnd.data, hnd.data + hnd.bytes);
                else                       chunkBuf[size_t(j)].clear();
            }
            cv::parallel_for_(cv::Range(0, cnt), [&](const cv::Range& rng) {   // parallel decode
                for (int j = rng.start; j < rng.end; ++j)
                    cacheOut[size_t(base + j)] =
                        chunkBuf[size_t(j)].empty()
                            ? cv::Mat()
                            : decodeGrayFromBytes(cfmt, chunkBuf[size_t(j)].data(),
                                                  chunkBuf[size_t(j)].size());
            });
        }
    }
    const bool haveCache = !cacheOut.empty();
    return [&window, &cov, &cfmt, &cacheOut, haveCache, nf](int i) -> cv::Mat {
        if (haveCache && i >= 0 && i < nf) return cacheOut[size_t(i)];
        return decodeGray(window, cov[size_t(i)], cfmt);
    };
}

} // namespace pinpoint::analysis
