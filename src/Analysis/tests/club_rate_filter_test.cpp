// Standalone tests for the club rate filter (src/Club/club_rate_filter.h,
// docs/design/club_rate_filter_design.md): one robust Kalman + RTS estimate of the face-on shaft
// angle and its rate through the downswing. A synthetic downswing with a KNOWN θ(t) — the rate a
// smooth hump peaking 20 ms before impact — sampled at 240 fps with noise, then the tracker's two
// failure modes injected: the stall on the legs through impact and a wrong-branch jump (KB-18 / #22).
// No fixture, no OpenCV, no Qt.
//
//   ninja -C build/tests -j8 club_rate_filter_test
//   ctest --test-dir build/tests -R club_rate_filter --output-on-failure

#include "../../Club/club_rate_filter.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

constexpr double kPi  = 3.14159265358979323846;
constexpr double kD2R = kPi / 180.0;

constexpr int64_t kTop    = 1'000'000;
constexpr int64_t kImpact = 1'300'000;
constexpr int64_t kEnd    = 1'400'000;
constexpr int64_t kFrame  = 4167;             // 240 fps
constexpr double  kPeakDps = 2200.0;
constexpr int64_t kPeakUs  = kImpact - 20000;  // the true peak, 20 ms before the ball

// The true rate (°/s): a Gaussian hump on the downswing, decaying after impact at a lower level.
double trueRate(int64_t t)
{
    if (t <= kImpact) {
        const double u = double(t - kPeakUs) / 110000.0;
        return kPeakDps * std::exp(-u * u);
    }
    return 0.75 * trueRate(kImpact) * std::exp(-double(t - kImpact) / 80000.0);
}

// θ(t) in radians by integrating the rate from the top (θ = 1.0 rad there).
double trueTheta(int64_t t)
{
    double th = 1.0;
    const int64_t step = 100;
    for (int64_t s = kTop - 60000; s < t; s += step) th += trueRate(s) * kD2R * double(step) * 1e-6;
    return th;
}

struct Rng {
    std::mt19937 g{ 12345 };
    std::normal_distribution<double> n{ 0.0, 1.0 };
    double normal() { return n(g); }
};

std::vector<ClubFilterObs> frames(Rng &rng, double sigmaDeg)
{
    std::vector<ClubFilterObs> o;
    for (int64_t t = kTop - 60000; t <= kEnd; t += kFrame)
        o.push_back({ t, trueTheta(t) + rng.normal() * sigmaDeg * kD2R, sigmaDeg * kD2R, ClubFilterObs::Frame });
    return o;
}

std::vector<int64_t> grid()
{
    std::vector<int64_t> g;
    for (int64_t t = kTop - 80000; t <= kEnd; t += 4167 / 2) g.push_back(t);   // ~480 Hz ticks
    return g;
}

ClubRateFilterConfig onCfg()
{
    ClubRateFilterConfig c;
    c.enabled = true;
    return c;
}

size_t nearestTick(const std::vector<int64_t> &g, int64_t t)
{
    size_t best = 0;
    for (size_t k = 0; k < g.size(); ++k)
        if (std::llabs(g[k] - t) < std::llabs(g[best] - t)) best = k;
    return best;
}

} // namespace

