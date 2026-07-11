// Standalone tests for Layer C "synthesis between anchors"
// (src/Analysis/shaft_synthesis.h): C¹ Hermite interpolation of the shaft state
// between located P-anchors, flagged ShaftSynthesized. Synthetic anchors, no
// fixture, no OpenCV (Qt only, for QPointF/ShaftSample2D).
//
//   cmake --build build/analyzer-tests --target shaft_synthesis_test
//   ctest --test-dir build/analyzer-tests -R shaft_synthesis --output-on-failure

#include "../shaft_synthesis.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// A P-anchor whose head is consistent with grip + len·dir(theta) (so an endpoint
// evaluation reproduces it exactly). theta in RADIANS.
static ShaftPosition anchor(int p, int64_t tUs, double gx, double gy,
                            double thetaRad, double lenPx, float conf)
{
    ShaftPosition a;
    a.p = p; a.t_us = tUs; a.thetaRad = thetaRad; a.lenPx = lenPx; a.conf = conf;
    a.gripPx = QPointF{ gx, gy };
    a.headPx = QPointF{ gx + lenPx * std::cos(thetaRad), gy + lenPx * std::sin(thetaRad) };
    return a;
}

int main()
{
    std::printf("=== shaft_synthesis: C⁰ continuity at anchors ===\n");
    {
        SynthConfig cfg; cfg.enabled = true; cfg.midConfFrac = 0.6;
        // Small, monotone rotation (0.3→0.9 rad) so the Hermite unwrap k = 0 and the
        // raw theta is directly comparable at the far endpoint.
        const ShaftPosition a = anchor(1, 1'000'000, 500.0, 600.0, 0.30, 400.0, 0.80f);
        const ShaftPosition b = anchor(2, 2'000'000, 540.0, 520.0, 0.90, 420.0, 0.50f);
        const double sec = 1.0;                            // (b.t_us − a.t_us) in s
        const double thDotA = (b.thetaRad - a.thetaRad) / sec;   // secant-consistent slopes
        const double thDotB = thDotA;
        const QPointF gvA{ (b.gripPx.x() - a.gripPx.x()) / sec, (b.gripPx.y() - a.gripPx.y()) / sec };
        const QPointF gvB = gvA;

        // At the near anchor (τ→0): geometry equals anchor a exactly.
        const ShaftSample2D s0 = synthSampleAt(a, thDotA, gvA, b, thDotB, gvB, a.t_us, cfg);
        check(near(s0.gripPx.x(), a.gripPx.x(), 1e-6) && near(s0.gripPx.y(), a.gripPx.y(), 1e-6),
              "τ=0 grip == anchor a grip");
        check(near(s0.thetaRad, a.thetaRad, 1e-6), "τ=0 theta == anchor a theta");
        check(near(s0.headPx.x(), a.headPx.x(), 1e-6) && near(s0.headPx.y(), a.headPx.y(), 1e-6),
              "τ=0 head == anchor a head");
        check(near(s0.visibleLenPx, a.lenPx, 1e-6), "τ=0 len == anchor a len");
        // At the far anchor (τ→1): geometry equals anchor b exactly.
        const ShaftSample2D s1 = synthSampleAt(a, thDotA, gvA, b, thDotB, gvB, b.t_us, cfg);
        check(near(s1.gripPx.x(), b.gripPx.x(), 1e-6) && near(s1.gripPx.y(), b.gripPx.y(), 1e-6),
              "τ=1 grip == anchor b grip");
        check(near(s1.thetaRad, b.thetaRad, 1e-6), "τ=1 theta == anchor b theta");
        check(near(s1.headPx.x(), b.headPx.x(), 1e-6) && near(s1.headPx.y(), b.headPx.y(), 1e-6),
              "τ=1 head == anchor b head");
        // Every synthesized sample carries the flag and only the flag.
        check(s0.flags == ShaftSynthesized && s1.flags == ShaftSynthesized, "flags == ShaftSynthesized");
    }

    std::printf("=== shaft_synthesis: conf decay shape ===\n");
    {
        SynthConfig cfg; cfg.enabled = true; cfg.midConfFrac = 0.6;
        const ShaftPosition a = anchor(1, 0,        100, 100, 0.10, 300.0, 0.90f);
        const ShaftPosition b = anchor(2, 1'000'000, 200, 150, 0.40, 300.0, 0.50f);   // min conf = 0.5
        const QPointF gv{ 0.0, 0.0 };
        const double base = 0.5;   // min(0.9, 0.5)
        // Midpoint τ=0.5 ⇒ conf = base·midConfFrac.
        const ShaftSample2D mid = synthSampleAt(a, 0.3, gv, b, 0.3, gv, 500'000, cfg);
        check(near(double(mid.conf), base * cfg.midConfFrac, 1e-6), "τ=0.5 conf == min·midConfFrac");
        // Quarter τ=0.25 ⇒ decay = 1 − 0.4·(4·0.25·0.75) = 0.7.
        const ShaftSample2D q = synthSampleAt(a, 0.3, gv, b, 0.3, gv, 250'000, cfg);
        check(near(double(q.conf), base * 0.7, 1e-6), "τ=0.25 conf == min·0.7 (parabolic decay)");
    }

    std::printf("=== shaft_synthesis: θ unwrap across a 200° pair (track-consistent branch) ===\n");
    {
        SynthConfig cfg; cfg.enabled = true;
        const double d2r = synth_detail::kSynthPi / 180.0;
        // 10° → 210° (the +200° branch), NOT the shorter −150° wrap. Positive slopes
        // (CCW/increasing) declare the rotation direction.
        const ShaftPosition a = anchor(1, 0,         300, 400,  10.0 * d2r, 400.0, 0.8f);
        // b.thetaRad stored wrapped into (−π,π] the way an atan2 fit would report it.
        const ShaftPosition b = anchor(4, 1'000'000, 320, 380, (210.0 - 360.0) * d2r, 400.0, 0.8f);
        const double thDot = 200.0 * d2r / 1.0;   // +200°/s ⇒ expected Δθ ≈ +200°
        const QPointF gv{ 20.0, -20.0 };
        // At the far endpoint the Hermite reproduces the UNWRAPPED target = +210°,
        // not −150°.
        const ShaftSample2D s1 = synthSampleAt(a, thDot, gv, b, thDot, gv, b.t_us, cfg);
        check(near(s1.thetaRad, 210.0 * d2r, 1e-6), "τ=1 theta == +210° (unwrapped long way)");
        check(s1.thetaRad > 0.0, "τ=1 theta on the +branch, not −150°");
        // Interior monotone-increasing toward +110° at the middle (short branch would
        // dip toward −70°).
        const ShaftSample2D mid = synthSampleAt(a, thDot, gv, b, thDot, gv, 500'000, cfg);
        check(mid.thetaRad > a.thetaRad && near(mid.thetaRad, 110.0 * d2r, 5.0 * d2r),
              "midpoint theta ≈ +110° (track-consistent, not −70°)");
    }

    std::printf("=== shaft_synthesis: emission window (strictly-between, monotone, flags) ===\n");
    {
        SynthConfig cfg; cfg.enabled = true;
        // Three anchors ⇒ two spans; frames span well before/after the anchor range.
        std::vector<ShaftPosition> anchors = {
            anchor(1, 100'000, 400, 500, 0.20, 380.0, 0.7f),
            anchor(4, 300'000, 350, 350, 1.20, 400.0, 0.6f),
            anchor(7, 500'000, 500, 500, 2.40, 400.0, 0.8f),
        };
        std::vector<double>  thDot(anchors.size(), 1.0);
        std::vector<QPointF> gvel(anchors.size(), QPointF{ 0.0, 0.0 });
        std::vector<int64_t> frames;
        for (int64_t t = -50'000; t <= 550'000; t += 10'000) frames.push_back(t);   // includes exact anchor times

        const std::vector<ShaftSample2D> out =
            synthesizeBetweenAnchors(anchors, thDot, gvel, frames, cfg);

        check(!out.empty(), "non-empty between anchors");
        bool flagsOk = true, windowOk = true, monoOk = true, noAnchorFrame = true;
        for (size_t i = 0; i < out.size(); ++i) {
            if (out[i].flags != ShaftSynthesized) flagsOk = false;
            if (out[i].t_us <= anchors.front().t_us || out[i].t_us >= anchors.back().t_us) windowOk = false;
            if (out[i].t_us == 300'000) noAnchorFrame = false;   // the mid anchor time must be skipped
            if (i && out[i].t_us <= out[i - 1].t_us) monoOk = false;
        }
        check(flagsOk, "every sample flagged ShaftSynthesized");
        check(windowOk, "nothing emitted outside (first, last) anchor");
        check(noAnchorFrame, "frame exactly at an interior anchor is skipped (strictly between)");
        check(monoOk, "t_us strictly increasing");
        // Expected count: frames strictly inside (100k,300k) and (300k,500k).
        int expect = 0;
        for (int64_t t : frames)
            if ((t > 100'000 && t < 300'000) || (t > 300'000 && t < 500'000)) ++expect;
        check(int(out.size()) == expect, "one sample per strictly-interior frame in each span");
    }

    std::printf("=== shaft_synthesis: absent-anchor subset (2 anchors ⇒ one span) ===\n");
    {
        SynthConfig cfg; cfg.enabled = true;
        // Only P1 and P4 located (P2/P3 absent) — a single span, no emission after P4.
        std::vector<ShaftPosition> anchors = {
            anchor(1, 0,        400, 500, 0.10, 380.0, 0.7f),
            anchor(4, 100'000,  350, 350, 1.10, 400.0, 0.6f),
        };
        std::vector<double>  thDot(2, 1.0);
        std::vector<QPointF> gvel(2, QPointF{ 0.0, 0.0 });
        std::vector<int64_t> frames;
        for (int64_t t = 0; t <= 200'000; t += 5'000) frames.push_back(t);

        const std::vector<ShaftSample2D> out =
            synthesizeBetweenAnchors(anchors, thDot, gvel, frames, cfg);
        bool oneSpan = !out.empty();
        for (const ShaftSample2D &s : out)
            if (s.t_us <= 0 || s.t_us >= 100'000) oneSpan = false;   // nothing past the last anchor
        check(oneSpan, "2 anchors ⇒ exactly one span, no emission past the last anchor");

        // < 2 anchors ⇒ nothing at all.
        std::vector<ShaftPosition> one(anchors.begin(), anchors.begin() + 1);
        const std::vector<ShaftSample2D> none =
            synthesizeBetweenAnchors(one, {1.0}, { QPointF{0.0, 0.0} }, frames, cfg);
        check(none.empty(), "< 2 anchors ⇒ empty result");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
