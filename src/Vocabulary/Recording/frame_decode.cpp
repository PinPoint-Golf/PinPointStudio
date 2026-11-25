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

#include "frame_decode.h"

#include <opencv2/imgproc.hpp>

namespace pinpoint {

DemosaicPlan demosaicPlanFor(PixelFormat fmt)
{
    switch (fmt) {
    case PixelFormat::BayerRG8: return {true, CV_8UC1, cv::COLOR_BayerRGGB2BGR_EA, "EA"};
    case PixelFormat::BayerBG8: return {true, CV_8UC1, cv::COLOR_BayerBGGR2BGR_EA, "EA"};
    case PixelFormat::BayerGR8: return {true, CV_8UC1, cv::COLOR_BayerGRBG2BGR_EA, "EA"};
    case PixelFormat::BayerGB8: return {true, CV_8UC1, cv::COLOR_BayerGBRG2BGR_EA, "EA"};
    case PixelFormat::Mono8:    return {true, CV_8UC1, cv::COLOR_GRAY2BGR,         "none"};
    case PixelFormat::BGR24:    return {true, CV_8UC3, -1,                         "none"};
    case PixelFormat::BGRA32:   return {true, CV_8UC4, cv::COLOR_BGRA2BGR,         "none"};
    case PixelFormat::YUV422:
    case PixelFormat::YUYV:     return {true, CV_8UC2, cv::COLOR_YUV2BGR_YUYV,     "none"};
    case PixelFormat::UYVY:     return {true, CV_8UC2, cv::COLOR_YUV2BGR_UYVY,     "none"};
    case PixelFormat::NV12:     return {true, CV_8UC1, cv::COLOR_YUV2BGR_NV12,     "none", 3, 2};
    case PixelFormat::YUV420P:  return {true, CV_8UC1, cv::COLOR_YUV2BGR_I420,     "none", 3, 2};
    default:                    return {};   // MJPEG, H264_NAL, 12/16-bit: unsupported in v1
    }
}

namespace {

// Raw-payload geometry for one frame: rows of the raw Mat wrap, plane-0
// stride, and the minimum plane-contiguous payload size.
struct FrameGeometry {
    int    rawRows  = 0;
    size_t stride   = 0;
    size_t minBytes = 0;
};

bool geometryFor(const CameraFormat& fmt, const DemosaicPlan& plan, FrameGeometry& g)
{
    const int srcW = static_cast<int>(fmt.width);
    const int srcH = static_cast<int>(fmt.height);
    if (!plan.supported || srcW <= 0 || srcH <= 0)
        return false;
    g.rawRows = srcH * plan.rowsNum / plan.rowsDen;
    const size_t bpp = static_cast<size_t>(CV_ELEM_SIZE(plan.matType));
    g.stride   = fmt.plane_strides[0] ? fmt.plane_strides[0]
                                      : static_cast<size_t>(srcW) * bpp;
    g.minBytes = g.stride * static_cast<size_t>(g.rawRows);
    return true;
}

} // namespace

bool frameGeometry(const CameraFormat& fmt, size_t& stride, size_t& minBytes)
{
    FrameGeometry g;
    if (!geometryFor(fmt, demosaicPlanFor(fmt.pixel_format), g))
        return false;
    stride   = g.stride;
    minBytes = g.minBytes;
    return true;
}

bool decodeToBgr(const CameraFormat& fmt, const std::byte* data, size_t bytes,
                 cv::Mat& out)
{
    if (!data)
        return false;
    const DemosaicPlan plan = demosaicPlanFor(fmt.pixel_format);
    FrameGeometry g;
    if (!geometryFor(fmt, plan, g) || bytes < g.minBytes)
        return false;

    // Zero-copy wrap of the caller's payload (frozen ring memory is stable
    // while the buffer stays Paused).
    const cv::Mat raw(g.rawRows, static_cast<int>(fmt.width), plan.matType,
                      const_cast<std::byte*>(data), g.stride);
    if (plan.cvtCode >= 0)
        cv::cvtColor(raw, out, plan.cvtCode);
    else
        out = raw;   // BGR24 passthrough — still no copy; `out` aliases `data`
    return true;
}

bool decodeToLuma(const CameraFormat& fmt, const std::byte* data, size_t bytes,
                  cv::Mat& out)
{
    if (!data)
        return false;
    const DemosaicPlan plan = demosaicPlanFor(fmt.pixel_format);
    FrameGeometry g;
    if (!geometryFor(fmt, plan, g) || bytes < g.minBytes)
        return false;

    const int srcW = static_cast<int>(fmt.width);
    const int srcH = static_cast<int>(fmt.height);
    auto* p = const_cast<std::byte*>(data);

    switch (fmt.pixel_format) {
    // Planar 4:2:0 — the Y plane is the leading `height` rows of the payload;
    // wrap it directly, no colour conversion. `out` aliases `data`.
    case PixelFormat::NV12:
    case PixelFormat::YUV420P:
    case PixelFormat::Mono8:
        out = cv::Mat(srcH, srcW, CV_8UC1, p, g.stride);
        return true;

    // 8-bit Bayer: full-resolution wrap of the raw mosaic as a cheap grey
    // approximation. The per-pixel R/G/B checkerboard modulation is noise to
    // ridge/anchor detection, and skipping the demosaic keeps the offline
    // loop zero-copy. `out` aliases `data`.
    case PixelFormat::BayerRG8:
    case PixelFormat::BayerBG8:
    case PixelFormat::BayerGR8:
    case PixelFormat::BayerGB8:
        out = cv::Mat(srcH, srcW, CV_8UC1, p, g.stride);
        return true;

    // Packed formats convert into `out` (allocates / reuses `out`'s buffer).
    case PixelFormat::BGRA32: {
        const cv::Mat raw(srcH, srcW, CV_8UC4, p, g.stride);
        cv::cvtColor(raw, out, cv::COLOR_BGRA2GRAY);
        return true;
    }
    case PixelFormat::BGR24: {
        const cv::Mat raw(srcH, srcW, CV_8UC3, p, g.stride);
        cv::cvtColor(raw, out, cv::COLOR_BGR2GRAY);
        return true;
    }
    case PixelFormat::YUV422:
    case PixelFormat::YUYV: {
        const cv::Mat raw(srcH, srcW, CV_8UC2, p, g.stride);
        cv::cvtColor(raw, out, cv::COLOR_YUV2GRAY_YUY2);
        return true;
    }
    case PixelFormat::UYVY: {
        const cv::Mat raw(srcH, srcW, CV_8UC2, p, g.stride);
        cv::cvtColor(raw, out, cv::COLOR_YUV2GRAY_UYVY);
        return true;
    }
    default:
        return false;
    }
}

} // namespace pinpoint
