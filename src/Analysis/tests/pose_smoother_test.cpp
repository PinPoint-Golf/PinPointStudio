// Standalone tests for the Phase-1 motion-overlay pose smoother
// (src/Analysis/pose_smoother — smoothPoseTrack). Pure synthetic: a noisy
// circular wrist arc + deterministic LCG noise; no frames, no OpenCV. Mirrors
// clubhead_temporal_test's style (own main(), check()-macros, no googletest).
//
//   cmake --build build/analyzer-tests --target pose_smoother_test
//   ctest --test-dir build/analyzer-tests -R pose_smoother --output-on-failure

#include "../pose_smoother.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static constexpr int    W = 1920, H = 1080;
static constexpr int    KP = 9;                 // COCO left_wrist — the tracked joint
static constexpr double kPi = 3.14159265358979323846;

// Deterministic uniform [0,1) — a 64-bit LCG (NO std::random). Box-Muller on top
// for a repeatable Gaussian-ish measurement noise.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    double u01() { s = s * 6364136223846793005ULL + 1442695040888963407ULL;
                   return double((s >> 11) & ((1ULL << 53) - 1)) / double(1ULL << 53); }
    double gauss() { double u1 = u01(); if (u1 < 1e-12) u1 = 1e-12;
                     return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u01()); }
};

// Ground-truth wrist arc in PIXELS at time t (s): a circle swept at ω rad/s.
struct Arc { double cx, cy, R, w, a0; };
static void truthPx(const Arc &a, double t, double &px, double &py)
{
    const double ang = a.a0 + a.w * t;
    px = a.cx + a.R * std::cos(ang);
    py = a.cy + a.R * std::sin(ang);
}

// Build a track that samples the arc at the given absolute times (s). KP carries
// the arc + noise at `conf`; a time inside [gapLo,gapHi) is dropped to gapConf.
// Every other keypoint is left at conf 0 (⇒ Off, ignored by these tests).
static std::vector<PoseFrame2D> buildTrack(const Arc &a, const std::vector<double> &times,
                                           double noisePx, float conf, Lcg &rng,
                                           double gapLo = 1e30, double gapHi = 1e30,
                                           float gapConf = 0.05f)
{
    std::vector<PoseFrame2D> f(times.size());
    for (std::size_t i = 0; i < times.size(); ++i) {
        const double t = times[i];
        double px, py; truthPx(a, t, px, py);
        px += noisePx * rng.gauss();
        py += noisePx * rng.gauss();
        f[i].t_us = int64_t(std::llround(t * 1e6));
        f[i].kp[KP]   = QPointF(px / double(W), py / double(H));
        f[i].conf[KP] = (t >= gapLo && t < gapHi) ? gapConf : conf;
    }
    return f;
}

static std::vector<double> uniformTimes(double rate, double T)
{
    std::vector<double> t;
    for (int i = 0; i * (1.0 / rate) < T; ++i) t.push_back(i / rate);
    return t;
}

// Mean / max Euclidean residual (px) of the KP output vs the arc truth.
struct Resid { double mean, max; int n; };
static Resid residual(const Arc &a, const std::vector<PoseFrame2D> &out)
{
    double sum = 0.0, mx = 0.0; int n = 0;
    for (const auto &fr : out) {
        double px, py; truthPx(a, double(fr.t_us) * 1e-6, px, py);
        const double dx = fr.kp[KP].x() * W - px, dy = fr.kp[KP].y() * H - py;
        const double d = std::hypot(dx, dy);
        sum += d; mx = std::max(mx, d); ++n;
    }
    return { n ? sum / n : 0.0, mx, n };
}
static Resid residualRaw(const Arc &a, const std::vector<PoseFrame2D> &in)
{
    return residual(a, in);   // raw frames carry the same KP-vs-truth geometry
}

