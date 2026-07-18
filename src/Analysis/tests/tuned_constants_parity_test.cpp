// Byte-identity guard for the consolidated frozen-constants header
// (src/Core/pp_tuned_constants.h). Asserts the named constants EQUAL the historical
// literals they replaced (hard-coded here, NOT re-derived from the header) and that the
// default-constructed config structs / filter still match their pre-refactor defaults.
// A typo'd freeze edit or a wrong member-initialiser indirection fails this test.
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests && \
//   cmake --build build/analyzer-tests --target tuned_constants_parity_test && \
//   ctest --test-dir build/analyzer-tests -R tuned_constants_parity_test --output-on-failure

#include "../../Core/pp_tuned_constants.h"
#include "../../IMU/orientation_filter.h"     // MadgwickFilter default beta
#include "../../IMU/imu_calibration.h"        // nominalArmMount / nominalHandMount
#include "../wrist_angle_sampler.h"           // PpWristSamplingConfig defaults
#include "../assessment_rule.h"               // RuleTuning defaults

#include <cstdio>

namespace t = pinpoint::tuned;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

int main()
{
    std::printf("=== pp_tuned_constants parity ===\n");

    // --- Header constants == historical literals --------------------------------
    check(t::filter::kBeta == 0.05f, "filter::kBeta == 0.05");

    check(t::seed::kInitAccelTolG == 0.15f,       "seed::kInitAccelTolG == 0.15");
    check(t::seed::kInitGyroMaxRadps == 0.5f,     "seed::kInitGyroMaxRadps == 0.5");
    check(t::seed::kInitMaxSeedAttempts == 200,   "seed::kInitMaxSeedAttempts == 200");

    check(t::calib::kAxisAngleMinDeg == 60.0f,    "calib::kAxisAngleMinDeg == 60");
    check(t::calib::kAxisAngleMaxDeg == 120.0f,   "calib::kAxisAngleMaxDeg == 120");
    check(t::calib::kNominalArmMount[0] == 0.5f && t::calib::kNominalArmMount[1] == -0.5f
          && t::calib::kNominalArmMount[2] == -0.5f && t::calib::kNominalArmMount[3] == -0.5f,
          "calib::kNominalArmMount == (0.5,-0.5,-0.5,-0.5)");
    check(t::calib::kNominalHandMount[0] == 0.4388f && t::calib::kNominalHandMount[1] == 0.6054f
          && t::calib::kNominalHandMount[2] == 0.4965f && t::calib::kNominalHandMount[3] == -0.4409f,
          "calib::kNominalHandMount == (0.4388,0.6054,0.4965,-0.4409)");

    check(t::scoring::kZIn == 1.0 && t::scoring::kZOut == 3.0 && t::scoring::kFalloffPow == 2.0,
          "scoring deadbands == (1,3,2)");
    check(t::scoring::bands::kFlexExtMu == 15.0 && t::scoring::bands::kFlexExtSigma == 12.0
          && t::scoring::bands::kFlexExtOneSided == +1 && t::scoring::bands::kFlexExtWeight == 0.45,
          "scoring FlexExt == (15,12,+1,0.45)");
    check(t::scoring::bands::kRadUlnMu == 0.0 && t::scoring::bands::kRadUlnSigma == 12.0
          && t::scoring::bands::kRadUlnOneSided == 0 && t::scoring::bands::kRadUlnWeight == 0.15,
          "scoring RadUln == (0,12,0,0.15)");
    check(t::scoring::bands::kPronationMu == 0.0 && t::scoring::bands::kPronationSigma == 25.0
          && t::scoring::bands::kPronationOneSided == 0 && t::scoring::bands::kPronationWeight == 0.20,
          "scoring Pronation == (0,25,0,0.20)");
    check(t::scoring::bands::kArmFlexionMu == 5.0 && t::scoring::bands::kArmFlexionSigma == 12.0
          && t::scoring::bands::kArmFlexionOneSided == -1 && t::scoring::bands::kArmFlexionWeight == 0.20,
          "scoring ArmFlexion == (5,12,-1,0.20)");

    check(t::sampler::kWindowHalfUs == 15000 && t::sampler::kGimbalThresholdDeg == 75.0
          && t::sampler::kMinValidSamples == 1, "sampler == (15000,75,1)");

    check(t::rules::kConfidenceFloor == 0.45f && t::rules::kScoreScale == 18.0
          && t::rules::kSeverityWeightFault == 1.0 && t::rules::kSeverityWeightWatch == 0.5
          && t::rules::kCorroborationBoost == 0.30 && t::rules::kStrengthsRequireAdjacentFault,
          "rules defaults == (0.45,18,1,0.5,0.30,true)");
    check(t::rules::kFlipFaultDeg == -8.0 && t::rules::kFlipWatchDeg == -5.0
          && t::rules::kTrailFlattenDeg == -8.0 && t::rules::kArchetypeTopDeltaDeg == 10.0
          && t::rules::kArchetypeFaceOffsetDeg == 10.0,
          "rules discrimination literals == (-8,-5,-8,10,10)");

    check(t::scoring::resemblance::kBlendedDeltaPts == 10.0
          && t::scoring::resemblance::kBowedMuTop == 13.0 && t::scoring::resemblance::kBowedMuImpact == 27.0
          && t::scoring::resemblance::kNeutralMuImpact == 5.0 && t::scoring::resemblance::kCuppedMuTop == -30.0
          && t::scoring::resemblance::kBowedSigma == 18.0,
          "scoring::resemblance provisional centres");
    check(t::scoring::uncertainty::kSensorSigmaDeg == 6.0 && t::scoring::uncertainty::kCrosstalkSigmaDeg == 12.0
          && t::scoring::uncertainty::kTimingSigmaUs == 10000 && t::scoring::uncertainty::kConfInflate == 1.5
          && t::scoring::uncertainty::kIntervalSigmas == 1.0,
          "scoring::uncertainty budget");

    // WB1 pose-accuracy defaults (crop + DARK both ON). The flags-off byte-parity
    // gate depends on kEnabled/kDark being the ONLY thing an override flips.
    check(t::pose::crop::kEnabled == true && t::pose::crop::kMarginFrac == 0.15
          && t::pose::crop::kMaxAreaFrac == 0.90 && t::pose::crop::kMinBboxFrames == 3,
          "pose::crop defaults == (true,0.15,0.90,3)");
    check(t::pose::decode::kDark == true, "pose::decode::kDark == true");
    check(t::pose::confScale::kFeet == 1.0 && t::pose::confScale::kFace == 1.0
          && t::pose::confScale::kHands == 1.0, "pose::confScale defaults == 1.0");

    // W3/W4 club-activity + tk0 A/B + club-quiet σ. Activity stays dark (keeps
    // BallRunner/swing.json byte-identical); the tk0 override was FROZEN OFF in
    // the 2026-07-17 Address/Takeaway freeze — the earliest-departure tk0 fires
    // on the first fidget departure and overwrote a good hold-end Address
    // (17-swing truth: Address-error median 0.564 s → 0.060 s with the freeze set).
    check(t::ball::activity::kClubActivity == false && t::ball::activity::kActivityRefFrames == 9
          && t::ball::activity::kActivityInnerR == 1.5 && t::ball::activity::kActivityOuterR == 5.0,
          "ball::activity defaults == (false,9,1.5,5.0)");
    check(t::ball::kTk0AddressOverride == false, "ball::kTk0AddressOverride == false (2026-07-17 freeze)");
    check(t::positions::kP1ClubQuietSigma == 3.0, "positions::kP1ClubQuietSigma == 3.0");

    // Camera-only Address/Takeaway hardening — FROZEN ON 2026-07-17 (user-
    // approved after the in-app eyeball; 17-swing truth evaluation, Address-
    // error median 0.564 s → 0.060 s): no-return veto box 7 / gap 15, run
    // bridging 10, vision Takeaway event on. 0 still disables each (the
    // swLow<=0 idiom) — the dark values remain the legacy soak baseline.
    check(t::shaft::kOnsetReturnBoxPx == 7.0 && t::shaft::kOnsetReturnGapFrames == 15,
          "shaft::onsetReturn == (7.0, 15) (2026-07-17 freeze)");
    check(t::shaft::kOnsetRunBridgeFrames == 10, "shaft::kOnsetRunBridgeFrames == 10 (2026-07-17 freeze)");
    check(t::shaft::kEmitTakeaway == true, "shaft::kEmitTakeaway == true (2026-07-17 freeze)");
    // m3gate FROZEN ON 2026-07-18 at 0.2 (0 still disables). Evidence: 17-swing
    // truth — s0002 Takeaway 1.857 → 2.480 s (+0.100), s0001 Address → +0.042,
    // other 15 zero-movement; 61-swing corpus — 19 corrective moves, 0 score
    // changes; 25× net/path separation (flap 0.013 vs legit >= 0.34).
    check(t::shaft::kOnsetBridgeMinNetFrac == 0.2,
          "shaft::kOnsetBridgeMinNetFrac == 0.2 (2026-07-18 freeze)");

    // Late-pipeline event refinement (EventRefine, P3) — all keys DARK at V1
    // (enabled flips only at the evidence-freeze commit, paired with
    // ball::activity::kClubActivity). activityQuietSigma seeds from the P1
    // club-quiet σ (positions::kP1ClubQuietSigma == 3.0).
    check(t::refine::kEnabled == false && t::refine::kTakeaway == true
          && t::refine::kAddress == true && t::refine::kImpactResidual == true
          && t::refine::kDepartThetaDeg == 25.0 && t::refine::kActivityQuietSigma == 3.0
          && t::refine::kReturnHoldMs == 200 && t::refine::kMinConf == 0.5
          && t::refine::kMaxShiftS == 3.0,
          "refine defaults == (off,on,on,on,25,3.0,200,0.5,3.0)");

    // --- Default-constructed consumers match their pre-refactor defaults ---------
    check(MadgwickFilter().beta() == 0.05f, "MadgwickFilter() default beta == 0.05");

    const QQuaternion arm = imu_calibration::nominalArmMount();
    check(arm.scalar() == 0.5f && arm.x() == -0.5f && arm.y() == -0.5f && arm.z() == -0.5f,
          "nominalArmMount() == (0.5,-0.5,-0.5,-0.5)");
    const QQuaternion hand = imu_calibration::nominalHandMount();
    check(hand.scalar() == 0.4388f && hand.x() == 0.6054f && hand.y() == 0.4965f && hand.z() == -0.4409f,
          "nominalHandMount() == (0.4388,0.6054,0.4965,-0.4409)");

    const PpWristSamplingConfig sc;
    check(sc.windowHalfUs == 15000 && sc.gimbalThresholdDeg == 75.0 && sc.minValidSamples == 1,
          "PpWristSamplingConfig{} defaults");

    const RuleTuning rt;
    check(rt.confidenceFloor == 0.45f && rt.scoreScale == 18.0 && rt.severityWeightFault == 1.0
          && rt.severityWeightWatch == 0.5 && rt.corroborationBoost == 0.30
          && rt.strengthsRequireAdjacentFault,
          "RuleTuning{} defaults");
    check(rt.flipFaultDeg == -8.0 && rt.flipWatchDeg == -5.0 && rt.trailFlattenDeg == -8.0
          && rt.archetypeTopDeltaDeg == 10.0,
          "RuleTuning{} discrimination thresholds");

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
