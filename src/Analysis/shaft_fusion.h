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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "det_rng.h"   // the plane bootstrap (uncertainty design principle 7)

// The shaft, in three dimensions, from the two image angles the face-on and the
// down-the-line trackers already publish (docs/design/shaft_fusion_design.md).
//
// THE FUSION IS TWO PLANES, NOT TWO LENGTHS. A published image angle says the shaft
// lies in the plane spanned by that camera's view ray and the image line. Two views
// give two planes and the shaft is where they cross:
//
//     n_F = d_F × (cos θ_F e_rF + sin θ_F e_dF)        n_D likewise
//     u   = ± (n_F × n_D) / |n_F × n_D|
//
// so the direction needs NEITHER projected length — and the face-on length is the
// weakest thing that tracker publishes (dtl_shaft_tracker_design.md §4.2). The
// lengths are then redundant, which is what makes them a CHECK and not an input.
// |n_F × n_D| is the conditioning: 0 where the two view planes coincide.
//
// THE FRAME IS THE CAMERAS', NOT THE GOLFER'S. X = face-on image-right, Z = up,
// Y = the face-on view ray (from the camera toward the golfer). The down-the-line
// camera looks along +X (image-right = −Y), yawed about Z and pitched down by the
// two config angles. Nothing here knows a handedness: a left-hander in the same
// cabin is the same two cameras, and only the READING of the plane's heading
// changes. For a right-hander +X is the target.
//
// ONE DIRECTION ONLY, again. This reads both trackers and feeds neither: the
// face-on tracker owes the DTL view nothing (dtl_shaft_tracker_design.md §5.10) and
// that stays true. What it gives back is a list of frames where the two disagree.
//
// WHAT IT DOES NOT CLAIM. The cameras are uncalibrated. The downswing plane's
// INCLINATION moved ≤ 1° over ±15° of yaw and 0–15° of pitch on the 07-04 session;
// its HEADING moves one-for-one with yaw. So the inclination is published as a
// measurement and the heading as a number that is only as good as `dtlYawDeg`.
//
// Pure header: no Qt, no OpenCV, no tracker types — standalone-testable. The caller
// selects the samples; this header never learns what a tracker is.

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

// A level orthographic camera: view ray, image-right, image-down (all unit, world).
struct Camera { Vec3 d, right, down; };

inline Camera faceOnCamera() { return { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }; }

// yaw > 0 turns the view ray from +X toward +Y — the camera standing on the BALL
// side of the hands line looking across at the golfer, which is where "behind the
// ball" puts it. pitch > 0 looks down. roll > 0 turns the image clockwise about the
// view ray (image-right dips toward image-down), as camera_pose_sticks.h measures
// it; 0 is the level camera every run before 2026-10-02 assumed.
inline Camera dtlCamera(double yawDeg, double pitchDeg, double rollDeg)
{
    const double p = pitchDeg * kPi / 180.0, a = yawDeg * kPi / 180.0, r = rollDeg * kPi / 180.0;
    const Vec3 d0 { 1, 0, 0 }, r0 { 0, -1, 0 }, dn0 { 0, 0, -1 };
    const Vec3 d1  = d0 * std::cos(p) + dn0 * std::sin(p);
    const Vec3 dn1 = dn0 * std::cos(p) - d0 * std::sin(p);
    const double c = std::cos(a), s = std::sin(a);
    auto rz = [c, s](const Vec3 &v) { return Vec3 { c * v.x - s * v.y, s * v.x + c * v.y, v.z }; };
    const Vec3 right = rz(r0), down = rz(dn1);
    // Roll about the axis: right → cos r·right + sin r·down, down → −sin r·right + cos r·down.
    return { rz(d1), right * std::cos(r) + down * std::sin(r), down * std::cos(r) - right * std::sin(r) };
}
inline Camera dtlCamera(double yawDeg, double pitchDeg) { return dtlCamera(yawDeg, pitchDeg, 0.0); }

inline Vec3 imageDir(const Camera &c, double theta)
{ return c.right * std::cos(theta) + c.down * std::sin(theta); }

