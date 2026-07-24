// Standalone test for GolferAnthro-from-2D estimation (swing_ref_anthro.h).
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure
//
// A synthetic golfer with KNOWN anthropometry is projected through the same
// orthographic face-on model the estimator inverts (assumption A1 in the header
// — a single synthetic swing validates INVERSION + assembly + gating, never the
// depth MODEL itself). Recover hub within 2 cm, armLength within 2 cm, pxPerM
// within 1 %. Plus the LH y-mirror, the degrade paths (no ball / no P1 / low
// conf → nullopt), and the manual-override short-circuit.

#include "../swing_ref_anthro.h"
#include "../swing_analysis.h"

#include <QPointF>
#include <QVector3D>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;
using namespace pinpoint::swingref;

static int g_fail = 0;

#define CHECK(label, cond)                                                             \
    do {                                                                               \
        const bool ok = (cond);                                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                       \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                              \
    do {                                                                               \
        const double g = (got), w = (want);                                            \
        const bool ok = std::abs(g - w) <= (tol);                                      \
        std::printf("  [%s] %-28s got %9.5f  want %9.5f (tol %.4f)\n",                  \
                    ok ? "PASS" : "FAIL", label, g, w, double(tol));                   \
        if (!ok) ++g_fail;                                                             \
    } while (0)

// ── Ground truth (ball frame; orthographic face-on: u=cx+s·X, v=cy−s·Z) ──────
struct GroundTruth {
    int    W = 1440, H = 1080;
    double cx = 720.0, cy = 850.0;
    double pxPerM = 400.0;
    double Lc = 1.0;          // club length (m)
    double lieDeg = 60.0;
    double gripOffsetM = 0.04;
    // Filled in ctor from the depth model so the fixture is A1–A6 consistent.
    QVector3D hub, butt;
    double stanceCentreX = -0.05;   // ⇒ ballOffsetX = +0.05 (ball toward target)
    qint64 tA = 1000000;
    qint64 winUs = 300000;

    GroundTruth() {
        const double lie = lieDeg * M_PI / 180.0;
        const double inPlane = Lc * std::sin(lie);
        const double depth   = -Lc * std::cos(lie);      // RH: y<0
        const double buttX = 0.10;
        const double buttZ = std::sqrt(inPlane * inPlane - buttX * buttX);
        butt = QVector3D(float(buttX), float(depth), float(buttZ));
        hub  = QVector3D(0.0f, float(depth), 1.35f);      // hands under shoulders ⇒ same depth
    }
    QPointF proj(const QVector3D& X) const {
        return QPointF(cx + pxPerM * X.x(), cy - pxPerM * X.z());
    }
    double armLengthTrue() const { return double((hub - butt).length()) + gripOffsetM; }
    double ballOffsetXTrue() const { return -stanceCentreX; }  // ball X − stanceCentreX, ball at 0
};

// Deterministic sub-pixel jitter (fixed LCG).
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    double n(double amp) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const double u = double((s >> 11) & ((1ULL << 53) - 1)) / double(1ULL << 53);
        return (2.0 * u - 1.0) * amp;
    }
};

// Build the three tracks + segmentation from ground truth. Toggles drive the
// degrade cases.
struct Fixture {
    PoseTrack2D pose;
    ShaftTrack2D shaft;
    BallTrack2D ball;
    Segmentation seg;
};

