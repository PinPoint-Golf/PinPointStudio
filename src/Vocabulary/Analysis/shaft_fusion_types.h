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

// The shaft fusion's RESULT and its configuration: the fused 3-D track (Track3D) with its samples,
// phase planes and out-of-plane curve, and the Config it was made under. Plain value types the
// shot's analysis record holds (SwingAnalysis::shaft3d and shaft3dCfg, swing_analysis.h), so they
// sit in the vocabulary; the cameras and the fusion itself stay in Club/shaft_fusion.h, which
// includes this. Pure std. Moved out of shaft_fusion.h unchanged (refactor 7.3, A-11).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace pinpoint::analysis::fusion {

constexpr double kPi  = 3.14159265358979323846;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3 &o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3 &o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(double s) const { return { x * s, y * s, z * s }; }
    double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3 &o) const
    { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    double norm() const { return std::sqrt(dot(*this)); }
    Vec3 unit() const { const double n = norm(); return n > 0 ? (*this) * (1.0 / n) : *this; }
};


struct Config {
    bool   enabled     = true;
    double dtlYawDeg   = 0.0;
    double dtlPitchDeg = 0.0;
    // The DTL camera's roll and its centre relative to the face-on camera (fusion
    // frame, metres), and whether the four came from a MEASURED calibration
    // (camera_pose_sticks.h on the protocol's stick clips) rather than the
    // assumed-zero placement. The orthographic fusion reads yaw/pitch/roll; the
    // offset is carried for the perspective projection (dtl_shaft_synth3d.h) and
    // for the record. All zero + calibrated=false is every run before 2026-10-02.
    double dtlRollDeg  = 0.0;
    double dtlOffsetM[3] = { 0.0, 0.0, 0.0 };
    bool   calibrated  = false;
    double minCond     = 0.26;   // view planes within 15° of each other ⇒ not a direction
    double maxGapUs    = 12000;  // face-on bracket wider than this is not a bracket
    int    minPlaneN   = 8;      // fewer joint frames than this is not a plane
    double planeOopMaxDeg  = 5.0;   // a "plane" scattered wider than this is not offered downstream
    double offPlaneK       = 3.0;   // |out-of-plane| > max(K·rms, floor) ⇒ the frame is flagged
    double offPlaneFloorDeg = 6.0;
    double backIncoherentDeg = 12.0; // backswing scatter above this ⇒ suspect a mirrored DTL band
    int    minAddressN = 5;          // fewer published DTL address frames than this is not an address plane

    // ── dtl_continuous_track_design_update.md §3.2a: three items, each its own switch,
    // each bit-identical OFF (the code below runs only inside `if (cfg.x)` blocks). ──
    // (A) The out-of-plane angle η(t): one smooth curve per swing through the fused
    // frames' oopDeg, fitted on face-on-MEASURED, unflagged frames (bridged excluded);
    // knots every knotMs; a second-difference smoothness penalty λ; and a zero prior on
    // every knot with no measured frame within priorReachMs (weight priorFar) so a gap
    // coasts to in-plane instead of extrapolating a slope — the one-swing probe read
    // 6–10° WORSE with a slope across the top (priorReach 400 ms). Feeds the DTL synth
    // and the record; the plane fits and every metric never read it.
    // ON by Mark's decision (1 Oct 2026) after reading dtl_precalibration_20261003.md: the
    // leave-one-band-out gate fails on this golfer (η is not continuous across an end-on
    // gap), the curve's value is at the band edges, and only the tile can show that.
    struct Eta {
        bool   enabled      = true;
        double knotMs       = 30.0;
        double lambda       = 4.0;
        double priorReachMs = 150.0;
        double priorFar     = 1.0;
        double priorNear    = 1e-3;
        double maxAbsDeg    = 25.0;   // a knot beyond this is clamped: a plane 25° off is not "out of plane", it is another plane
    } eta;
    // (C) The DTL-anchored direction on a bridged frame: the DTL view plane's intersection
    // with the phase's fitted plane (rotated by η(t) when (A) is on). Published beside the
    // bridged value; kept out of every fit; REFUSED where the two planes are within
    // asin(minCond) of each other — which, with a camera on the plane's node line, is
    // every impact frame (the one-swing probe: cond 0.06–0.08).
    bool   dtlAnchor = false;
    // (D) A mirrored backswing band re-read inside the fusion: when the backswing fit is
    // incoherent, each non-address backswing band is tried reflected about the DTL
    // image's vertical (θ → π − θ: the corridor's two centres of tracker design §4.1 (c)
    // are atan2(s, ∓q), symmetric about the vertical, so that IS the corridor's centre
    // line) and kept, greedily, while the refit's rms falls by more than reflectMinGainDeg;
    // the set is kept only if the final rms is under backIncoherentDeg AND no reflected
    // band's sign-disagreement count rose. The DTL track is untouched. ON by its gate
    // (dtl_precalibration_20261003.md §5): 07-04 s2/s3 20.9°/18.0° → 12.0°/4.4° rms, headings
    // within 1° of the downswing plane's, no coherent swing of 24 changed.
    bool   reflectBands = true;
    double reflectMinGainDeg = 0.5;

