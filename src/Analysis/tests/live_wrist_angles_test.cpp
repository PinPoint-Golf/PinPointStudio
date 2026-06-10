// Characterization of the LIVE wrist-angle path's math contract (Track A, Phase 0.5).
//
// LiveWristAngles (src/Gui/live_wrist_angles.cpp) is a QObject wired to ImuManager /
// AppSettings / AthleteController — too Gui-coupled to construct in this standalone
// harness, and Track A forbids a production refactor to expose its math. So this test
// pins the FORMULA tick() runs, replicated verbatim from live_wrist_angles.cpp:76-95
// (kept in sync by review against the cited lines), driven by the same synthetic poses
// as pipeline_test (forearm/upper identity, hand flexes about Z).
//
// What it guards (docs/implementation/IMU_REARCHITECTURE.md §7 #6, #8):
//   * The live path derives leftArm from athlete handedness (leftArm = handedness !=
//     "Left", so a RIGHT-handed golfer — the common case — passes leftArm=TRUE), while
//     the offline path derives it from SegmentRole. They agree ONLY because leftArm is
//     currently a no-op. This freezes that equivalence so the unverified left-handed
//     mirror flags loudly the instant it is implemented.
//   * The live rel constructions (fore⁻¹·hand for bow/hinge; upper⁻¹·fore for roll)
//     equal the offline wristRel/elbowRel with an identity address reference.
//
// NOT covered (needs the app stack): slot resolution (A/B/C) and the anatCalibrated()
// validity gating. A future tick()-math extraction into a pure helper would let this
// test call the real code; noted as a Phase-1 refactor candidate.

#include "../wrist_angles.h"

#include <QQuaternion>
#include <QString>
#include <QVector3D>
#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static const QVector3D Zc(0, 0, 1), Yc(0, 1, 0);
static QQuaternion R(const QVector3D &a, float deg) { return QQuaternion::fromAxisAndAngle(a, deg); }

static void checkNear(const char *label, double got, double want, double tol)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  [%s] %-32s got %8.3f  want %8.3f  (tol %.2g)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}
static void checkLabel(const char *label, const QString &got, const char *want)
{
    const bool ok = (got == QString::fromUtf8(want));
    std::printf("  [%s] %-32s got \"%s\"  want \"%s\"\n",
                ok ? "PASS" : "FAIL", label, got.toUtf8().constData(), want);
    if (!ok) ++g_fail;
}

// Verbatim mirror of LiveWristAngles::tick() lines 80-95 (rel build + extraction + labels).
struct LiveResult { double bow, hinge, roll; QString bowLabel, hingeLabel, rollLabel; };
static LiveResult liveCompute(const QQuaternion &fore, const QQuaternion &hand,
                              const QQuaternion &upper, bool leftArm)
{
    LiveResult r;
    const QQuaternion relBow = (fore.conjugated() * hand).normalized();        // :80
    const WristAngles w = wristFlexExtDeviation(relBow, leftArm);              // :81
    r.bow        = radToDeg(w.feRad);
    r.hinge      = radToDeg(w.rudRad);
    r.bowLabel   = wristMetricLabel(QStringLiteral("leadWristFlexExt"), r.bow);
    r.hingeLabel = wristMetricLabel(QStringLiteral("leadWristRadUln"),  r.hinge);

    const QQuaternion relRoll = (upper.conjugated() * fore).normalized();      // :92
    const ForearmElbow ef = forearmPronElbowFlex(relRoll, leftArm);           // :93
    r.roll       = radToDeg(ef.pronRad);
    r.rollLabel  = wristMetricLabel(QStringLiteral("forearmPronation"), r.roll);
    return r;
}

