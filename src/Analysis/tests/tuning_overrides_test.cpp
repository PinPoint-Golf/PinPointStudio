// Verifies the dotted-key `tuningOverrides` path actually mutates the values a stage
// consumes, and that an empty map is a no-op (byte-identical to defaults). Covers the
// `score.*` namespace (SwingScorer); later phases extend this with sampler.*/rules.*/filter.*.
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests && \
//   cmake --build build/analyzer-tests --target tuning_overrides_test && \
//   ctest --test-dir build/analyzer-tests -R tuning_overrides_test --output-on-failure

#include "../swing_scorer.h"
#include "../wrist_assessment_tuning.h"
#include "../reference_bands.h"
#include "../analysis_tuning.h"
#include "../../Core/pp_tuned_constants.h"

#include <QVariantMap>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static MetricSeries metric(const char *key, double impactValue)
{
    MetricSeries m;
    m.key = QString::fromLatin1(key); m.label = m.key; m.unit = QStringLiteral("°");
    m.t_us = { 0, 1 }; m.value = { 0.0, impactValue };
    m.phaseSamples.push_back({ Phase::Impact, 1, impactValue, QString() });
    return m;
}

static const ScoredMetric *sm(const ScoreBreakdown &b, const char *key)
{
    for (const ScoredMetric &m : b.metrics)
        if (m.key == QString::fromLatin1(key)) return &m;
    return nullptr;
}

