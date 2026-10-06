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

// ball.roiDecode (analysis_dag_design.md step C): BallRunner decodes only the rows the
// padded ROI needs — a 2-row margin, even-aligned, for the Bayer edge-aware demosaic —
// and converts only the padded crop to grey/float. This pins that the DoG response R and
// the W3 activity crop from that path are BYTE-identical to the full-frame path, for
// every pixel format the runner cuts into rows, with the ROI at the frame's edges too.

#include "../ball_temporal.h"
#include "../../Export/frame_decode.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <opencv2/imgproc.hpp>

using namespace pinpoint::balltemporal;

static int g_fail = 0;

#define CHECK(label, cond)                                                    \
    do {                                                                      \
        const bool ok = (cond);                                              \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);            \
        if (!ok) ++g_fail;                                                  \
    } while (0)

static bool sameBytes(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type()) return false;
    for (int y = 0; y < a.rows; ++y)
        if (std::memcmp(a.ptr(y), b.ptr(y), size_t(a.cols) * a.elemSize()) != 0) return false;
    return true;
}

// A scene with structure (a smooth field + a bright disc + noise) so the edge-aware
// demosaic's gradient choices actually vary.
static std::vector<std::byte> makePayload(int W, int H, int bpp, uint64_t seed)
{
    cv::Mat f(H, W * bpp, CV_8U);
    cv::RNG rng(seed);
    rng.fill(f, cv::RNG::UNIFORM, 0, 256);
    cv::GaussianBlur(f, f, cv::Size(0, 0), 2.0);
    cv::circle(f, cv::Point(W * bpp / 2, H * 3 / 4), 12, cv::Scalar(250), -1);
    cv::Mat n(H, W * bpp, CV_8U);
    rng.fill(n, cv::RNG::UNIFORM, 0, 20);
    f += n;
    std::vector<std::byte> p(size_t(W) * bpp * H);
    std::memcpy(p.data(), f.data, p.size());
    return p;
}

static void checkFormat(const char *name, pinpoint::PixelFormat pf, int bpp, int margin)
{
    std::printf("\n[%s]\n", name);
    const int W = 344, H = 512;
    pinpoint::CameraFormat fmt{};
    fmt.pixel_format = pf;
    fmt.width = uint32_t(W);
    fmt.height = uint32_t(H);
    const std::vector<std::byte> payload = makePayload(W, H, bpp, 7 + uint64_t(pf));
    const double rHat = radiusForWidth(W);
    size_t stride = 0, fullBytes = 0;
    CHECK("frameGeometry", pinpoint::frameGeometry(fmt, stride, fullBytes) && fullBytes == payload.size());

    // Interior, odd top row, touching the bottom edge, touching the top edge.
    const cv::Rect rois[] = { { 60, 301, 200, 140 }, { 0, 377, 344, 135 }, { 30, 0, 120, 61 }, { 90, 255, 80, 40 } };
    for (const cv::Rect &roi : rois) {
        cv::Mat bgr, g8, g32;
        CHECK("full decode", pinpoint::decodeToBgr(fmt, payload.data(), payload.size(), bgr));
        cv::cvtColor(bgr, g8, cv::COLOR_BGR2GRAY);
        g8.convertTo(g32, CV_32F);
        const cv::Mat Rfull = paddedResponse(g32, roi, rHat);
        const cv::Rect act = roi & cv::Rect(0, 0, W, H);
        const cv::Mat actFull = g8(act).clone();

        // The runner's row-band path.
        const PaddedCrop pc = paddedCropRect(W, H, roi, rHat);
        int y0 = std::max(0, pc.padded.y - margin);
        y0 -= y0 % 2;
        const int y1 = std::min(H, pc.padded.y + pc.padded.height + margin);
        std::vector<std::byte> band(payload.begin() + ptrdiff_t(size_t(y0) * stride),
                                    payload.begin() + ptrdiff_t(size_t(y1) * stride));
        pinpoint::CameraFormat bf = fmt;
        bf.height = uint32_t(y1 - y0);
        bf.plane_strides[0] = uint32_t(stride);
        cv::Mat bbgr, cg8, cg32;
        CHECK("band decode", pinpoint::decodeToBgr(bf, band.data(), band.size(), bbgr));
        cv::cvtColor(bbgr(cv::Rect(pc.padded.x, pc.padded.y - y0, pc.padded.width, pc.padded.height)), cg8,
                     cv::COLOR_BGR2GRAY);
        cg8.convertTo(cg32, CV_32F);
        const cv::Mat Rroi = responseFromPaddedCrop(cg32, pc, rHat);
        const cv::Mat actRoi = cg8(act - pc.padded.tl()).clone();
        char label[128];
        std::snprintf(label, sizeof label, "R byte-identical, ROI %d,%d %dx%d", roi.x, roi.y, roi.width,
                      roi.height);
        CHECK(label, !Rfull.empty() && sameBytes(Rfull, Rroi));
        CHECK("activity crop byte-identical", sameBytes(actFull, actRoi));
    }
}

int main()
{
    std::printf("=== ball_roi_decode_test (ball.roiDecode parity) ===\n");
    checkFormat("BayerRG8, 2-row margin", pinpoint::PixelFormat::BayerRG8, 1, 2);
    checkFormat("BayerGB8, 2-row margin", pinpoint::PixelFormat::BayerGB8, 1, 2);
    checkFormat("BGR24 (the MP4 path)", pinpoint::PixelFormat::BGR24, 3, 0);
    checkFormat("Mono8", pinpoint::PixelFormat::Mono8, 1, 0);

    // The negative control: with NO margin the Bayer band's edge rows differ, so the
    // test above is not passing vacuously.
    {
        std::printf("\n[control: Bayer band with no margin]\n");
        const int W = 344, H = 512;
        pinpoint::CameraFormat fmt{};
        fmt.pixel_format = pinpoint::PixelFormat::BayerRG8;
        fmt.width = uint32_t(W);
        fmt.height = uint32_t(H);
        const std::vector<std::byte> payload = makePayload(W, H, 1, 99);
        cv::Mat full, part;
        pinpoint::decodeToBgr(fmt, payload.data(), payload.size(), full);
        pinpoint::CameraFormat bf = fmt;
        bf.height = 100;
        pinpoint::decodeToBgr(bf, payload.data() + 200 * W, size_t(100) * W, part);
        CHECK("no-margin band differs at its edge rows", !sameBytes(full.rowRange(200, 300), part));
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