// Projection of a unit direction: image angle (atan2, y down) and the projected
// length as a fraction of the true length.
inline void project(const Camera &c, const Vec3 &u, double &theta, double &rho)
{
    const double px = u.dot(c.right), py = u.dot(c.down);
    theta = std::atan2(py, px);
    rho   = std::hypot(px, py);
}

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

// One face-on angle sample, already unwrapped by the caller. `measured` false = the
// tracker coasted, predicted or synthesised it.
struct FoSample  { int64_t t_us = 0; double theta = 0; bool measured = false; double sigmaDeg = kNan; };
struct DtlSampleIn { int64_t t_us = 0; double theta = 0; int band = -1; double sigmaDeg = kNan; };

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

// ── one frame ────────────────────────────────────────────────────────────────
inline bool fuseOne(const Camera &fo, const Camera &dtl, double thetaF, double thetaD,
                    Vec3 &u, double &cond, bool &agree)
{
    const Vec3 imgF = imageDir(fo, thetaF), imgD = imageDir(dtl, thetaD);
    const Vec3 c = fo.d.cross(imgF).unit().cross(dtl.d.cross(imgD).unit());
    cond = c.norm();
    if (cond < 1e-9) { agree = false; return false; }
    u = c * (1.0 / cond);
    // The view that sees more of the shaft decides which way along the line; the
    // other one votes, and a lost vote is a finding, not a tie-break.
    const double sf = u.dot(imgF), sd = u.dot(imgD);
    if ((std::fabs(sf) >= std::fabs(sd) ? sf : sd) < 0) u = u * -1.0;
    agree = u.dot(imgF) > 0 && u.dot(imgD) > 0;
    return true;
}

