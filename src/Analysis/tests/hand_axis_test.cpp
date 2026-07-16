// Standalone tests for the shared COCO-WholeBody hand geometry (WB4,
// src/Analysis/hand_axis.h): handCentroid, computeGripAnchors (the
// pose.gripFromSmoothedHands recompute) and handAxisDirection (the ShaftTracker
// θ prior). Pure, header-only — no OpenCV, no fixture. Own main()/check() macros.
//
//   cmake --build build/analyzer-tests --target hand_axis_test
//   ctest --test-dir build/analyzer-tests -R hand_axis --output-on-failure

#include "../hand_axis.h"

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static constexpr int W = 1920, H = 1080;

// Fill one hand's 21 keypoints at a fixed normalized point, given conf.
static void fillHand(PoseFrame2D &f, int base, double nx, double ny, float conf)
{
    for (int k = 0; k < kHandKpCount; ++k) {
        f.kp[base + k]   = QPointF(nx, ny);
        f.conf[base + k] = conf;
    }
}

int main()
{
    std::printf("hand_axis_test\n");

    // ── handCentroid — score-weighted, gate at 0.2, conf = mean of contributors ──
    {
        QPointF pts[kHandKpCount];
        float   sc[kHandKpCount];
        for (int k = 0; k < kHandKpCount; ++k) { pts[k] = QPointF(0.5, 0.5); sc[k] = 0.1f; }  // all below gate
        pts[0] = QPointF(0.4, 0.6); sc[0] = 0.8f;
        pts[9] = QPointF(0.6, 0.4); sc[9] = 0.4f;
        const HandCentroid c = handCentroid(pts, sc);
        // weighted mean x = (0.8*0.4 + 0.4*0.6)/(1.2) = 0.4667; conf = (0.8+0.4)/2 = 0.6
        check(c.ok, "handCentroid ok with two contributors");
        check(near(c.pt.x(), (0.8 * 0.4 + 0.4 * 0.6) / 1.2, 1e-9), "handCentroid weighted x");
        check(near(c.conf, 0.6f, 1e-6), "handCentroid conf = mean of scores");

        for (int k = 0; k < kHandKpCount; ++k) sc[k] = 0.1f;   // all below the 0.2 floor
        check(!handCentroid(pts, sc).ok, "handCentroid !ok when all below floor");
    }

    // ── computeGripAnchors — centroids when both hands confident, else wrist ─────
    {
        PoseFrame2D f;
        fillHand(f, kLeftHandFirstKp,  0.30, 0.50, 0.9f);   // left  hand cluster
        fillHand(f, kRightHandFirstKp, 0.70, 0.50, 0.9f);   // right hand cluster
        f.kp[kLeftWristKp]  = QPointF(0.10, 0.10);
        f.kp[kRightWristKp] = QPointF(0.90, 0.10);
        f.leadHand = QPointF(-1, -1);                       // sentinel — proves it changed

        computeGripAnchors(f, /*leftLeads=*/true);          // right-handed: left leads
        check(near(f.leadHand.x(), 0.30, 1e-9) && near(f.leadHand.y(), 0.50, 1e-9),
              "grip: lead = left-hand centroid (leftLeads)");
        check(near(f.trailHand.x(), 0.70, 1e-9), "grip: trail = right-hand centroid");
        check(f.handConf > 0.f, "grip: handConf > 0 (real hands)");

        computeGripAnchors(f, /*leftLeads=*/false);         // left-handed: right leads
        check(near(f.leadHand.x(), 0.70, 1e-9), "grip: lead = right-hand centroid (rightLeads)");

        // Low-conf hands ⇒ wrist fallback, handConf 0.
        PoseFrame2D g;
        fillHand(g, kLeftHandFirstKp,  0.30, 0.50, 0.1f);
        fillHand(g, kRightHandFirstKp, 0.70, 0.50, 0.1f);
        g.kp[kLeftWristKp]  = QPointF(0.12, 0.20);
        g.kp[kRightWristKp] = QPointF(0.88, 0.20);
        computeGripAnchors(g, /*leftLeads=*/true);
        check(near(g.leadHand.x(), 0.12, 1e-9) && near(g.handConf, 0.f, 1e-9),
              "grip: low-conf hands fall back to the wrist (handConf 0)");
    }

    // ── grip recompute on a "smoothed" frame vs an untouched copy (flag on/off) ──
    {
        PoseFrame2D smoothed;                               // hands moved by the smoother
        fillHand(smoothed, kLeftHandFirstKp,  0.42, 0.55, 0.8f);
        fillHand(smoothed, kRightHandFirstKp, 0.58, 0.55, 0.8f);
        smoothed.kp[kLeftWristKp]  = QPointF(0.20, 0.30);
        smoothed.kp[kRightWristKp] = QPointF(0.80, 0.30);
        smoothed.leadHand = QPointF(0.25, 0.30);            // copied-through raw grip
        const QPointF before = smoothed.leadHand;

        PoseFrame2D untouched = smoothed;                   // flag OFF path: never recomputed
        computeGripAnchors(smoothed, /*leftLeads=*/true);   // flag ON path
        check(!near(smoothed.leadHand.x(), before.x(), 1e-6),
              "recompute: ON changes the grip to the smoothed centroid");
        check(near(smoothed.leadHand.x(), 0.42, 1e-9), "recompute: grip == smoothed left centroid");
        check(near(untouched.leadHand.x(), before.x(), 1e-12),
              "recompute: OFF leaves the grip byte-identical");
    }

    // ── handAxisDirection — px-space angle + conf gating ────────────────────────
    {
        // Left hand: wrist-root at (0.50,0.50), middle-MCP straight up-image
        // (smaller y). In px the vector is (0, -Δy·H) ⇒ atan2(-,0) = -90°.
        PoseFrame2D f;
        for (int k = 0; k < kHandKpCount; ++k) f.conf[kLeftHandFirstKp + k] = 0.0f;
        f.kp[kLeftHandFirstKp]                  = QPointF(0.50, 0.50);
        f.kp[kLeftHandFirstKp + kHandMiddleMcp] = QPointF(0.50, 0.40);
        f.conf[kLeftHandFirstKp]                  = 0.9f;
        f.conf[kLeftHandFirstKp + kHandMiddleMcp] = 0.9f;
        double deg = 999.0;
        float conf = handAxisDirection(f, W, H, 0.30, deg);
        check(conf > 0.f, "handAxis: confident left hand contributes");
        check(near(deg, -90.0, 1e-6), "handAxis: straight-up axis = -90° (y-down px)");

        // Below-gate endpoint ⇒ no contribution, deg untouched, conf 0.
        f.conf[kLeftHandFirstKp + kHandMiddleMcp] = 0.1f;
        double deg2 = 12345.0;
        check(handAxisDirection(f, W, H, 0.30, deg2) == 0.f && near(deg2, 12345.0, 1e-9),
              "handAxis: below-confMin endpoint ⇒ conf 0, outDeg untouched");
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
