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

// The 3-D synthetic shaft, projected into the down-the-line tile
// (docs/design/dtl_continuous_track_design_update.md §3.2).
//
// WHY THIS AND NOT A CURVE IN DTL PIXELS. The DTL image angle θ_D passes through
// a pole at P2, P4 and P6 — the projected length goes to zero and the angle flips
// sign as the shaft crosses the camera's axis — so there is no smooth function of
// time to fit in that image. What IS smooth is the shaft's 3-D direction, and the
// face-on Layer C synth (shaft_synthesis.h) already carries a C¹ face-on angle
// θ_F through every gap. A face-on angle confines the shaft to the plane through
// the face-on view ray and the image line; the swing's fitted plane (shaft_fusion.h,
// one per phase) supplies the second constraint; the direction is where the two
// planes cross — the same intersection fusion does with two CAMERAS, done here
// with one camera and a plane. Projected through the DTL camera it is a DTL line.
//
// WHAT IT CLAIMS, AND WHAT IT DOES NOT. Every sample is SYNTHESISED: never read
// by fusion, the kinematic sequence or any metric (they read tier ≥ RAY on the
// measured track and nothing here); drawn dimmer on the tile; and never claimed
// where the DTL measured. Its heading is only as good as the DTL camera it was
// projected through: with `calibrated` false the camera is the assumed-zero
// placement and the line is a PREVIEW — off by exactly the unknown yaw, which the
// alignment stick says was 4–9° on the corpus. The protocol session's stick clips
// (camera_pose_sticks.h) are what make it honest. Dark by default until then.
//
// THE ANCHOR. The DTL grip: the tracker's own anchor on frames where it was not
// quarantined; where it was (the hands hidden behind the body at the top and
// through the finish), the 3-D skeleton's hands projected into the DTL view
// (skeleton3d::projectPoint). The caller resolves that and hands in pixels.
//
// THE PLANES. Address (from the DTL view alone, the median view-plane of the
// published address frames), backswing and downswing (fusion's fits); after the
// downswing window the downswing plane is held for a bounded time and flagged
// extrapolated. Where no plane is fitted for the instant, no sample.
//
// Pure std + shaft_fusion.h. Orthographic like the fusion it stands on: the
// camera's offset does not enter (it would for a perspective projection, and it
// is carried on the record for that day).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "shaft_fusion.h"

