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

// Standalone tests for the two-view shaft fusion (src/Analysis/shaft_fusion.h).
// ONE synthetic swing: a backswing on a 50° plane and a downswing on a 60° plane,
// imaged by the two cameras, then handed back as the two angle tracks the trackers
// publish. Pure std, no fixture.
//
//   cmake --build build/analyzer-tests --target shaft_fusion_test
//   ctest --test-dir build/analyzer-tests -R shaft_fusion_test --output-on-failure

#include "../shaft_fusion.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace pinpoint::analysis::fusion;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A plane inclined `inclDeg` to the ground whose normal heads −Y (tilted toward the
// ball): e1 along the target line, e2 up the plane. u(α) = cos α e1 + sin α e2.
struct Plane {
    Vec3 e1, e2, n;
    explicit Plane(double inclDeg)
    {
        const double i = inclDeg * kPi / 180.0;
        e1 = { 1, 0, 0 };
        e2 = { 0, std::cos(i), std::sin(i) };
        n  = e1.cross(e2).unit();
    }
    Vec3 at(double alphaDeg) const
    {
        const double a = alphaDeg * kPi / 180.0;
        return e1 * std::cos(a) + e2 * std::sin(a);
    }
};

struct Swing {
    std::vector<FoSample>    fo, bridge;
    std::vector<DtlSampleIn> dtl;
    std::vector<Vec3>        truth;       // per dtl sample
    int64_t backFrom = 0, top = 0, downTo = 0;
};

// Face-on at 5 ms from t=0; DTL offset by 3.2 ms (the streams are not frame-synchronous,
// so every fused sample is an INTERPOLATION of face-on). α sweeps slowly enough that
// linear interpolation of θ_F is exact to ~1e-3°.
static Swing makeSwing(double yawDeg, double pitchDeg)
{
    Swing s;
    const Camera cf = faceOnCamera(), cd = dtlCamera(yawDeg, pitchDeg);
    const Plane back(50.0), down(60.0);
    const int64_t dt = 5000;
    const int nBack = 160, nDown = 60;
    s.backFrom = 0; s.top = nBack * dt; s.downTo = (nBack + nDown) * dt;
    auto dirAt = [&](double t) {
        // backswing: α from −100° (address, head down-and-behind) to +80°; downswing back again
        if (t < double(s.top)) return back.at(-100.0 + 180.0 * t / double(s.top));
        return down.at(80.0 - 175.0 * (t - double(s.top)) / double(s.downTo - s.top));
    };
    double prev = 0; bool first = true;
    for (int64_t t = 0; t <= s.downTo + dt; t += dt) {
        double th, rho;
        project(cf, dirAt(double(t)), th, rho);
        if (!first) { while (th - prev > kPi) th -= 2 * kPi; while (th - prev < -kPi) th += 2 * kPi; }
        prev = th; first = false;
        s.fo.push_back({ t, th, true });
    }
    s.bridge = s.fo;
    for (int64_t t = 3200; t < s.downTo; t += dt) {
        const Vec3 u = dirAt(double(t));
        double th, rho;
        project(cd, u, th, rho);
        if (rho < 0.3) continue;                    // end-on: the DTL tracker publishes nothing
        s.dtl.push_back({ t, th, 0 });
        s.truth.push_back(u);
    }
    return s;
}

static double maxDirErrDeg(const Track3D &t, const Swing &s)
{
    double worst = 0;
    size_t j = 0;
    for (const Sample3D &p : t.samples) {
        while (j < s.dtl.size() && s.dtl[j].t_us != p.t_us) ++j;
        if (j >= s.dtl.size()) return 999;
        // The frame that straddles the top interpolates face-on across the synthetic swing's
        // own kink (the plane steps 50° → 60° there). That is the fixture, not the fusion.
        if (std::llabs(p.t_us - s.top) < 5000) continue;
        worst = std::max(worst, std::acos(std::clamp(p.u.dot(s.truth[j]), -1.0, 1.0)) * 180 / kPi);
    }
    return worst;
}

