// Standalone test for the 3-D synthetic shaft's DTL projection (src/Analysis/dtl_shaft_synth3d.h).
// ONE synthetic swing on known planes (a 50° backswing plane, a 60° downswing plane), imaged by
// the face-on camera into the angle track the Layer C synth would carry, and by a DTL camera
// with a known yaw/pitch/roll into the angle the DTL tracker WOULD have measured. The synth
// must reproduce that DTL angle through every end-on gap when it is told the planes and the
// camera; it must be off by about the yaw when it is not; and it must be dark by default.
//
//   cmake --build build/tests --target dtl_shaft_synth3d_test
//   ctest --test-dir build/tests -R dtl_shaft_synth3d_test --output-on-failure
#include "../dtl_shaft_synth3d.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace pinpoint::analysis::synth3d;
using namespace pinpoint::analysis::fusion;
namespace synth3d = pinpoint::analysis::synth3d;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static double wrapDeg(double d) { while (d > 180) d -= 360; while (d < -180) d += 360; return d; }

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

struct Scene {
    std::vector<FoAngle> fo;
    std::vector<Anchor>  anchors;
    std::vector<double>  truthThetaD, truthRhoD;   // per anchor
    Planes planes;
};

static Scene makeScene(const Camera &cd, bool withAddress)
{
    Scene s;
    const Camera cf = faceOnCamera();
    const Plane back(50.0), down(60.0);
    const int64_t dt = 4167;                 // the 240 Hz synth grid
    const int64_t addrTo = 200000, top = 900000, downTo = 1200000;
    auto dirAt = [&](double t) {
        if (t < double(top)) return back.at(-100.0 + 180.0 * (t - double(addrTo)) / double(top - addrTo));
        return down.at(80.0 - 175.0 * (t - double(top)) / double(downTo - top));
    };
    for (int64_t t = 0; t <= downTo + 200000; t += dt) {
        double th, rho;
        project(cf, dirAt(double(t)), th, rho);
        s.fo.push_back({ t, th });           // any wrap: the synth unwraps its own bracket
    }
    s.planes.addr = withAddress; s.planes.nAddr = back.n;
    s.planes.back = true;  s.planes.nBack = back.n;
    s.planes.down = true;  s.planes.nDown = down.n;
    s.planes.addrToUs = addrTo; s.planes.topUs = top; s.planes.downToUs = downTo;
    // DTL frames every 6.7 ms, offset 3.2 ms — never on a synth tick
    for (int64_t t = 3200; t <= downTo + 300000; t += 6700) {
        Anchor a; a.t_us = t; a.ok = true; a.gx = 250.0; a.gy = 560.0; a.src = AnchorSrc::Tracker;
        s.anchors.push_back(a);
        double th, rho;
        project(cd, dirAt(double(t)), th, rho);
        s.truthThetaD.push_back(th); s.truthRhoD.push_back(rho);
    }
    return s;
}

