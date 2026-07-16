// Standalone tests for the motion-overlay pose synthesis
// (src/Analysis/pose_synthesis.h): C¹ Hermite temporal upsampling of the
// offline-smoothed skeleton onto a dense grid, viz-only. Synthetic frames, no
// fixture, no OpenCV (Qt only, for QPointF/PoseFrame2D).
//
//   cmake --build build/analyzer-tests --target pose_synthesis_test
//   ctest --test-dir build/analyzer-tests -R pose_synthesis --output-on-failure

#include "../pose_synthesis.h"

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
static bool near(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// A smoothed frame with joints j5 and j6 set (x,y,conf); all other joints Off.
static PoseFrame2D frame(int64_t tUs, double x5, double y5, float c5,
                         double x6, double y6, float c6)
{
    PoseFrame2D f;
    f.t_us = tUs;
    f.kp[5] = QPointF{ x5, y5 }; f.conf[5] = c5;
    f.kp[6] = QPointF{ x6, y6 }; f.conf[6] = c6;
    return f;
}

// Find the synth sample at exactly t_us (nullptr if absent).
static const PoseFrame2D *at(const std::vector<PoseFrame2D> &v, int64_t t)
{
    for (const PoseFrame2D &f : v) if (f.t_us == t) return &f;
    return nullptr;
}

int main()
{
    // A joint moving linearly (x 0.1→0.4 over 4 frames, y const) — a Hermite with
    // Catmull-Rom tangents reproduces the straight line exactly at any tau; a joint
    // that goes Off (conf 0) at frame 2 must stay Off across every bracket touching it.
    std::vector<PoseFrame2D> sm = {
        frame(0,        0.10, 0.50, 0.80f,  0.10, 0.20, 0.80f),
        frame(1'000'000, 0.20, 0.50, 0.80f,  0.20, 0.20, 0.80f),
        frame(2'000'000, 0.30, 0.50, 0.80f,  0.30, 0.20, 0.00f),   // j6 Off here
        frame(3'000'000, 0.40, 0.50, 0.80f,  0.40, 0.20, 0.80f),
    };
    PoseSynthConfig cfg;                 // enabled, 240 Hz default
    cfg.rateHz = 2.0;                    // step 0.5 s ⇒ grid lands on 0,0.5,1.0,1.5,… s

    const std::vector<PoseFrame2D> out = synthesizePoseTrack(sm, cfg);

    std::printf("=== pose_synthesis: grid, window, monotonic ===\n");
    check(!out.empty(), "non-empty");
    bool windowOk = true, monoOk = true;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].t_us < 0 || out[i].t_us > 3'000'000) windowOk = false;
        if (i && out[i].t_us <= out[i - 1].t_us) monoOk = false;
    }
    check(windowOk, "every sample within [t0, t1]");
    check(monoOk, "t_us strictly increasing");

    std::printf("=== pose_synthesis: C⁰ at endpoints ===\n");
    const PoseFrame2D *s0 = at(out, 0);
    const PoseFrame2D *s3 = at(out, 3'000'000);
    check(s0 && near(s0->kp[5].x(), 0.10, 1e-6) && near(double(s0->conf[5]), 0.80, 1e-6),
          "t=t0 reproduces frame 0 (j5)");
    check(s3 && near(s3->kp[5].x(), 0.40, 1e-6), "t=t1 reproduces frame 3 (j5)");

    std::printf("=== pose_synthesis: linear reproduction at bracket midpoint ===\n");
    const PoseFrame2D *mid = at(out, 1'500'000);   // bracket [1,2], tau=0.5
    check(mid && near(mid->kp[5].x(), 0.25, 1e-6), "j5 x == 0.25 (on the line)");
    check(mid && near(mid->kp[5].y(), 0.50, 1e-6), "j5 y == 0.50 (const)");
    check(mid && near(double(mid->conf[5]), 0.80, 1e-6), "j5 conf == 0.80 (interp)");

    std::printf("=== pose_synthesis: Off joint stays Off across touching brackets ===\n");
    // j6 is Off at frame 2 ⇒ brackets [1,2] and [2,3] emit it Off.
    const PoseFrame2D *b12 = at(out, 1'500'000);
    const PoseFrame2D *b23 = at(out, 2'500'000);
    const PoseFrame2D *b01 = at(out,   500'000);   // bracket [0,1] — both ends valid
    check(b12 && b12->conf[6] == 0.f, "bracket [1,2] j6 Off (conf 0)");
    check(b23 && b23->conf[6] == 0.f, "bracket [2,3] j6 Off (conf 0)");
    check(b01 && b01->conf[6] > 0.f && near(b01->kp[6].x(), 0.15, 1e-6),
          "bracket [0,1] j6 drawn (conf>0, x==0.15)");

    std::printf("=== pose_synthesis: disabled / degenerate ⇒ empty ===\n");
    PoseSynthConfig off; off.enabled = false;
    check(synthesizePoseTrack(sm, off).empty(), "enabled=false ⇒ empty");
    std::vector<PoseFrame2D> one(sm.begin(), sm.begin() + 1);
    check(synthesizePoseTrack(one, cfg).empty(), "< 2 frames ⇒ empty");
    PoseSynthConfig zero; zero.rateHz = 0.0;
    check(synthesizePoseTrack(sm, zero).empty(), "rateHz<=0 ⇒ empty");

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