int main()
{
    std::printf("shaft_fusion_test\n");

    // §1 round trip, ideal cameras
    {
        const Swing s = makeSwing(0, 0);
        Config cfg;
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        check(t.valid && t.samples.size() == s.dtl.size(), "§1 every published DTL frame fuses");
        check(maxDirErrDeg(t, s) < 0.05, "§1 direction recovered to < 0.05°");
        check(t.nSignDisagree == 0 && t.nOffPlane == 0, "§1 a clean swing raises no flag");
        check(t.down.fitted && near(t.down.inclDeg, 60.0, 0.05), "§1 downswing plane 60°");
        check(t.back.fitted && near(t.back.inclDeg, 50.0, 0.05), "§1 backswing plane 50°");
        check(near(t.down.azimDeg, -90.0, 0.05), "§1 normal heads −Y");
        check(t.down.oopRmsDeg < 0.05, "§1 out-of-plane rms ~0");
        check(near(t.down.foRatio, std::sin(60 * kPi / 180), 1e-3), "§1 face-on ratio = sin(incl) for a −Y normal");
        check(near(t.down.foNodeDeg, 0.0, 0.1) || near(t.down.foNodeDeg, 180.0, 0.1), "§1 node line horizontal");
        check(!t.backIncoherent, "§1 a planar backswing is coherent");
        // The predicted projections obey the identity ρ_F² + ρ_D² = 1 + u_z².
        bool ident = true;
        for (const Sample3D &p : t.samples)
            ident = ident && near(p.rhoF * p.rhoF + p.rhoD * p.rhoD, 1.0 + p.u.z * p.u.z, 1e-9);
        check(ident, "§1 ρ_F² + ρ_D² = 1 + u_z²");
    }

    // §2 the de-projection the sequence will apply: tan(ψ − ν) = k tan α makes the face-on
    // angle uniform in the plane angle again.
    {
        const Swing s = makeSwing(0, 0);
        Config cfg;
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        const double k = t.down.foRatio, nu = t.down.foNodeDeg * kPi / 180;
        std::vector<double> alpha;
        for (const FoSample &f : s.fo) {
            if (f.t_us < s.top || f.t_us > s.downTo) continue;
            const double phi = f.theta - nu;
            alpha.push_back(std::atan2(std::sin(phi), k * std::cos(phi)));
        }
        double worst = 0;
        for (size_t i = 2; i < alpha.size(); ++i) {
            auto wrap = [](double a) { while (a > kPi) a -= 2 * kPi; while (a < -kPi) a += 2 * kPi; return a; };
            worst = std::max(worst, std::fabs(wrap(alpha[i] - alpha[i - 1]) - wrap(alpha[i - 1] - alpha[i - 2])));
        }
        check(worst * 180 / kPi < 0.02, "§2 de-projected through the fused plane, the sweep is uniform");
    }

    // §3 a yawed, pitched DTL camera, fused with the SAME model: exact. Fused with the
    // IDEAL model: the inclination barely moves, the heading moves with the yaw.
    {
        const Swing s = makeSwing(10, 8);
        Config right; right.dtlYawDeg = 10; right.dtlPitchDeg = 8;
        const Track3D a = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, right);
        check(maxDirErrDeg(a, s) < 0.05 && near(a.down.inclDeg, 60.0, 0.05), "§3 the right camera model is exact");
        Config ideal;
        const Track3D b = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, ideal);
        check(b.down.fitted && near(b.down.inclDeg, 60.0, 2.5), "§3 wrong camera: inclination within 2.5°");
        check(std::fabs(b.down.azimDeg + 90.0) > 3.0, "§3 wrong camera: the heading is what moves");
    }

    // §4 a mirrored DTL band in the backswing (the wrong root of the two-valued depth
    // sign — 07-04 swings 1–3) reads as an incoherent backswing, not as a plane.
    {
        Swing s = makeSwing(0, 0);
        for (DtlSampleIn &d : s.dtl)
            if (d.t_us > s.top / 2 && d.t_us < s.top) d.theta = kPi - d.theta;
        Config cfg;
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        check(t.backIncoherent, "§4 mirrored backswing band ⇒ backIncoherent");
        check(t.down.fitted && near(t.down.inclDeg, 60.0, 0.05), "§4 and the downswing plane is untouched");
    }

    // §5 one bad downswing frame is flagged OffPlane, and only that one; a 180° flip is a
    // sign disagreement and stays out of the fit.
    {
        Swing s = makeSwing(0, 0);
        std::vector<size_t> downIdx;
        for (size_t i = 0; i < s.dtl.size(); ++i) if (s.dtl[i].t_us > s.top) downIdx.push_back(i);
        const size_t bad = downIdx[downIdx.size() / 2], flip = downIdx[downIdx.size() / 4];
        s.dtl[bad].theta  += 25.0 * kPi / 180;
        s.dtl[flip].theta += kPi;
        Config cfg;
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        int off = 0, sign = 0; bool rightOne = false, rightFlip = false;
        for (const Sample3D &p : t.samples) {
            if (p.flags & OffPlane)     { ++off;  rightOne  = p.t_us == s.dtl[bad].t_us; }
            if (p.flags & SignDisagree) { ++sign; rightFlip = p.t_us == s.dtl[flip].t_us; }
        }
        check(off == 1 && rightOne, "§5 the one bad frame is the one flagged OffPlane");
        check(sign == 1 && rightFlip, "§5 the flipped frame is a sign disagreement");
        check(near(t.down.inclDeg, 60.0, 1.0), "§5 the plane survives one outlier");
    }

    // §6 face-on coasting through impact: those frames fuse against the bridge, are
    // marked, and never enter the plane fit.
    {
        Swing s = makeSwing(0, 0);
        const int64_t from = s.downTo - 60000;
        for (FoSample &f : s.fo) if (f.t_us >= from) f.measured = false;
        Config cfg;
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        int bridged = 0, measuredLate = 0;
        for (const Sample3D &p : t.samples) {
            if (p.foSrc == FoSource::Bridged) ++bridged;
            else if (p.t_us > from) ++measuredLate;
        }
        check(bridged > 5 && bridged == t.nBridged && measuredLate == 0, "§6 coasted face-on ⇒ Bridged");
        const Track3D full = fuseTracks(makeSwing(0, 0).fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        check(t.down.n == full.down.n - bridged, "§6 bridged samples are not in the fit");
        bool oop = true;
        for (const Sample3D &p : t.samples)
            if (p.foSrc == FoSource::Bridged) oop = oop && std::isfinite(p.oopDeg) && std::fabs(p.oopDeg) < 0.1;
        check(oop, "§6 but they ARE measured against it");
        // no bridge at all ⇒ counted, not invented
        const Track3D none = fuseTracks(s.fo, {}, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        check(none.nNoFaceOn == bridged && none.nBridged == 0, "§6 no bridge ⇒ nNoFaceOn");
    }

    // §7 too few frames is not a plane; disabled is not a track.
    {
        Swing s = makeSwing(0, 0);
        Config cfg; cfg.minPlaneN = 1000;
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        check(t.valid && !t.down.fitted && !t.down.offered(cfg), "§7 below minPlaneN ⇒ no plane");
        Config off; off.enabled = false;
        check(!fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, off).valid, "§7 disabled ⇒ invalid");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