namespace pinpoint::analysis::synth3d {

using fusion::Camera;
using fusion::Vec3;
using fusion::kNan;
using fusion::kPi;

struct Config {
    bool    enabled = false;         // DARK until the protocol session exists (§3.2)
    int64_t holdDownPlaneUs = 250000; // past the downswing window, hold its plane this long (flagged)
    double  minCond = 0.15;          // |n_plane × n_F| below this: the face-on line runs along the plane's node — no direction
    int64_t maxFoGapUs = 12000;      // a face-on bracket wider than this is not a bracket
};

struct FoAngle { int64_t t_us = 0; double theta = 0; };   // face-on synth angle (rad), any wrap

enum class PlaneUsed : uint8_t { None = 0, Address = 1, Back = 2, Down = 3, DownExtrapolated = 4 };

struct Planes {
    bool addr = false, back = false, down = false;
    Vec3 nAddr, nBack, nDown;        // unit normals, cameras' frame
    int64_t addrToUs = 0, topUs = 0, downToUs = 0;
};

enum class AnchorSrc : uint8_t { None = 0, Tracker = 1, Skeleton = 2 };

struct Anchor {
    int64_t   t_us = 0;
    bool      ok = false;
    double    gx = kNan, gy = kNan;  // DTL px
    AnchorSrc src = AnchorSrc::None;
};

struct Sample {
    int64_t   t_us = 0;
    bool      ok = false;
    double    thetaF = kNan;         // the face-on angle used (rad)
    double    thetaD = kNan;         // the projected DTL angle (rad, atan2, y down)
    double    rhoD = kNan;           // projected length fraction
    Vec3      u;                     // butt → head, unit, cameras' frame
    double    cond = 0;              // |n_plane × n_F|
    PlaneUsed plane = PlaneUsed::None;
    AnchorSrc anchor = AnchorSrc::None;
    double    gx = kNan, gy = kNan, lenPx = kNan;   // the drawn line: grip and ρ_D·L̂_D
};

// The address plane's normal from the DTL view alone: the median of the DTL
// view-plane normals over the published address frames (shaft_fusion.h's
// addressInclDeg is the inclination of exactly this plane).
inline bool addressPlaneNormal(const Camera &cd, const std::vector<double> &thetasD, Vec3 &n, int minN = 5)
{
    if (int(thetasD.size()) < minN) return false;
    std::vector<double> xs, ys, zs;
    for (double th : thetasD) {
        Vec3 v = cd.d.cross(fusion::imageDir(cd, th)).unit();
        if (v.z < 0) v = v * -1.0;
        xs.push_back(v.x); ys.push_back(v.y); zs.push_back(v.z);
    }
    auto med = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    n = Vec3 { med(xs), med(ys), med(zs) };
    if (n.norm() < 1e-9) return false;
    n = n.unit();
    return true;
}

// De-project one face-on angle through a plane: u ⊥ n_plane and u ⊥ n_F, headed
// along the face-on image direction. false when ill-conditioned.
inline bool deproject(const Camera &cf, double thetaF, const Vec3 &nPlane, double minCond, Vec3 &u, double &cond)
{
    const Vec3 img = fusion::imageDir(cf, thetaF);
    const Vec3 nF  = cf.d.cross(img).unit();
    const Vec3 c   = nPlane.cross(nF);
    cond = c.norm();
    if (cond < minCond) return false;
    u = c * (1.0 / cond);
    if (u.dot(img) < 0) u = u * -1.0;
    return true;
}

inline std::vector<Sample> synthesize(const std::vector<FoAngle> &fo, const Planes &p,
                                      const Camera &cf, const Camera &cd,
                                      const std::vector<Anchor> &anchors, double lFullPx,
                                      const Config &cfg)
{
    std::vector<Sample> out;
    if (!cfg.enabled || fo.size() < 2) return out;
    out.reserve(anchors.size());
    auto foAt = [&](int64_t t, double &theta) {
        auto hi = std::lower_bound(fo.begin(), fo.end(), t,
                                   [](const FoAngle &s, int64_t tt) { return s.t_us < tt; });
        if (hi == fo.begin() || hi == fo.end()) return false;
        const FoAngle &b = *hi, &a = *(hi - 1);
        if (b.t_us <= a.t_us || double(b.t_us - a.t_us) > double(cfg.maxFoGapUs)) return false;
        double tb = b.theta;
        tb += 2.0 * kPi * std::round((a.theta - tb) / (2.0 * kPi));   // the short way round
        const double w = double(t - a.t_us) / double(b.t_us - a.t_us);
        theta = a.theta + w * (tb - a.theta);
        return true;
    };
    for (const Anchor &an : anchors) {
        Sample s;
        s.t_us = an.t_us;
        s.anchor = an.src;
        s.gx = an.gx; s.gy = an.gy;
        const Vec3 *n = nullptr;
        if (an.t_us <= p.addrToUs) {
            if (p.addr)      { n = &p.nAddr; s.plane = PlaneUsed::Address; }
            else if (p.back) { n = &p.nBack; s.plane = PlaneUsed::Back; }
        } else if (an.t_us < p.topUs) {
            if (p.back)      { n = &p.nBack; s.plane = PlaneUsed::Back; }
        } else if (an.t_us <= p.downToUs) {
            if (p.down)      { n = &p.nDown; s.plane = PlaneUsed::Down; }
        } else if (an.t_us <= p.downToUs + cfg.holdDownPlaneUs) {
            if (p.down)      { n = &p.nDown; s.plane = PlaneUsed::DownExtrapolated; }
        }
        if (!an.ok || !n || !foAt(an.t_us, s.thetaF)) { out.push_back(s); continue; }
        if (!deproject(cf, s.thetaF, *n, cfg.minCond, s.u, s.cond)) { out.push_back(s); continue; }
        double th, rho;
        fusion::project(cd, s.u, th, rho);
        s.thetaD = th;
        s.rhoD   = rho;
        if (std::isfinite(lFullPx) && lFullPx > 0) s.lenPx = rho * lFullPx;
        s.ok = true;
        out.push_back(s);
    }
    return out;
}

} // namespace pinpoint::analysis::synth3d