// `ballPxOverride`: when x>=0, force the detected-ball image position (px) — used
// to inject a SPURIOUS ball far from the true clubhead. Default (<0) places the
// ball at the true clubhead/origin (A1–A6 consistent).
static Fixture buildFixture(const GroundTruth& gt,
                            bool withBall = true, bool withP1 = true,
                            float shoulderConf = 0.9f,
                            QPointF ballPxOverride = QPointF(-1, -1))
{
    Fixture fx;
    fx.seg.events.push_back(PhaseEvent{Phase::Address, gt.tA, 1.0f, SegmentRole::Unknown});

    fx.shaft.frameWidth = gt.W;
    fx.shaft.frameHeight = gt.H;
    fx.shaft.valid = true;
    if (withP1) {
        ShaftPosition p;
        p.p = 1;
        p.t_us = gt.tA;
        p.gripPx = gt.proj(gt.butt);      // image px
        p.headPx = gt.proj(QVector3D(0, 0, 0));
        p.conf = 0.9f;
        fx.shaft.positions.push_back(p);
    }

    // Shoulders (5/6) symmetric ±0.2 m about the hub in X; ankles (15/16) on the
    // ground symmetric ±0.15 m about the stance centre; hips (11/12) mid-body.
    const QVector3D lSh = gt.hub + QVector3D(-0.20f, 0, 0);
    const QVector3D rSh = gt.hub + QVector3D(+0.20f, 0, 0);
    const QVector3D lAnk(float(gt.stanceCentreX) - 0.15f, float(gt.hub.y()), 0.0f);
    const QVector3D rAnk(float(gt.stanceCentreX) + 0.15f, float(gt.hub.y()), 0.0f);

    Lcg rng(0xA11CE);
    const qint64 dt = 10000;   // 100 Hz
    for (qint64 t = gt.tA - gt.winUs / 2; t <= gt.tA + gt.winUs / 2; t += dt) {
        PoseFrame2D f;
        f.t_us = t;
        auto setKp = [&](int idx, const QVector3D& world, float conf) {
            const QPointF px = gt.proj(world);
            f.kp[std::size_t(idx)] = QPointF((px.x() + rng.n(0.3)) / gt.W,
                                             (px.y() + rng.n(0.3)) / gt.H);
            f.conf[std::size_t(idx)] = conf;
        };
        setKp(5, lSh, shoulderConf);
        setKp(6, rSh, shoulderConf);
        setKp(15, lAnk, 0.9f);
        setKp(16, rAnk, 0.9f);
        fx.pose.frames.push_back(f);

        if (withBall) {
            BallSample2D b;
            b.t_us = t;
            b.found = true;
            const QPointF bp = ballPxOverride.x() >= 0.0 ? ballPxOverride
                                                         : gt.proj(QVector3D(0, 0, 0));
            b.center = QPointF(bp.x() / gt.W, bp.y() / gt.H);
            b.conf = 0.9f;
            fx.ball.frames.push_back(b);
        }
    }
    return fx;
}

