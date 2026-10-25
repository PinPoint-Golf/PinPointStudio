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

// A SHORT GAP IN A RATE CURVE, BRIDGED — the one implementation of the chest's probable peak
// (docs/design/kinematic_sequence_design.md §12.6; docs/design/charts_panel_traces_relations.md §2.2.1).
// The analysis reads it per swing (segment_rates.cpp, with the kernel the corpus fitted); the Charts
// Session view reads it for swings analysed before the analysis did (session_bridge.h, with a kernel
// fitted to the session).
//
// THE GAP. The face-on chest route stops measuring for 3–6 frames (20–40 ms) at or just before impact,
// where the shoulder line squares to that camera; on 24 of 32 shots of 07-04 and 10-20 the curve rises
// into the gap and falls out of it, so the peak is in the gap. Mark, 25 Oct 2026: the gap "is relatively
// short and I would argue cannot plausibly spike during the swing … bridge the gap and use that as an
// estimator (with a warning to the user)".
//
// THE ESTIMATOR: a Gaussian process (Matérn 5/2, constant mean, white noise) through the measured samples
// beforeMs before and afterMs after the gap. Chosen over a cubic bridge and a parabola (25 Oct prototype:
// the same times to 1–4 ms, but only the GP's ± was honest, and only it said "still rising" where a curve
// dips into the gap). The per-sample noise is ASSUMED (the curves are already smoothed: a fitted noise
// reads ~1 °/s and a ±1–3 ms that is false precision). The peak is the median of `draws` posterior
// samples' maxima over the gap ± searchMarginMs (clipped at P8 — the KS rule is "the max before P8"),
// ± half their 16–84 % spread. Most draws at the right edge = still rising; at the left = it fell into the
// gap and its top is before it: no probable peak then. A taller MEASURED value earlier in the downswing
// (Top to the search window) is recorded, never taken as the peak (Mark's option A, 25 Oct).
//
// DETERMINISTIC: draws from mt19937_64 seeded by the caller, normals by Box–Muller on its raw output
// (std::normal_distribution differs between standard libraries), so the Mac and the cabin PC agree.
//
// Pure C++ (std only); times are ms from impact, + after.

#include <cstdint>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

// The analysis' seed: one fixed seed, so a swing re-analysed draws the same estimate (its data differs per swing).
inline constexpr uint64_t kGapBridgeSeed = 0x9E3779B97F4A7C15ULL;

struct GapBridgeKernel {
    double ellMs    = 45.0;     // Matérn length
    double sfDps    = 190.0;    // Matérn height
    double noiseDps = 20.0;     // assumed per-sample noise
};

struct GapBridgeOptions {
    double beforeMs       = 110.0;   // measured samples this far before the gap train the fit…
    double afterMs        = 80.0;    // …and this far after it
    double searchMarginMs = 40.0;    // the peak is looked for this far either side of the gap
    double gridStepMs     = 1.0;
    int    draws          = 600;
    // No limit by default (Mark, 25 Oct 2026: "I want the chest timing to always be reported so long as we warn
    // the user. better to indicate than say nothing"): a long gap is bridged and its ± says how little it knows.
    double maxGapMs       = std::numeric_limits<double>::infinity();
    int    minEachSide    = 3;       // measured samples needed on each side
    double nearFromMs     = -80.0;   // the gap must reach into [nearFromMs, nearToMs] about impact
    double nearToMs       = 40.0;
    double edgeShare      = 0.5;     // draws at a search edge above this: no probable peak
};

enum class GapBridgeOutcome {
    NoGap,           // nothing to bridge near impact
    GapTooLong,      // longer than maxGapMs
    TooLittleCurve,  // fewer than minEachSide measured samples on a side, or the fit failed
    StillRising,     // most draws peak at the search window's right edge: the peak is no earlier than there
    FellIntoGap,     // most draws peak at its left edge: the top is at or before there
    Bridged          // a probable peak inside the window
};

// The run of unmeasured samples nearest impact with a measured sample either side, reaching into
// [nearFromMs, nearToMs]: `fromMs` the last measured sample before it, `toMs` the first after.
struct ImpactGap {
    bool   found = false;
    double fromMs = std::numeric_limits<double>::quiet_NaN();
    double toMs   = std::numeric_limits<double>::quiet_NaN();
};
ImpactGap findImpactGap(const std::vector<double> &tMs, const std::vector<uint8_t> &measured,
                        const GapBridgeOptions &o = {});

// The training samples for a gap (measured, finite, within beforeMs / afterMs of it). False when either
// side has fewer than minEachSide. `noise` (per input sample, °/s) is carried into `nn` when both are given.
bool gapTrainingSet(const std::vector<double> &tMs, const std::vector<double> &v, const std::vector<uint8_t> &measured,
                    const ImpactGap &gap, const GapBridgeOptions &o, std::vector<double> *tt, std::vector<double> *vv,
                    const std::vector<double> *noise = nullptr, std::vector<double> *nn = nullptr);

