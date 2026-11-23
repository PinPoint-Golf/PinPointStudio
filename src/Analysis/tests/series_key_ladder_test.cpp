// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// series_key_ladder_test — best instrument first, and the same anatomy either way.
// ---------------------------------------------------------------------------
//
// Run via CTest (src/Analysis/tests/CMakeLists.txt):
//   cmake --build build/analyzer-tests --target series_key_ladder_test
//   ctest --test-dir build/analyzer-tests -R series_key_ladder --output-on-failure
//
// Two header-only functions, nine lines between them, and everything a wG3 swing
// shows a golfer depends on both:
//
//   findByPrecedence (measurement_precedence.h)  "hm.<key>" if present, else "<key>" — the one
//                                                precedence rule's (refactor 6.1a, R-INS-3), which also
//                                                orders the pack's measures and the board's low point
//                                                (P-cases below), with the source-tagged set (S-cases)
//   dofForMetricKey    (wrist_assessment_types.h)  strip "hm." before the DOF join
//
// ⚠ THEY ARE PINNED BECAUSE THEIR FAILURE MODE IS SILENCE, NOT A WRONG ANSWER.
// Four files had each grown their own copy of a bare `m.key == key` loop. On a
// HackMotion swing the series arrive as `hm.leadWristFlexExt`, so all four found
// nothing: no wrist score, no resemblance, no uncertainty interval, no trace — not
// because the wrist was unmeasured but because it was measured by the BETTER
// instrument. Nothing errors in that state. The panels are simply empty, which
// reads as "this swing had no wrist data" and is the opposite of the truth.
//
// The same shape one layer down: a DOF is an anatomical axis, and which instrument
// measured it is not part of its identity. A grid that went blank because the
// series was named `hm.leadWristFlexExt` would be reporting a NAMING decision as
// missing anatomy.

#include "../../Vocabulary/Analysis/swing_analysis.h"             // MetricSeries
#include "../../Diagnostics/measures/measurement_precedence.h" // findByPrecedence, measurementLadder (6.1a)
#include "../../Metrics/measurement.h"     // the source-tagged set and the stored keys (6.1a)

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "../../Vocabulary/Analysis/wrist_assessment_types.h" // dofForMetricKey, PpJointDof

#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

MetricSeries series(const char *key, double v)
{
    MetricSeries m;
    m.key = QString::fromLatin1(key);
    m.value.push_back(v);
    return m;
}

double firstValue(const MetricSeries *m) { return m && !m->value.empty() ? m->value.front() : -1.0; }

// --- findByPrecedence ------------------------------------------------------

void test_the_measured_rung_wins_when_both_are_present()
{
    // The case that only exists in principle — one instrument per swing means both
    // keys are never emitted together today. Pinned anyway, because the whole
    // function is the statement of WHICH would win, and a future dual capture must
    // not discover the answer by experiment.
    const std::vector<MetricSeries> v = { series("leadWristFlexExt", 1.0),
                                          series("hm.leadWristFlexExt", 2.0) };
    check(firstValue(findByPrecedence(v, QStringLiteral("leadWristFlexExt"))) == 2.0,
          "hm. rung beats the bare key");
}

void test_order_in_the_vector_does_not_decide_it()
{
    // ⚠ THE ONE THAT CATCHES A REWRITE INTO A SINGLE LOOP. A single pass that takes
    // the first key matching EITHER spelling gives the right answer here and the
    // wrong one when the vector is built in the other order. Two passes, preferred
    // first, is the contract — so the same query is asked of both orderings.
    const std::vector<MetricSeries> hmFirst = { series("hm.leadWristRadUln", 2.0),
                                                series("leadWristRadUln", 1.0) };
    const std::vector<MetricSeries> hmLast  = { series("leadWristRadUln", 1.0),
                                                series("hm.leadWristRadUln", 2.0) };
    check(firstValue(findByPrecedence(hmFirst, QStringLiteral("leadWristRadUln"))) == 2.0,
          "hm. rung wins when it is first");
    check(firstValue(findByPrecedence(hmLast, QStringLiteral("leadWristRadUln"))) == 2.0,
          "hm. rung wins when it is last");
}

