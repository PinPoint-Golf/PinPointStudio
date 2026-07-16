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

// Heatmap sub-pixel decode — the two modes PoseRunner selects between
// (wholebody_pose_design.md §3.3). Pure C++ (no Qt / no ORT / no OpenCV) so it
// stands alone in unit tests; PoseEstimatorViTPose is the only production caller.
//
//   Argmax : argmax + a fixed ±0.25-cell shift toward the higher neighbour —
//            the ORIGINAL decode, ported here verbatim so it is byte-identical.
//   Dark   : DARK distribution-aware refinement — Gaussian-modulate the channel,
//            then a Taylor/Hessian step on the log-heatmap at the argmax. Falls
//            back to the Argmax rule at a border peak or a degenerate (non
//            negative-definite log-)Hessian. The reported score is ALWAYS the
//            raw (pre-blur) peak — score semantics are unchanged.
//
// Both write the peak normalised to [0,1] in input space: nx = hx / W, ny = hy / H
// (dividing the fractional cell index by the cell count — the original
// convention, preserved for parity).

#include <algorithm>
#include <cmath>

namespace pinpoint::pose {

enum class DecodeMode { Argmax = 0, Dark = 1 };

// Argmax + ±0.25 sub-pixel shift. VERBATIM port of the original inline decode —
// must stay byte-identical (the WB1 flags-off parity gate depends on it).
inline void decodeArgmax(const float *hm, int W, int H,
                         float &nx, float &ny, float &score)
{
    int   maxIdx = 0;
    float maxVal = hm[0];
    for (int i = 1; i < H * W; ++i) {
        if (hm[i] > maxVal) { maxVal = hm[i]; maxIdx = i; }
    }
    const int iHx = maxIdx % W;
    const int iHy = maxIdx / W;
    float hx = static_cast<float>(iHx);
    float hy = static_cast<float>(iHy);

    if (iHx > 0 && iHx < W - 1)
        hx += (hm[iHy * W + iHx + 1] > hm[iHy * W + iHx - 1]) ? 0.25f : -0.25f;
    if (iHy > 0 && iHy < H - 1)
        hy += (hm[(iHy + 1) * W + iHx] > hm[(iHy - 1) * W + iHx]) ? 0.25f : -0.25f;

    nx    = hx / W;
    ny    = hy / H;
    score = maxVal;
}

// 3×3 binomial (Gaussian) blur, border-replicate, into `dst` (size W*H). Does NOT
// mutate `src` — the DARK path must leave the ORT output untouched.
inline void gaussianBlur3x3(const float *src, int W, int H, float *dst)
{
    auto at = [&](int x, int y) -> float {
        x = x < 0 ? 0 : (x >= W ? W - 1 : x);
        y = y < 0 ? 0 : (y >= H ? H - 1 : y);
        return src[y * W + x];
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float s = at(x - 1, y - 1) + 2.f * at(x, y - 1) + at(x + 1, y - 1)
                          + 2.f * at(x - 1, y) + 4.f * at(x, y) + 2.f * at(x + 1, y)
                          + at(x - 1, y + 1) + 2.f * at(x, y + 1) + at(x + 1, y + 1);
            dst[y * W + x] = s * (1.f / 16.f);
        }
    }
}

// DARK refinement. `blur` is caller-owned scratch of size W*H (avoids a per-call
// allocation in the hot 133-channel loop). score is the RAW peak value.
inline void decodeDark(const float *hm, int W, int H, float *blur,
                       float &nx, float &ny, float &score)
{
    int   maxIdx = 0;
    float maxVal = hm[0];
    for (int i = 1; i < H * W; ++i) {
        if (hm[i] > maxVal) { maxVal = hm[i]; maxIdx = i; }
    }
    const int px = maxIdx % W;
    const int py = maxIdx / W;

    // Border peak ⇒ no 3×3 neighbourhood ⇒ fall back (also sets score = raw peak).
    if (px <= 0 || px >= W - 1 || py <= 0 || py >= H - 1) {
        decodeArgmax(hm, W, H, nx, ny, score);
        return;
    }

    // Modulate, then renormalise so the blurred peak equals the raw peak (keeps
    // the log-values on the raw scale; the score itself stays the raw peak).
    gaussianBlur3x3(hm, W, H, blur);
    float bmax = blur[0];
    for (int i = 1; i < H * W; ++i)
        if (blur[i] > bmax) bmax = blur[i];
    if (bmax > 0.f) {
        const float s = maxVal / bmax;
        for (int i = 0; i < H * W; ++i) blur[i] *= s;
    }

    auto L = [&](int x, int y) -> double {
        const float v = blur[y * W + x];
        return std::log(v < 1e-10f ? 1e-10 : double(v));
    };
    const double c   = L(px, py);
    const double lx1 = L(px + 1, py), lx0 = L(px - 1, py);
    const double ly1 = L(px, py + 1), ly0 = L(px, py - 1);
    const double dx  = 0.5 * (lx1 - lx0);
    const double dy  = 0.5 * (ly1 - ly0);
    const double dxx = lx1 - 2.0 * c + lx0;
    const double dyy = ly1 - 2.0 * c + ly0;
    const double dxy = 0.25 * (L(px + 1, py + 1) - L(px + 1, py - 1)
                             - L(px - 1, py + 1) + L(px - 1, py - 1));
    const double det = dxx * dyy - dxy * dxy;

    // A well-formed peak ⇒ log-Hessian negative definite (dxx<0, dyy<0, det>0).
    // Degenerate / non-negative-definite ⇒ fall back to the ±0.25 rule.
    if (!(dxx < 0.0 && dyy < 0.0 && det > 1e-12)) {
        decodeArgmax(hm, W, H, nx, ny, score);
        return;
    }

    // offset = -H⁻¹∇, clamped to one cell each axis.
    double offx = -(dyy * dx - dxy * dy) / det;
    double offy = -(-dxy * dx + dxx * dy) / det;
    offx = std::clamp(offx, -1.0, 1.0);
    offy = std::clamp(offy, -1.0, 1.0);

    nx    = float((double(px) + offx) / W);
    ny    = float((double(py) + offy) / H);
    score = maxVal;
}

} // namespace pinpoint::pose
