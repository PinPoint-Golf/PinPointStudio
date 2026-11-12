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

// The per-shot assembly of shaftDirection (shaft_direction_series.h, what ShaftDirectionStage
// publishes): route, σ, the trust records, and — the W1 review's blocker — that the value the
// diagnostics GRADE agrees with the phase reading they stamp the σ of. The grader is reproduced
// exactly: series_reduce.h reduceAt over the published curve at ±sampler::kWindowHalfUs, falling
// back to the phase sample only when that window holds no valid frame (measure_sample.cpp).
// The bearing, its sign and the source rule themselves are pinned in shaft_fusion_test §SD.
//
//   ctest --test-dir build/tests -R '^shaft_direction_test$' --output-on-failure

#include "../../Club/shaft_direction_series.h"
#include "../../Metrics/series_reduce.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;
namespace sd = pinpoint::analysis::shaftdir;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static fusion::Vec3 dirAt(double headingDeg, double elevDeg = 2.0)
{
    const double h = headingDeg * fusion::kPi / 180.0, e = elevDeg * fusion::kPi / 180.0;
    return { std::cos(e) * std::cos(h), std::cos(e) * std::sin(h), std::sin(e) };
}
static fusion::Sample3D fused(int64_t t, double headingDeg, uint8_t flags = 0,
                              fusion::FoSource src = fusion::FoSource::Measured)
{
    fusion::Sample3D s;
    s.t_us = t; s.u = dirAt(headingDeg); s.flags = flags; s.foSrc = src; s.sigmaDeg = 1.5;
    return s;
}
static void addFit(std::vector<sd::FitFrame> &v, int64_t from, int64_t to, int64_t step, double headingDeg, uint8_t tier)
{
    for (int64_t t = from; t <= to; t += step) v.push_back({ t, dirAt(headingDeg), tier });
}

// What the grader reads at a phase, and from where.
struct Graded { bool fromCurve = false; double value = std::nan(""); };
static Graded grade(const MetricSeries &m, Phase ph)
{
    SeriesView view;
    view.t = m.t_us.data(); view.v = m.value.data(); view.n = m.t_us.size();
    view.valid = m.valid.size() == m.t_us.size() ? m.valid.data() : nullptr;
    const PhaseSample *ps = nullptr;
    for (const PhaseSample &p : m.phaseSamples) if (p.phase == ph) ps = &p;
    Graded g;
    if (!ps) return g;
    const Reduced r = reduceAt(view, ps->t_us, ReduceConfig{});
    if (r.ok) { g.fromCurve = true; g.value = r.value; }
    else g.value = ps->value;
    return g;
}
static const PhaseSample *sampleAt(const MetricSeries &m, Phase ph)
{
    for (const PhaseSample &p : m.phaseSamples) if (p.phase == ph) return &p;
    return nullptr;
}
static const GateRecord *gate(const ShaftDirectionOutput &o, const char *id)
{
    for (const GateRecord &g : o.gates) if (g.gate == QLatin1String(id)) return &g;
    return nullptr;
}
// The agreement the review asked for: graded value == stamped value, and the source is as expected.
static void agree(const ShaftDirectionOutput &o, Phase ph, const sd::Reading &r, sd::Source want, const char *what)
{
    const PhaseSample *ps = sampleAt(o.series, ph);
    const Graded g = grade(o.series, ph);
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s: graded %.4f° (%s) == stamped %.4f°, source %s", what, g.value,
                  g.fromCurve ? "curve median" : "phase fallback", ps ? ps->value : std::nan(""),
                  want == sd::Source::Fusion ? "fusion" : "fit");
    check(ps && std::isfinite(g.value) && near(g.value, ps->value, 1e-9) && r.src == want, buf);
}

