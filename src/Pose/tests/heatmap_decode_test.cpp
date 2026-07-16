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

// Standalone test for the heatmap sub-pixel decode (src/Pose/heatmap_decode.h).
// Pure C++ — no ORT / OpenCV / Qt. Locks:
//   * DARK recovers a synthetic Gaussian's fractional peak within tolerance;
//   * the reported score is ALWAYS the RAW (pre-blur) peak;
//   * degenerate inputs (border peak, flat map) fall back to the argmax rule
//     (DARK output == argmax output).

#include "../heatmap_decode.h"

#include <cmath>
#include <cstdio>
#include <vector>

using pinpoint::pose::decodeArgmax;
using pinpoint::pose::decodeDark;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

static constexpr int W = 48;
static constexpr int H = 64;

// Build a Gaussian heatmap peaked (in the continuum) at (cx, cy).
static std::vector<float> gaussianMap(double cx, double cy, double sigma)
{
    std::vector<float> hm(size_t(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double dx = x - cx, dy = y - cy;
            hm[size_t(y) * W + x] = float(std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma)));
        }
    return hm;
}

static int argmaxIdx(const std::vector<float> &hm)
{
    int mi = 0;
    for (int i = 1; i < W * H; ++i)
        if (hm[size_t(i)] > hm[size_t(mi)]) mi = i;
    return mi;
}

int main()
{
    std::printf("=== heatmap_decode (DARK sub-pixel) ===\n");
    std::vector<float> blur(size_t(W) * H);

    // 1) Sub-pixel recovery of a fractional Gaussian peak.
    {
        const double cx = 23.4, cy = 40.7;
        auto hm = gaussianMap(cx, cy, 1.5);
        float nx = 0, ny = 0, score = 0;
        decodeDark(hm.data(), W, H, blur.data(), nx, ny, score);
        const double rx = double(nx) * W, ry = double(ny) * H;
        std::printf("    recovered (%.3f, %.3f) vs (%.3f, %.3f)\n", rx, ry, cx, cy);
        CHECK("DARK recovers x within 0.35 cell", std::fabs(rx - cx) < 0.35);
        CHECK("DARK recovers y within 0.35 cell", std::fabs(ry - cy) < 0.35);
        // Score is the RAW peak (value at the argmax cell), NOT the blurred peak.
        const int mi = argmaxIdx(hm);
        CHECK("score == raw argmax peak", score == hm[size_t(mi)]);
        // And strictly better than the ±0.25 argmax rule for this fractional peak.
        float ax = 0, ay = 0, as = 0;
        decodeArgmax(hm.data(), W, H, ax, ay, as);
        const double argErr = std::hypot(double(ax) * W - cx, double(ay) * H - cy);
        const double darkErr = std::hypot(rx - cx, ry - cy);
        std::printf("    argmax err %.3f  dark err %.3f\n", argErr, darkErr);
        CHECK("DARK error <= argmax error", darkErr <= argErr + 1e-9);
    }

    // 2) Border peak ⇒ no 3×3 neighbourhood ⇒ fall back to the argmax rule.
    {
        std::vector<float> hm(size_t(W) * H, 0.f);
        hm[0] = 1.f;   // top-left corner
        float nx = 0, ny = 0, score = 0;
        decodeDark(hm.data(), W, H, blur.data(), nx, ny, score);
        float ax = 0, ay = 0, as = 0;
        decodeArgmax(hm.data(), W, H, ax, ay, as);
        CHECK("border: DARK nx == argmax nx", nx == ax);
        CHECK("border: DARK ny == argmax ny", ny == ay);
        CHECK("border: score == raw peak (1.0)", score == 1.f);
    }

    // 3) Flat/uniform map ⇒ argmax lands on the (border) index 0 ⇒ fall back.
    {
        std::vector<float> hm(size_t(W) * H, 0.5f);
        float nx = 0, ny = 0, score = 0;
        decodeDark(hm.data(), W, H, blur.data(), nx, ny, score);
        float ax = 0, ay = 0, as = 0;
        decodeArgmax(hm.data(), W, H, ax, ay, as);
        CHECK("flat: DARK == argmax (nx)", nx == ax);
        CHECK("flat: DARK == argmax (ny)", ny == ay);
        CHECK("flat: score == raw value (0.5)", score == 0.5f);
    }

    // 4) argmax rule sanity: fractional peak biases toward the higher neighbour by
    //    exactly ±0.25 cell (the frozen legacy behaviour DARK must preserve on fallback).
    {
        auto hm = gaussianMap(10.6, 20.4, 1.5);   // argmax cell (11, 20)
        float ax = 0, ay = 0, as = 0;
        decodeArgmax(hm.data(), W, H, ax, ay, as);
        const int mi = argmaxIdx(hm);
        const int px = mi % W, py = mi / W;
        // x: left neighbour higher (peak at 10.6 < 11) ⇒ -0.25; y: bottom higher ⇒ +0.25
        CHECK("argmax x == cell - 0.25", std::fabs(double(ax) * W - (px - 0.25)) < 1e-5);
        CHECK("argmax y == cell + 0.25", std::fabs(double(ay) * H - (py + 0.25)) < 1e-5);
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
