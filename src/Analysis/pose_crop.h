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

// Swing-level person-crop geometry (wholebody_pose_design.md §3.2) plus the
// pose-accuracy tunable config (crop + DARK decode). PoseRunner owns the crop:
// one fixed rect per swing, aspect-locked to the estimator input (192:256 = 3:4
// W:H), so a plain cv::Mat ROI + the existing resize is equivalent to a
// warpAffine, and back-projection through the (constant) affine is exact.
//
// The geometry is a pure function so it is unit-tested without OpenCV/ORT.

#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <optional>

#include "../Core/pp_tuned_constants.h"
#include "analysis_tuning.h"

namespace pinpoint::analysis {

// Crop knobs. Defaults track the frozen constants (pp_tuned_constants.h pose::);
// SwingLab sweeps them via "pose.crop.*" dotted keys.
struct PoseCropConfig {
    bool   enabled       = tuned::pose::crop::kEnabled;        // pose.crop.enabled
    double marginFrac    = tuned::pose::crop::kMarginFrac;     // pose.crop.marginFrac
    double maxAreaFrac   = tuned::pose::crop::kMaxAreaFrac;    // pose.crop.maxAreaFrac
    int    minBboxFrames = tuned::pose::crop::kMinBboxFrames;  // pose.crop.minBboxFrames

    static PoseCropConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        PoseCropConfig c;
        apply(ov, "pose.crop.enabled",       c.enabled);
        apply(ov, "pose.crop.marginFrac",    c.marginFrac);
        apply(ov, "pose.crop.maxAreaFrac",   c.maxAreaFrac);
        apply(ov, "pose.crop.minBboxFrames", c.minBboxFrames);
        return c;
    }
};

// Combined pose-accuracy config resolved from the tuning map, mirroring the way
// ShaftV3Config::fromOverrides reaches ShaftTracker: PoseRunner builds it from
// the job's tuningOverrides and applies it to the estimator + crop pass.
struct PoseAccuracyConfig {
    PoseCropConfig crop;
    bool           decodeDark = tuned::pose::decode::kDark;    // pose.decode.dark

    static PoseAccuracyConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        PoseAccuracyConfig c;
        c.crop = PoseCropConfig::fromOverrides(ov);
        apply(ov, "pose.decode.dark", c.decodeDark);
        return c;
    }
};

// Integer-pixel crop rect (a cv::Rect without the OpenCV dependency).
struct PoseCropRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Pure geometry: union body bbox (normalized [0,1]) → 15 % margin each side →
// grow the deficient dimension to the estimator's 3:4 W:H aspect → clamp to
// frame → integer pixels. Returns nullopt (⇒ full-frame fallback) when the crop
// is disabled, too few frames contributed a bbox, the bbox is empty/invalid, or
// the final crop covers ≥ maxAreaFrac of the frame (no resolution gain).
inline std::optional<PoseCropRect>
computePoseCropRect(double nx0, double ny0, double nx1, double ny1,
                    int contributingFrames, int frameW, int frameH,
                    const PoseCropConfig &cfg)
{
    if (!cfg.enabled)
        return std::nullopt;
    if (contributingFrames < cfg.minBboxFrames)
        return std::nullopt;
    if (frameW <= 0 || frameH <= 0)
        return std::nullopt;
    if (!(nx1 > nx0) || !(ny1 > ny0))
        return std::nullopt;
    if (!std::isfinite(nx0) || !std::isfinite(ny0) || !std::isfinite(nx1) || !std::isfinite(ny1))
        return std::nullopt;

    // Normalized bbox → pixels.
    double px0 = nx0 * frameW, py0 = ny0 * frameH;
    double px1 = nx1 * frameW, py1 = ny1 * frameH;

    // 15 % margin on each side (raised-arm / grip headroom).
    const double mx = cfg.marginFrac * (px1 - px0);
    const double my = cfg.marginFrac * (py1 - py0);
    px0 -= mx; px1 += mx;
    py0 -= my; py1 += my;

    // Grow the deficient dimension to the 192:256 = 3:4 W:H input aspect, centred.
    const double target = 192.0 / 256.0;   // width / height
    double bw = px1 - px0, bh = py1 - py0;
    const double cx = 0.5 * (px0 + px1), cy = 0.5 * (py0 + py1);
    if (bw / bh < target)
        bw = target * bh;                   // too tall/narrow ⇒ widen
    else
        bh = bw / target;                   // too wide ⇒ heighten
    px0 = cx - 0.5 * bw; px1 = cx + 0.5 * bw;
    py0 = cy - 0.5 * bh; py1 = cy + 0.5 * bh;

    // Clamp to frame, round to integer pixels (outer floor/ceil so the crop never
    // clips the margined bbox on the inside).
    const int ix0 = int(std::floor(std::max(0.0, px0)));
    const int iy0 = int(std::floor(std::max(0.0, py0)));
    const int ix1 = int(std::ceil(std::min(double(frameW), px1)));
    const int iy1 = int(std::ceil(std::min(double(frameH), py1)));
    const int w = ix1 - ix0, h = iy1 - iy0;
    if (w <= 0 || h <= 0)
        return std::nullopt;
    if (double(w) * double(h) >= cfg.maxAreaFrac * double(frameW) * double(frameH))
        return std::nullopt;

    return PoseCropRect{ ix0, iy0, w, h };
}

} // namespace pinpoint::analysis
