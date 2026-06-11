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

// Shared frame decode for frozen-window payloads (EventBuffer ring bytes →
// cv::Mat). Extracted from SwingExporter's demosaic table so the exporter and
// the shot analyzer share ONE decode path that can never diverge. Pure
// functions — no Qt, no logging; callers handle failures.

#include <cstddef>

#include <opencv2/core.hpp>

#include "format_descriptor.h"

namespace pinpoint {

// How a payload of a given PixelFormat becomes a BGR frame.
struct DemosaicPlan {
    bool supported   = false;
    int  matType     = 0;       // cv::Mat element type of the raw payload
    int  cvtCode     = -1;      // cv::cvtColor code; -1 = passthrough (already BGR)
    const char* tag  = "none";  // recorded in swing.json processing.demosaic
    // Raw Mat rows = height * rowsNum / rowsDen. Planar YUV 4:2:0 payloads
    // (NV12/I420 — Y plane followed by chroma, stored contiguously by
    // CameraInstance::publishFrameToBuffer) are 3/2 image-height rows tall.
    int  rowsNum     = 1;
    int  rowsDen     = 1;
};

// Mirrors the live-view mapping (camera_instance.cpp PixelFormat->BayerPattern,
// raw_video_frame.cpp pattern->COLOR_Bayer{RGGB,BGGR,GRBG,GBRG}2BGR) so decoded
// colour matches the on-screen image exactly.  The _EA (edge-aware) variants
// share the same pattern naming — better quality, irrelevant cost off the hot
// path.  v1 handles 8-bit formats only.
DemosaicPlan demosaicPlanFor(PixelFormat fmt);

// Plane-0 stride and the minimum payload size of one frame of `fmt`
// (plane-contiguous storage: stride × rawRows). Returns false when the pixel
// format is unsupported or the dimensions are degenerate.
bool frameGeometry(const CameraFormat& fmt, size_t& stride, size_t& minBytes);

// Decode one frozen-ring payload to a BGR CV_8UC3 frame. Returns false on
// null/short payloads and unsupported formats (MJPEG, H264_NAL, 12/16-bit).
//
// `out` MAY alias `data` (BGR24 passthrough is a zero-copy header) — it is
// valid only while the caller's payload buffer (the ring read handle) lives.
bool decodeToBgr(const CameraFormat& fmt, const std::byte* data, size_t bytes,
                 cv::Mat& out);

// Luma-only fast path to a CV_8UC1 frame. Same failure contract as
// decodeToBgr. For NV12/YUV420P/Mono8 the Y plane is wrapped directly, and for
// 8-bit Bayer the raw mosaic is wrapped as a cheap full-resolution grey
// approximation (the per-pixel checkerboard error is irrelevant to
// ridge/anchor detection and avoids a demosaic on the offline hot loop) —
// in all these cases `out` ALIASES `data` and is valid only while the
// caller's buffer (the ring read handle) lives. Packed formats (BGRA32,
// BGR24, YUYV/UYVY) convert into `out`.
bool decodeToLuma(const CameraFormat& fmt, const std::byte* data, size_t bytes,
                  cv::Mat& out);

} // namespace pinpoint