int main()
{
    const Arc arc{ 960.0, 540.0, 400.0, 5.0, 0.3 };   // R=400px, ω=5 rad/s ⇒ ~2000 px/s tip speed

    // ── 1. noisy arc: smoothed residual ≪ raw residual ───────────────────────
    std::printf("=== smoothPoseTrack: noise reduction on a swing arc ===\n");
    {
        Lcg rng(0x1234ABCDu);
        const auto times = uniformTimes(150.0, 0.6);
        const auto in    = buildTrack(arc, times, 4.0, 0.8f, rng);
        const auto res   = smoothPoseTrack(in, W, H);
        check(res.smoothed.size() == in.size() && res.aux.size() == in.size(),
              "one smoothed frame + aux per input frame");
        const Resid raw = residualRaw(arc, in);
        const Resid sm  = residual(arc, res.smoothed);
        std::printf("       raw mean=%.2fpx  smoothed mean=%.2fpx (max %.2fpx)\n",
                    raw.mean, sm.mean, sm.max);
        check(sm.mean < 0.6 * raw.mean, "smoothed mean residual < 0.6 x raw");
        check(sm.mean < 3.0, "smoothed mean residual is small in absolute px");
        int nMeas = 0; for (const auto &ax : res.aux) if (ax.tier[KP] == uint8_t(PoseTier::Meas)) ++nMeas;
        check(nMeas > int(in.size()) * 3 / 4, "a strong confident run is mostly meas-tier");
    }

    // ── 2. confidence dropout is bridged (no spike), tiers Pred, conf ≥ 0.5 ───
    std::printf("=== smoothPoseTrack: dropout bridged without a spike ===\n");
    {
        const Arc slow{ 960.0, 540.0, 400.0, 3.0, 0.3 };   // gentler so the bridge is honest
        Lcg rng(0x55AA1234u);
        const auto times = uniformTimes(150.0, 0.7);
        const double gLo = 0.30, gHi = 0.45;               // 150 ms occlusion mid-arc
        const auto in  = buildTrack(slow, times, 3.0, 0.8f, rng, gLo, gHi);
        const auto res = smoothPoseTrack(in, W, H);

        bool allPred = true, allConf = true; double gapMax = 0.0; int nGap = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const double t = double(in[i].t_us) * 1e-6;
            if (t < gLo || t >= gHi) continue;
            ++nGap;
            if (res.aux[i].tier[KP] != uint8_t(PoseTier::Pred)) allPred = false;
            if (res.smoothed[i].conf[KP] < 0.5f) allConf = false;
            double px, py; truthPx(slow, t, px, py);
            gapMax = std::max(gapMax, std::hypot(res.smoothed[i].kp[KP].x() * W - px,
                                                 res.smoothed[i].kp[KP].y() * H - py));
        }
        std::printf("       gap frames=%d  max deviation across gap=%.2fpx\n", nGap, gapMax);
        check(nGap > 15, "the 150 ms gap spans several frames");
        check(allPred, "every bridged gap frame is Pred tier");
        check(allConf, "every bridged gap frame paints at conf >= 0.5");
        check(gapMax < 30.0, "bridge deviation is bounded (no spike across the gap)");
    }

    // ── 3. determinism: identical input ⇒ byte-identical output ───────────────
    std::printf("=== smoothPoseTrack: determinism ===\n");
    {
        Lcg rng(0xDEADBEEFu);
        const auto in = buildTrack(arc, uniformTimes(120.0, 0.5), 4.0, 0.75f, rng);
        const auto a = smoothPoseTrack(in, W, H);
        const auto b = smoothPoseTrack(in, W, H);
        bool same = a.smoothed.size() == b.smoothed.size() && a.aux.size() == b.aux.size();
        for (std::size_t i = 0; same && i < a.smoothed.size(); ++i) {
            for (int k = 0; k < 17 && same; ++k) {
                same = a.smoothed[i].kp[k] == b.smoothed[i].kp[k]
                    && a.smoothed[i].conf[k] == b.smoothed[i].conf[k]
                    && a.aux[i].tier[k] == b.aux[i].tier[k]
                    && a.aux[i].sigma[k] == b.aux[i].sigma[k];
            }
            same = same && a.smoothed[i].t_us == b.smoothed[i].t_us;
        }
        check(same, "two runs are exactly equal on every value");
    }

    // ── 4. fps-independence: 150 fps and 30 fps recover the truth similarly ───
    std::printf("=== smoothPoseTrack: fps-independence ===\n");
    {
        const Arc a4{ 960.0, 540.0, 400.0, 4.0, 0.3 };
        Lcg r150(0xABC0001u), r30(0xABC0002u);
        const auto in150 = buildTrack(a4, uniformTimes(150.0, 0.6), 3.0, 0.85f, r150);
        const auto in30  = buildTrack(a4, uniformTimes(30.0,  0.6), 3.0, 0.85f, r30);
        const Resid s150 = residual(a4, smoothPoseTrack(in150, W, H).smoothed);
        const Resid s30  = residual(a4, smoothPoseTrack(in30,  W, H).smoothed);
        std::printf("       150fps mean=%.2fpx (n=%d)   30fps mean=%.2fpx (n=%d)\n",
                    s150.mean, s150.n, s30.mean, s30.n);
        check(s150.mean < 4.0 && s30.mean < 4.5, "both frame rates recover the arc to a few px");
        const double hi = std::max(s150.mean, s30.mean), lo = std::min(s150.mean, s30.mean);
        check(hi < 2.5 * std::max(lo, 1e-6), "residuals are within a similar band (fps-independent)");
    }

    // ── 5. non-uniform sampling: mixed dt doesn't break segmentation ──────────
    std::printf("=== smoothPoseTrack: non-uniform sampling ===\n");
    {
        // Coarse address (~90 ms), then a dense ~150 fps burst through 'impact',
        // then a ~40 fps sparse follow-through — the real PoseRunner cadence.
        std::vector<double> times; double t = 0.0;
        for (int i = 0; i < 4;  ++i) { times.push_back(t); t += 0.090; }   // coarse
        for (int i = 0; i < 40; ++i) { times.push_back(t); t += 1.0/150; } // dense
        for (int i = 0; i < 12; ++i) { times.push_back(t); t += 1.0/40;  } // sparse
        Lcg rng(0x0FF1CEu);
        const auto in  = buildTrack(arc, times, 3.5, 0.8f, rng);
        const auto res = smoothPoseTrack(in, W, H);

        int nOff = 0; double jump = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i)
            if (res.aux[i].tier[KP] == uint8_t(PoseTier::Off)) ++nOff;
        for (std::size_t i = 1; i < res.smoothed.size(); ++i)   // px jump between neighbours
            jump = std::max(jump, std::hypot(
                (res.smoothed[i].kp[KP].x() - res.smoothed[i-1].kp[KP].x()) * W,
                (res.smoothed[i].kp[KP].y() - res.smoothed[i-1].kp[KP].y()) * H));
        const Resid sm = residual(arc, res.smoothed);
        std::printf("       nOff=%d  max neighbour jump=%.1fpx  mean residual=%.2fpx\n",
                    nOff, jump, sm.mean);
        check(nOff == 0, "one confident segment spans the whole track (no spurious Off)");
        check(sm.mean < 5.0, "mixed-dt track still recovers the arc");
        check(jump < 260.0, "no discontinuity at a stride boundary (jump ~ true motion)");
    }

    // ── 6. tier honesty: a never-confident keypoint stays Off / passthrough ───
    std::printf("=== smoothPoseTrack: tier honesty (Off passthrough) ===\n");
    {
        std::vector<PoseFrame2D> in(30);
        for (int i = 0; i < 30; ++i) {
            in[i].t_us = int64_t(i) * 6667;                 // ~150 fps
            in[i].kp[KP]   = QPointF(0.2 + 0.01 * i, 0.6 - 0.005 * i);
            in[i].conf[KP] = 0.2f;                          // always below confMeasMin (0.35)
        }
        const auto res = smoothPoseTrack(in, W, H);
        bool off = true, passthrough = true, zeroConf = true, zeroSig = true;
        for (int i = 0; i < 30; ++i) {
            off         &= res.aux[i].tier[KP]   == uint8_t(PoseTier::Off);
            passthrough &= res.smoothed[i].kp[KP] == in[i].kp[KP];
            zeroConf    &= res.smoothed[i].conf[KP]  == 0.0f;
            zeroSig     &= res.aux[i].sigma[KP]      == 0.0f;
        }
        check(off, "sub-threshold keypoint never leaves Off tier");
        check(passthrough, "Off output is the raw input kp, byte-identical");
        check(zeroConf, "Off output conf is 0 (paint-alpha contract)");
        check(zeroSig, "Off output sigma is 0 (no smoothed value)");
    }

    // ── degenerate inputs ─────────────────────────────────────────────────────
    std::printf("=== smoothPoseTrack: degenerate ===\n");
    {
        check(smoothPoseTrack({}, W, H).smoothed.empty(), "empty input ⇒ empty output");
        std::vector<PoseFrame2D> one(1);
        one[0].kp[KP] = QPointF(0.5, 0.5); one[0].conf[KP] = 0.9f;
        const auto r = smoothPoseTrack(one, W, H);
        check(r.smoothed.size() == 1 && r.aux[0].tier[KP] == uint8_t(PoseTier::Off),
              "a lone frame (<2 steps) has no smoothed value ⇒ Off passthrough");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