    // Uncertainty (shaft_uncertainty_propagation_design.md §4.7). false ⇒ nothing below runs and
    // every output is bit-identical. With it: each fused frame's direction σ from the two inputs'
    // σθ (finite differences), the planes' inclination σ by a block bootstrap of their frames
    // (blocks of bootstrapBlock consecutive frames — the correlation correction), ⊕ the camera
    // floor, and the address plane's σ from its spread over √n_eff.
    bool     uncertainty    = false;
    int      bootstrapN     = 200;
    int      bootstrapBlock = 5;
    uint64_t seed           = 0x5eedc1ab5eedc1abull;
    double   cameraFloorDeg = 0.5;
    double   rho            = 0.6;
};


enum class FoSource : uint8_t { Measured = 0, Bridged = 1 };

enum SampleFlag : uint8_t {
    SignDisagree   = 0x01,   // the two views disagree on which way along the line the head is
    IllConditioned = 0x02,   // |n_F × n_D| < minCond
    OffPlane       = 0x04,   // far off its phase's fitted plane (set after the fit)
    DtlAnchored    = 0x08,   // (C) u is the DTL de-projection; uBridged holds the bridged value
    Reflected      = 0x10,   // (D) thetaD is the tracker's angle reflected about the vertical
};

struct Sample3D {
    int64_t  t_us = 0;
    Vec3     u;                       // butt → head, unit, camera frame
    double   cond = 0;
    FoSource foSrc = FoSource::Measured;
    uint8_t  flags = 0;
    int      dtlBand = -1;
    double   thetaF = 0, thetaD = 0;  // the two inputs, as used (θ_F interpolated)
    double   rhoF = kNan, rhoD = kNan;   // projected length fractions the direction PREDICTS
    double   oopDeg = kNan;           // signed distance off its phase plane; NaN = no plane / outside both
    double   etaDeg = kNan;           // (A) the fitted η(t) at this instant; NaN when no curve
    Vec3     uBridged;                // (C) the bridged direction this sample had before anchoring (DtlAnchored only)
    double   anchorCond = kNan;       // (C) |n_plane × n_viewD| on a bridged frame the anchor was tried on
    double   sigmaDeg = kNan;         // 1σ of u's direction (deg) propagated from σθ_F, σθ_D (uncertainty §4.7); NaN = not assessed
};