int main()
{
    std::printf("=== swing_ref_anthro.h ===\n\n");
    const GroundTruth gt;
    AnthroConfig cfg;
    cfg.gripOffsetM = gt.gripOffsetM;

    // ── RH recovery ─────────────────────────────────────────────────────────
    std::printf("-- RH recovery --\n");
    {
        const Fixture fx = buildFixture(gt);
        auto e = estimateGolferAnthro(fx.pose, fx.shaft, fx.ball, fx.seg,
                                      gt.Lc, gt.lieDeg, /*handedness=*/+1, cfg);
        CHECK("estimate present", bool(e));
        if (e) {
            CHECK_NEAR("hub.x (m)", e->anthro.hub.x(), gt.hub.x(), 0.02);
            CHECK_NEAR("hub.y (m)", e->anthro.hub.y(), gt.hub.y(), 0.02);
            CHECK_NEAR("hub.z (m)", e->anthro.hub.z(), gt.hub.z(), 0.02);
            CHECK_NEAR("armLength (m)", e->anthro.armLength, gt.armLengthTrue(), 0.02);
            CHECK_NEAR("pxPerM (rel)", e->pxPerM / gt.pxPerM, 1.0, 0.01);
            CHECK_NEAR("ballOffsetX (m)", e->ballOffsetX, gt.ballOffsetXTrue(), 0.02);
            CHECK("rightHanded", e->anthro.rightHanded);
            CHECK("conf > 0", e->conf > 0.f);
        }
    }

    // ── LH y-mirror ─────────────────────────────────────────────────────────
    std::printf("\n-- LH mirror (y → −y) --\n");
    {
        const Fixture fx = buildFixture(gt);   // same image; only handedness differs
        auto e = estimateGolferAnthro(fx.pose, fx.shaft, fx.ball, fx.seg,
                                      gt.Lc, gt.lieDeg, /*handedness=*/-1, cfg);
        CHECK("estimate present", bool(e));
        if (e) {
            CHECK("left-handed", !e->anthro.rightHanded);
            CHECK_NEAR("hub.y mirrored (+)", e->anthro.hub.y(), -gt.hub.y(), 0.02);
            CHECK_NEAR("hub.x unchanged", e->anthro.hub.x(), gt.hub.x(), 0.02);
            CHECK_NEAR("hub.z unchanged", e->anthro.hub.z(), gt.hub.z(), 0.02);
            CHECK_NEAR("armLength unchanged", e->anthro.armLength, gt.armLengthTrue(), 0.02);
        }
    }

    // ── Origin from the P1 clubhead, NOT the ball (spurious-ball robustness) ──
    // A3: at address the clubhead sits at the ball, so the measured P1 head px is
    // the reliable origin/scale reference. A spurious ball blob far from it must
    // be IGNORED for scale — the recovered anthropometry stays correct, and the
    // estimate no longer even requires a ball when a P1 head exists.
    std::printf("\n-- origin from P1 head (ball spurious / absent) --\n");
    {
        // Ball placed 300 px up-and-left of the true clubhead — the exact failure
        // mode seen in the field (a fixed bright spot above the hands).
        const QPointF trueHead = gt.proj(QVector3D(0, 0, 0));
        const QPointF spurious(trueHead.x() - 300.0, trueHead.y() - 300.0);
        const Fixture fx = buildFixture(gt, /*withBall=*/true, /*withP1=*/true,
                                        /*shoulderConf=*/0.9f, spurious);
        auto e = estimateGolferAnthro(fx.pose, fx.shaft, fx.ball, fx.seg,
                                      gt.Lc, gt.lieDeg, +1, cfg);
        CHECK("estimate present with spurious ball", bool(e));
        if (e) {
            // Old code grounded pxPerM on grip→spurious-ball and inflated scale
            // ~3×; the fix pins it to the measured club px, so truth is recovered.
            CHECK_NEAR("pxPerM (rel) despite spurious ball", e->pxPerM / gt.pxPerM, 1.0, 0.01);
            CHECK_NEAR("armLength (m) despite spurious ball", e->anthro.armLength,
                       gt.armLengthTrue(), 0.02);
            CHECK_NEAR("hub.z (m) despite spurious ball", e->anthro.hub.z(), gt.hub.z(), 0.02);
            CHECK("ball-inconsistent flag set", (e->flags & AnthroFlagBallInconsistent) != 0u);
        }

        // No ball at all, but a P1 head present → still recovers (origin = head).
        const Fixture noBall = buildFixture(gt, /*withBall=*/false);
        auto en = estimateGolferAnthro(noBall.pose, noBall.shaft, noBall.ball, noBall.seg,
                                       gt.Lc, gt.lieDeg, +1, cfg);
        CHECK("no ball → still recovers from P1 head", bool(en));
        if (en) {
            CHECK_NEAR("armLength (m) no-ball", en->anthro.armLength, gt.armLengthTrue(), 0.02);
            CHECK_NEAR("pxPerM (rel) no-ball", en->pxPerM / gt.pxPerM, 1.0, 0.01);
        }
    }

    // ── Degrade cases ───────────────────────────────────────────────────────
    std::printf("\n-- degrade → nullopt --\n");
    {
        const Fixture noP1 = buildFixture(gt, true, /*withP1=*/false);
        CHECK("no P1 position → nullopt",
              !estimateGolferAnthro(noP1.pose, noP1.shaft, noP1.ball, noP1.seg,
                                    gt.Lc, gt.lieDeg, +1, cfg).has_value());

        const Fixture lowConf = buildFixture(gt, true, true, /*shoulderConf=*/0.1f);
        CHECK("low shoulder conf → nullopt",
              !estimateGolferAnthro(lowConf.pose, lowConf.shaft, lowConf.ball, lowConf.seg,
                                    gt.Lc, gt.lieDeg, +1, cfg).has_value());

        Segmentation noAddr;   // no Address event
        const Fixture f = buildFixture(gt);
        CHECK("no Address event → nullopt",
              !estimateGolferAnthro(f.pose, f.shaft, f.ball, noAddr,
                                    gt.Lc, gt.lieDeg, +1, cfg).has_value());
    }

    // ── Manual override short-circuit ───────────────────────────────────────
    std::printf("\n-- manual override --\n");
    {
        AnthroConfig m = cfg;
        m.hubX = 0.11; m.hubY = -0.47; m.hubZ = 1.30;
        m.armLengthM = 0.66;
        // Even with LOW shoulder confidence, overrides short-circuit the refusal.
        const Fixture fx = buildFixture(gt, true, true, /*shoulderConf=*/0.05f);
        auto e = estimateGolferAnthro(fx.pose, fx.shaft, fx.ball, fx.seg,
                                      gt.Lc, gt.lieDeg, +1, m);
        CHECK("estimate present with overrides", bool(e));
        if (e) {
            CHECK_NEAR("hub.x = override", e->anthro.hub.x(), 0.11, 1e-4);
            CHECK_NEAR("hub.y = override", e->anthro.hub.y(), -0.47, 1e-4);
            CHECK_NEAR("hub.z = override", e->anthro.hub.z(), 1.30, 1e-4);
            CHECK_NEAR("armLength = override", e->anthro.armLength, 0.66, 1e-4);
            CHECK("manual flags set",
                  (e->flags & AnthroFlagManualHub) && (e->flags & AnthroFlagManualArm));
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