// THE GAP IS ONLY WHAT DISAGREES (Mark, 25 Oct 2026). `candidate` samples — kept by the pose but near square to
// the camera, where the rate is read less surely — are admitted to `measured` from each edge of the gap inward,
// one at a time, while each agrees with the fit through the samples already admitted: within `k` of
// √(posterior sd² + its own noise²). The first that disagrees closes that side (a spike from a missed
// unfold, a keypoint jump). Clean near-square samples (07-04, 10-20 Mark) keep the gap to the frames the pose
// lost; junk ones (10-20 Nick, ±2000 °/s) stay out. `noise` per input sample (°/s), empty = the kernel's.
std::vector<uint8_t> admitAgreeing(const std::vector<double> &tMs, const std::vector<double> &v,
                                   const std::vector<uint8_t> &measured, const std::vector<uint8_t> &candidate,
                                   const std::vector<double> &noise, const GapBridgeKernel &kernel, double k,
                                   const GapBridgeOptions &o = {});

struct GapBridge {
    GapBridgeOutcome outcome = GapBridgeOutcome::NoGap;
    double fromMs = std::numeric_limits<double>::quiet_NaN(), toMs = std::numeric_limits<double>::quiet_NaN();
    double searchFromMs = std::numeric_limits<double>::quiet_NaN(), searchToMs = std::numeric_limits<double>::quiet_NaN();
    // the fit across the gap: posterior mean and sd every gridStepMs from fromMs to toMs (empty before the fit)
    std::vector<double> tMs, mean, sd;
    // the probable peak: inside the window (Bridged), or at the edge the draws crowd (StillRising, FellIntoGap)
    double peakMs = std::numeric_limits<double>::quiet_NaN(), peakSigmaMs = std::numeric_limits<double>::quiet_NaN();
    double peakValue = std::numeric_limits<double>::quiet_NaN(), peakSigmaValue = std::numeric_limits<double>::quiet_NaN();
    double edgeShare = std::numeric_limits<double>::quiet_NaN();
    // a taller measured value earlier in the downswing (topMs to the search window), NaN when none
    double earlierMs = std::numeric_limits<double>::quiet_NaN(), earlierValue = std::numeric_limits<double>::quiet_NaN();

    bool bridged() const { return outcome == GapBridgeOutcome::Bridged; }
    // a peak to report: inside the window, or at the edge the curve was still rising to / falling from
    bool hasPeak() const { return bridged() || outcome == GapBridgeOutcome::StillRising || outcome == GapBridgeOutcome::FellIntoGap; }
};

// One curve, the whole way: find the gap, train on its sides, draw, read the peak. `topMs` / `p8Ms` NaN
// when unknown (the earlier search then starts 400 ms before the window; no P8 clip).
// `noise` (per input sample, °/s) replaces the kernel's noise where given — a near-square sample is read
// less surely; empty = the kernel's everywhere.
GapBridge bridgeImpactGap(const std::vector<double> &tMs, const std::vector<double> &v, const std::vector<uint8_t> &measured,
                          double topMs, double p8Ms, const GapBridgeKernel &kernel, uint64_t seed,
                          const GapBridgeOptions &o = {}, const std::vector<double> &noise = {});

// THE FASTEST THE CAMERA CLEARLY SAW (sequence.chestIndicated): the highest measured value in the window the
// bridge searches — from `fromMs` (the caller passes nearFromMs − searchMarginMs, −120 ms) to P8 (`p8Ms`; NaN =
// the curve's end) — skipping any within `minAfterReversalMs` of the last value at or below zero before it (a
// trunk one window past its reversal is a keypoint spike, not its peak). OPTION A, as the bridge: a taller
// measured value earlier (from `topMs`, or the curve's start, up to the window) is the shoulder-keypoint artefact
// after the top, recorded in `earlier*`, never taken (the first cut searched from the top and took it on 25 of 37
// library shots, 923–2422 °/s). ±: half the run of measured samples within `noise` of the peak, at least
// `minSigmaMs` (the derivative window's own resolution), and `noise` on the value.
struct SeenPeak {
    bool   found = false;
    double tMs = std::numeric_limits<double>::quiet_NaN(), tSigmaMs = std::numeric_limits<double>::quiet_NaN();
    double value = std::numeric_limits<double>::quiet_NaN(), valueSigma = std::numeric_limits<double>::quiet_NaN();
    double earlierMs = std::numeric_limits<double>::quiet_NaN(), earlierValue = std::numeric_limits<double>::quiet_NaN();
};
SeenPeak seenPeak(const std::vector<double> &tMs, const std::vector<double> &v, const std::vector<uint8_t> &measured,
                  double topMs, double fromMs, double p8Ms, double minAfterReversalMs, double noise, double minSigmaMs);

// The kernel that best explains several curves at once (maximum marginal likelihood, Nelder–Mead in log
// space; noise held at `start.noiseDps`). `ok` false — and `start` returned — when the fit failed.
GapBridgeKernel fitGapBridgeKernel(const std::vector<std::vector<double>> &ts, const std::vector<std::vector<double>> &vs,
                                   const GapBridgeKernel &start, bool *ok = nullptr);

// The pieces, for the tests.
struct GpPeakDraws {
    std::vector<double> mean, sd;            // posterior over the grid
    std::vector<double> peakT, peakV;        // each draw's maximum
    std::vector<int>    peakIndex;
};
bool gpPeakDraws(const std::vector<double> &t, const std::vector<double> &v, const std::vector<double> &grid,
                 const GapBridgeKernel &kernel, int draws, uint64_t seed, GpPeakDraws *out,
                 const std::vector<double> *noise = nullptr);
double gpNegLogMarginal(const std::vector<std::vector<double>> &ts, const std::vector<std::vector<double>> &vs,
                        double ellMs, double sfDps, double noiseDps);

} // namespace pinpoint::analysis
