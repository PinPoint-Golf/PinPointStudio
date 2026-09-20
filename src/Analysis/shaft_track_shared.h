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

// The face-on tracker's view-INDEPENDENT half, named so a second view can call it
// (dtl_shaft_tracker_design.md §5.1: "sharing … the Viterbi and snap from
// shaft_track_assembly.* through the seams they already have"). Every body below
// still lives in shaft_track_assembly.cpp, unchanged — this header only lifts the
// declarations out of that file's anonymous namespaces so the DTL tracker links
// against the same code rather than a second copy of it. Nothing here is new
// behaviour; the one genuinely new entry point is viterbiBanded, which is the
// phase-free spelling of viterbiDP's loop (viterbiDP now calls it).
//
// The sub-namespace is deliberate. These are generic names — `percentile`,
// `unwrap`, `normScores` — and callers (shaft_evidence_test.cpp among them) have
// their own. Qualify them, or open the directive in a .cpp; never in a header.

#include <cstdint>
#include <cmath>
#include <vector>

#include <opencv2/core.hpp>

#include "shaft_track_assembly.h"   // DPResult / SnapConfig / RidgeConfig

namespace pinpoint::analysis::shaftshared {

// ── the decode-once frame-cache byte cap ─────────────────────────────────────
// Sized for a full 5 s / 150 fps / 1.3 MP coverage range (~1 GB gray); offline
// analysis is a one-at-a-time job, so the transient is acceptable. Over the cap
// both cache layers skip caching and fall back to the serial per-call decode,
// which is byte-identical to the pre-cache tracker.
//
// It lives HERE, in the view-independent half, because two layers consult it —
// shaft_frame_io.h's buildFrameCache and shaft_track_assembly.cpp's own cache in
// decideTrack — and they must reach the same verdict about the same span. Two
// copies of the number is how they stop doing that. (This header, not
// shaft_frame_io.h, so the assembly does not pull SwingWindow/decode in for a
// constant.)
inline constexpr size_t kFrameCacheCapBytes = 1200ull * 1024 * 1024;

// Wrap to (−180, 180]. The one body that lives HERE rather than in the .cpp: it
// is a single expression and every caller wants it inlined.
inline double circWrap(double a) { return std::fmod(std::fmod(a + 180.0, 360.0) + 360.0, 360.0) - 180.0; }

// scipy.ndimage.median_filter(x, size) — odd size, mode='reflect', origin 0.
std::vector<double> medianFilter1d(const std::vector<double>& x, int size);

// scipy.ndimage.gaussian_filter1d(x, sigma, truncate=4.0), mode='reflect'.
std::vector<double> gaussianFilter1d(const std::vector<double>& x, double sigma);

// np.unwrap over a radian sequence.
std::vector<double> unwrap(const std::vector<double>& p);

// np.percentile with linear interpolation.
float percentile(std::vector<float> v, double p);

// Python norm(): clip((s - p50)/(p97 - p50 + 1e-6), 0, 1).
std::vector<float> normScores(const std::vector<float>& s);

// np.interp fill of NaN entries using the frame index as the abscissa.
void interpFillNan(std::vector<double>& v);

// ── Layer A snap: local ridge line re-registration ───────────────────────────
// One sample's snap search over (⊥ offset d, Δθ). Grid: 1 px offset × 0.5° angle.
struct SnapResult {
    double offsetPx      = 0.0;   // best perpendicular offset from the original anchor (px)
    double dThetaDeg     = 0.0;   // best angular delta from the original θ (deg)
    float  bestLineConf  = 0.f;   // support under the winning line
    float  originLineConf = 0.f;  // support under the original (d=0,Δθ=0) line — recorded on reject
};
SnapResult snapSearch(const cv::Mat& g32, double gx, double gy, double theta0Rad,
                      double drawnLenPx, const SnapConfig& sc, const RidgeConfig& rc);

// ── banded Viterbi DP (club_track_v3 C3), phase-free ─────────────────────────
// viterbiDP's loop with the per-frame band expressed as data rather than as a
// SwingPhase lookup: wmaxBins[f] is the |Δbin| ceiling and sgn[f] the permitted
// direction (+1 increasing only, −1 decreasing only, 0 both). Face-on fills them
// from its phase model (wmaxFor/phaseSign, which stay private to the .cpp); DTL
// has no phase model of its own and fills them from the visibility schedule
// (design §5.8: ω_max(t) = ω_base / ρ̂_D(t), no phase-signed direction term).
// Bit-identical to viterbiDP for the face-on inputs — it IS viterbiDP's body.
//
// Contract, because the solver indexes all three without checking:
//   · emis is nf rows; the state count NS is taken from emis[0].size() and every
//     other row must be that long.
//   · wmaxBins and sgn must have size ≥ emis.size() — they are read at every
//     f in [1, nf), by index.
//   · emis empty, or emis[0] empty, returns an EMPTY DPResult. Both are degenerate
//     problems (no frames / no θ states) and the back-pointer walk would index an
//     empty row. Face-on cannot reach it — decideTrack returns before the DP when
//     nf < 2, and NS is lround(360/grid) with the shipped grid — so it changes no
//     face-on output; it is here for callers that build their own emission.
DPResult viterbiBanded(const std::vector<std::vector<float>>& emis,
                       const std::vector<int>& wmaxBins, const std::vector<int>& sgn,
                       double kSmooth, double gridDeg);

} // namespace pinpoint::analysis::shaftshared
