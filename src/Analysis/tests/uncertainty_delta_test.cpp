// The closed-form pieces of the shaft uncertainty budgets (src/Analysis/shaft_sigma.h,
// uncertainty_config.h, uncertainty_json.h; shaft_uncertainty_propagation_design.md §5–§7):
// each checked against a brute-force Monte Carlo or a central finite difference, and the JSON
// helpers' absent-means-unassessed contract.
//
//   ctest --test-dir <build> -R uncertainty_delta --output-on-failure

#include "../shaft_sigma.h"
#include "../uncertainty_config.h"
#include "../uncertainty_json.h"
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

int main()
{
    std::printf("=== uncertainty: closed forms ===\n");
    // lineAngleSigmaDeg: the angle of a segment whose ends carry independent isotropic σ.
    {
        const double L = 300.0, sa = 3.0, sb = 4.0;
        DetRng r(11u);
        double s2 = 0.0; const int n = 20000;
        for (int i = 0; i < n; ++i) {
            const double ax = sa * r.normal(), ay = sa * r.normal();
            const double bx = L + sb * r.normal(), by = sb * r.normal();
            const double a = std::atan2(by - ay, bx - ax) * shaftsigma::kRad2Deg;
            s2 += a * a;
        }
        const double mc = std::sqrt(s2 / n), cf = shaftsigma::lineAngleSigmaDeg(sa, sb, L);
        std::printf("    line angle σ: closed %.4f°, Monte Carlo %.4f°\n", cf, mc);
        check(std::abs(cf - mc) / mc < 0.05, "lineAngleSigmaDeg matches a Monte Carlo within 5%");
        check(!std::isfinite(shaftsigma::lineAngleSigmaDeg(1, 1, 0.5)), "no angle for a sub-pixel segment");
    }
    // timingTerm: |rate| × σ_t, the first-order change of a reading under a time shift.
    {
        const double rate = 1800.0, sT = 4000.0;   // deg/s, µs
        const auto f = [&](double tS) { return rate * tS; };
        const double fd = (f(sT * 1e-6) - f(-sT * 1e-6)) / 2.0;
        check(std::abs(shaftsigma::timingTerm(rate, sT) - std::abs(fd)) < 1e-9, "timingTerm = central difference of a linear reading");
        check(shaftsigma::timingTerm(rate, -1.0) == 0.0, "no timing σ ⇒ no term");
    }
    // sigmaAt: the larger σ of the two straddling samples; NaN where unassessed.
    {
        std::vector<ShaftSample2D> S(3);
        for (int i = 0; i < 3; ++i) { S[size_t(i)].t_us = i * 1000; S[size_t(i)].sigmaThetaDeg = float(1 + i); S[size_t(i)].pGross = 0.1f * i; }
        const shaftsigma::SigmaAt a = shaftsigma::sigmaAt(S, 1500);
        check(a.sigDeg == 3.0 && std::abs(a.pGross - 0.2) < 1e-6, "sigmaAt takes the worse of the straddling pair");
        S[1].sigmaThetaDeg = -1.f;
        check(!std::isfinite(shaftsigma::sigmaAt(S, 500).sigDeg), "an unassessed sample ⇒ NaN σ");
    }
    // The table lookup: base + slope·|θ̇|, clamped rows.
    {
        const double s0 = sigTableDeg(SigTier::Ray, SigGroup::Downswing, 0.0);
        check(s0 == pinpoint::tuned::uncertainty::kSigBaseDeg[1][4], "table base read from the calibrated constants");
        check(sigTableDeg(SigTier::Ray, SigGroup::Downswing, 10.0) == s0 + 10.0 * pinpoint::tuned::uncertainty::kSigSlope[1],
              "σ grows by slope × |θ̇|");
    }
    // Effective sample size.
    {
        UncertaintyConfig c; c.rho = 0.6;
        check(std::abs(c.nEff(100.0) - 25.0) < 1e-9, "n_eff = n(1−ρ)/(1+ρ)");
        c.rho = 0.0;
        check(c.nEff(10.0) == 10.0, "independent readings keep n");
    }
    // The JSON helpers: written only when assessed, read back as −1 when absent.
    {
        ShaftSample2D s;
        QJsonObject o;
        insertShaftSigma(o, s);
        check(o.isEmpty(), "an unassessed sample writes no σ keys (byte-identity when off)");
        s.sigmaThetaDeg = 2.5f; s.pGross = 0.125f; s.tier = 1;
        insertShaftSigma(o, s);
        ShaftSample2D r;
        readShaftSigma(o, r);
        check(r.sigmaThetaDeg == 2.5f && r.pGross == 0.125f && r.tier == 1, "σ / pGross / tier round-trip");
        ShaftSample2D r2;
        readShaftSigma(QJsonObject{}, r2);
        check(r2.sigmaThetaDeg < 0.f && r2.pGross < 0.f && r2.tier < 0, "absent keys read as not assessed");
        PhaseSample ps;
        QJsonObject po;
        insertPhaseSampleSigma(po, ps);
        check(po.isEmpty(), "a phase sample without σ writes nothing");
        ps.sigma = 1.5; ps.sigmaKind = 2; ps.grossRisk = 0.05f;
        insertPhaseSampleSigma(po, ps);
        check(po.value("sigma").toDouble() == 1.5 && po.value("sigmaKind").toInt() == 2, "phase sample σ written");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