void test_a_bare_key_always_means_our_own_estimate()
{
    // On a wG3 capture the bare keys are not emitted at all, so a bare key found
    // here IS our estimate and is the correct answer — not a fallback that should
    // have been something better.
    const std::vector<MetricSeries> v = { series("leadWristFlexExt", 1.0) };
    check(firstValue(findByPrecedence(v, QStringLiteral("leadWristFlexExt"))) == 1.0,
          "the bare key is found when it is the only rung");
}

void test_a_wg3_swing_resolves_through_the_anatomical_name()
{
    // The real shape of a HackMotion swing: hm.* only. Callers ask for the
    // ANATOMICAL key — they never spell "hm." — and this is what makes the four
    // former copies' silence impossible.
    const std::vector<MetricSeries> v = { series("hm.leadWristFlexExt", 2.0),
                                          series("hm.leadWristRadUln", 3.0),
                                          series("hm.forearmRotation", 4.0) };
    check(firstValue(findByPrecedence(v, QStringLiteral("leadWristFlexExt"))) == 2.0,
          "bow/cup resolves on a wG3 swing");
    check(firstValue(findByPrecedence(v, QStringLiteral("leadWristRadUln"))) == 3.0,
          "hinge resolves on a wG3 swing");
    check(firstValue(findByPrecedence(v, QStringLiteral("forearmRotation"))) == 4.0,
          "forearm rotation resolves on a wG3 swing");
}

void test_a_missing_key_is_null_not_the_wrong_series()
{
    const std::vector<MetricSeries> v = { series("hm.leadWristFlexExt", 2.0) };
    check(findByPrecedence(v, QStringLiteral("forearmPronation")) == nullptr,
          "an unrelated key returns null");
    check(findByPrecedence({}, QStringLiteral("leadWristFlexExt")) == nullptr,
          "an empty vector returns null");
}

void test_the_prefix_is_not_matched_loosely()
{
    // Asking for "hm.leadWristFlexExt" would look for "hm.hm.leadWristFlexExt"
    // first and then find the literal key. Callers should not do this, but the
    // behaviour is stated rather than left to be discovered — a substring or
    // startsWith match here would collide `leadWristFlexExt` with a future
    // `leadWristFlexExtRate`.
    const std::vector<MetricSeries> v = { series("hm.leadWristFlexExt", 2.0),
                                          series("leadWristFlexExtRate", 9.0) };
    check(firstValue(findByPrecedence(v, QStringLiteral("hm.leadWristFlexExt"))) == 2.0,
          "an already-prefixed query still finds its literal key");
    check(findByPrecedence(v, QStringLiteral("leadWristFlexE")) == nullptr,
          "a partial key matches nothing — the compare is exact");
}

// --- dofForMetricKey ---------------------------------------------------------

bool dofIs(const char *key, PpJointDof want)
{
    const auto d = dofForMetricKey(QString::fromLatin1(key));
    return d.has_value() && *d == want;
}

void test_the_instrument_prefix_is_stripped_before_the_dof_join()
{
    // The line that keeps the whole DOF x P-position assessment grid working on a
    // HackMotion swing. Without it every hm.* series joins to nothing and the grid
    // renders empty — reporting a naming decision as missing anatomy.
    check(dofIs("hm.leadWristFlexExt", PpJointDof::LeadWristFlexExt), "hm. bow/cup -> DOF");
    check(dofIs("hm.leadWristRadUln", PpJointDof::LeadWristRadUln), "hm. hinge -> DOF");
    check(dofIs("leadWristFlexExt", PpJointDof::LeadWristFlexExt), "bare bow/cup -> the SAME DOF");
    check(dofIs("leadWristRadUln", PpJointDof::LeadWristRadUln), "bare hinge -> the SAME DOF");
}

void test_both_producer_spellings_still_reach_their_dof()
{
    // The mockup spellings predate the metric_extractor ones and both are live.
    check(dofIs("forearmPronation", PpJointDof::LeadForearmRot), "forearmPronation -> roll");
    check(dofIs("leadForearmRot", PpJointDof::LeadForearmRot), "leadForearmRot -> roll");
    check(dofIs("leadArmFlexion", PpJointDof::LeadElbowFlex), "leadArmFlexion -> elbow");
    check(dofIs("leadElbowFlex", PpJointDof::LeadElbowFlex), "leadElbowFlex -> elbow");
    check(dofIs("trailWristExt", PpJointDof::TrailWristFlexExt), "trailWristExt -> trail wrist");
    check(dofIs("trailWristFlexExt", PpJointDof::TrailWristFlexExt), "trailWristFlexExt -> trail wrist");
}

