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
static Swing makeSwing(double yawDeg, double pitchDeg, double rollDeg = 0.0)
{
    Swing s;
    const Camera cf = faceOnCamera(), cd = dtlCamera(yawDeg, pitchDeg, rollDeg);
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
        Config cfg; cfg.reflectBands = false;   // the repair (§D below) is on by default since 2026-10-03
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

    // §8 the address plane, from the DTL view alone: the synthetic address (α = −100° on the 50°
    // backswing plane) lies ON that plane, so the address plane reads 50° and the 60° downswing
    // is delivered 10° above it. Face-on coasting at address must not cost it a frame.
    {
        Swing s = makeSwing(0, 0);
        for (FoSample &f : s.fo) if (f.t_us < 100000) f.measured = false;
        Config cfg;
        const Track3D t = fuseTracks(s.fo, {}, s.dtl, s.backFrom, s.top, s.downTo, cfg, 60000);
        check(t.addressN >= cfg.minAddressN && near(t.addressInclDeg, 50.0, 0.1), "§8 address plane 50° from DTL alone");
        check(near(t.deliveryVsAddressDeg, 10.0, 0.15), "§8 delivered 10° above the address plane");
        const Track3D none = fuseTracks(s.fo, {}, s.dtl, s.backFrom, s.top, s.downTo, cfg);
        check(none.addressN == 0 && !std::isfinite(none.deliveryVsAddressDeg), "§8 no address window ⇒ no claim");
    }

    // ── dtl_continuous_track_design_update.md §3.2a ──────────────────────────────

    // §A the out-of-plane curve η(t): a swing whose direction leaves its plane by a known
    // sinusoid, imaged by both cameras, published in three DTL bands with two gaps. The
    // fit recovers η inside the bands, coasts to zero in a long gap (the prior), and the
    // rotated de-projection reproduces every fused direction from its face-on angle alone.
    {
        auto makeEta = [](double ampDeg, int64_t periodUs, bool gaps) {
            Swing s;
            const Camera cf = faceOnCamera(), cd = dtlCamera(0, 0);
            const Plane back(50.0), down(60.0);
            const int64_t dt = 5000;
            const int nBack = 160, nDown = 60;
            s.backFrom = 0; s.top = nBack * dt; s.downTo = (nBack + nDown) * dt;
            auto etaAt = [&](double t) { return ampDeg * std::sin(2 * kPi * t / double(periodUs)); };
            auto dirAt = [&](double t) {
                const Plane &p = t < double(s.top) ? back : down;
                const Vec3 in = t < double(s.top) ? back.at(-100.0 + 180.0 * t / double(s.top))
                                                  : down.at(80.0 - 175.0 * (t - double(s.top)) / double(s.downTo - s.top));
                const double e = etaAt(t) * kPi / 180.0;
                return in * std::cos(e) + p.n * std::sin(e);
            };
            double prev = 0; bool first = true;
            for (int64_t t = 0; t <= s.downTo + dt; t += dt) {
                double th, rho; project(cf, dirAt(double(t)), th, rho);
                if (!first) { while (th - prev > kPi) th -= 2 * kPi; while (th - prev < -kPi) th += 2 * kPi; }
                prev = th; first = false;
                s.fo.push_back({ t, th, true });
            }
            s.bridge = s.fo;
            for (int64_t t = 3200; t < s.downTo; t += dt) {
                const Vec3 u = dirAt(double(t));
                double th, rho; project(cd, u, th, rho);
                if (rho < 0.3) continue;
                int band = 0;
                if (gaps) {
                    if (t >= 250000 && t < 370000) continue;          // a 120 ms gap
                    if (t >= 560000 && t < 960000) continue;          // a 400 ms gap over the top
                    band = t < 250000 ? 0 : t < 560000 ? 1 : 2;
                }
                s.dtl.push_back({ t, th, band });
                s.truth.push_back(u);
            }
            return s;
        };
        const double amp = 6.0; const int64_t period = 700000;
        auto etaTrue = [&](int64_t t) { return amp * std::sin(2 * kPi * double(t) / double(period)); };

        Swing s = makeEta(amp, period, false);
        Config on; on.eta.enabled = true;
        check(Config().eta.enabled, "§A on by default (Mark, 2026-10-01)");
        const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on);
        check(t.eta.fitted && t.eta.n > 100, "§A the curve is fitted on the measured frames");
        double worstIn = 0, worstDe = 0; int nDe = 0;
        const Camera cf = faceOnCamera();
        for (const Sample3D &p : t.samples) {
            // The planes switch at the top (50° → 60°): the frames' out-of-plane angle JUMPS
            // there and a smooth curve cannot follow a jump — that is the fixture's kink, and
            // the curve's clamped ends are the other exclusion.
            if (std::llabs(p.t_us - s.top) < 90000) continue;
            if (p.t_us < 40000 || p.t_us > s.downTo - 40000) continue;
            // The fitted plane absorbs the sinusoid's mean over its window, so the truth the
            // curve must follow is each frame's own oopDeg (η relative to THAT plane), not
            // the analytic η relative to the generating plane.
            worstIn = std::max(worstIn, std::fabs(p.etaDeg - p.oopDeg));
            const PlaneFit &pf = p.t_us < s.top ? t.back : t.down;
            Vec3 u; double cond;
            if (deprojectEta(cf, p.thetaF, pf.normal, p.oopDeg, on.minCond, u, cond)) {
                ++nDe;
                worstDe = std::max(worstDe, std::acos(std::clamp(u.dot(p.u), -1.0, 1.0)) * 180 / kPi);
            }
        }
        std::printf("       η follows the frames' out-of-plane angle to %.3f° worst (amplitude %.0f°); de-projection through η reproduces %d fused directions to %.4f° worst\n",
                    worstIn, amp, nDe, worstDe);
        check(worstIn < 0.6, "§A η(t) follows the measured out-of-plane angle to < 0.6°");
        check(nDe > 100 && worstDe < 0.01, "§A the face-on angle rotated by the frame's own η IS the fused direction");
        // η = 0 is `deproject` bit for bit.
        {
            Vec3 a, b; double ca, cb; const Sample3D &p = t.samples[t.samples.size() / 3];
            deproject(cf, p.thetaF, t.back.normal, on.minCond, a, ca);
            deprojectEta(cf, p.thetaF, t.back.normal, 0.0, on.minCond, b, cb);
            check(a.x == b.x && a.y == b.y && a.z == b.z && ca == cb, "§A η = 0 is the plain de-projection, bit for bit");
        }
        // Gaps: inside the bands the curve still follows; across the 400 ms gap it rests on the prior.
        Swing g = makeEta(amp, period, true);
        const Track3D tg = fuseTracks(g.fo, g.bridge, g.dtl, g.backFrom, g.top, g.downTo, on);
        double worstBand = 0, midGap = std::fabs(tg.eta.at(760000));
        for (const Sample3D &p : tg.samples) {
            if (p.t_us < 40000 || std::llabs(p.t_us - s.top) < 90000 || p.t_us > g.downTo - 40000) continue;
            worstBand = std::max(worstBand, std::fabs(p.etaDeg - p.oopDeg));
        }
        // The true direction in the middle of the gap, off the plane the fit found.
        double trueMid = 0;
        {
            const Plane back(50.0);
            const double e = etaTrue(760000) * kPi / 180.0;
            const Vec3 u = back.at(-100.0 + 180.0 * 760000.0 / double(s.top)) * std::cos(e) + back.n * std::sin(e);
            trueMid = std::asin(std::clamp(u.dot(tg.back.normal), -1.0, 1.0)) * 180 / kPi;
        }
        std::printf("       with two gaps: %.3f° worst inside the bands; mid-gap (400 ms, truly %.1f° off the fitted plane) the curve reads %.2f°\n",
                    worstBand, trueMid, tg.eta.at(760000));
        check(worstBand < 1.0, "§A inside the bands η still follows the frames (< 1°)");
        check(midGap < 0.75 && std::fabs(trueMid) > 2.0, "§A in a 400 ms gap the curve coasts to the in-plane prior, it does not extrapolate");
        // OFF: no curve, no etaDeg, directions untouched.
        Config off; off.eta.enabled = false;
        const Track3D to = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, off);
        bool same = !to.eta.fitted && to.samples.size() == t.samples.size();
        for (size_t i = 0; same && i < to.samples.size(); ++i)
            same = !std::isfinite(to.samples[i].etaDeg) && to.samples[i].u.x == t.samples[i].u.x
                && to.samples[i].u.y == t.samples[i].u.y && to.samples[i].u.z == t.samples[i].u.z;
        check(same, "§A OFF: no curve, no etaDeg, and the fused directions are the same bits");
    }

    // §C the DTL-anchored direction where face-on coasted. With the camera on the plane's
    // node line (down the line, yaw 0) the DTL view plane at impact IS the swing plane and
    // every anchor is refused for conditioning; with the camera 25° off the node the anchor
    // lands on the truth where the bridge (a face-on synth 4° wrong) does not.
    {
        auto coast = [](double yawDeg) {
            Swing s = makeSwing(yawDeg, 0);
            const int64_t from = s.downTo - 60000;
            for (FoSample &f : s.fo) if (f.t_us >= from) f.measured = false;
            for (FoSample &f : s.bridge) if (f.t_us >= from) f.theta += 4.0 * kPi / 180.0;   // the bridge is not the truth
            return s;
        };
        {
            Swing s = coast(0.0);
            Config on; on.dtlAnchor = true;
            const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on);
            double worstCond = 0;
            for (const Sample3D &p : t.samples) if (p.foSrc == FoSource::Bridged) worstCond = std::max(worstCond, p.anchorCond);
            std::printf("       camera on the node: %d bridged frames, %d anchored, %d refused, conditioning ≤ %.4f\n",
                        t.nBridged, t.nDtlAnchored, t.nDtlAnchorRefused, worstCond);
            check(t.nBridged > 5 && t.nDtlAnchored == 0 && t.nDtlAnchorRefused == t.nBridged,
                  "§C down the line, the DTL view plane at impact is the swing plane: every anchor refused");
            check(worstCond < 0.05, "§C …and the record says why (conditioning ≈ 0)");
        }
        {
            Swing s = coast(25.0);
            Config on; on.dtlAnchor = true; on.dtlYawDeg = 25.0;
            Config off; off.dtlYawDeg = 25.0;
            const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on);
            const Track3D b = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, off);
            double worstA = 0, bestB = 999; int n = 0; bool beside = true;
            size_t j = 0;
            for (size_t i = 0; i < t.samples.size(); ++i) {
                const Sample3D &p = t.samples[i];
                while (j < s.dtl.size() && s.dtl[j].t_us != p.t_us) ++j;
                if (!(p.flags & DtlAnchored) || j >= s.dtl.size()) continue;
                ++n;
                const double ea = std::acos(std::clamp(p.u.dot(s.truth[j]), -1.0, 1.0)) * 180 / kPi;
                const double eb = std::acos(std::clamp(b.samples[i].u.dot(s.truth[j]), -1.0, 1.0)) * 180 / kPi;
                worstA = std::max(worstA, ea); bestB = std::min(bestB, eb);
                beside = beside && p.uBridged.x == b.samples[i].u.x && p.uBridged.y == b.samples[i].u.y && p.uBridged.z == b.samples[i].u.z;
            }
            std::printf("       camera 25° off the node: %d anchored; anchored error ≤ %.3f°, bridged error ≥ %.3f°\n", n, worstA, bestB);
            check(n > 5 && n == t.nDtlAnchored, "§C off the node, the bridged frames anchor");
            check(worstA < 0.05 && bestB > 1.0, "§C the anchored direction is the truth; the bridged one carries the synth's error");
            check(beside, "§C the bridged value is kept beside it, bit for bit");
            check(t.down.n == b.down.n && near(t.down.inclDeg, b.down.inclDeg, 1e-9), "§C anchored frames never enter the plane fit");
        }
    }

    // §D the mirrored backswing band, re-read. The §4 swing again, with band ids: the
    // address-to-P2 band, the mirrored mid-backswing band, the rest. Reflected inside the
    // fusion the backswing is coherent and 50° again; the address band is never a
    // candidate; a clean swing is untouched; a swing the reflection cannot make coherent
    // is put back exactly.
    {
        auto banded = [](bool mirror, bool wreck) {
            Swing s = makeSwing(0, 0);
            for (size_t i = 0; i < s.dtl.size(); ++i) {
                DtlSampleIn &d = s.dtl[i];
                d.band = d.t_us < s.top / 2 ? 0 : d.t_us < s.top ? 1 : 2;
                if (mirror && d.band == 1) d.theta = kPi - d.theta;
                // A band no reflection can repair: every other frame mirrored, so the band's
                // reflection only swaps which half is wrong and the rms does not move.
                if (wreck && d.band == 1) { if ((d.t_us / 5000) % 2) d.theta = kPi - d.theta; d.band = 3; }
            }
            return s;
        };
        Config on; on.reflectBands = true;
        Config off; off.reflectBands = false;
        check(Config().reflectBands, "§D the repair is on by default (its gate passed, 2026-10-03)");
        {
            Swing s = banded(true, false);
            const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on, 60000);
            const Track3D b = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, off, 60000);
            std::printf("       mirrored band: backswing rms %.1f° → %.1f°, inclination %.1f° → %.1f°, reflected bands %zu\n",
                        b.back.oopRmsDeg, t.back.oopRmsDeg, b.back.inclDeg, t.back.inclDeg, t.reflectedBands.size());
            check(b.backIncoherent && !t.backIncoherent, "§D the incoherent backswing is coherent once the band is reflected");
            check(t.reflectedBands.size() == 1 && t.reflectedBands[0] == 1, "§D the reflected band is the mirrored one, and only it");
            check(near(t.back.inclDeg, 50.0, 0.1) && t.back.oopRmsDeg < 0.1, "§D the backswing plane is 50° again");
            check(near(t.backRmsBeforeReflectDeg, b.back.oopRmsDeg, 1e-9), "§D the rms before the reflection is on the record");
            bool flagged = true, restored = true;
            size_t j = 0;
            for (const Sample3D &p : t.samples) {
                while (j < s.dtl.size() && s.dtl[j].t_us != p.t_us) ++j;
                if (p.dtlBand == 1) {
                    flagged = flagged && (p.flags & Reflected);
                    if (std::llabs(p.t_us - s.top) < 5000) continue;   // the fixture's own kink (§1)
                    restored = restored && j < s.dtl.size()
                        && std::acos(std::clamp(p.u.dot(s.truth[j]), -1.0, 1.0)) * 180 / kPi < 0.05;
                } else flagged = flagged && !(p.flags & Reflected);
            }
            check(flagged, "§D every sample of that band is flagged Reflected, no other is");
            check(restored, "§D and its directions are the truth again");
            check(near(t.down.inclDeg, b.down.inclDeg, 1e-9), "§D the downswing plane is untouched");
            check(t.back.normal.dot(t.down.normal) > 0, "§D the repaired plane faces the downswing plane's way");
            // The address band is the mirror-image candidate: with no address window declared it
            // is a candidate by time, and only the downswing plane's side keeps it out.
            const Track3D noAddr = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on);
            check(noAddr.reflectedBands.size() == 1 && noAddr.reflectedBands[0] == 1 && near(noAddr.back.inclDeg, 50.0, 0.1),
                  "§D without an address window the mirror-image choice (the address band) is refused by the downswing plane's side");
        }
        {
            Swing s = banded(false, false);
            const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on, 60000);
            const Track3D b = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, off, 60000);
            bool same = t.reflectedBands.empty() && !std::isfinite(t.backRmsBeforeReflectDeg) && t.samples.size() == b.samples.size();
            for (size_t i = 0; same && i < t.samples.size(); ++i)
                same = t.samples[i].u.x == b.samples[i].u.x && t.samples[i].thetaD == b.samples[i].thetaD && t.samples[i].flags == b.samples[i].flags;
            check(same, "§D a coherent swing is not touched: the same samples, bit for bit");
        }
        {
            Swing s = banded(false, true);
            const Track3D t = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, on, 60000);
            const Track3D b = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, off, 60000);
            std::printf("       a half-mirrored band: rms before %.1f°, after the search %.1f°, kept %zu\n",
                        b.back.oopRmsDeg, t.back.oopRmsDeg, t.reflectedBands.size());
            bool same = t.samples.size() == b.samples.size();
            for (size_t i = 0; same && i < t.samples.size(); ++i)
                same = t.samples[i].u.x == b.samples[i].u.x && t.samples[i].thetaD == b.samples[i].thetaD && t.samples[i].flags == b.samples[i].flags;
            check(t.backIncoherent && t.reflectedBands.empty() && same,
                  "§D when no reflection reaches coherence nothing is kept and every sample is put back exactly");
        }
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

    // §R the DTL camera's roll (2026-10-02, camera_pose_sticks.h / dtl_continuous_track
    // update). roll = 0 is the camera every earlier run used, bit for bit; a rolled
    // camera still recovers the plane's inclination and the direction once the
    // fusion is told the roll, and gets it wrong by about the roll if it is not.
    {
        const Camera a = dtlCamera(7.0, 4.0), b = dtlCamera(7.0, 4.0, 0.0);
        check(a.d.x == b.d.x && a.d.y == b.d.y && a.d.z == b.d.z
              && a.right.x == b.right.x && a.right.y == b.right.y && a.right.z == b.right.z
              && a.down.x == b.down.x && a.down.y == b.down.y && a.down.z == b.down.z,
              "§R dtlCamera(yaw, pitch) is dtlCamera(yaw, pitch, 0) exactly");
        const Camera r = dtlCamera(7.0, 4.0, 5.0);
        check(near(r.right.dot(r.down), 0.0, 1e-12) && near(r.right.dot(r.d), 0.0, 1e-12)
              && near(r.right.norm(), 1.0, 1e-12) && near(r.down.norm(), 1.0, 1e-12),
              "§R a rolled camera is still orthonormal");
        check(near(std::atan2(r.right.dot(b.down), r.right.dot(b.right)) * 180.0 / kPi, 5.0, 1e-9),
              "§R its image-right sits 5° toward image-down of the level camera's");
        // A swing imaged by a rolled DTL camera: told the roll, the fusion recovers it.
        const Swing s = makeSwing(0.0, 0.0, 6.0);
        Config told; told.dtlRollDeg = 6.0; told.calibrated = true;
        const Track3D t1 = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, told);
        check(t1.valid && t1.down.fitted && near(t1.down.inclDeg, 60.0, 0.1),
              "§R told the 6° roll, the downswing plane reads 60° within 0.1°");
        check(t1.calibrated && near(t1.dtlRollDeg, 6.0, 1e-12), "§R the track carries roll and the calibrated flag");
        Config blind;
        const Track3D t0 = fuseTracks(s.fo, s.bridge, s.dtl, s.backFrom, s.top, s.downTo, blind);
        check(t0.valid && t0.down.fitted && std::fabs(t0.down.inclDeg - 60.0) > 1.0,
              "§R blind to the roll, the same swing's plane is off by more than 1°");
        std::printf("       rolled DTL camera: told 6° ⇒ down %.2f°; blind ⇒ %.2f°\n", t1.down.inclDeg, t0.down.inclDeg);
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
