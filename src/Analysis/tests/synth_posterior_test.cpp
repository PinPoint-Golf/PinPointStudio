// The Layer C synth posterior (src/Analysis/shaft_synthesis.h fitSynthToEvidence(post),
// synthPosteriorSigma, synthDraws; shaft_uncertainty_propagation_design.md §4.5):
// the fit's MEAN never moves when a posterior is requested, a stretch with no evidence keeps the
// Hermite exactly, the closed-form σ matches the draws' spread, and the anchors' σ reaches the
// ticks through the bracket weights.
//
//   ctest --test-dir <build> -R synth_posterior --output-on-failure

#include "../shaft_synthesis.h"
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

static ShaftPosition anchor(int p, int64_t tUs, double thetaRad)
{
    ShaftPosition a;
    a.p = p; a.t_us = tUs; a.thetaRad = thetaRad; a.lenPx = 300.0; a.conf = 0.8f;
    a.gripPx = QPointF{ 500, 500 };
    a.headPx = QPointF{ 500 + 300 * std::cos(thetaRad), 500 + 300 * std::sin(thetaRad) };
    return a;
}

static std::vector<ShaftSample2D> hermite(const std::vector<ShaftPosition> &A, const SynthConfig &cfg)
{
    std::vector<int64_t> grid;
    for (int64_t t = A.front().t_us; t <= A.back().t_us; t += 4167) grid.push_back(t);
    std::vector<double> rates(A.size(), 4.0);
    std::vector<QPointF> gv(A.size(), QPointF(0, 0));
    return synthesizeBetweenAnchors(A, rates, rates, gv, grid, cfg);
}

int main()
{
    std::printf("=== synth posterior ===\n");
    SynthConfig cfg;
    const std::vector<ShaftPosition> A = { anchor(4, 0, 0.0), anchor(5, 120000, 0.5), anchor(6, 250000, 1.0) };
    const std::vector<ShaftSample2D> base = hermite(A, cfg);
    std::vector<SynthEvidence> ev;
    for (int k = 1; k < 10; ++k) ev.push_back({ int64_t(k) * 25000, 0.004 * k * 25.0 + 0.02 * ((k % 3) - 1), 3.0 * M_PI / 180.0 });

    // (a) the posterior is a by-product: the fitted mean is bit-identical with and without it.
    {
        std::vector<ShaftSample2D> s1 = base, s2 = base;
        fitSynthToEvidence(s1, A, ev, cfg);
        SynthPosterior post;
        fitSynthToEvidence(s2, A, ev, cfg, false, &post, 0.33);
        bool same = true;
        for (size_t i = 0; i < s1.size(); ++i) same = same && s1[i].thetaRad == s2[i].thetaRad && s1[i].thetaDotRadS == s2[i].thetaDotRadS;
        check(same, "requesting the posterior moves no tick");
        check(!post.stretches.empty(), "the posterior kept its stretch factor");
    }
    // (b) no evidence: the Hermite stands, the prior's factor is kept.
    {
        std::vector<ShaftSample2D> s = base;
        SynthPosterior post;
        fitSynthToEvidence(s, A, {}, cfg, false, &post, 1.0);
        bool same = true;
        for (size_t i = 0; i < s.size(); ++i) same = same && s[i].thetaRad == base[i].thetaRad;
        check(same, "a stretch with no evidence keeps the Hermite exactly");
        check(!post.stretches.empty(), "…and still carries the smoothness prior's factor");
    }
    // (c)(d) σ shape and the draws.
    {
        std::vector<ShaftSample2D> s = base;
        SynthPosterior post;
        fitSynthToEvidence(s, A, ev, cfg, false, &post, 1.0);
        post.anchorSigmaRad.assign(A.size(), 0.0);
        synthPosteriorSigma(s, A, post);
        size_t mid = 0, edge = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            if (std::llabs(s[i].t_us - 60000) < std::llabs(s[mid].t_us - 60000)) mid = i;
            if (std::llabs(s[i].t_us - 4167) < std::llabs(s[edge].t_us - 4167)) edge = i;
        }
        check(s[mid].sigmaThetaDeg > 0.f, "a tick between anchors carries σ");
        check(s[edge].sigmaThetaDeg < s[mid].sigmaThetaDeg, "σ is smallest beside a fixed anchor");
        DetRng rng(3u);
        const auto draws = synthDraws(s, A, post, 3000, rng);
        double m = 0.0, v = 0.0;
        for (const auto &d : draws) m += d[mid].thetaRad;
        m /= double(draws.size());
        for (const auto &d : draws) v += (d[mid].thetaRad - m) * (d[mid].thetaRad - m);
        const double sdDeg = std::sqrt(v / double(draws.size() - 1)) * 180.0 / M_PI;
        std::printf("    mid tick σ: closed form %.4f°, draws %.4f°\n", double(s[mid].sigmaThetaDeg), sdDeg);
        check(std::abs(sdDeg - s[mid].sigmaThetaDeg) / s[mid].sigmaThetaDeg < 0.1, "the selected-inverse σ matches the draws within 10%");
        check(std::abs(m - s[mid].thetaRad) * 180.0 / M_PI < 0.1 * s[mid].sigmaThetaDeg + 1e-6, "the draws are centred on the fitted curve");
    }
    // (e) an anchor's σ reaches the ticks beside it.
    {
        std::vector<ShaftSample2D> s = base;
        SynthPosterior post;
        fitSynthToEvidence(s, A, ev, cfg, false, &post, 1.0);
        post.anchorSigmaRad = { 0.0, 5.0 * M_PI / 180.0, 0.0 };
        synthPosteriorSigma(s, A, post);
        size_t near5 = 0;
        for (size_t i = 0; i < s.size(); ++i) if (std::llabs(s[i].t_us - 116000) < std::llabs(s[near5].t_us - 116000)) near5 = i;
        check(s[near5].sigmaThetaDeg > 4.5f, "a tick beside a 5° anchor inherits most of its σ");
    }
    // (f) κ < 1 inflates σ by 1/√κ and nothing else.
    {
        std::vector<ShaftSample2D> s1 = base, s2 = base;
        SynthPosterior p1, p2;
        fitSynthToEvidence(s1, A, ev, cfg, false, &p1, 1.0);
        fitSynthToEvidence(s2, A, ev, cfg, false, &p2, 0.25);
        p1.anchorSigmaRad.assign(A.size(), 0.0); p2.anchorSigmaRad = p1.anchorSigmaRad;
        synthPosteriorSigma(s1, A, p1);
        synthPosteriorSigma(s2, A, p2);
        check(std::abs(s2[5].sigmaThetaDeg / s1[5].sigmaThetaDeg - 2.0) < 1e-4, "κ = 1/4 doubles σ");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