void test_forearm_rotation_deliberately_maps_to_no_dof()
{
    // ⚠ AN ASSERTION THAT SOMETHING DOES NOT WORK, AND IT IS LOAD-BEARING.
    // The grid's LeadForearmRot row is the ISB radioulnar angle, fed by
    // `forearmPronation`. `forearmRotation` is a different quantity that happens to
    // concern the same bone. Routing it there would put two measurements in one
    // cell on a three-sensor rig — last writer wins, silently — and would let a wG3
    // fill a row for an angle it cannot measure at all. An empty row is the honest
    // answer, and a future reader "fixing" this gap has to delete this test first.
    check(!dofForMetricKey(QStringLiteral("forearmRotation")).has_value(),
          "forearmRotation maps to NO dof");
    check(!dofForMetricKey(QStringLiteral("hm.forearmRotation")).has_value(),
          "and its measured rung maps to no dof either");
}

void test_an_unknown_key_is_nullopt()
{
    check(!dofForMetricKey(QStringLiteral("clubheadSpeed")).has_value(), "unrelated key -> nullopt");
    check(!dofForMetricKey(QString()).has_value(), "empty key -> nullopt");
    // Only the exact "hm." prefix is stripped, and only once.
    check(!dofForMetricKey(QStringLiteral("hm.hm.leadWristFlexExt")).has_value(),
          "a doubled prefix is not unwound twice");
    check(!dofForMetricKey(QStringLiteral("HM.leadWristFlexExt")).has_value(),
          "the prefix is case-sensitive");
}

void test_the_two_functions_agree_on_the_same_swing()
{
    // The join the assessment grid actually performs, end to end: find the series
    // by its anatomical name, then map the key it FOUND back to a DOF. Both halves
    // have to strip the prefix or the round trip breaks in the middle, which is
    // exactly how a wG3 swing lost its grid.
    const std::vector<MetricSeries> v = { series("hm.leadWristFlexExt", 2.0) };
    const MetricSeries *m = findByPrecedence(v, QStringLiteral("leadWristFlexExt"));
    check(m != nullptr, "wG3 series located by anatomical name");
    if (!m) return;
    const auto dof = dofForMetricKey(m->key);
    check(dof.has_value() && *dof == PpJointDof::LeadWristFlexExt,
          "and the key it returned still maps to the anatomical DOF");
}

// --- the one precedence rule (refactor 6.1a; R-INS-3) ------------------------

bool ladderIs(const QStringList &got, const QStringList &want, const char *label)
{
    const bool ok = got == want;
    std::printf("  [%s] %s (%s)\n", ok ? "PASS" : "FAIL", label, qPrintable(got.join(QStringLiteral(", "))));
    if (!ok) ++g_fail;
    return ok;
}

void test_the_default_order_readings_then_derived_then_ours()
{
    ladderIs(measurementLadder(QStringLiteral("leadWristFlexExt")),
             { QStringLiteral("hm.leadWristFlexExt"), QStringLiteral("leadWristFlexExt") },
             "P1 a HackMotion reading before our own wrist maths");
    ladderIs(measurementLadder(QStringLiteral("clubheadSpeed")),
             { QStringLiteral("lm.clubheadSpeed"), QStringLiteral("clubheadSpeed") },
             "P2 the launch monitor's reading before our estimate");
    ladderIs(measurementLadder(QStringLiteral("lowPointAhead")),
             { QStringLiteral("lm.lowPointAhead"), QStringLiteral("lowPointFromAttack"), QStringLiteral("lowPointAhead") },
             "P3 reading, then what we derive from a reading, then ours");
    ladderIs(measurementLadder(QStringLiteral("xFactor")), { QStringLiteral("xFactor") },
             "P4 a quantity no instrument reads has one rung, its own");
    ladderIs(measurementLadder(QStringLiteral("lm.faceAngle")), { QStringLiteral("lm.faceAngle") },
             "P5 a key that names its source is asked for as itself");
    ladderIs(measurementLadder(QStringLiteral("hm.leadWristFlexExt")), { QStringLiteral("hm.leadWristFlexExt") },
             "P6 …a HackMotion key too (no hm.hm.)");
    ladderIs(measurementLadder(QString()), {}, "P7 no metric: no rungs");
}

