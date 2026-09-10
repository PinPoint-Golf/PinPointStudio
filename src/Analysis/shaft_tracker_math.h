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

#include <cstdint>
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

// ── E4: steel-segment lock (docs/design/markerless_club_tracker_design.md §4.2)
//
// The bare shaft is its own ruler. Along the ray from the grip anchor the club
// presents, at known mm from the butt: the GRIP END (dark rubber → bright
// steel), the exposed STEEL, and the FERRULE/HOSEL (thin bright run → dark gap
// → wide chrome). Along-shaft positions survive tangential motion blur exactly
// as band spacings do, so two landmarks at known distances give the same (s, r0)
// the band matcher gives — with no 180° ambiguity, because the grip end is the
// one near the hands. Bands, when present, are extra landmarks, never required.
//
// rayProfile() is E2's own per-sample reduction (shared code — the two engines
// cannot drift) sampled at 1 px along ONE ray; segmentLock() parses it into
// runs, extracts the landmarks, fits and gates. Pure math, core OpenCV only.

struct RayProfile {
    std::vector<float>   r;       // radius (px) per sample
    std::vector<float>   e;       // E2 evidence: ±(on − bg) − 12, clipped [−eClipNeg, +eClipPos]
    std::vector<float>   on;      // on-ridge value (lateral max-of-5 bright / min-of-5 dark)
    std::vector<float>   bg;      // lateral background (median of 4 at ±9/±12 px)
    std::vector<float>   wide;    // mean of 4 lateral samples at ±5/±7 px (thin line vs blob)
    std::vector<uint8_t> bright;  // 1 = bright-line regime (bg ≤ bgHi), 0 = dark line on blown bg
    std::vector<uint8_t> inb;     // 1 = sample inside the image; off-frame samples carry zero evidence
};

// Sample r = rLo, rLo+rStep, … < rHi along direction thetaRad from (gx,gy) on a
// CV_32F image. With rStep = cfg.rStep and rLo/rHi = cfg's, e[] reproduces the
// per-sample evidence ridgeSweep(brightOnly=false) accumulates, exactly.
RayProfile rayProfile(const cv::Mat& img32, double gx, double gy, double thetaRad,
                      double rLo, double rHi, double rStep, const RidgeConfig& cfg);

// "shaft.seg.*" keys via ShaftV3Config::fromOverrides. enabled=false ⇒ the
// tracker never calls the engine — byte-identical output.
struct SegmentConfig {
    bool  enabled      = true;   // DEFAULT ON (P6 flip): the markerless stack, graded on the unmarked 6-iron 0909
    float rLo          = 4.0f;    // first radius (px)
    float eOn          = 30.0f;   // sample is BRIGHT (shaft) at e ≥ this
    float eOff         = 8.0f;    // sample is DARK at e ≤ this (E2's support threshold)
    float wideMin      = 30.0f;   // (diagnostic) lateral ±5/±7 lit by ≥ this over bg ⇒ the sample is a ribbon, not a hairline
    int   minLenPx     = 60;      // shortest credible steel run (px)
    int   maxHolePx    = 80;      // evidence-free samples bridged inside a run: bare steel drops out for 50–70 px
                                  // between lit stretches on real frames; support (below) bounds the total
    float onsetFrac    = 0.45f;   // the proximal landmark is searched within this fraction of the run
    float handsEndMm   = 180.f;   // butt → bottom of the trail hand for a standard grip (lead hand ~100 mm, trail ~80 mm);
                                  // the hands'-edge onset's millimetre. An athlete setting in P4 (measured once with a tape).
    float handsSigmaMm = 25.f;    // its σ (corpus: the hands' edge measured 127–181 mm from the butt on one swing)
    float holeBgTol    = 60.0f;   // a hole is bridged only if its background stays within this of the anchor's:
                                  // a steel dropout sits on the same background, a head's interior does not
    int   minDarkPx    = 3;       // dark samples required before the onset / after the terminus
    int   minBrightPx  = 5;       // a bright RUN this long after the ferrule gap is the hosel; a wide blob's rim is 1–3 px
    int   lookAheadPx  = 25;      // window after the terminus in which the hosel/head must appear
    float proxFrac     = 0.45f;   // onset must lie within this fraction of rmax
    float supportMin   = 0.60f;   // fraction of e > eOff over the run
    float refineAddrDeg = 5.0f;   // … except on address frames, where grip→ball is a far-end anchor 3° off the shaft
    float addrLenTol   = 0.15f;   // address is in-plane, so the ball length gates ±this (a crease at 60% dies)
    float refineDeg    = 1.0f;    // the probed direction is refined over ±this …
    float refineStep   = 0.5f;    // … in these steps; a 1° grid misses a 4 px line at 250 px
    float headBgFrac   = 0.5f;    // after the run, bg ≥ this × the run's on-level ⇒ a bright wide head follows (distal 2)
    float edgeMin      = 30.0f;   // evidence step at a landmark: mean of 4 px inside − 4 px outside
    float sMin         = 0.05f;   // px/mm foreshortening bounds (E1's)
    float sMax         = 0.55f;
    float r0Min        = 0.0f;    // butt→anchor offset (mm): the anchor sits inside the grip
    float r0Max        = 260.0f;
    float lenTol       = 0.20f;   // projected length s·(clubLenMm − r0) ≤ (1 + lenTol)·lenPriorPx …
    float lenMinFrac   = 0.40f;   // … and ≥ lenMinFrac·lenPriorPx: the prior is the in-plane (address) length,
                                  // a foreshortened mid-swing projection is legitimately much shorter
    float r0MinHands   = -60.0f;  // r0 floor for a hands'-edge onset: handsEndMm carries σ 25–40 mm, so the
                                  // implied anchor may sit "behind the butt" by that much and still be real
    float sTol         = 0.25f;   // |s − sPrior| ≤ sTol·sPrior (FULL mode only)
    float bandSat      = 235.0f;  // band plateau level on the run (bright regime)
    float rmsMax       = 3.0f;    // landmark-fit RMS gate when bands join (n ≥ 3)
    float ferruleMm    = 12.0f;   // ferrule length: a resolved ferrule puts the steel's end at hoselMm − this
    float hoselLenMm   = 40.0f;   // hosel length: a run that reaches the wide head ends at hoselMm + this
    float hoselTolMm   = 15.0f;   // distal landmark σ when the ferrule is not resolved
    float ferruleTolMm = 5.0f;    // … and when it is
    // Assembly-side (P2 wiring; unread by the engine): candidate directions per
    // frame, emission well depths, tier confidences, wrist-rail weights, fusion σ.
    bool  placeHead    = false;   // place the head from the terminus on SEG frames — OFF: measured 73 px vs hand truth
                                  // on the unmarked 6-iron (2026-09-09) against 34–48 px for the head pass it displaced
    bool  probeStill   = true;    // probe frames OUTSIDE the evidence span (address hold, held finish) along the nearest in-span DP direction
    int   maxCand      = 6;
    float well         = 6.0f;
    float wellTerminus = 4.0f;
    float conf         = 0.70f;
    float confTerminus = 0.62f;
    float wIso         = 6.0f;
    float wIsoTerminus = 3.0f;
    float sigFrac      = 0.35f;
};