int main()
{
    const std::vector<int64_t> g = grid();

    std::printf("=== club_rate_filter: a clean downswing with honest noise ===\n");
    {
        Rng rng;
        const auto obs = frames(rng, 1.5);
        const ClubRateFilterResult r = runClubRateFilter(g, obs, kTop, kImpact, kEnd, onCfg());
        check(r.stats.ran, "the filter runs on a downswing with readings");
        int in2 = 0, n = 0;
        double worst = 0.0, peakT = 0.0, peakV = -1.0;
        for (size_t k = 0; k < g.size(); ++k) {
            if (!r.covered[k] || g[k] < kTop || g[k] > kImpact) continue;
            const double est = r.thetaDotRadS[k] / kD2R, tru = trueRate(g[k]);
            const double sig = r.sigmaThetaDotRadS[k] / kD2R;
            ++n; if (std::fabs(est - tru) <= 2.0 * sig) ++in2;
            if (g[k] > kTop + 30000) worst = std::max(worst, std::fabs(est - tru));
            if (est > peakV) { peakV = est; peakT = double(g[k]); }
        }
        std::printf("      %d of %d downswing ticks inside 2σ; worst |error| %.0f °/s; peak at %+.1f ms (true %+.1f)\n",
                    in2, n, worst, (peakT - double(kImpact)) / 1000.0, double(kPeakUs - kImpact) / 1000.0);
        check(n > 100 && in2 >= int(0.9 * n), "the rate is inside 2σ of the truth on ≥ 90 % of the downswing");
        check(worst < 200.0, "…and never more than 200 °/s off once the club is moving");
        // The true hump is flat on top (it falls only 72 °/s from its peak to the ball), so the peak's
        // INSTANT is not resolvable to a frame by any estimator; what matters is that no early hump
        // appears and the peak's height is right.
        check(peakT >= double(kImpact) - 40000.0, "no early hump: the peak is within 40 ms of impact");
        check(std::fabs(peakV - kPeakDps) < 150.0, "…and its height is within 150 °/s of the true peak");
        check(r.stats.nisMean > 0.5 && r.stats.nisMean < 1.6, "the innovations are the size σθ says (mean NIS ≈ 1)");
        check(r.stats.humpsFiltered == 1, "the filtered rate speeds up once");
        bool before = false, after = false;
        for (size_t k = 0; k < g.size(); ++k) {
            if (g[k] < kTop && r.covered[k]) before = true;
            if (g[k] > kImpact && r.covered[k]) after = true;
        }
        check(!before, "ticks before the top are left to the synth");
        check(after, "the after-impact segment runs on its own");
        const size_t kI = nearestTick(g, kImpact - 2000), kA = nearestTick(g, kImpact + 6000);
        check(r.thetaDotRadS[kA] / kD2R < r.thetaDotRadS[kI] / kD2R,
              "the collision is a step: the rate just after impact is below the rate just before");
    }

    std::printf("=== club_rate_filter: the stall on the legs and a wrong branch ===\n");
    {
        Rng rng;
        auto obs = frames(rng, 1.5);
        int injected = 0;
        // The legs: the tracker holds the angle it had 25 ms before impact for the last 25 ms.
        double held = 0.0; bool holding = false;
        for (ClubFilterObs &o : obs) {
            if (o.t_us >= kImpact - 25000 && o.t_us <= kImpact) {
                if (!holding) { held = o.thetaRad; holding = true; continue; }
                o.thetaRad = held; ++injected;
            }
            // A wrong branch: one frame 25° off mid-downswing.
            if (o.t_us > kTop + 150000 && o.t_us <= kTop + 150000 + kFrame) { o.thetaRad += 25.0 * kD2R; ++injected; }
        }
        obs.push_back({ kImpact, trueTheta(kImpact), 8.0 * kD2R, ClubFilterObs::Ball });
        const ClubRateFilterResult r = runClubRateFilter(g, obs, kTop, kImpact, kEnd, onCfg());
        const size_t k = nearestTick(g, kImpact - 5000);
        const double est = r.thetaDotRadS[k] / kD2R, tru = trueRate(g[k]);
        std::printf("      %d frames injected; gated %d, down-weighted %d; rate 5 ms before impact %.0f (true %.0f)\n",
                    injected, r.stats.nGated, r.stats.nDownweighted, est, tru);
        check(r.stats.nGated + r.stats.nDownweighted >= injected, "every injected frame is gated or down-weighted");
        check(std::fabs(est - tru) < 0.15 * tru, "…and the rate through impact stays within 15 % of the truth");
        check(r.stats.humpsFiltered == 1, "…and the filtered rate still speeds up once");
        check(r.stats.humpsRaw >= 2, "the raw rate shows the wobble the diagnostic is there to count");
        check(r.stats.rateWobble > 0.3, "…and the wobble statistic says so");
    }

    std::printf("=== club_rate_filter: countHumps ===\n");
    {
        check(countHumps({ 1, 2, 3, 4, 5 }) == 1, "a rate still rising at the window's end is one hump");
        check(countHumps({ 1, 5, 4.8, 5.05, 4.9, 5.1 }) == 1, "ripples on a flat top are not humps");
        check(countHumps({ 1, 10, 6, 11, 3 }) == 2, "a dip of 40 % and a recovery is two");
    }

    std::printf("=== club_rate_filter: σ ===\n");
    {
        Rng rng1, rng2;
        auto plain = frames(rng1, 1.5);
        auto withEdges = frames(rng2, 1.5);
        for (int64_t t = kImpact - 40000; t < kImpact; t += kFrame / 3)
            withEdges.push_back({ t, trueTheta(t), 2.0 * kD2R, ClubFilterObs::Wedge });
        const ClubRateFilterResult a = runClubRateFilter(g, plain, kTop, kImpact, kEnd, onCfg());
        const ClubRateFilterResult b = runClubRateFilter(g, withEdges, kTop, kImpact, kEnd, onCfg());
        const size_t k = nearestTick(g, kImpact - 10000);
        check(b.sigmaThetaDotRadS[k] < a.sigmaThetaDotRadS[k], "blur-wedge edges near impact tighten σθ̇ there");

        Rng rng3;
        auto gap = frames(rng3, 1.5);
        std::vector<ClubFilterObs> kept;
        for (const ClubFilterObs &o : gap)
            if (!(o.t_us > kTop + 100000 && o.t_us < kTop + 150000)) kept.push_back(o);
        const ClubRateFilterResult c = runClubRateFilter(g, kept, kTop, kImpact, kEnd, onCfg());
        const size_t mid = nearestTick(g, kTop + 125000), meas = nearestTick(g, kTop + 200000);
        check(c.sigmaThetaRad[mid] > 1.5 * c.sigmaThetaRad[meas], "σθ grows across a gap in the readings");
    }

    std::printf("=== club_rate_filter: off, and the posterior draws ===\n");
    {
        Rng rng;
        const auto obs = frames(rng, 1.5);
        ClubRateFilterConfig off = onCfg(); off.enabled = false;
        const ClubRateFilterResult r0 = runClubRateFilter(g, obs, kTop, kImpact, kEnd, off);
        bool any = false; for (char c : r0.covered) any = any || c;
        check(!any && !r0.stats.ran, "disabled ⇒ nothing covered, nothing ran");

        const ClubRateFilterResult r = runClubRateFilter(g, obs, kTop, kImpact, kEnd, onCfg());
        const ClubFilterSegment &seg = *r.segments.front();
        size_t j = 0;
        for (size_t q = 0; q < seg.tick.size(); ++q)
            if (std::llabs(g[seg.tick[q]] - (kImpact - 30000)) < std::llabs(g[seg.tick[j]] - (kImpact - 30000))) j = q;
        Rng dr;
        const int N = 400;
        double sum = 0.0, sum2 = 0.0;
        std::vector<double> p, v;
        for (int d = 0; d < N; ++d) {
            seg.kf.sample(dr, p, v);
            const double x = v[size_t(seg.step[j])];
            sum += x; sum2 += x * x;
        }
        const double mean = sum / N, sd = std::sqrt(std::max(0.0, sum2 / N - mean * mean));
        const double mu = r.thetaDotRadS[seg.tick[j]], sg = r.sigmaThetaDotRadS[seg.tick[j]];
        std::printf("      draws: mean %.2f vs %.2f rad/s, SD %.3f vs σ %.3f\n", mean, mu, sd, sg);
        check(std::fabs(mean - mu) < 4.0 * sg / std::sqrt(double(N)), "the draws centre on the smoothed rate");
        check(sd > 0.85 * sg && sd < 1.15 * sg, "…and spread by its σ");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASS", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