// ── plane through the origin: the normal is the scatter matrix's smallest
// eigenvector (cyclic Jacobi on a symmetric 3×3 — a dozen sweeps is exact here) ──
inline Vec3 smallestEigenvector(double a[3][3])
{
    double v[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (int sweep = 0; sweep < 24; ++sweep) {
        double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
        if (off < 1e-15) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-300) continue;
                const double th = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
                const double c = std::cos(th), s = std::sin(th);
                for (int k = 0; k < 3; ++k) {
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq; a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk; a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq; v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    int m = 0;
    for (int i = 1; i < 3; ++i) if (a[i][i] < a[m][m]) m = i;
    return Vec3 { v[0][m], v[1][m], v[2][m] }.unit();
}

inline PlaneFit fitPlane(const std::vector<Vec3> &U, const Camera &fo, const Config &cfg)
{
    PlaneFit f;
    f.n = int(U.size());
    if (f.n < cfg.minPlaneN) return f;
    double a[3][3] = {};
    for (const Vec3 &u : U) {
        const double c[3] = { u.x, u.y, u.z };
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) a[i][j] += c[i] * c[j];
    }
    Vec3 n = smallestEigenvector(a);
    if (n.z < 0) n = n * -1.0;
    std::vector<double> off;
    double ss = 0;
    for (const Vec3 &u : U) {
        const double d = std::asin(std::clamp(u.dot(n), -1.0, 1.0)) * 180.0 / kPi;
        off.push_back(std::fabs(d));
        ss += d * d;
    }
    std::sort(off.begin(), off.end());
    f.fitted    = true;
    f.normal    = n;
    f.inclDeg   = std::acos(std::clamp(std::fabs(n.z), 0.0, 1.0)) * 180.0 / kPi;
    f.azimDeg   = std::atan2(n.y, n.x) * 180.0 / kPi;
    f.oopRmsDeg = std::sqrt(ss / double(U.size()));
    f.oopP90Deg = off[std::min(off.size() - 1, size_t(0.9 * double(off.size())))];
    // As the face-on image sees it: a unit circle on the plane images as an ellipse whose
    // minor/major ratio is |n · d_F|, with its major axis along the node line n × d_F.
    f.foRatio = std::fabs(n.dot(fo.d));
    const Vec3 node = n.cross(fo.d);
    if (node.norm() > 1e-9) {
        double deg = std::atan2(node.dot(fo.down), node.dot(fo.right)) * 180.0 / kPi;
        while (deg < 0)      deg += 180.0;
        while (deg >= 180.0) deg -= 180.0;
        f.foNodeDeg = deg;
    }
    return f;
}

// ── de-projection: one camera's image angle through a plane ──────────────────
// u ⊥ n_plane and u ⊥ the view-plane normal, headed along the image direction. `cond`
// is |n_plane × n_view|: 0 where the view plane IS the plane — a camera on the plane's
// node line (down the line, at impact) has no direction to give. false when < minCond.
inline bool deproject(const Camera &c, double theta, const Vec3 &nPlane, double minCond, Vec3 &u, double &cond)
{
    const Vec3 img = imageDir(c, theta);
    const Vec3 nV  = c.d.cross(img).unit();
    const Vec3 x   = nPlane.cross(nV);
    cond = x.norm();
    if (cond < minCond) return false;
    u = x * (1.0 / cond);
    if (u.dot(img) < 0) u = u * -1.0;
    return true;
}

// (A) The same, with the direction sitting η off the plane, on the side the sign says:
// u(φ) = cos φ·u0 + sin φ·w with w = n_view × u0 (in the view plane, ⊥ u0), so that
// u·n = sin φ·(w·n) and w·n = cond ⇒ sin φ = sin η / cond. |sin η| > cond has no such
// direction in the view plane and the frame is refused. η = 0 is `deproject` bit for bit.
inline bool deprojectEta(const Camera &c, double theta, const Vec3 &nPlane, double etaDeg,
                         double minCond, Vec3 &u, double &cond)
{
    if (!deproject(c, theta, nPlane, minCond, u, cond)) return false;
    if (!std::isfinite(etaDeg) || etaDeg == 0.0) return true;
    const Vec3 img = imageDir(c, theta);
    const Vec3 nV  = c.d.cross(img).unit();
    const Vec3 w   = nV.cross(u);
    const double wn = w.dot(nPlane);
    if (std::fabs(wn) < 1e-12) return true;
    const double sn = std::sin(etaDeg * kPi / 180.0) / wn;
    if (std::fabs(sn) > 1.0) return false;
    const double phi = std::asin(sn);
    Vec3 v = u * std::cos(phi) + w * std::sin(phi);
    if (v.dot(img) < 0) v = v * -1.0;
    u = v;
    return true;
}

// (A) One smooth curve through (t, oop) pairs — knot values v_k on a uniform grid every
// knotUs from tLo to ≥ tHi minimising
//     Σ_i (η(t_i) − oop_i)²  +  λ Σ_k (v_{k−1} − 2v_k + v_{k+1})²  +  Σ_k μ_k v_k²
// with η(t) the piecewise-linear interpolant in the data term (the evaluation is
// Catmull-Rom through the same values, C¹, equal at the knots) and μ_k = priorFar on a
// knot with no frame within priorReach, priorNear otherwise. One dense Cholesky solve
// (K ≈ 40–60). Mirrored in tools/shaftlab/fusion_geom.py (fit_eta) so a grader can
// re-fit leave-one-band-out from a run root; the two agree at the knots to < 1e-6°.
inline EtaFit fitEtaCurve(const std::vector<int64_t> &t, const std::vector<double> &oop,
                          int64_t tLo, int64_t tHi, const Config::Eta &e)
{
    EtaFit f;
    if (t.size() < 4 || t.size() != oop.size() || tHi <= tLo || e.knotMs <= 0) return f;
    const int64_t knotUs = int64_t(std::llround(e.knotMs * 1000.0));
    const int K = std::max(2, int(std::ceil(double(tHi - tLo) / double(knotUs))) + 1);
    f.knotsUs.resize(size_t(K));
    for (int k = 0; k < K; ++k) f.knotsUs[size_t(k)] = tLo + int64_t(k) * knotUs;
    std::vector<double> A(size_t(K) * size_t(K), 0.0), b(size_t(K), 0.0);
    auto at = [&](int i, int j) -> double & { return A[size_t(i) * size_t(K) + size_t(j)]; };
    const double h = double(knotUs);
    for (size_t i = 0; i < t.size(); ++i) {
        int k = int(std::floor(double(t[i] - f.knotsUs[0]) / h));
        k = std::max(0, std::min(K - 2, k));
        double a = double(t[i] - f.knotsUs[size_t(k)]) / h;
        a = std::max(0.0, std::min(1.0, a));
        const double c0 = 1.0 - a, c1 = a;
        at(k, k) += c0 * c0; at(k, k + 1) += c0 * c1;
        at(k + 1, k) += c1 * c0; at(k + 1, k + 1) += c1 * c1;
        b[size_t(k)] += c0 * oop[i]; b[size_t(k) + 1] += c1 * oop[i];
    }
    for (int k = 1; k < K - 1; ++k) {
        const int idx[3] = { k - 1, k, k + 1 }; const double co[3] = { 1.0, -2.0, 1.0 };
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) at(idx[i], idx[j]) += e.lambda * co[i] * co[j];
    }
    const int64_t reach = int64_t(std::llround(e.priorReachMs * 1000.0));
    f.near.assign(size_t(K), 0);
    for (int k = 0; k < K; ++k) {
        for (int64_t ti : t) if (std::llabs(ti - f.knotsUs[size_t(k)]) <= reach) { f.near[size_t(k)] = 1; break; }
        at(k, k) += f.near[size_t(k)] ? e.priorNear : e.priorFar;
    }
    // Cholesky A = L Lᵀ (A is SPD: a Gram matrix plus positive diagonal terms).
    std::vector<double> L(size_t(K) * size_t(K), 0.0);
    auto Lat = [&](int i, int j) -> double & { return L[size_t(i) * size_t(K) + size_t(j)]; };
    for (int i = 0; i < K; ++i)
        for (int j = 0; j <= i; ++j) {
            double sum = at(i, j);
            for (int m = 0; m < j; ++m) sum -= Lat(i, m) * Lat(j, m);
            if (i == j) { if (sum <= 0) return f; Lat(i, i) = std::sqrt(sum); }
            else Lat(i, j) = sum / Lat(j, j);
        }
    std::vector<double> y(static_cast<size_t>(K));
    for (int i = 0; i < K; ++i) {
        double sum = b[size_t(i)];
        for (int m = 0; m < i; ++m) sum -= Lat(i, m) * y[size_t(m)];
        y[size_t(i)] = sum / Lat(i, i);
    }
    f.values.assign(size_t(K), 0.0);
    for (int i = K - 1; i >= 0; --i) {
        double sum = y[size_t(i)];
        for (int m = i + 1; m < K; ++m) sum -= Lat(m, i) * f.values[size_t(m)];
        f.values[size_t(i)] = sum / Lat(i, i);
    }
    for (double &v : f.values) v = std::max(-e.maxAbsDeg, std::min(e.maxAbsDeg, v));
    f.fitted = true;
    f.n = int(t.size());
    double ss = 0;
    for (size_t i = 0; i < t.size(); ++i) { const double r = f.at(t[i]) - oop[i]; ss += r * r; }
    f.rmsDeg = std::sqrt(ss / double(t.size()));
    return f;
}