void test_an_authored_ladder_is_the_measures_statement()
{
    ladderIs(measurementLadder(QStringLiteral("clubheadSpeed"), QStringList{}), { QStringLiteral("clubheadSpeed") },
             "P8 an empty authored ladder reads its own key only (the editor's dropped rung stays dropped)");
    ladderIs(measurementLadder(QStringLiteral("clubPath"), QStringList{ QStringLiteral("lm.clubPath") }),
             { QStringLiteral("lm.clubPath"), QStringLiteral("clubPath") }, "P9 the authored rungs, then the measure's key");
    ladderIs(measurementLadder(QString(), QStringList{ QStringLiteral("lm.ballSpeed") }), { QStringLiteral("lm.ballSpeed") },
             "P10 a measure with no key of its own: its rungs");
}

void test_every_shipped_ladder_equals_the_default()
{
    // The shipped pack's 27 authored ladders are the default order, so a measure that authors none reads as one that does.
    QFile f(QStringLiteral(PP_SRC_DIR "/Resources/diagnostics/core.json"));
    if (!f.open(QIODevice::ReadOnly)) { check(false, "P11 the shipped pack opens"); return; }
    const QJsonArray measures = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("measures")).toArray();
    int authored = 0, same = 0;
    for (const QJsonValue &v : measures) {
        const QJsonObject m = v.toObject();
        const QJsonArray pk = m.value(QStringLiteral("preferKeys")).toArray();
        if (pk.isEmpty()) continue;
        ++authored;
        QStringList keys;
        for (const QJsonValue &k : pk) keys << k.toString();
        const QString key = m.value(QStringLiteral("metricKey")).toString();
        if (measurementLadder(key, keys) == measurementLadder(key)) ++same;
        else std::printf("    differs: %s\n", qPrintable(m.value(QStringLiteral("id")).toString()));
    }
    std::printf("    %d authored ladders, %d equal to the default\n", authored, same);
    check(authored > 0 && same == authored, "P11 every authored ladder in core.json equals the default order");
}

void test_pick_takes_the_first_rung_that_answers()
{
    QHash<QString, std::optional<double>> offered{ { QStringLiteral("lm.lowPointAhead"), std::nullopt },
                                                   { QStringLiteral("lowPointAhead"), 1.25 } };
    auto p = pickByPrecedence(QStringLiteral("lowPointAhead"), offered);
    check(p && p->key == QStringLiteral("lowPointAhead") && p->value == 1.25,
          "P12 the device omitted its low point: our estimate answers");
    offered.insert(QStringLiteral("lm.lowPointAhead"), -0.5);
    p = pickByPrecedence(QStringLiteral("lowPointAhead"), offered);
    check(p && p->key == QStringLiteral("lm.lowPointAhead") && p->value == -0.5, "P13 the device's reading wins when it answers");
    check(!pickByPrecedence(QStringLiteral("lowPointAhead"), {}), "P14 nothing offered: nothing picked");
}

// --- the source-tagged set (OBJ-MEASUREMENT; R-INS-2) ------------------------

void test_a_stored_key_is_told_one_way()
{
    StoredKey k = parseStoredKey(QStringLiteral("lm.carryDistance"));
    check(k.metric == QStringLiteral("carryDistance") && k.instrument == Instrument::LaunchMonitor
              && k.kind == MeasurementKind::Reading, "S1 lm.: the launch monitor's reading");
    k = parseStoredKey(QStringLiteral("hm.leadWristRadUln"));
    check(k.metric == QStringLiteral("leadWristRadUln") && k.instrument == Instrument::HackMotion
              && k.kind == MeasurementKind::Reading, "S2 hm.: HackMotion's reading");
    k = parseStoredKey(QStringLiteral("lowPointFromAttack"));
    check(k.metric == QStringLiteral("lowPointAhead") && k.instrument == Instrument::LaunchMonitor
              && k.kind == MeasurementKind::Derived, "S3 an LM-derived key: derived from the monitor, a low point");
    k = parseStoredKey(QStringLiteral("clubheadSpeed"));
    check(k.metric == QStringLiteral("clubheadSpeed") && k.instrument == Instrument::Own
              && k.kind == MeasurementKind::Derived, "S4 a bare key: our own measurement rules");
    check(storedKey(QStringLiteral("leadWristFlexExt"), Instrument::HackMotion) == QStringLiteral("hm.leadWristFlexExt")
              && storedKey(QStringLiteral("ballSpeed"), Instrument::LaunchMonitor) == QStringLiteral("lm.ballSpeed")
              && storedKey(QStringLiteral("xFactor"), Instrument::Own) == QStringLiteral("xFactor"),
          "S5 and spelled back the same way");
}