struct PlaneFit {
    bool   fitted = false;
    int    n = 0;
    Vec3   normal;                    // unit, z ≥ 0
    double inclDeg = kNan;            // plane against the ground; 90 = vertical
    double azimDeg = kNan;            // heading of the normal's ground projection (atan2(y, x))
    double oopRmsDeg = kNan, oopP90Deg = kNan;
    // The same plane as the face-on de-projection wants it (segment_rates.cpp
    // PlaneParams): minor/major ratio of the imaged circle, and the node line's bearing
    // in the face-on image's atan2 convention.
    double foRatio = kNan, foNodeDeg = kNan;
    // 1σ of inclDeg from a block bootstrap of the input frames ⊕ the camera floor
    // (uncertainty §4.7); NaN = not assessed. NOT the out-of-plane rms above, which is
    // the frames' scatter about the plane, not the uncertainty of the plane.
    double inclSigmaDeg = kNan;
    bool offered(const Config &c) const { return fitted && oopRmsDeg <= c.planeOopMaxDeg; }
};

// (A) The out-of-plane curve: knot values on a uniform grid, evaluated as a Catmull-Rom
// spline through them (C¹, constant beyond the ends). `near` says which knots had a
// measured frame within reach; the others rest on the zero prior.
struct EtaFit {
    bool   fitted = false;
    std::vector<int64_t> knotsUs;
    std::vector<double>  values;    // degrees
    std::vector<uint8_t> near;
    int    n = 0;                   // frames fitted
    double rmsDeg = kNan;           // residual over the fitted frames
    double at(int64_t t) const
    {
        const size_t K = knotsUs.size();
        if (!fitted || K < 2) return kNan;
        if (t <= knotsUs.front()) return values.front();
        if (t >= knotsUs.back())  return values.back();
        const double h = double(knotsUs[1] - knotsUs[0]);
        size_t k = size_t(std::floor(double(t - knotsUs[0]) / h));
        k = std::min(k, K - 2);
        const double s = double(t - knotsUs[k]) / h;
        const double p0 = k > 0 ? values[k - 1] : values[k];
        const double p1 = values[k], p2 = values[k + 1];
        const double p3 = k + 2 < K ? values[k + 2] : values[k + 1];
        return 0.5 * ((2 * p1) + (-p0 + p2) * s + (2 * p0 - 5 * p1 + 4 * p2 - p3) * s * s
                      + (-p0 + 3 * p1 - 3 * p2 + p3) * s * s * s);
    }
};

struct Track3D {
    bool valid = false;
    std::vector<Sample3D> samples;
    PlaneFit back, down;
    // §3.2a records. (A) the curve; (C) how many bridged frames were anchored and how
    // many refused for conditioning; (D) which DTL bands the backswing fit reads
    // reflected, and the backswing rms before the reflection.
    EtaFit eta;
    int    nDtlAnchored = 0, nDtlAnchorRefused = 0;
    std::vector<int> reflectedBands;
    double backRmsBeforeReflectDeg = kNan;
    int nDtlPublished = 0, nNoFaceOn = 0, nBridged = 0;
    int nSignDisagree = 0, nIllConditioned = 0, nOffPlane = 0;
    bool backIncoherent = false;
    double dtlYawDeg = 0, dtlPitchDeg = 0, dtlRollDeg = 0;
    double dtlOffsetM[3] = { 0, 0, 0 };
    bool   calibrated = false;
    // The ADDRESS shaft plane — the plane through the target line's parallel and the shaft at
    // address — as its inclination to the ground. Read from the DTL view alone: a DTL image angle
    // confines the shaft to a plane through that camera's view ray, and with the camera looking
    // down the line THAT PLANE IS the address plane. It owes face-on nothing, which matters
    // because face-on coasts at address (the club is still) and would leave a dozen frames where
    // the DTL tracker has ninety. Median over the DTL frames published up to `addressToUs`.
    double addressInclDeg = kNan;
    int    addressN = 0;
    double addressInclSigmaDeg = kNan;   // 1σ of addressInclDeg (scaled MAD / √n_eff); NaN = not assessed
    // Delivery plane against the address plane: + = delivered STEEPER (above the address plane).
    // NaN unless both exist and the downswing plane was offered.
    double deliveryVsAddressDeg = kNan;
};

} // namespace pinpoint::analysis::fusion