// ── the track ────────────────────────────────────────────────────────────────
// `fo` ascending in time with θ UNWRAPPED; `foBridge` the same for the track the
// face-on tracker draws where it did not measure (its synth; may be empty). A DTL
// frame with a measured face-on bracket is fused against it; otherwise against the
// bridge and marked Bridged, and a Bridged sample never enters a plane fit. Windows
// are [from, to] instants; from ≥ to disables that fit.
inline Track3D fuseTracks(const std::vector<FoSample> &fo, const std::vector<FoSample> &foBridge,
                          const std::vector<DtlSampleIn> &dtl,
                          int64_t backFromUs, int64_t topUs, int64_t downToUs, const Config &cfg,
                          int64_t addressToUs = std::numeric_limits<int64_t>::min())
{
    Track3D out;
    out.dtlYawDeg = cfg.dtlYawDeg;
    out.dtlPitchDeg = cfg.dtlPitchDeg;
    out.dtlRollDeg = cfg.dtlRollDeg;
    for (int k = 0; k < 3; ++k) out.dtlOffsetM[k] = cfg.dtlOffsetM[k];
    out.calibrated = cfg.calibrated;
    if (!cfg.enabled || fo.size() < 2 || dtl.empty()) return out;
    const Camera cf = faceOnCamera(), cd = dtlCamera(cfg.dtlYawDeg, cfg.dtlPitchDeg, cfg.dtlRollDeg);

    auto at = [&cfg](const std::vector<FoSample> &v, int64_t t, bool needMeasured, double &theta) {
        auto hi = std::lower_bound(v.begin(), v.end(), t,
                                   [](const FoSample &s, int64_t tt) { return s.t_us < tt; });
        if (hi == v.begin() || hi == v.end()) return false;
        const FoSample &b = *hi, &a = *(hi - 1);
        if (needMeasured && !(a.measured && b.measured)) return false;
        if (double(b.t_us - a.t_us) > cfg.maxGapUs || b.t_us <= a.t_us) return false;
        const double w = double(t - a.t_us) / double(b.t_us - a.t_us);
        theta = a.theta + w * (b.theta - a.theta);
        return true;
    };

    out.nDtlPublished = int(dtl.size());
    {
        std::vector<double> incl;
        for (const DtlSampleIn &d : dtl) {
            if (d.t_us > addressToUs) continue;
            const Vec3 n = cd.d.cross(imageDir(cd, d.theta)).unit();
            incl.push_back(std::acos(std::clamp(std::fabs(n.z), 0.0, 1.0)) * 180.0 / kPi);
        }
        out.addressN = int(incl.size());
        if (out.addressN >= cfg.minAddressN) {
            std::sort(incl.begin(), incl.end());
            out.addressInclDeg = incl[incl.size() / 2];
            if (cfg.uncertainty) {
                std::vector<double> dev;
                for (double v : incl) dev.push_back(std::fabs(v - out.addressInclDeg));
                std::sort(dev.begin(), dev.end());
                const double n = double(incl.size()), r = std::clamp(cfg.rho, 0.0, 0.95);
                const double nEff = std::max(1.0, n * (1.0 - r) / (1.0 + r));
                // the median's standard error ≈ 1.2533 σ/√n_eff, σ from the scaled MAD
                out.addressInclSigmaDeg = std::max(cfg.cameraFloorDeg,
                                                   1.2533 * 1.4826 * dev[dev.size() / 2] / std::sqrt(nEff));
            }
        }
    }
    for (const DtlSampleIn &d : dtl) {
        Sample3D s;
        if (at(fo, d.t_us, true, s.thetaF))             s.foSrc = FoSource::Measured;
        else if (at(foBridge, d.t_us, false, s.thetaF)) s.foSrc = FoSource::Bridged;
        else { ++out.nNoFaceOn; continue; }
        bool agree = false;
        if (!fuseOne(cf, cd, s.thetaF, d.theta, s.u, s.cond, agree)) { ++out.nIllConditioned; continue; }
        s.t_us = d.t_us;
        s.thetaD = d.theta;
        s.dtlBand = d.band;
        if (!agree)               { s.flags |= SignDisagree;   ++out.nSignDisagree; }
        if (s.cond < cfg.minCond) { s.flags |= IllConditioned; ++out.nIllConditioned; }
        if (s.foSrc == FoSource::Bridged) ++out.nBridged;
        double th;
        project(cf, s.u, th, s.rhoF);
        project(cd, s.u, th, s.rhoD);
        // Direction σ (deg): perturb each input by its σ and take the angular change, in
        // quadrature. Grows as 1/cond where the two view planes close up.
        if (cfg.uncertainty) {
            double fSig = kNan;
            {
                auto hi = std::lower_bound(fo.begin(), fo.end(), d.t_us,
                                           [](const FoSample &x, int64_t tt) { return x.t_us < tt; });
                if (hi != fo.end() && std::isfinite(hi->sigmaDeg)) fSig = hi->sigmaDeg;
                if (hi != fo.begin() && std::isfinite((hi - 1)->sigmaDeg))
                    fSig = std::isfinite(fSig) ? std::max(fSig, (hi - 1)->sigmaDeg) : (hi - 1)->sigmaDeg;
                if (s.foSrc == FoSource::Bridged) {
                    auto hb = std::lower_bound(foBridge.begin(), foBridge.end(), d.t_us,
                                               [](const FoSample &x, int64_t tt) { return x.t_us < tt; });
                    if (hb != foBridge.end() && std::isfinite(hb->sigmaDeg)) fSig = hb->sigmaDeg;
                }
            }
            if (std::isfinite(fSig) && std::isfinite(d.sigmaDeg)) {
                const auto turn = [&](double dF, double dD) {
                    Vec3 u2; double c2; bool ag;
                    if (!fuseOne(cf, cd, s.thetaF + dF, d.theta + dD, u2, c2, ag)) return kNan;
                    return std::acos(std::clamp(std::fabs(u2.dot(s.u)), -1.0, 1.0)) * 180.0 / kPi;
                };
                const double eF = turn(fSig * kPi / 180.0, 0.0), eD = turn(0.0, d.sigmaDeg * kPi / 180.0);
                if (std::isfinite(eF) && std::isfinite(eD)) s.sigmaDeg = std::sqrt(eF * eF + eD * eD);
            }
        }
        out.samples.push_back(s);
    }
    out.valid = !out.samples.empty();
    if (!out.valid) return out;

    // A sample enters a fit when it is face-on measured and carries no DISAGREEMENT flag;
    // (D)'s Reflected is a reading, not a disagreement, and the fits are what it is for.
    auto fitWindow = [&](int64_t from, int64_t to) {
        std::vector<Vec3> U;
        if (to > from)
            for (const Sample3D &s : out.samples)
                if (s.t_us >= from && s.t_us <= to && (s.flags & ~uint8_t(Reflected)) == 0 && s.foSrc == FoSource::Measured)
                    U.push_back(s.u);
        return fitPlane(U, cf, cfg);
    };

    // ── (D) a mirrored backswing band, re-read ─────────────────────────────────
    // Only when the backswing fit is incoherent. Every non-address backswing band is a
    // candidate; the address band never is (the DTL ball anchors it, and reflecting it
    // is the mirror image of reflecting all the others). Greedy: the band whose
    // reflection lowers the refit's rms most is taken, while the gain exceeds
    // reflectMinGainDeg and the band's own sign disagreements do not rise; the whole set
    // is kept only if the final fit is coherent, else every sample is put back exactly.
    if (cfg.reflectBands && topUs > backFromUs) {
        const PlaneFit b0 = fitWindow(backFromUs, topUs - 1);
        if (b0.fitted && b0.oopRmsDeg > cfg.backIncoherentDeg) {
            out.backRmsBeforeReflectDeg = b0.oopRmsDeg;
            const std::vector<Sample3D> original = out.samples;
            std::vector<int> cand, addr;
            for (const Sample3D &s : out.samples) {
                if (s.dtlBand < 0) continue;
                if (s.t_us <= addressToUs) { addr.push_back(s.dtlBand); continue; }
                if (s.t_us >= backFromUs && s.t_us < topUs) cand.push_back(s.dtlBand);
            }
            std::sort(cand.begin(), cand.end()); cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
            cand.erase(std::remove_if(cand.begin(), cand.end(), [&](int b) {
                return std::find(addr.begin(), addr.end(), b) != addr.end(); }), cand.end());
            auto signDis = [&](int band) {
                int n = 0;
                for (const Sample3D &s : out.samples) if (s.dtlBand == band && (s.flags & SignDisagree)) ++n;
                return n;
            };
            // Reflect a band in place (about the DTL image's vertical: θ → π − θ) and re-fuse.
            auto reflect = [&](int band) {
                for (Sample3D &s : out.samples) {
                    if (s.dtlBand != band || s.t_us < backFromUs || s.t_us >= topUs) continue;
                    const double th = kPi - s.thetaD;
                    Vec3 u; double cond; bool agree = false;
                    if (!fuseOne(cf, cd, s.thetaF, th, u, cond, agree)) continue;
                    s.thetaD = th; s.u = u; s.cond = cond;
                    s.flags &= uint8_t(~(SignDisagree | IllConditioned));
                    if (!agree)            s.flags |= SignDisagree;
                    if (cond < cfg.minCond) s.flags |= IllConditioned;
                    s.flags ^= Reflected;
                    double t; project(cf, s.u, t, s.rhoF); project(cd, s.u, t, s.rhoD);
                }
            };
            // Reflecting one band or reflecting all the others gives the same scatter and
            // MIRROR-IMAGE planes; only one of the two faces the way the swing does. The
            // downswing plane, fitted from frames no reflection touches, says which: a refit
            // whose normal points away from it (n · n_down < 0) is the mirror, and refused.
            // (On 07-04 s2 the face-on track starts after the address instant, so the
            // address band's fused frames all lie past it and the time rule alone let the
            // greedy pick that band first — the 82.7° → 54.8° plane with a 137° heading.)
            const PlaneFit downRef = fitWindow(topUs, downToUs);
            auto facesTheSwing = [&](const PlaneFit &pf) {
                return !downRef.fitted || pf.normal.dot(downRef.normal) > 0;
            };
            std::vector<int> kept;
            double best = b0.oopRmsDeg;
            for (;;) {
                int bestBand = -1; double bestRms = best;
                for (int b : cand) {
                    if (std::find(kept.begin(), kept.end(), b) != kept.end()) continue;
                    const int disBefore = signDis(b);
                    reflect(b);
                    const PlaneFit pf = fitWindow(backFromUs, topUs - 1);
                    const bool better = pf.fitted && pf.oopRmsDeg < bestRms - cfg.reflectMinGainDeg && signDis(b) <= disBefore
                                     && facesTheSwing(pf);
                    reflect(b);   // undo the trial
                    if (better) { bestBand = b; bestRms = pf.oopRmsDeg; }
                }
                if (bestBand < 0) break;
                reflect(bestBand);
                kept.push_back(bestBand);
                best = bestRms;
            }
            if (!kept.empty() && best < cfg.backIncoherentDeg && facesTheSwing(fitWindow(backFromUs, topUs - 1))) {
                out.reflectedBands = kept;
                int dis = 0, ill = 0, dis0 = 0, ill0 = 0;
                for (const Sample3D &s : out.samples) { dis += (s.flags & SignDisagree) != 0; ill += (s.flags & IllConditioned) != 0; }
                for (const Sample3D &s : original)    { dis0 += (s.flags & SignDisagree) != 0; ill0 += (s.flags & IllConditioned) != 0; }
                out.nSignDisagree += dis - dis0;
                out.nIllConditioned += ill - ill0;
            } else {
                out.samples = original;
            }
        }
    }

    out.back = fitWindow(backFromUs, topUs - 1);
    out.down = fitWindow(topUs, downToUs);
    // Inclination σ by a block bootstrap of the window's own frames (uncertainty §4.7).
    if (cfg.uncertainty) {
        DetRng rng(cfg.seed);
        const auto boot = [&](int64_t from, int64_t to, PlaneFit &pf) {
            if (!pf.fitted) return;
            std::vector<Vec3> U;
            for (const Sample3D &s : out.samples)
                if (s.t_us >= from && s.t_us <= to && (s.flags & ~uint8_t(Reflected)) == 0 && s.foSrc == FoSource::Measured)
                    U.push_back(s.u);
            const int B = std::max(1, cfg.bootstrapBlock), n = int(U.size());
            if (n < cfg.minPlaneN) return;
            std::vector<double> inc;
            for (int b = 0; b < cfg.bootstrapN; ++b) {
                std::vector<Vec3> R;
                while (int(R.size()) < n) {
                    const int s0 = int(rng.below(uint64_t(std::max(1, n - B + 1))));
                    for (int k = 0; k < B && int(R.size()) < n; ++k) R.push_back(U[size_t(std::min(n - 1, s0 + k))]);
                }
                const PlaneFit f = fitPlane(R, cf, cfg);
                if (f.fitted) inc.push_back(f.inclDeg);
            }
            if (inc.size() < 10) return;
            double m = 0.0; for (double v : inc) m += v; m /= double(inc.size());
            double v2 = 0.0; for (double v : inc) v2 += (v - m) * (v - m);
            const double sd = std::sqrt(v2 / double(inc.size() - 1));
            pf.inclSigmaDeg = std::sqrt(sd * sd + cfg.cameraFloorDeg * cfg.cameraFloorDeg);
        };
        boot(backFromUs, topUs - 1, out.back);
        boot(topUs, downToUs, out.down);
    }
    out.backIncoherent = out.back.fitted && out.back.oopRmsDeg > cfg.backIncoherentDeg;
    if (out.down.offered(cfg) && std::isfinite(out.addressInclDeg))
        out.deliveryVsAddressDeg = out.down.inclDeg - out.addressInclDeg;

    // Distance off the phase's own plane — for EVERY sample in the window, the Bridged
    // ones included: that is where a face-on bridge through impact gets checked against
    // the view that sees impact sharply.
    for (Sample3D &s : out.samples) {
        const PlaneFit *pf = nullptr;
        if (s.t_us >= topUs && s.t_us <= downToUs)          pf = &out.down;
        else if (s.t_us >= backFromUs && s.t_us < topUs)    pf = &out.back;
        if (!pf || !pf->fitted) continue;
        s.oopDeg = std::asin(std::clamp(s.u.dot(pf->normal), -1.0, 1.0)) * 180.0 / kPi;
        // Only the downswing is asked to be planar. The backswing is not a plane (the
        // takeaway and the lift are two), so its scatter is a swing-level finding
        // (backIncoherent), never a per-frame one.
        if (pf == &out.down
            && std::fabs(s.oopDeg) > std::max(cfg.offPlaneK * pf->oopRmsDeg, cfg.offPlaneFloorDeg)) {
            s.flags |= OffPlane;
            ++out.nOffPlane;
        }
    }

    auto phasePlane = [&](int64_t t) -> const PlaneFit * {
        if (t >= topUs && t <= downToUs)       return out.down.fitted ? &out.down : nullptr;
        if (t >= backFromUs && t < topUs)      return out.back.fitted ? &out.back : nullptr;
        return nullptr;
    };

    // ── (A) the out-of-plane curve η(t) ────────────────────────────────────────
    // Fitted on the frames the plane fits themselves rest on (face-on measured,
    // unflagged, with a phase plane), over the fusion's window. Recorded on every
    // sample inside the window; read by the DTL synth and by (C), by nothing else.
    if (cfg.eta.enabled && downToUs > backFromUs) {
        std::vector<int64_t> t; std::vector<double> oop;
        for (const Sample3D &s : out.samples)
            if (s.foSrc == FoSource::Measured && s.flags == 0 && std::isfinite(s.oopDeg)) {
                t.push_back(s.t_us); oop.push_back(s.oopDeg);
            }
        // The grid runs one prior-reach past the window so the curve decays to in-plane
        // over that reach after impact (the held-plane frames the synth still draws)
        // instead of holding its last fitted value.
        const int64_t reach = int64_t(std::llround(cfg.eta.priorReachMs * 1000.0));
        out.eta = fitEtaCurve(t, oop, backFromUs, downToUs + reach, cfg.eta);
        if (out.eta.fitted)
            for (Sample3D &s : out.samples)
                if (s.t_us >= backFromUs && s.t_us <= downToUs) s.etaDeg = out.eta.at(s.t_us);
    }

    // ── (C) the DTL-anchored direction where face-on coasted ───────────────────
    // A bridged frame's direction becomes the DTL view plane's intersection with the
    // phase plane (η(t) off it when (A) fitted); the bridged value is kept beside it.
    // Refused, with the conditioning on the record, where the DTL view plane and the
    // phase plane are within asin(minCond) of each other — the down-the-line camera
    // looks along the plane's node, so at impact that is the rule, not the exception.
    if (cfg.dtlAnchor) {
        for (Sample3D &s : out.samples) {
            if (s.foSrc != FoSource::Bridged) continue;
            const PlaneFit *pf = phasePlane(s.t_us);
            if (!pf) continue;
            Vec3 ua; double cond = 0;
            const bool ok = deprojectEta(cd, s.thetaD, pf->normal, s.etaDeg, cfg.minCond, ua, cond);
            s.anchorCond = cond;
            if (!ok) { ++out.nDtlAnchorRefused; continue; }
            s.uBridged = s.u;
            s.u = ua;
            s.flags |= DtlAnchored;
            if (s.flags & OffPlane) { s.flags &= uint8_t(~OffPlane); --out.nOffPlane; }
            double th; project(cf, s.u, th, s.rhoF); project(cd, s.u, th, s.rhoD);
            s.oopDeg = std::asin(std::clamp(s.u.dot(pf->normal), -1.0, 1.0)) * 180.0 / kPi;
            ++out.nDtlAnchored;
        }
    }
    return out;
}

} // namespace pinpoint::analysis::fusion
