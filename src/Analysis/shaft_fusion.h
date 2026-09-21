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
// ball" puts it. pitch > 0 looks down.
inline Camera dtlCamera(double yawDeg, double pitchDeg)
{
    const double p = pitchDeg * kPi / 180.0, a = yawDeg * kPi / 180.0;
    const Vec3 d0 { 1, 0, 0 }, r0 { 0, -1, 0 }, dn0 { 0, 0, -1 };
    const Vec3 d1  = d0 * std::cos(p) + dn0 * std::sin(p);
    const Vec3 dn1 = dn0 * std::cos(p) - d0 * std::sin(p);
    const double c = std::cos(a), s = std::sin(a);
    auto rz = [c, s](const Vec3 &v) { return Vec3 { c * v.x - s * v.y, s * v.x + c * v.y, v.z }; };
    return { rz(d1), rz(r0), rz(dn1) };
}

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
    double minCond     = 0.26;   // view planes within 15° of each other ⇒ not a direction
    double maxGapUs    = 12000;  // face-on bracket wider than this is not a bracket
    int    minPlaneN   = 8;      // fewer joint frames than this is not a plane
    double planeOopMaxDeg  = 5.0;   // a "plane" scattered wider than this is not offered downstream
    double offPlaneK       = 3.0;   // |out-of-plane| > max(K·rms, floor) ⇒ the frame is flagged
    double offPlaneFloorDeg = 6.0;
    double backIncoherentDeg = 12.0; // backswing scatter above this ⇒ suspect a mirrored DTL band
};

// One face-on angle sample, already unwrapped by the caller. `measured` false = the
// tracker coasted, predicted or synthesised it.
struct FoSample  { int64_t t_us = 0; double theta = 0; bool measured = false; };
struct DtlSampleIn { int64_t t_us = 0; double theta = 0; int band = -1; };

enum class FoSource : uint8_t { Measured = 0, Bridged = 1 };

enum SampleFlag : uint8_t {
    SignDisagree   = 0x01,   // the two views disagree on which way along the line the head is
    IllConditioned = 0x02,   // |n_F × n_D| < minCond
    OffPlane       = 0x04,   // far off its phase's fitted plane (set after the fit)
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
    bool offered(const Config &c) const { return fitted && oopRmsDeg <= c.planeOopMaxDeg; }
};

struct Track3D {
    bool valid = false;
    std::vector<Sample3D> samples;
    PlaneFit back, down;
    int nDtlPublished = 0, nNoFaceOn = 0, nBridged = 0;
    int nSignDisagree = 0, nIllConditioned = 0, nOffPlane = 0;
    bool backIncoherent = false;
    double dtlYawDeg = 0, dtlPitchDeg = 0;
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

// ── the track ────────────────────────────────────────────────────────────────
// `fo` ascending in time with θ UNWRAPPED; `foBridge` the same for the track the
// face-on tracker draws where it did not measure (its synth; may be empty). A DTL
// frame with a measured face-on bracket is fused against it; otherwise against the
// bridge and marked Bridged, and a Bridged sample never enters a plane fit. Windows
// are [from, to] instants; from ≥ to disables that fit.
inline Track3D fuseTracks(const std::vector<FoSample> &fo, const std::vector<FoSample> &foBridge,
                          const std::vector<DtlSampleIn> &dtl,
                          int64_t backFromUs, int64_t topUs, int64_t downToUs, const Config &cfg)
{
    Track3D out;
    out.dtlYawDeg = cfg.dtlYawDeg;
    out.dtlPitchDeg = cfg.dtlPitchDeg;
    if (!cfg.enabled || fo.size() < 2 || dtl.empty()) return out;
    const Camera cf = faceOnCamera(), cd = dtlCamera(cfg.dtlYawDeg, cfg.dtlPitchDeg);

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
        out.samples.push_back(s);
    }
    out.valid = !out.samples.empty();
    if (!out.valid) return out;

    auto fitWindow = [&](int64_t from, int64_t to) {
        std::vector<Vec3> U;
        if (to > from)
            for (const Sample3D &s : out.samples)
                if (s.t_us >= from && s.t_us <= to && s.flags == 0 && s.foSrc == FoSource::Measured)
                    U.push_back(s.u);
        return fitPlane(U, cf, cfg);
    };
    out.back = fitWindow(backFromUs, topUs - 1);
    out.down = fitWindow(topUs, downToUs);
    out.backIncoherent = out.back.fitted && out.back.oopRmsDeg > cfg.backIncoherentDeg;

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
    return out;
}

} // namespace pinpoint::analysis::fusion