int main()
{
    std::printf("=== tuningOverrides (score.*) ===\n");

    // 1. Empty map == default path (the production no-op).
    {
        const std::vector<MetricSeries> s = { metric("leadWristFlexExt", 15.0) };
        const ScoreBreakdown def = SwingScorer::score(s, 1);
        const ScoreBreakdown ovr = SwingScorer::score(s, 1, QVariantMap{});
        check(def.overall == ovr.overall && sm(def, "leadWristFlexExt")->subScore
                                                == sm(ovr, "leadWristFlexExt")->subScore,
              "empty map == default (no-op)");
    }

    // 2. score.<key>.mu shifts the band centre → an on-centre value becomes penalised.
    {
        const std::vector<MetricSeries> s = { metric("leadWristFlexExt", 15.0) };  // default mu 15 → 100
        const ScoreBreakdown def = SwingScorer::score(s, 1);
        check(sm(def, "leadWristFlexExt")->subScore == 100.0, "value at default mu → 100");

        QVariantMap ov; ov["score.leadWristFlexExt.mu"] = 40.0;   // move centre far above
        const ScoreBreakdown b = SwingScorer::score(s, 1, ov);
        check(sm(b, "leadWristFlexExt")->mu == 40.0, "override mu reaches ScoredMetric.mu");
        check(sm(b, "leadWristFlexExt")->subScore < 100.0,
              "value now below shifted mu (one-sided) → penalised");
    }

    // 3. score.zIn widens the deadband → a previously-yellow value becomes green (100).
    {
        const std::vector<MetricSeries> s = { metric("leadWristFlexExt", -9.0) }; // z=-2 (penalised) → yellow
        const ScoreBreakdown def = SwingScorer::score(s, 1);
        check(sm(def, "leadWristFlexExt")->band == QStringLiteral("yellow")
              && sm(def, "leadWristFlexExt")->subScore < 100.0, "z≈2 → yellow, <100 by default");

        QVariantMap ov; ov["score.zIn"] = 2.5;   // deadband now covers |z|=2
        const ScoreBreakdown b = SwingScorer::score(s, 1, ov);
        check(sm(b, "leadWristFlexExt")->subScore == 100.0 && sm(b, "leadWristFlexExt")->band
                                                                  == QStringLiteral("green"),
              "widened deadband → green/100");
    }

    // 4. score.<key>.weight changes aggregation (sanity: a fault's weight matters).
    {
        const std::vector<MetricSeries> s = {
            metric("leadWristFlexExt", -30.0),  // catastrophic (one-sided, ~1)
            metric("leadArmFlexion",    5.0),   // 100
        };
        const ScoreBreakdown def = SwingScorer::score(s, 1);
        QVariantMap ov; ov["score.leadWristFlexExt.weight"] = 0.01;  // de-weight the fault
        const ScoreBreakdown b = SwingScorer::score(s, 1, ov);
        check(b.overall > def.overall, "de-weighting the fault raises overall");
    }

    std::printf("--- assessment engine: sampler.* / rules.* / bands.* ---\n");

    // 5. wristAssessmentConfigFor maps every key onto the config; empty map → defaults.
    {
        QVariantMap ov;
        ov["sampler.windowHalfUs"] = 9000;
        ov["sampler.gimbalThresholdDeg"] = 60.0;
        ov["sampler.minValidSamples"] = 3;
        ov["rules.confidenceFloor"] = 0.6;
        ov["rules.scoreScale"] = 22.0;
        ov["rules.severityWeightFault"] = 1.5;
        ov["rules.severityWeightWatch"] = 0.7;
        ov["rules.corroborationBoost"] = 0.4;
        ov["rules.strengthsRequireAdjacentFault"] = false;
        ov["bands.flexExtMargin"] = 8.0;
        const WristAssessmentConfig cfg = wristAssessmentConfigFor(ov);
        check(cfg.sampling.windowHalfUs == 9000 && cfg.sampling.gimbalThresholdDeg == 60.0
              && cfg.sampling.minValidSamples == 3, "sampler.* mapped");
        check(cfg.rules.confidenceFloor == 0.6f && cfg.rules.scoreScale == 22.0
              && cfg.rules.severityWeightFault == 1.5 && cfg.rules.severityWeightWatch == 0.7
              && cfg.rules.corroborationBoost == 0.4 && cfg.rules.strengthsRequireAdjacentFault == false,
              "rules.* mapped");
        check(cfg.band.tuning.flexExtMargin == 8.0, "bands.flexExtMargin mapped");

        const WristAssessmentConfig def = wristAssessmentConfigFor(QVariantMap{});
        check(def.sampling.gimbalThresholdDeg == 75.0 && def.rules.scoreScale == 18.0
              && def.band.tuning.flexExtMargin == -1.0, "empty map → frozen defaults");
    }

    // 6. The band provider honours a bands.* margin override (amber widens; green unchanged).
    {
        ConfigReferenceBandProvider prov;
        const Band b0 = prov.band(PpJointDof::LeadWristFlexExt, PpSwingPosition::P4, BandContext{});
        BandContext ctx; ctx.tuning.flexExtMargin = 20.0;
        const Band b1 = prov.band(PpJointDof::LeadWristFlexExt, PpSwingPosition::P4, ctx);
        check(b1.amberLo < b0.amberLo && b1.amberHi > b0.amberHi, "bands.flexExtMargin widens amber");
        check(b1.greenLo == b0.greenLo && b1.greenHi == b0.greenHi, "margin override leaves green unchanged");
    }

    std::printf("--- pose.* offline ORT intra-op thread count (PoseRunner seam) ---\n");

    // 7. pose.intraOpThreads resolves onto its frozen-constant-seeded local — the
    //    exact seam PoseRunner uses (seed from opt.intraOpThreads ==
    //    pose::kIntraOpThreads, then tuning::apply the override, then
    //    estimator.setIntraOpThreads before load() sizes the pool). Empty map ⇒
    //    frozen default 0, i.e. the legacy heuristic (thread-count-identical to
    //    history). > 0 pins; -1 is the opt-in physical-core topology sentinel.
    {
        int intraOp = pinpoint::tuned::pose::kIntraOpThreads;
        tuning::apply(QVariantMap{}, "pose.intraOpThreads", intraOp);
        check(intraOp == 0, "empty map → pose.intraOpThreads frozen default 0 (legacy auto)");

        QVariantMap ov; ov["pose.intraOpThreads"] = 6;
        tuning::apply(ov, "pose.intraOpThreads", intraOp);
        check(intraOp == 6, "pose.intraOpThreads override reaches the load() seam");

        ov["pose.intraOpThreads"] = -1;   // topology-auto sentinel (physicalCoreCount clamped [1,16])
        tuning::apply(ov, "pose.intraOpThreads", intraOp);
        check(intraOp == -1, "pose.intraOpThreads = -1 (topology auto) resolves");
    }

    std::printf("--- shaft.* onset no-return veto + bridging + Takeaway event (segmentPhases seam) ---\n");

    // 8. The four camera-only Address/Takeaway knobs resolve onto their frozen-
    //    constant-seeded locals — the exact seam ShaftV3Config::fromOverrides uses
    //    (seed from tuned::shaft::k*, then tuning::apply the override). Empty map ⇒
    //    the DARK defaults (box 0 = veto off, bridge 0 = off, emitTakeaway false)
    //    so segmentPhases / phasesToSegmentation stay byte-identical to the
    //    pre-veto tracker. (The retired anchor-box veto's shaft.onsetReturnPhiDeg /
    //    shaft.onsetReturnStillFrames keys are GONE — the departure-referenced
    //    revisit scan needs neither.)
    {
        double boxPx  = pinpoint::tuned::shaft::kOnsetReturnBoxPx;
        int    gapF   = pinpoint::tuned::shaft::kOnsetReturnGapFrames;
        int    bridge = pinpoint::tuned::shaft::kOnsetRunBridgeFrames;
        bool   emitTk = pinpoint::tuned::shaft::kEmitTakeaway;
        tuning::apply(QVariantMap{}, "shaft.onsetReturnBoxPx", boxPx);
        tuning::apply(QVariantMap{}, "shaft.onsetReturnGapFrames", gapF);
        tuning::apply(QVariantMap{}, "shaft.onsetRunBridgeFrames", bridge);
        tuning::apply(QVariantMap{}, "shaft.emitTakeaway", emitTk);
        check(boxPx == 0.0 && gapF == 15 && bridge == 0 && emitTk == false,
              "empty map → shaft onset/bridge/Takeaway frozen defaults (all dark)");

        QVariantMap ov;
        ov["shaft.onsetReturnBoxPx"] = 7.0;
        ov["shaft.onsetReturnGapFrames"] = 20;
        ov["shaft.onsetRunBridgeFrames"] = 10;
        ov["shaft.emitTakeaway"] = true;
        tuning::apply(ov, "shaft.onsetReturnBoxPx", boxPx);
        tuning::apply(ov, "shaft.onsetReturnGapFrames", gapF);
        tuning::apply(ov, "shaft.onsetRunBridgeFrames", bridge);
        tuning::apply(ov, "shaft.emitTakeaway", emitTk);
        check(boxPx == 7.0 && gapF == 20 && bridge == 10 && emitTk == true,
              "shaft onset/bridge/Takeaway overrides reach their fromOverrides seam");
    }

    std::printf("--- ball.* club-activity + positions.p1ClubQuietSigma + ball.tk0AddressOverride (W3/W4) ---\n");

    // 9. The W3 club-activity producer knobs (BallActivityConfig::fromOverrides),
    //    the W3 club-quiet consumer gate (PositionsConfig via ShaftV3Config::
    //    fromOverrides) and the W4 tk0 A/B key (applyBallAnchor) resolve onto their
    //    frozen-constant-seeded locals — the exact seam each fromOverrides uses.
    //    Empty map ⇒ the DARK defaults (activity off, tk0 override ON = today), so
    //    BallRunner/addressHoldEndFrame/applyBallAnchor stay byte-identical.
    {
        bool   act    = pinpoint::tuned::ball::activity::kClubActivity;
        int    refF   = pinpoint::tuned::ball::activity::kActivityRefFrames;
        double innerR = pinpoint::tuned::ball::activity::kActivityInnerR;
        double outerR = pinpoint::tuned::ball::activity::kActivityOuterR;
        double quietS = pinpoint::tuned::positions::kP1ClubQuietSigma;
        bool   tk0Ov  = pinpoint::tuned::ball::kTk0AddressOverride;
        tuning::apply(QVariantMap{}, "ball.clubActivity", act);
        tuning::apply(QVariantMap{}, "ball.activityRefFrames", refF);
        tuning::apply(QVariantMap{}, "ball.activityInnerR", innerR);
        tuning::apply(QVariantMap{}, "ball.activityOuterR", outerR);
        tuning::apply(QVariantMap{}, "positions.p1ClubQuietSigma", quietS);
        tuning::apply(QVariantMap{}, "ball.tk0AddressOverride", tk0Ov);
        check(act == false && refF == 9 && innerR == 1.5 && outerR == 5.0
              && quietS == 3.0 && tk0Ov == true,
              "empty map → W3/W4 frozen defaults (activity dark, tk0 override on)");

        QVariantMap ov;
        ov["ball.clubActivity"] = true;
        ov["ball.activityRefFrames"] = 5;
        ov["ball.activityInnerR"] = 2.0;
        ov["ball.activityOuterR"] = 6.0;
        ov["positions.p1ClubQuietSigma"] = 2.0;
        ov["ball.tk0AddressOverride"] = false;
        tuning::apply(ov, "ball.clubActivity", act);
        tuning::apply(ov, "ball.activityRefFrames", refF);
        tuning::apply(ov, "ball.activityInnerR", innerR);
        tuning::apply(ov, "ball.activityOuterR", outerR);
        tuning::apply(ov, "positions.p1ClubQuietSigma", quietS);
        tuning::apply(ov, "ball.tk0AddressOverride", tk0Ov);
        check(act == true && refF == 5 && innerR == 2.0 && outerR == 6.0
              && quietS == 2.0 && tk0Ov == false,
              "W3/W4 overrides reach their fromOverrides seams");
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