int main()
{
    std::printf("=== LiveWristAngles math-contract characterization ===\n\n");
    const QQuaternion I;
    const bool kLiveDefaultLeftArm = true;   // right-handed golfer ⇒ handedness != "Left" ⇒ true

    // -- A. live default (right-handed, leftArm=true): synthetic flexion poses --
    //    forearm = identity, hand = R(Z, fe), upper = identity (pipeline_test geometry).
    std::printf("-- A. flexion poses (fore=I, hand=R(Z,θ), upper=I) --\n");
    {
        const LiveResult top = liveCompute(I, R(Zc, 50), I, kLiveDefaultLeftArm);   // backswing top
        checkNear ("top bow°",   top.bow,   50.0, 0.4);
        checkNear ("top hinge°", top.hinge,  0.0, 0.4);
        checkNear ("top roll°",  top.roll,   0.0, 0.4);
        checkLabel("top bowLabel",   top.bowLabel,   "50° bowed");
        checkLabel("top hingeLabel", top.hingeLabel, "neutral");
        checkLabel("top rollLabel",  top.rollLabel,  "square");

        const LiveResult imp = liveCompute(I, R(Zc, -10), I, kLiveDefaultLeftArm);  // impact
        checkNear ("impact bow°", imp.bow, -10.0, 0.4);
        checkLabel("impact bowLabel", imp.bowLabel, "10° cupped");
    }

    // -- B. roll path (upper=I, fore=R(Y,φ) pronated, hand follows fore → no wrist motion) --
    std::printf("\n-- B. pronation pose (fore=hand=R(Y,40), upper=I) --\n");
    {
        const LiveResult pr = liveCompute(R(Yc, 40), R(Yc, 40), I, kLiveDefaultLeftArm);
        checkNear ("pron bow°",  pr.bow,   0.0, 0.4);
        checkNear ("pron roll°", pr.roll, 40.0, 0.4);
        checkLabel("pron bowLabel",  pr.bowLabel,  "flat");
        checkLabel("pron rollLabel", pr.rollLabel, "40° pronated");
    }

    // -- C. leftArm is a no-op across the live (true) vs offline (false) source split --
    //    §7 #6: live derives leftArm from handedness, offline from SegmentRole; they
    //    agree ONLY because leftArm is a no-op. Freeze that across a battery of poses.
    std::printf("\n-- C. live(leftArm=true) ≡ offline(leftArm=false) (no-op guard) --\n");
    {
        const QQuaternion poses[][3] = {
            { I,           R(Zc, 50),  I          },
            { I,           R(Zc, -10), I          },
            { R(Yc, 40),   R(Yc, 40),  I          },
            { R(Zc, 15),   R(Zc, 35),  R(Yc, 20)  },
        };
        int idx = 0;
        for (const auto &p : poses) {
            const LiveResult live = liveCompute(p[0], p[1], p[2], /*leftArm=*/true);
            const LiveResult offl = liveCompute(p[0], p[1], p[2], /*leftArm=*/false);
            char lb[40], lh[40], lr[40];
            std::snprintf(lb, sizeof lb, "bow  no-op  [#%d]", idx);
            std::snprintf(lh, sizeof lh, "hinge no-op [#%d]", idx);
            std::snprintf(lr, sizeof lr, "roll no-op  [#%d]", idx);
            checkNear(lb, live.bow,   offl.bow,   1e-6);
            checkNear(lh, live.hinge, offl.hinge, 1e-6);
            checkNear(lr, live.roll,  offl.roll,  1e-6);
            ++idx;
        }
    }

    // -- D. live rel construction == offline wristRel/elbowRel with identity address --
    std::printf("\n-- D. live rel ≡ offline wristRel(addr=I) --\n");
    {
        const QQuaternion fore = R(Zc, 15), hand = R(Zc, 35), upper = R(Yc, 20);
        const LiveResult live = liveCompute(fore, hand, upper, true);

        const WristAngles offW = wristFlexExtDeviation(wristRel(fore, hand, /*addr=*/I), false);
        const ForearmElbow offE = forearmPronElbowFlex(elbowRel(upper, fore, /*addr=*/I), false);
        // wristRel/elbowRel re-normalize (and multiply by an identity address), so the
        // two paths agree only to quaternion float round-off, not bit-for-bit.
        checkNear("bow  == offline", live.bow,  radToDeg(offW.feRad),  1e-3);
        checkNear("hinge== offline", live.hinge,radToDeg(offW.rudRad), 1e-3);
        checkNear("roll == offline", live.roll, radToDeg(offE.pronRad),1e-3);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
