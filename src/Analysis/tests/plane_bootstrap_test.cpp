// Block-bootstrap plane uncertainty (shaft_plane.h bootstrapPlaneChannel; shaft_fusion.h
// fuseTracks with Config::uncertainty; shaft_uncertainty_propagation_design.md §4.7):
// deterministic for a seed, finite on a clean ellipse, and smaller with more points.
// Pure std.
//
//   ctest --test-dir <build> -R plane_bootstrap --output-on-failure

#include "../shaft_plane.h"
#include "../shaft_fusion.h"
#include "../det_rng.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static std::vector<ShaftPlanePoint> ellipse(int n, double ratio, double noise, uint64_t seed)
{
    DetRng r(seed);
    std::vector<ShaftPlanePoint> v;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * M_PI * 0.9 * i / n;    // ~0.45 turn per window: an arc of 81° is a
                                                        // genuinely ill-conditioned conic (the first draft)
        ShaftPlanePoint p;
        p.tUs = int64_t(i) * 4000;
        p.vx = 300.0 * std::cos(a) + noise * r.normal();
        p.vy = 300.0 * ratio * std::sin(a) + noise * r.normal();
        v.push_back(p);
    }
    return v;
}

int main()
{
    std::printf("=== plane bootstrap ===\n");
    const std::vector<ShaftPlanePoint> pts = ellipse(120, 0.7, 2.0, 1u);
    const int64_t mid = pts[60].tUs, end = pts.back().tUs;
    {
        DetRng a(9u), b(9u);
        const PlaneBootstrap p1 = bootstrapPlaneChannel(pts, 0, mid, end, 200, 5, a);
        const PlaneBootstrap p2 = bootstrapPlaneChannel(pts, 0, mid, end, 200, 5, b);
        check(p1.sigmaBack == p2.sigmaBack && p1.sigmaDelta == p2.sigmaDelta, "deterministic for a seed");
        check(p1.sigmaBack > 0.0 && p1.sigmaDown > 0.0 && p1.sigmaDelta > 0.0, "finite σ on a clean ellipse");
        check(p1.pNeedle >= 0.0 && p1.pNeedle <= 0.05, "a well-conditioned ellipse rarely resamples into a needle");
        const std::vector<ShaftPlanePoint> dense = ellipse(480, 0.7, 2.0, 1u);
        DetRng c(9u);
        const PlaneBootstrap p3 = bootstrapPlaneChannel(dense, 0, dense[240].tUs, dense.back().tUs, 200, 5, c);
        std::printf("    σι back: 120 pts %.3f°, 480 pts %.3f°\n", p1.sigmaBack, p3.sigmaBack);
        check(p3.sigmaBack < p1.sigmaBack, "more points ⇒ a smaller σ");
    }
    // The fused plane's bootstrap: a planar 3-D sweep seen by both cameras.
    {
        using namespace fusion;
        const Camera cf = faceOnCamera(), cd = dtlCamera(0, 0);
        const Vec3 n = Vec3{ 0.0, -0.5, 0.866 }.unit();
        const Vec3 e1 = Vec3{ 1, 0, 0 }, e2 = n.cross(e1).unit();
        std::vector<FoSample> fo; std::vector<DtlSampleIn> dt;
        DetRng r(5u);
        for (int i = 0; i < 60; ++i) {
            const double a = -1.0 + 2.0 * i / 59.0;
            const Vec3 u = (e1 * std::cos(a) + e2 * std::sin(a)).unit();
            double thF, rhoF, thD, rhoD;
            project(cf, u, thF, rhoF); project(cd, u, thD, rhoD);
            fo.push_back({ int64_t(i) * 6000, thF + 0.01 * r.normal(), true, 1.0 });
            dt.push_back({ int64_t(i) * 6000 + 100, thD + 0.01 * r.normal(), 0, 1.0 });
        }
        Config cfg; cfg.uncertainty = true; cfg.bootstrapN = 100;
        const Track3D t = fuseTracks(fo, {}, dt, 0, 180000, 360000, cfg);
        check(t.down.fitted && std::isfinite(t.down.inclSigmaDeg) && t.down.inclSigmaDeg >= cfg.cameraFloorDeg,
              "the fused downswing plane carries a bootstrap σ at or above the camera floor");
        bool anySig = false;
        for (const Sample3D &s : t.samples) anySig = anySig || std::isfinite(s.sigmaDeg);
        check(anySig, "fused frames carry a direction σ");
        Config off;
        const Track3D t0 = fuseTracks(fo, {}, dt, 0, 180000, 360000, off);
        check(!std::isfinite(t0.down.inclSigmaDeg) && t0.down.inclDeg == t.down.inclDeg,
              "off ⇒ no σ, and the plane itself is identical either way");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