int main()
{
    std::printf("shaft_direction_test\n");
    const int64_t P2 = 1000000, TOP = 1600000;

    // ── 1. fusion chosen 20 ms off; skeleton frames inside ±15 ms that the old curve left valid ──
    {
        ShaftDirectionInput in;
        in.p2Us = P2; in.topUs = TOP;
        for (int64_t t = P2 - 12000; t <= P2 + 12000; t += 8000)
            in.fused.push_back(fused(t, 175.0, fusion::IllConditioned));   // the end-on band
        in.fused.push_back(fused(P2 + 20000, 168.0));                        // the chosen frame: inside −12
        in.fused.push_back(fused(P2 + 28000, 167.0));
        for (int64_t t = TOP - 24000; t <= TOP + 24000; t += 8000) in.fused.push_back(fused(t, double(-6 + (t - TOP) / 8000)));
        addFit(in.fit, P2 - 60000, P2 + 60000, 4000, 172.0, 2);              // the skeleton, inside −8
        const ShaftDirectionOutput o = buildShaftDirection(in);
        check(o.ok, "§1 a series came out");
        bool fitValidInWindow = false;
        for (size_t i = 0; i < o.series.t_us.size(); ++i)
            if (std::llabs(o.series.t_us[i] - P2) <= pinpoint::tuned::sampler::kWindowHalfUs
                && (o.series.valid.empty() || o.series.valid[i]))
                fitValidInWindow = true;
        check(!fitValidInWindow, "§1 inside P2's ±15 ms no frame of the unchosen source (the skeleton) is valid");
        check(o.p2.src == sd::Source::Fusion && near(o.p2.deg, -12.0, 1e-9), "§1 P2 is the fused frame 20 ms off (−12)");
        agree(o, Phase::ShaftParallelBack, o.p2, sd::Source::Fusion, "§1 P2, fusion chosen, nothing valid in the window");
        agree(o, Phase::Top, o.top, sd::Source::Fusion, "§1 top, fusion chosen, several fused frames in the window");
        check(!gate(o, "club3d.shaftDirectionFromFit"),
              "§1 skeleton frames on the CURVE alone do not fire club3d.shaftDirectionFromFit");
    }

    // ── 2. fused and skeleton frames 25–30 ms apart, both valid inside the window before the fix ──
    {
        ShaftDirectionInput in;
        in.p2Us = P2;
        in.fused.push_back(fused(P2 - 40000, 170.0));
        in.fused.push_back(fused(P2 + 12000, 169.0));            // chosen (12 ms)
        in.fused.push_back(fused(P2 + 40000, 170.0));
        in.fit.push_back({ P2 - 14000, dirAt(176.0), 3 });       // 26 ms from the fused frame
        in.fit.push_back({ P2 - 15000, dirAt(176.0), 3 });
        const ShaftDirectionOutput o = buildShaftDirection(in);
        agree(o, Phase::ShaftParallelBack, o.p2, sd::Source::Fusion, "§2 P2, fusion chosen beside surviving skeleton frames");
        check(near(o.p2.deg, -11.0, 1e-9), "§2 …and the value is the fused frame's (−11), not a mix");
    }

    // ── 3. the skeleton chosen: no clean fused frame within ±25 ms, one 30 ms off ──
    ShaftDirectionOutput fitChosen;
    {
        ShaftDirectionInput in;
        in.p2Us = P2; in.topUs = TOP;
        for (int64_t t = P2 - 24000; t <= P2 + 24000; t += 8000)
            in.fused.push_back(fused(t, 175.0, t < P2 ? fusion::Reflected : fusion::SignDisagree));
        in.fused.push_back(fused(P2 + 30000, 165.0));             // clean, but outside ±25 ms
        in.fused.push_back(fused(P2 + 38000, 165.0));
        in.fused.push_back(fused(TOP, -4.0));
        in.fused.push_back(fused(TOP + 8000, -4.0));
        // Skeleton headings that vary, so a median of the wrong set would show.
        for (int64_t t = P2 - 40000; t <= P2 + 40000; t += 4000)
            in.fit.push_back({ t, dirAt(180.0 - double(t - P2) / 4000.0), 2 });
        const ShaftDirectionOutput o = buildShaftDirection(in);
        check(o.p2.src == sd::Source::Fit, "§3 P2 falls to the skeleton");
        agree(o, Phase::ShaftParallelBack, o.p2, sd::Source::Fit,
              "§3 P2, skeleton chosen; its frames near the 30 ms fused frame join the window");
        int fitInWindow = 0;
        for (size_t i = 0; i < o.series.t_us.size(); ++i)
            if (std::llabs(o.series.t_us[i] - P2) <= pinpoint::tuned::sampler::kWindowHalfUs
                && (o.series.valid.empty() || o.series.valid[i]))
                ++fitInWindow;
        check(fitInWindow == 7, "§3 …every skeleton frame inside ±15 ms is on the curve and valid (7)");
        check(near(o.p2.sigmaDeg, std::hypot(15.0 / std::cos(2.0 * fusion::kPi / 180.0), pinpoint::tuned::dtlCamera::kDtlYawSigmaUncalDeg), 1e-9),
              "§3 …σ = the one-view fit σ over |h| ⊕ σ_yaw (uncalibrated)");
        const GateRecord *ff = gate(o, "club3d.shaftDirectionFromFit");
        check(ff && ff->effect == GateEffect::Downgraded && ff->subject == QStringLiteral("shaftDirection")
                  && ff->covers == QStringList{ QStringLiteral("shaftDirection") }
                  && ff->evidence.value(QStringLiteral("code")).toString() == QStringLiteral("p2"),
              "§3 club3d.shaftDirectionFromFit fires, Downgraded, covering shaftDirection, code p2");
        fitChosen = o;
    }

    // ── 4. route, σ and the camera record: uncalibrated vs calibrated ──
    {
        const auto shot = [](bool calibrated) {
            ShaftDirectionInput in;
            in.p2Us = 1000000; in.topUs = 1600000; in.calibrated = calibrated;
            for (int64_t t = 960000; t <= 1040000; t += 8000) in.fused.push_back(fused(t, 172.0));
            for (int64_t t = 1560000; t <= 1640000; t += 8000) in.fused.push_back(fused(t, -9.0));
            return buildShaftDirection(in);
        };
        const ShaftDirectionOutput u = shot(false), c = shot(true);
        check(u.series.key == QStringLiteral("shaftDirection") && u.series.routeId == QStringLiteral("faceOn+dtl")
                  && !u.series.routeDirect,
              "§4 the series is shaftDirection on route faceOn+dtl, not Direct");
        check(u.series.sigma.has_value() && u.series.sigmaKind == uint8_t(SigmaKind::Propagated),
              "§4 the series carries σ, Propagated");
        const PhaseSample *up2 = sampleAt(u.series, Phase::ShaftParallelBack), *utop = sampleAt(u.series, Phase::Top);
        check(up2 && utop && up2->sigma && utop->sigma && up2->sigmaKind == uint8_t(SigmaKind::Propagated)
                  && utop->sigmaKind == uint8_t(SigmaKind::Propagated),
              "§4 both phase samples carry σ, Propagated");
        const double h = std::cos(2.0 * fusion::kPi / 180.0);
        check(up2 && near(*up2->sigma, std::hypot(1.5 / h, pinpoint::tuned::dtlCamera::kDtlYawSigmaUncalDeg), 1e-9), "§4 uncalibrated: σ = σ_dir/|h| ⊕ σ_yaw (uncalibrated)");
        const GateRecord *ca = gate(u, "club3d.cameraAssumed");
        check(ca && ca->effect == GateEffect::Downgraded && ca->subject == QStringLiteral("shaftDirection")
                  && ca->covers == QStringList{ QStringLiteral("shaftDirection") }
                  && near(ca->evidence.value(QStringLiteral("sigmaYawDeg")).toDouble(), pinpoint::tuned::dtlCamera::kDtlYawSigmaUncalDeg, 1e-12),
              "§4 uncalibrated: club3d.cameraAssumed, Downgraded, covering shaftDirection, σ_yaw (uncalibrated)");
        const PhaseSample *cp2 = sampleAt(c.series, Phase::ShaftParallelBack);
        check(cp2 && near(*cp2->sigma, std::hypot(1.5 / h, 1.0), 1e-9) && near(c.yawSigmaDeg, 1.0, 1e-12),
              "§4 calibrated: σ_yaw 1°");
        check(!gate(c, "club3d.cameraAssumed") && c.gates.empty(), "§4 calibrated: no camera record, no record at all");
        check(up2 && near(up2->value, -8.0, 1e-9) && utop && near(utop->value, 9.0, 1e-9),
              "§4 P2 inside reads −8, the top across reads +9");
        agree(u, Phase::ShaftParallelBack, u.p2, sd::Source::Fusion, "§4 P2");
        agree(u, Phase::Top, u.top, sd::Source::Fusion, "§4 top");
        ShaftDirectionInput off;
        off.p2Us = 1000000; off.withSigma = false;
        for (int64_t t = 960000; t <= 1040000; t += 8000) off.fused.push_back(fused(t, 172.0));
        const ShaftDirectionOutput o = buildShaftDirection(off);
        const PhaseSample *op2 = sampleAt(o.series, Phase::ShaftParallelBack);
        check(op2 && !op2->sigma && !o.series.sigma, "§4 uncertainty off: no σ written anywhere");
    }

    // ── 5. both positions from the fit: code both; none at all: no series ──
    {
        ShaftDirectionInput in;
        in.p2Us = P2; in.topUs = TOP;
        addFit(in.fit, P2 - 100000, TOP + 100000, 4000, 175.0, 3);
        const ShaftDirectionOutput o = buildShaftDirection(in);
        const GateRecord *ff = gate(o, "club3d.shaftDirectionFromFit");
        check(ff && ff->evidence.value(QStringLiteral("code")).toString() == QStringLiteral("both"),
              "§5 both positions from the fit ⇒ code both");
        agree(o, Phase::Top, o.top, sd::Source::Fit, "§5 top from the fit");
        ShaftDirectionInput none;
        none.p2Us = P2;
        check(!buildShaftDirection(none).ok, "§5 no fused track and no fit ⇒ no series");
    }
    (void)fitChosen;

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