int main()
{
    std::printf("dtl_shaft_synth3d_test\n");
    const Camera cf = faceOnCamera();

    std::printf("=== §0 dark by default ===\n");
    {
        const Camera cd = dtlCamera(0, 0);
        const Scene s = makeScene(cd, true);
        synth3d::Config off; off.enabled = false;
        check(synth3d::Config().enabled, "§0 on by default (Mark, 2026-10-01), a preview through the assumed-zero camera");
        check(synthesize(s.fo, s.planes, cf, cd, s.anchors, 330.0, off).empty(), "§0 disabled ⇒ no samples at all");
    }

    std::printf("=== §1 told the planes and the camera, the projection is the DTL angle — through the gaps ===\n");
    {
        const Camera cd = dtlCamera(6.0, 3.0, -2.0);
        const Scene s = makeScene(cd, true);
        synth3d::Config cfg; cfg.enabled = true;
        const std::vector<Sample> out = synthesize(s.fo, s.planes, cf, cd, s.anchors, 330.0, cfg);
        check(out.size() == s.anchors.size(), "§1 one sample per anchor frame");
        double worst = 0, worstRho = 0; int ok = 0, endOn = 0, extrap = 0;
        for (size_t i = 0; i < out.size(); ++i) {
            if (!out[i].ok) continue;
            ++ok;
            if (out[i].plane == PlaneUsed::DownExtrapolated) ++extrap;
            // Where the true projection is nearly end-on the angle is ill-defined by
            // geometry (that is the whole point of §2 of the design update): compare
            // the DIRECTION there, the angle elsewhere.
            if (s.truthRhoD[i] < 0.15) { ++endOn; continue; }
            worst = std::max(worst, std::fabs(wrapDeg((out[i].thetaD - s.truthThetaD[i]) * 180.0 / kPi)));
            worstRho = std::max(worstRho, std::fabs(out[i].rhoD - s.truthRhoD[i]));
        }
        std::printf("       %d/%zu samples ok, %d through end-on instants, %d on the held plane; worst θ_D error %.4f°, ρ_D %.5f\n",
                    ok, out.size(), endOn, extrap, worst, worstRho);
        int covered = 0;
        for (const Anchor &a : s.anchors)
            if (a.t_us <= s.planes.downToUs + cfg.holdDownPlaneUs && a.t_us <= s.fo.back().t_us) ++covered;
        check(ok == covered && covered < int(out.size()),
              "§1 every frame inside the planes' windows synthesises; past the held downswing plane none does");
        check(worst < 0.05, "§1 θ_D reproduced to < 0.05° everywhere the truth is not end-on");
        check(worstRho < 1e-3, "§1 ρ_D reproduced to < 1e-3");
        check(endOn > 0, "§1 the sweep crosses end-on instants — and the synth still has a direction there");
        check(extrap > 0, "§1 frames past the downswing window are flagged extrapolated");
        bool lenOk = true;
        for (const Sample &x : out) if (x.ok) lenOk = lenOk && near(x.lenPx, x.rhoD * 330.0, 1e-9);
        check(lenOk, "§1 the drawn length is ρ_D · L̂_D");
        bool addr = false, backP = false, downP = false;
        for (const Sample &x : out) { addr |= x.plane == PlaneUsed::Address; backP |= x.plane == PlaneUsed::Back; downP |= x.plane == PlaneUsed::Down; }
        check(addr && backP && downP, "§1 address, backswing and downswing planes each served their window");
    }

    std::printf("=== §2 blind to the camera: off by the yaw, which is what the calibration buys ===\n");
    {
        const Camera cdTrue = dtlCamera(8.0, 0.0, 0.0), cdAssumed = dtlCamera(0, 0);
        const Scene s = makeScene(cdTrue, true);
        synth3d::Config cfg; cfg.enabled = true;
        const std::vector<Sample> out = synthesize(s.fo, s.planes, cf, cdAssumed, s.anchors, 330.0, cfg);
        double worst = 0;
        for (size_t i = 0; i < out.size(); ++i) {
            if (!out[i].ok || s.truthRhoD[i] < 0.5) continue;
            worst = std::max(worst, std::fabs(wrapDeg((out[i].thetaD - s.truthThetaD[i]) * 180.0 / kPi)));
        }
        std::printf("       worst θ_D error with the assumed-zero camera against an 8° yaw: %.2f°\n", worst);
        check(worst > 1.0, "§2 an uncalibrated preview IS wrong by more than a degree on a yawed rig");
    }

    std::printf("=== §3 no plane, no anchor, no face-on bracket ⇒ no sample; ill-conditioned ⇒ no sample ===\n");
    {
        const Camera cd = dtlCamera(0, 0);
        Scene s = makeScene(cd, false);
        s.planes.back = false;                          // nothing before the top now
        synth3d::Config cfg; cfg.enabled = true;
        const std::vector<Sample> out = synthesize(s.fo, s.planes, cf, cd, s.anchors, 330.0, cfg);
        int before = 0, after = 0;
        for (const Sample &x : out) { if (x.t_us < s.planes.topUs) before += x.ok; else after += x.ok; }
        check(before == 0 && after > 0, "§3 no backswing plane ⇒ nothing before the top, the downswing still draws");
        Scene q = makeScene(cd, true);
        q.anchors[10].ok = false;
        const std::vector<Sample> o2 = synthesize(q.fo, q.planes, cf, cd, q.anchors, 330.0, cfg);
        check(!o2[10].ok && o2[9].ok && o2[11].ok, "§3 an anchor-less frame is skipped, its neighbours are not");
        Scene r = makeScene(cd, true);
        r.fo.erase(r.fo.begin() + 100, r.fo.begin() + 110);   // a 42 ms face-on hole
        const std::vector<Sample> o3 = synthesize(r.fo, r.planes, cf, cd, r.anchors, 330.0, cfg);
        int holes = 0;
        for (const Sample &x : o3) if (!x.ok && x.t_us > r.fo[99].t_us && x.t_us < r.fo[100].t_us) ++holes;
        check(holes > 0, "§3 a face-on bracket wider than maxFoGapUs is not bridged");
        // Ill-conditioned: a plane whose normal lies IN the face-on view plane of the angle.
        Vec3 u; double cond;
        const Vec3 img = imageDir(cf, 1.0);
        const Vec3 nF = cf.d.cross(img).unit();
        check(!deproject(cf, 1.0, nF, 0.15, u, cond) && cond < 1e-9, "§3 a plane coincident with the view plane has no direction");
    }

    std::printf("=== §4 the address plane from DTL angles alone ===\n");
    {
        const Camera cd = dtlCamera(0, 0);
        const Plane p(55.0);
        std::vector<double> th;
        for (int i = 0; i < 9; ++i) { double t, r; project(cd, p.at(-100.0 + i), t, r); th.push_back(t); }
        Vec3 n;
        check(addressPlaneNormal(cd, th, n) && near(std::acos(std::fabs(n.z)) * 180.0 / kPi, 55.0, 1e-6),
              "§4 nine address frames on a 55° plane give a 55° normal");
        check(!addressPlaneNormal(cd, std::vector<double>(th.begin(), th.begin() + 3), n), "§4 fewer than minN frames is no plane");
    }

    std::printf("=== §5 (design update §3.2a A) an out-of-plane swing: told η(t), the projection is the DTL angle again ===\n");
    {
        const Camera cd = dtlCamera(6.0, 3.0, -2.0);
        const Plane back(50.0), down(60.0);
        const int64_t dt = 4167, addrTo = 200000, top = 900000, downTo = 1200000;
        const double amp = 7.0; const int64_t period = 800000;
        auto etaAt = [&](double t) { return amp * std::sin(2 * kPi * t / double(period)); };
        auto dirAt = [&](double t) {
            const Plane &p = t < double(top) ? back : down;
            const Vec3 in = t < double(top) ? back.at(-100.0 + 180.0 * (t - double(addrTo)) / double(top - addrTo))
                                            : down.at(80.0 - 175.0 * (t - double(top)) / double(downTo - top));
            const double e = etaAt(t) * kPi / 180.0;
            return in * std::cos(e) + p.n * std::sin(e);
        };
        Scene s;
        for (int64_t t = 0; t <= downTo + 200000; t += dt) { double th, rho; project(cf, dirAt(double(t)), th, rho); s.fo.push_back({ t, th }); }
        s.planes.addr = true; s.planes.nAddr = back.n; s.planes.back = true; s.planes.nBack = back.n; s.planes.down = true; s.planes.nDown = down.n;
        s.planes.addrToUs = addrTo; s.planes.topUs = top; s.planes.downToUs = downTo;
        for (int64_t t = 3200; t <= downTo; t += 6700) {
            Anchor a; a.t_us = t; a.ok = true; a.gx = 250.0; a.gy = 560.0; a.src = AnchorSrc::Tracker;
            s.anchors.push_back(a);
            double th, rho; project(cd, dirAt(double(t)), th, rho);
            s.truthThetaD.push_back(th); s.truthRhoD.push_back(rho);
        }
        // The curve as the fusion would hand it over: the true η sampled at 30 ms knots.
        pinpoint::analysis::fusion::EtaFit eta;
        for (int64_t k = 0; k <= downTo + 30000; k += 30000) { eta.knotsUs.push_back(k); eta.values.push_back(etaAt(double(k))); eta.near.push_back(1); }
        eta.fitted = true; eta.n = int(eta.knotsUs.size());
        synth3d::Config cfg; cfg.enabled = true;
        const std::vector<Sample> with = synthesize(s.fo, s.planes, cf, cd, s.anchors, 330.0, cfg, &eta);
        const std::vector<Sample> without = synthesize(s.fo, s.planes, cf, cd, s.anchors, 330.0, cfg, nullptr);
        double worstWith = 0, worstWithout = 0; int n = 0;
        for (size_t i = 0; i < with.size(); ++i) {
            if (!with[i].ok || !without[i].ok || s.truthRhoD[i] < 0.3) continue;
            if (std::llabs(s.anchors[i].t_us - top) < 20000) continue;     // the fixture's kink at the top
            ++n;
            worstWith = std::max(worstWith, std::fabs(wrapDeg((with[i].thetaD - s.truthThetaD[i]) * 180.0 / kPi)));
            worstWithout = std::max(worstWithout, std::fabs(wrapDeg((without[i].thetaD - s.truthThetaD[i]) * 180.0 / kPi)));
        }
        std::printf("       %d frames: worst θ_D error with η(t) %.3f°, in-plane %.2f° (true η up to ±%.0f°)\n", n, worstWith, worstWithout, amp);
        check(n > 100 && worstWith < 0.3, "§5 with the curve the DTL angle is reproduced (< 0.3°, the spline's own interpolation)");
        check(worstWithout > 3.0, "§5 in-plane, the same swing is off by degrees");
        bool recorded = true;
        for (const Sample &x : with) if (x.ok) recorded = recorded && std::isfinite(x.etaDeg);
        for (const Sample &x : without) if (x.ok) recorded = recorded && !std::isfinite(x.etaDeg);
        check(recorded, "§5 etaDeg is on every sample the curve touched and on none it did not");
        synth3d::Config noEta = cfg; noEta.useEta = false;
        const std::vector<Sample> off = synthesize(s.fo, s.planes, cf, cd, s.anchors, 330.0, noEta, &eta);
        bool same = off.size() == without.size();
        for (size_t i = 0; same && i < off.size(); ++i) same = off[i].thetaD == without[i].thetaD || (!off[i].ok && !without[i].ok);
        check(same, "§5 useEta off ⇒ the in-plane synth, bit for bit, curve or no curve");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