// Club geometry (mm from the butt) from the athlete's club record.
struct SegmentGeom {
    double gripEndMm = 0.0;       // bottom of the grip = start of the exposed shaft (hoselFromButtMm − shaftLengthMm)
    double hoselMm   = 0.0;       // top of the hosel = end of the exposed shaft (hoselFromButtMm)
    double clubLenMm = 0.0;       // butt → sole (length consistency + head placement)
    std::vector<double> bandsMm;  // retro-band centres; empty = unmarked club
    bool valid() const { return hoselMm > gripEndMm + 100.0 && clubLenMm >= hoselMm; }
};

enum class SegmentMode : uint8_t { None = 0, Full = 1, Terminus = 2 };

struct SegmentLock {
    bool        ok       = false;
    SegmentMode mode     = SegmentMode::None;
    float thetaDeg = 0.f;   // the probed direction, grip→head, 0..360
    float s        = 0.f;   // px/mm (BandMatch semantics)
    float r0       = 0.f;   // butt→anchor offset (mm): point m mm from the butt is at r = s·(m − r0)
    float rG       = -1.f;  // onset radius (px); < 0 when unresolved (Terminus mode)
    float rF       = -1.f;  // terminus radius (px)
    int   n        = 0;     // landmarks in the fit: 2 ends + matched bands; 1 in Terminus mode
    float rms      = 0.f;   // landmark-fit RMS (px); 0 for n ≤ 2
    float support  = 0.f;   // fraction of e > eOff over the run
    int   distal   = 0;     // 0 unresolved · 1 ferrule (dark gap, then the hosel/head) · 2 ran into the bright head · 3 dark end
    float sigmaMm  = 0.f;   // σ of the distal landmark (ferruleTolMm / hoselTolMm)
    float mFmm     = 0.f;   // the millimetre the terminus refers to (hosel end, or hosel − ferrule when resolved)
    int   onset    = 0;     // 0 unresolved · 1 grip end (a visible dark grip precedes the run) · 2 hands' edge (the hands' bloom precedes it)
    int   stage    = 0;     // how far the probe got: 0 geom · 1 no run · 2 support · 3 off-frame · 4 no distal landmark
                            // · 5 distal edge · 6 no onset and no prior · 7 s/r0 gate · 8 length gate · 9 locked
    float runLenPx = 0.f;   // the steel run's length (px), for ranking refinements
};

// One ray. sPrior (px/mm, ≤ 0 = none) lets a frame whose onset is hidden in the
// hands' bloom still lock on its terminus (Terminus mode) and gates a FULL fit's
// scale; lenPriorPx (grip→head px, ≤ 0 = none) gates the fitted length.
SegmentLock segmentLock(const cv::Mat& img32, double gx, double gy, double thetaRad,
                        double rmax, const SegmentGeom& geom, const SegmentConfig& cfg,
                        const RidgeConfig& ridge, double sPrior = 0.0, double lenPriorPx = 0.0);

} // namespace pinpoint::analysis
