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

#include <opencv2/core.hpp>

#include <vector>

// Shaft-tracker v3.0-r1 EVIDENCE ENGINES — the hot core. Faithful C++ port of
// the validated Python exemplar tools/shaftlab/{stripe_fusion,stripe_annotate}.py
// (E1 discrete retro-band match + E2 polarity-aware radial ridge sweep), which
// club_track_v3.py imports UNCHANGED. Pure math: no Qt, core OpenCV only (no
// ximgproc/contrib). Standalone-testable in src/Analysis/tests. The deciding
// half (phase model, constraints, DP, ψ-reconcile, tiering) is
// shaft_track_assembly.*; the window/decode orchestration is shaft_tracker.*.
//
// Porting invariants (docs/design/club_track_v3_exemplar_explained.md §15):
//  * sampleRays is NEAREST-NEIGHBOUR integer-clamp (matches Python _sample) —
//    NOT bilinear remap. The bg reduction is a median of exactly 4 samples.
//  * percentile normalisation (50th/97th) is load-bearing — never min/max/mean.
//  * evidence engines must port numerically-identical to stripe_fusion.

namespace pinpoint::analysis {

// ── E2: polarity-aware radial ridge sweep (stripe_fusion.ridge_sweep) ────────
struct RidgeConfig {
    float rStep    = 2.0f;    // radial sample step (px)
    float rLo      = 8.0f;    // first radius (px)
    float rHi      = 470.0f;  // last radius (px, exclusive)
    float bgHi     = 200.0f;  // background above this ⇒ shaft must be DARK
    float eClipNeg = 30.0f;   // per-sample evidence clamp (low)
    float eClipPos = 90.0f;   // per-sample evidence clamp (high)
    float minLenPx = 90.0f;   // shortest credible visible shaft (foreshortened)
};

struct RidgeResult {
    std::vector<float> score;    // per-θ sqrt-normalised cumulative ridge score
    std::vector<float> rEnd;     // per-θ accepted terminus radius (px)
    std::vector<float> support;  // per-θ fraction of supported samples (e>8)
};

// Sweep many rays about (gx,gy). img: CV_32F gray, or |gray - scene_med| when
// brightOnly. thetasRad: ray directions (radians). One (score,rEnd,support)
// triple per θ. brightOnly uses a mean-of-3 on-ridge sample; the default
// polarity-aware path credits the lateral max (bright line) or min (dark shaft
// over blown mat) per the bgHi split.
RidgeResult ridgeSweep(const cv::Mat& img, double gx, double gy,
                       const std::vector<float>& thetasRad,
                       const RidgeConfig& cfg, bool brightOnly = false);

// ── E1: discrete retro-band match (stripe_fusion.frame_band_match) ───────────
struct BandMatchConfig {
    int   satT     = 235;     // band pixels saturate above this
    int   areaMin  = 3;
    int   areaMax  = 2500;
    int   maxBlobs = 20;
    float gripGate = 80.0f;   // line must pass within this of the grip (px)
    float latTol   = 4.0f;    // blob-to-line lateral inlier tolerance (px)
    float sMin     = 0.05f;   // px/mm foreshortening bounds
    float sMax     = 0.55f;
    float r0Min    = -50.0f;  // butt→grip offset along shaft (mm)
    float r0Max    = 260.0f;
    float rms4     = 1.5f;    // gate for n==4
    float rms5     = 3.0f;    // gate for n>=5
    float gapMmMax = 60.0f;   // "within-group" adjacent band spacing (mm)
    int   gapDark  = 222;     // bare steel between group bands must dip below
};

struct BandMatch {
    bool  ok  = false;
    int   n   = 0;      // matched band count
    float rms = 0.f;    // fit RMS
    float s   = 0.f;    // px/mm foreshortening scale
    float r0  = 0.f;    // butt→grip-anchor offset (mm)
    float thetaDeg = 0.f;   // grip→head direction, 0..360
    float mbx = 0.f, mby = 0.f;   // matched-blob centroid (px)
};

// Per-frame band match. gray: CV_8UC1. bandsMm: retro-band centres from the
// club record (mm); empty ⇒ ok=false (untaped club, E1 disabled). rmax: blob
// radius gate (px). ok=false when fewer than 4 bands match within the gates.
BandMatch frameBandMatch(const cv::Mat& gray, double gx, double gy, double rmax,
                         const std::vector<double>& bandsMm,
                         const BandMatchConfig& cfg);

} // namespace pinpoint::analysis