QJsonObject entry(const char *key, double impact, const char *route = nullptr)
{
    QJsonObject m{ { QStringLiteral("key"), QString::fromLatin1(key) }, { QStringLiteral("unit"), QStringLiteral("mph") },
                   { QStringLiteral("phaseSamples"), QJsonArray{
                         QJsonObject{ { QStringLiteral("phase"), int(Phase::Top) }, { QStringLiteral("value"), -99.0 } },
                         QJsonObject{ { QStringLiteral("phase"), int(Phase::Impact) }, { QStringLiteral("value"), impact } } } } };
    if (route) m.insert(QStringLiteral("routeId"), QString::fromLatin1(route));
    return m;
}

void test_the_set_tags_every_measurement_with_its_source_and_kind()
{
    QJsonObject an{ { QStringLiteral("metrics"), QJsonArray{ entry("clubheadSpeed", 84.0, "faceOn"), entry("lm.clubheadSpeed", 86.5),
                                                             entry("lowPointFromAttack", 1.5), entry("hm.leadWristFlexExt", 12.0) } },
                    { QStringLiteral("gates"), QJsonArray{ QJsonObject{ { QStringLiteral("gate"), QStringLiteral("lm.noData") } } } },
                    { QStringLiteral("gatesVersion"), 2 } };
    const MeasurementSet set = measurementSetFromAnalysis(an);
    check(set.items.size() == 4, "S6 one measurement per stored row");
    const Measurement *ours = set.find(QStringLiteral("clubheadSpeed"), Instrument::Own);
    const Measurement *lm   = set.find(QStringLiteral("clubheadSpeed"), Instrument::LaunchMonitor, MeasurementKind::Reading);
    check(ours && ours->value && *ours->value == 84.0 && ours->source.route == QStringLiteral("faceOn"),
          "S7 ours: its Impact sample, never the first sample; its route");
    check(lm && lm->value && *lm->value == 86.5 && lm->key == QStringLiteral("lm.clubheadSpeed"),
          "S8 the monitor's: the same metric, its own source, its stored key");
    const QHash<QString, double> readings = set.values(Instrument::LaunchMonitor, MeasurementKind::Reading);
    check(readings.size() == 1 && readings.value(QStringLiteral("lm.clubheadSpeed")) == 86.5,
          "S9 filtered by source and kind: the monitor's readings only (the derived low point is not one)");
    check(set.find(QStringLiteral("lowPointAhead"), Instrument::LaunchMonitor, MeasurementKind::Derived) != nullptr,
          "S10 the LM-derived low point is a derived measurement of the monitor's");
    check(set.gateRecords.size() == 1 && set.gatesVersion && *set.gatesVersion == 2, "S11 the gate records ride with the set");
    check(measurementSetFromAnalysis({}).isEmpty() && !measurementSetFromAnalysis({}).gatesVersion, "S12 no analysis: an empty set");
}

} // namespace

int main()
{
    std::printf("=== series key ladder: best instrument first, same anatomy ===\n");

    test_the_measured_rung_wins_when_both_are_present();
    test_order_in_the_vector_does_not_decide_it();
    test_a_bare_key_always_means_our_own_estimate();
    test_a_wg3_swing_resolves_through_the_anatomical_name();
    test_a_missing_key_is_null_not_the_wrong_series();
    test_the_prefix_is_not_matched_loosely();

    test_the_instrument_prefix_is_stripped_before_the_dof_join();
    test_both_producer_spellings_still_reach_their_dof();
    test_forearm_rotation_deliberately_maps_to_no_dof();
    test_an_unknown_key_is_nullopt();
    test_the_two_functions_agree_on_the_same_swing();

    test_the_default_order_readings_then_derived_then_ours();
    test_an_authored_ladder_is_the_measures_statement();
    test_every_shipped_ladder_equals_the_default();
    test_pick_takes_the_first_rung_that_answers();
    test_a_stored_key_is_told_one_way();
    test_the_set_tags_every_measurement_with_its_source_and_kind();

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
