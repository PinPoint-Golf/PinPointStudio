// THE FIRST TIME THE PACK MEETS A REAL SWING.
//
// Everything upstream of this file is dark by design. characteristic_engine_test feeds synthetic
// values; norm_measure_source_test feeds a fake norm set; measure_sample_test reduces a
// hand-written phase grid. Each proves one link. NONE of them can fail if the shipped content is
// wrong about what a recorded swing carries, and that is precisely the failure this stage exists to
// find: 109 measures declare themselves `live`, and until something read a swing off disk nobody
// could say how many of them a capture actually answers.
//
// So this test is a KNOWN-GROUPS validation, not a unit test. The fixtures are real swings copied
// out of the corpus, the values asserted against them were computed independently from the
// JSON, and the coverage numbers are OBSERVED and pinned — they are a measurement of the pipeline,
// and the reason to pin them is that a silent drop from 30 assessable conditions to 3 looks exactly
// like a healthy run.
//
// ── The fixtures, and what each is for ─────────────────────────────────────────────────────────
//
//   rich_7iron      2026-07-10 Wrist_02 / swing_0001 — 43 metrics, 10 segmented phases, club 7 IRON.
//                   The best capture in the corpus, and therefore the ceiling on what today's
//                   producers can answer.
//
//                   ⚠ RE-COPIED FROM THE LIBRARY ON 2026-09-07, AND KEEP DOING THAT. It had been
//                   sitting here since 11 August, analysed by a build from before the P5/P6 ladder
//                   work — 5 phases and 39 metrics — so the ceiling it measured was the ceiling of
//                   a month-old pipeline, not of today's. Re-copying moved the coverage number
//                   from 54 to 63 and the resolved measures from 39 to 53, which is nine
//                   conditions the model could already answer and this file was reporting that it
//                   could not. It also replaced a 41° impact shaft lean — not a golf swing — with
//                   12.5°, and with it a `excessive_shaft_lean` firing that had only ever been an
//                   artefact of the bad number.
//
//                   A STALE FIXTURE UNDERSTATES THE MODEL AND IT DOES IT SILENTLY: every number
//                   below still passes, so nothing says the yardstick has drifted. Re-copy it
//                   whenever the analysis pipeline changes materially, and re-pin what moves.
//   lm_7iron        2026-08-04 Wrist_05 / swing_0025 — 25 lm.* metrics from a GCQuad, ONE phase
//                   (impact), no body metrics at all. The launch-monitor half of the vocabulary.
//   sparse_noclub   2026-07-08 Wrist_01 / swing_0002 — one metric, four phases, and no review block
//                   so no declared club. The degradation case.
//   lm_partial      2026-08-04 Wrist_05 / swing_0004 — the SAME monitor on the same session
//                   reporting 13 of its 25 fields, which is what makes "your monitor did not report
//                   this" a real state rather than a branch invented to fill out an enum. 8 KB, so
//                   it is cheaper to carry than to argue about.
//
// rich_7iron is the ONE fixture that is not byte-for-byte its original: `analysis.pose2d` was
// dropped, taking it from 33 MB to 1.5 MB. Nothing in this chain reads pose2d — buildPhaseGrid()
// consumes `analysis.metrics` and `analysis.phases` and readPhaseGrid() the identity fields — and a
// 33 MB blob of per-frame landmarks in the source tree is a cost every clone pays forever. Every
// metric, every phase, every sample is verbatim.
//
// The fixtures are COPIED TO A TEMPORARY DIRECTORY before use, because readPhaseGrid() writes a
// swing_phasegrid.json sidecar beside the document it parses and a test must not leave artefacts in
// the source tree. It also means the sidecar write/reload path is exercised on real data.
//
//   cmake --build build/analysis-tests --target live_measure_source_test
//   ctest --test-dir build/analysis-tests -R live_measure_source --output-on-failure

#include "../measures/live_measure_source.h"
#include "../norms/norm_provider.h"
#include "../packs/pack_provider.h"
#include "../../Club/p8_refusal_gate.h"     // why there is no P8 (plane stack D6)
#include "../../Metrics/gate_catalogue.h"   // its catalogue entry and sentence

#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QStringList>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <memory>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

// A value spot-check, printed with both numbers so a failure says what moved rather than that
// something did.
static void checkValue(const LiveMeasureSource &src, const char *measureId, double want,
                       double tol = 1e-6)
{
    const std::optional<IMeasureValueSource::Value> v = src.value(QLatin1String(measureId));
    if (!v) {
        std::printf("  [FAIL] %s did not read (%s)\n", measureId,
                    qPrintable(src.missingReason(QLatin1String(measureId))));
        ++g_fail;
        return;
    }
    const bool ok = near(v->value, want, tol);
    std::printf("  [%s] %-24s = %.9g (expected %.9g)\n", ok ? "PASS" : "FAIL", measureId,
                v->value, want);
    if (!ok) ++g_fail;
}

static void checkMissing(const LiveMeasureSource &src, const char *measureId, MissingKind want)
{
    const std::optional<IMeasureValueSource::Value> v = src.value(QLatin1String(measureId));
    const MissingKind got = src.missingKind(QLatin1String(measureId));
    const bool ok = !v.has_value() && got == want;
    std::printf("  [%s] %-24s absent: \"%s\"\n", ok ? "PASS" : "FAIL", measureId,
                qPrintable(src.missingReason(QLatin1String(measureId))));
    if (!ok) ++g_fail;
}

// ── Fixtures ────────────────────────────────────────────────────────────────

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

// Copy <source>/<name>/swing.json to <tmp>/live_swings/<name>/swing.json.
//
// The `live_swings` level is kept because readPhaseGrid() derives the session id from the swing
// directory's PARENT — that is the swing library's shape, and flattening it here would make every
// fixture report a session id this test invented.
static QString stageFixture(const QTemporaryDir &tmp, const char *name)
{
    const QString dst = QDir(tmp.path()).filePath(QLatin1String("live_swings/") + QLatin1String(name));
    if (!QDir().mkpath(dst))
        return QString();
    const QString src = QDir(QLatin1String(PP_LIVE_SWINGS_DIR))
                            .filePath(QLatin1String(name) + QLatin1String("/swing.json"));
    if (!QFile::copy(src, QDir(dst).filePath(QStringLiteral("swing.json"))))
        return QString();
    return dst;
}

// ── The full-stack pass, per fixture ────────────────────────────────────────

struct Coverage {
    int findings    = 0;   // conditions detect() asked at all
    int assessable  = 0;   // Fired or NotFired
    int fired       = 0;
    int unavailable = 0;
    int measures    = 0;   // live measures that produced a value
    // WHICH conditions and measures, not just how many. The pinned counts below
    // are a ledger of what the model can answer, and every move of one is
    // supposed to arrive with a written reason — but a count cannot tell you
    // which condition moved, so working that reason out meant rebuilding the
    // pack at two revisions and reasoning about the graph by hand. Set
    // PP_COVERAGE_DUMP=1 to print these; diff two runs and the delta is the
    // reason, ready to be written down.
    int assessedPartial = 0;   // assessed, but with at least one term it could not read
    QStringList partialIds;    // …which, and the terms it could not read
    QStringList assessableIds;
    QStringList firedIds;
    QStringList phaseBlockedIds;   // live, the metric is here, a phase the reducer needs is not
    QStringList resolvedMeasureIds;
};

// Print the sets behind the counts, on request. Sorted so two runs diff cleanly.
static void dumpCoverage(const Coverage &cov, const char *label)
{
    if (qEnvironmentVariableIsEmpty("PP_COVERAGE_DUMP"))
        return;
    QStringList conds = cov.assessableIds;
    QStringList fired = cov.firedIds;
    QStringList meas  = cov.resolvedMeasureIds;
    conds.sort();
    fired.sort();
    meas.sort();
    for (const QString &id : conds)
        std::printf("  DUMP %s assessable %s\n", label, qPrintable(id));
    for (const QString &id : fired)
        std::printf("  DUMP %s FIRED      %s\n", label, qPrintable(id));
    QStringList partial = cov.partialIds;
    partial.sort();
    for (const QString &id : partial)
        std::printf("  DUMP %s partial    %s\n", label, qPrintable(id));
    QStringList blocked = cov.phaseBlockedIds;
    blocked.sort();
    for (const QString &id : blocked)
        std::printf("  DUMP %s phaseBlocked %s\n", label, qPrintable(id));
    for (const QString &id : meas)
        std::printf("  DUMP %s measure    %s\n", label, qPrintable(id));
}

static Coverage runFixture(const QString &dir, const CharacteristicPack &pack,
                           const std::shared_ptr<const INormProvider> &norms, const char *label)
{
    std::printf("\n── %s ──────────────────────────────────────\n", label);

    const LiveMeasureSource src(dir, pack);
    const LiveDetection     d = detectForSwing(src, pack, norms);

    Coverage cov;
    cov.findings = int(d.result.findings.size());

    // (a) NO FABRICATED VALUE. Every finding is either an assessment carrying the reading that
    // produced it, or Unavailable carrying nothing at all. A NotFired finding with no evidence
    // would be the module's founding mistake — a clean bill of health nobody measured.
    bool shapeOk = true;
    for (const Finding &f : d.result.findings) {
        switch (f.state) {
        case FindingState::Fired:    ++cov.fired; cov.firedIds.append(f.conditionId); [[fallthrough]];
        case FindingState::NotFired: ++cov.assessable;
            cov.assessableIds.append(f.conditionId);
            if (!f.missingMeasures.isEmpty()) {
                ++cov.assessedPartial;
                cov.partialIds.append(f.conditionId + QLatin1Char(' ') + f.missingMeasures.join(QLatin1Char(',')));
            }
            if (!f.evidence.hasEvidence) {
                shapeOk = false;
                std::printf("      %s assessed with no evidence\n", qPrintable(f.conditionId));
            }
            break;
        case FindingState::Unavailable: ++cov.unavailable;
            if (f.evidence.hasEvidence) {
                shapeOk = false;
                std::printf("      %s unavailable but carries evidence\n", qPrintable(f.conditionId));
            }
            break;
        }
    }

    // WHY THE OTHERS DID NOT READ, counted by kind. The coverage number says how much of
    // the model a swing can answer; this says what is standing in the way, which is the
    // question the next stage of work is actually asking. A capture whose gap is
    // MetricNotProduced needs a producer; one whose gap is PhaseNotSegmented needs the
    // ladder, and re-analysing it may be enough on its own.
    QMap<MissingKind, int> why;
    for (const Measure &m : pack.measures) {
        if (m.status != MeasureStatus::Live)
            continue;
        if (src.value(m.id).has_value()) {
            ++cov.measures;
            cov.resolvedMeasureIds.append(m.id);
            continue;
        }
        const MissingKind k = src.missingKind(m.id);
        why[k] += 1;
        if (k == MissingKind::PhaseNotSegmented)
            cov.phaseBlockedIds.append(m.id);
    }

    std::printf("  club %s -> context %s (session %s)\n", qPrintable(d.club),
                qPrintable(d.contextId), qPrintable(src.sessionId()));
    std::printf("  COVERAGE: %d of 192 conditions assessable on %s"
                "  (%d evaluated, %d fired, %d unavailable)\n",
                cov.assessable, label, cov.findings, cov.fired, cov.unavailable);
    // The denominator is COUNTED, not written down. It was the literal 109 while the pack carried
    // 111 live measures, so the line understated the gap it exists to show — and a stale
    // denominator in a coverage report is worse than none, because it reads as precise.
    int liveTotal = 0;
    for (const Measure &m : pack.measures)
        if (m.status == MeasureStatus::Live) ++liveTotal;
    std::printf("  MEASURES: %d of %d live measures resolved\n", cov.measures, liveTotal);
    std::printf("  PARTIAL:  %d of %d assessed with a term they could not read\n",
                cov.assessedPartial, cov.assessable);
    const auto kindName = [](MissingKind k) {
        switch (k) {
        case MissingKind::None:                     return "read";
        case MissingKind::UnknownMeasure:           return "unknownMeasure";
        case MissingKind::NoMetricBinding:          return "noMetricBinding";
        case MissingKind::NoAnalysis:               return "noAnalysis";
        case MissingKind::NoLaunchMonitor:          return "noLaunchMonitor";
        case MissingKind::LaunchMonitorFieldAbsent: return "lmFieldAbsent";
        case MissingKind::MetricNotProduced:        return "metricNotProduced";
        case MissingKind::PhaseNotSegmented:        return "phaseNotSegmented";
        case MissingKind::CaptureDataIssue:         return "captureDataIssue";
        case MissingKind::SessionScope:             return "sessionScope";
        case MissingKind::CompositeCoverage:        return "compositeCoverage";
        }
        return "?";
    };
    std::printf("  BLOCKED: ");
    for (auto it = why.constBegin(); it != why.constEnd(); ++it)
        std::printf("%s=%d ", kindName(it.key()), it.value());
    std::printf("\n");
    dumpCoverage(cov, label);

    check(shapeOk, "every finding is assessed WITH evidence or unavailable WITHOUT it");
    // 145 -> 153 on 2026-10-22: the eight posture conditions (posture_metrics_design.md §7.2).
    // 153 -> 154 the same day: lead_knee_folds_backswing (posture_chart_design.md D7).
    // 160 -> 162 on 2026-11-08: fade and draw (shot_outcomes_design.md §3.4).
    // 162 -> 164 on 2026-11-13: arms_swing_around, arms_lift (plane stack D2).
    check(cov.findings == 164,
          "detect() asked all 164 non-latent, non-withdrawn conditions (the shipped pack binds none)");   // +4 absolute swing-plane tails (2026-10-29)   // +club_before_arms, D27 (2026-10-17)   // +2 lie tails (2026-10-01)   // +7 kinematic-sequence checks (2026-10-14)   // +5 peak-against-impact faults (2026-10-15)

    // (d) Where a norm resolved, the corridor on the evidence is a real band and z is finite.
    bool corridorsOk = true;
    for (const Finding &f : d.result.findings) {
        if (!f.evidence.hasEvidence || !f.evidence.hasCorridor)
            continue;
        const MeasureEvidence &e = f.evidence;
        // Open on exactly one side is a one-sided measure and legal; open on both is not a band.
        const bool sane = !(e.lowOpen && e.highOpen) && e.corridorLo <= e.corridorHi
                          && std::isfinite(e.z) && std::isfinite(e.value);
        if (!sane) {
            corridorsOk = false;
            std::printf("      %s: [%g,%g] z=%g\n", qPrintable(f.conditionId), e.corridorLo,
                        e.corridorHi, e.z);
        }
    }
    check(corridorsOk, "every graded finding carries a sane corridor and a finite z");

    return cov;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("live_measure_source_test\n");

    // THE SHIPPED CONTENT, and deliberately NOT makeCharacteristicPackProvider() /
    // sharedNormProvider(). Those assemble the developer's own packs and norm sets from
    // AppDataLocation on top of core, which would make the pinned coverage numbers below depend on
    // whose machine the suite ran on. The resource providers read core and nothing else.
    const std::unique_ptr<ICharacteristicPackProvider> packProv = makeResourcePackProvider();
    const CharacteristicPack                          &pack     = packProv->pack();
    const std::shared_ptr<const INormProvider> norms{ makeResourceNormProvider().release() };

    std::printf("\ncontent\n");
    // 157 -> 158 on 2026-09-23: hands_set_high, the setup habit behind reaching for the ball.
    // 173 -> 181 on 2026-10-22: the posture family and setup conditions (posture_metrics_design.md
    // §7.2) — loss_of_posture, posture_sinks_impact, loss_of_posture_backswing,
    // posture_sinks_backswing, poor_setup_posture, weight_on_toes_address, arms_reaching_at_address,
    // reverse_tilt_at_address.
    // 181 -> 182 on 2026-10-22: lead_knee_folds_backswing, the lead knee folding going back
    // (posture_chart_design.md D7).
    check(pack.conditions.size() == 192, "the shipped pack carries 192 conditions");   // +arms_swing_around, +arms_lift (2026-11-13, plane stack D2)   // +fade, +draw (2026-11-08)   // +4 absolute swing-plane tails (2026-10-29)   // +club_before_arms, D27 (2026-10-17)   // +5 peak-against-impact faults (2026-10-15)   // +7 kinematic-sequence checks (2026-10-14, design §10.4)   // +lie_steep_at_impact, +lie_flat_at_impact (2026-10-01)
    // 130 -> 135 with the plumb-bob work: hipLineTilt gained an impact reading and an
    // address-to-impact delta, and plumbBobDistance arrived with three of its own.
    //
    // 134 -> 135 on 2026-09-06 (39542bc): the posture cluster was rebuilt. `m_spineBendLoss` went
    // and `m_spineBendDive` and `m_headLiftDown` arrived, so net one. That commit did not update
    // this line and the suite has been red since; the numbers here are a ledger and moving one
    // without writing down why is the whole thing this file is for.
    //
    // 135 -> 134 on 2026-09-04: `m_pelvisSwayFinish` deleted. It reduced pelvisSway at the FINISH,
    // and pelvisSway carries a P1-P7 phase domain — past impact the pelvis has turned, so its
    // lateral offset in a face-on image is the rotation and not the translation the measure named.
    // 135 -> 136 on 2026-09-14: m_pelvisSinkTop, noProducer on a belt-line series (the hip keypoints
    // cannot read a sink; see its gapReason).
    // 136 -> 137 on 2026-09-23: m_handPathLoop, the down-the-line hand loop over_the_top now reads.
    // 138 -> 140 on 2026-10-02: m_pelvisPeakTime, m_thoraxPeakTime — sequence_order's Order test reads
    // event TIMES, and was fed rotation peak ANGLES until then.
    // 140 -> 141 on 2026-10-08: m_trailForearmTop, flying_elbow's down-the-line reading.
    // 141 -> 142 on 2026-10-08: m_spineBendLossDown, early extension's standing-up half.
    // 153 -> 156 on 2026-10-15: m_leadForearmRotation_p2/_p4/_p7, the one-segment forearm twist
    // (forearmRotation / hm.forearmRotation) beside the radioulnar roll rows, monitored on
    // placeholder corridors until a directed capture fixes their sign.
    // 156 -> 168 on 2026-10-22: the posture composites (posture_metrics_design.md §3.4, §6.1) — nine
    // part measures read at one instant (m_spineBendChangeImpact, m_pelvisThrustImpact,
    // m_headLiftImpact, m_pelvisLiftImpact, m_trunkRiseImpact, m_spineBendChangeTop,
    // m_axisTiltAddress, m_armHangAddress, m_headPitchAddress) and the three composites
    // m_postureHeldImpact, m_postureHeldTop, m_setupPosture.
    // 169 -> 200 on 2026-10-22: the posture chart's per-position changes from address
    // (posture_chart_design.md §7.3) — m_headLift_p2/_p3/_p5/_p6/_p8, m_pelvisThrust_p2…_p6,
    // m_spineBendChange_p2/_p3/_p5/_p6, m_leadKneeFlex_p2…_p8, m_trailKneeFlex_p2/_p3/_p5…_p8,
    // m_pelvisLift_p2/_p3/_p5/_p6; the other cells reuse existing P1 -> PN deltas.
    // 200 -> 203 on 2026-10-22: m_hipDepthButt_p3/_p4/_p5 — hip depth at P3–P5 read on the butt
    // line (hipDepthButt) instead of the hip joints the turn drags (posture_chart_design.md §2.3,
    // stage 4c); P2, P6 and P7 stay on pelvisThrust.
    // 203 -> 206 on 2026-10-22: m_spineBackLine_p3/_p4/_p5 — the back line's change from address at P3–P5,
    // drawn as the trunk-bend row's second reading under its own ⚠ (posture_chart_design.md §2.3); no signal
    // reads them and they carry no band of their own.
    check(pack.measures.size() == 218, "…and 218 measures");   // +m_clubSlotTop (2026-11-13, plane stack D3: the climb to P3 placed in the on-plane window halfway back; the slot pair reads it)   // +m_armLiftTop, +m_leadElbowBendTop3d (2026-11-13, plane stack D2: the lead upper arm against the shoulder turn at P4; bent_lead_arm's 3-D elbow)   // +m_leadElbowFoldAfterImpact (2026-11-13, plane stack D5: the lead elbow's added bend in the 100 ms after impact; chicken_wing reads it)   // +m_takeawayPlane (2026-11-13, plane stack D1: the swing plane at P2 against address; flat/steep_takeaway read it)   // +m_shoulderTiltVsBend (2026-11-13, W7: the shoulder tilt at P4 less the forward bend at P1; the flat/steep faults read it)   // +m_carryShortfall, +m_ballSpeedShortfall (2026-11-08)   // +m_backswingPlaneAngle, +m_downswingPlaneAngle (2026-10-29)   // +m_headRiseBackDtl (2026-10-22, standing up going back from down the line)   // +11 kinematic-sequence measures (2026-10-14)   // +m_shaftLieDelta
    check(!norms->norms().norms.empty(), "the shipped norm set loaded");

    QTemporaryDir tmp;
    check(tmp.isValid(), "a temporary directory for the fixture copies");
    if (!tmp.isValid()) { std::printf("\nFAILURES\n"); return 1; }

    const QString rich    = stageFixture(tmp, "rich_7iron");
    const QString lm      = stageFixture(tmp, "lm_7iron");
    const QString sparse  = stageFixture(tmp, "sparse_noclub");
    const QString lmPart  = stageFixture(tmp, "lm_partial");
    check(!rich.isEmpty() && !lm.isEmpty() && !sparse.isEmpty() && !lmPart.isEmpty(),
          "the fixture swings staged out of the source tree");
    if (rich.isEmpty() || lm.isEmpty() || sparse.isEmpty() || lmPart.isEmpty()) {
        std::printf("\nFAILURES\n");
        return 1;
    }

    // ── Known values, computed independently from the JSON ──────────────────
    //
    // Each figure below was derived by hand from the fixture's own `metrics[]` and `phases[]`,
    // applying the conventions measure_sample.h states: a ±15 ms windowed median about each phase
    // instant, falling back to the metric's own phaseSamples entry where the curve is empty. They
    // are the arithmetic the code is SUPPOSED to do, not a recording of what it did.
    std::printf("\nrich_7iron: known values\n");
    {
        const LiveMeasureSource src(rich, pack);

        check(src.club() == QLatin1String("7 IRON"), "the club is read off the swing doc");
        check(src.contextId() == QLatin1String("iron_7"), "…and resolves to the iron_7 context node");
        check(!src.hasLaunchMonitor(), "no launch monitor on this capture");
        check(src.grid().metrics.size() == 43, "43 metrics reached the phase grid");

        // At P1, straight off the metric's phaseSamples entry — ballPosition ships with an EMPTY
        // curve, so this is the fallback path measure_sample.h argues for.
        checkValue(src, "m_ballPosition", 45.4721757);
        checkValue(src, "m_stanceWidth",  84.3203248);

        // At P7, from the windowed median of a 745-sample curve.
        checkValue(src, "m_impactShaftLean",     12.5);
        checkValue(src, "m_clubheadSpeedImpact", 61.5298797);

        // Summary metric, empty curve, labelled at impact.
        checkValue(src, "m_tempoRatio", 2.9368742);

        // Delta P1 -> P4.
        checkValue(src, "m_headSwayBack", -2.69404549);

        // THE ONE THAT PROVES THE SPANS ARE REAL. An anchored Extremum over P1..P4: the pelvis
        // reaches -28.60 somewhere between the top and address, while the P4 ENDPOINT reads only
        // -24.08. A reduction over phase values alone would miss 4.5% of stance width, and the
        // signed-deviation contract makes the answer (peak - anchor) rather than |peak - anchor|.
        // An anchored Extremum over the backswing. Until 2026-09-04 this was the raw minimum sample
        // (-28.6010); the span extremes are now the extremum of the 40 ms centred-window mean
        // (series_reduce.h, design §5.2), which pulls a one-frame trough toward its neighbours —
        // the direction every extremum measure is expected to move, by about one frame's noise.
        checkValue(src, "m_pelvisSwayBack", -28.3617744);
        checkValue(src, "m_pelvisSwayImpact", 17.1839723);
        // Was `m_pelvisSwayFinish`, 20.78817002111798, until 2026-09-04. That measure read
        // pelvisSway at the FINISH, and pelvisSway is a P1-P7 quantity: past impact the pelvis has
        // turned, so its lateral offset in a face-on image is the rotation and not the translation
        // the measure named (MetricDescriptor::domain, design 5.1). It was deleted, and the honest
        // finish reading is this one — a distance ALONG the stance line, which survives the turn.
        // Single-sample +-15 ms window here, which is why it equals the producer's own phaseSample.
        checkValue(src, "m_comOverLeadFootFinish", 50.5375417);
    }

    std::printf("\nlm_7iron: known values\n");
    {
        const LiveMeasureSource src(lm, pack);
        check(src.hasLaunchMonitor(), "the lm.* metrics are seen as launch-monitor data");
        check(src.contextId() == QLatin1String("iron_7"), "club 7 IRON -> iron_7");

        // Straight passthroughs of the GCQuad's own readings, labelled at impact.
        checkValue(src, "m_lmClubPath",    -0.494054);
        checkValue(src, "m_lmAttackAngle", -2.730836);
        checkValue(src, "m_smashFactor",    1.388462623429449);
    }

    std::printf("\nsparse_noclub: known values\n");
    {
        const LiveMeasureSource src(sparse, pack);
        check(src.grid().metrics.size() == 1, "exactly one metric reached the grid");
        // No review block and no captured club, so NOTHING declared it — and since 2026-09-14 the
        // grid says so rather than borrowing the picker's DRIVER stub: 58 corpus shots that were an
        // iron in the frame had been graded against driver corridors on that stub. An empty club
        // resolves to the default context, whose rows were written for a swing of unknown club.
        check(src.club().isEmpty(), "an undeclared club reads as no club at all, never as the stub");
        checkValue(src, "m_impactShaftLean", 163.63672117342375);
    }

    // ── The missing-reason vocabulary ───────────────────────────────────────
    std::printf("\nwhy a measure did not read\n");
    {
        const LiveMeasureSource rich_(rich, pack);
        const LiveMeasureSource lm_(lm, pack);
        const LiveMeasureSource part_(lmPart, pack);

        checkMissing(rich_, "m_notAMeasureAtAll", MissingKind::UnknownMeasure);

        // An lm.* measure on a capture with no monitor, and the same measure on a capture whose
        // monitor simply did not report that field. The distinction is the difference between
        // "connect a launch monitor" and "yours did not answer this one", and it is the only pair
        // in the vocabulary where a golfer's next action differs.
        checkMissing(rich_, "m_smashFactor", MissingKind::NoLaunchMonitor);
        checkMissing(part_, "m_lmClubPath",  MissingKind::LaunchMonitorFieldAbsent);
        check(part_.hasLaunchMonitor() && part_.grid().metrics.size() == 13,
              "…and lm_partial really is a monitor reporting 13 of its 25 fields");

        // A body metric on the launch-monitor-only capture: the producer exists, this shot has none.
        checkMissing(lm_, "m_ballPosition", MissingKind::MetricNotProduced);

        // The metric IS here; the reducer wants P5 and P6, which this swing never segmented. This is
        // the reason that would otherwise be invisible — it looks identical to a missing producer
        // from outside, and it is the one a golfer can act on by re-recording.
        //
        // rich_ is a static copied swing.json fixture, so this stays a valid PhaseNotSegmented
        // pin regardless of segmenter changes upstream. But note: as of the 2026-08-09 segmenter
        // change, the segmenter DOES now emit P5 (ArmParallelDown) on forearm-bound swings — this
        // fixture predates that and simply has no P5/P6 ticks recorded. When the fixture is
        // regenerated from a current segmenter run, re-check whether it still lacks P5/P6 (and
        // still exercises PhaseNotSegmented) or whether this assertion needs to flip to a
        // different missing-measure fixture.
        // ⚠ MOVED ON 2026-09-07 WHEN THE FIXTURE WAS REGENERATED, and the note above is now
        // history: rich_7iron came from a current analysis run and DOES have P5 and P6, so
        // `m_lagAngleDown` reads and no longer exercises this. Three live measures still do,
        // all of them reading `at p4`: m_shoulderPlane, m_spineSideBendTop, m_trailElbowRise.
        //
        // WORTH A LOOK BY SOMEONE CHASING COVERAGE, because the capture is not obviously short
        // of a top: it carries a Top tick, and these three still cannot be resolved at P4. So
        // the block is in how the reducer resolves p4 rather than in a plainly absent phase,
        // and three measures is three conditions' worth of coverage sitting behind it.
        checkMissing(rich_, "m_spineSideBendTop", MissingKind::PhaseNotSegmented);

        // session_measure_refused_per_swing — a session-scope measure (§10.8 E2) has no value on
        // any one swing, and says so rather than blaming the capture.
        checkMissing(rich_, "m_ksOrderShare", MissingKind::SessionScope);
        checkMissing(rich_, "m_ksTimingSpread", MissingKind::SessionScope);
        check(missingReasonText(MissingKind::SessionScope) == QLatin1String("read over the session, never one swing"),
              "session_measure_refused_per_swing: \"read over the session, never one swing\"");

        // composite_part_endpoints (posture_metrics_design.md §6.3 step 5): a change read between two
        // instants carries the series at both, and they are the value's own ends — atEnd − atStart IS
        // a Delta's value, and atStart + value IS an anchored extremum's peak. A value read AT one
        // instant carries neither.
        {
            int deltas = 0, extrema = 0, ats = 0, bad = 0;
            for (const Measure &m : pack.measures) {
                const auto v = rich_.value(m.id);
                if (!v) continue;
                const Reducer &r = m.reducer;
                if (r.kind == ReducerKind::Delta) {
                    ++deltas;
                    if (!(std::fabs(v->atEnd - v->atStart - v->value) < 1e-9)) ++bad;
                } else if (r.kind == ReducerKind::Extremum && r.anchor.has_value()) {
                    ++extrema;
                    if (!std::isfinite(v->atStart) || !std::isfinite(v->atEnd)) ++bad;
                } else if (r.kind == ReducerKind::At) {
                    ++ats;
                    if (!std::isnan(v->atStart) || !std::isnan(v->atEnd)) ++bad;
                }
            }
            std::printf("  ENDPOINTS: %d deltas, %d anchored extrema, %d instants read on rich_7iron\n",
                        deltas, extrema, ats);
            check(deltas > 0 && bad == 0,
                  "composite_part_endpoints: every Delta's ends differ by its value, every anchored "
                  "extremum has both ends, every instant has neither");
        }

        // (c) Nothing that has no producer reports a number. Zero would be a value the golfer's
        // ledger would happily plot.
        int planned = 0, wrong = 0;
        for (const Measure &m : pack.measures) {
            if (m.status == MeasureStatus::Live)
                continue;
            ++planned;
            if (rich_.value(m.id).has_value()) {
                ++wrong;
                std::printf("      %s (%s) produced a value\n", qPrintable(m.id),
                            qPrintable(measureStatusName(m.status)));
            }
        }
        std::printf("  %d measures are not live on the richest capture in the corpus\n", planned);
        // 20 -> 19: m_transitionPlaneShift (planned, `swingPlane` delta p4->p5, down-the-line) was
        // retired unbuilt and replaced by m_transitionPlaneDelta, which is LIVE — it has a producer
        // (shaft_plane.h via ShaftPlaneStage). Worth knowing WHY that one is live rather than
        // planned, because it reads oddly next to a measure that is deliberately normless: this
        // check equates "not live" with "nothing produces it", and a measure that DOES produce a
        // number while awaiting real norms would break that equation if it were parked as planned.
        // 19 -> 20: m_pelvisRotRateP6P7 was retired to `noProducer` and re-pointed at a new series,
        // `pelvisRotationSigned`, which nothing emits. The rate it asked for cannot be taken from
        // the magnitude series it used to read — that curve folds through zero as the pelvis
        // squares up, so |x|' = sign(x)x' inverts the reading at the exact instant the measure is
        // about, and seven of seven shots on the 9 Sep session fired by construction.
        // 20 -> 22 on 2026-09-14, the same trade made twice more. m_shoulderAlignment is
        // `noProducer` on the planned `shoulderLineYaw`: it had read shoulderPlaneAngle at P1, the
        // face-on TILT, which the grip sets at ~-10° on every right-hander, so a 0 ± 4° corridor
        // fired "open" on square shoulders. m_handSpeedP6P7 is GONE and m_clubheadPeakLead stands in
        // its place, `noProducer` on a series nothing emits: the hands' delivery-to-impact rate is
        // negative on every good release (97 of 97 corpus swings), so a floor under it graded the
        // release working as the golfer quitting. Neither could be re-seated into honesty — both
        // asked a question their series could not answer, which is what a gap row is for.
        // 22 -> 21 the same day: m_clubheadPeakLead went live when the kinematics stage learned
        // to emit the peak's timing. This fixture is a verbatim corpus copy from an older build,
        // so it still carries no clubheadPeakLead key and the measure resolves nothing here —
        // which is the honest state of every swing on disk until its next re-analysis.
        // 21 -> 23 on 2026-09-14 (the corridor review): m_shoulderPlane is noProducer on the planned
        // shoulderPlaneAngle3d (the face-on line at P4 is foreshortened into noise), and the new
        // m_pelvisSinkTop waits on a belt-line series (the hip keypoints migrate as the pelvis turns).
        // 23 -> 20 on 2026-09-22: the down-the-line rungs were BUILT (848512a3), so
        // m_pelvisThrustDown, m_spineBendDive and m_shaftPlaneDelivery each gained a producer and
        // went live. Note the DIRECTION: every earlier move on this number came from a measure being
        // retired to `noProducer` because its series could not answer the question honestly. This one
        // falls because the work landed, which is the only reason this count is supposed to fall.
        //
        // ⚠ 848512a3 MOVED metric_catalogue_test.cpp's planned count (20 -> 13) AND STOPPED THERE.
        // This suite and diagnostics_catalogue_integrity_test kept their old pins and went red on the
        // next full run — a producer landing has to be walked through EVERY pinned count, in all
        // three suites, or the ones that were missed read as regressions to whoever runs them next.
        // 20 -> 17 on 2026-09-23: crouching (excessive_knee_flex, itself caused by poor core
        // stability) was authored as the cause of standing too upright, trail knee straightening
        // and pelvis thrust going back, so m_spineBendAtAddress, m_trailKneeFlex and
        // m_pelvisThrustBack left `held` for live. Their producers had shipped with 848512a3; what
        // they waited on was an explanation, not a pipeline. 17 -> 16 the same day: hands set too
        // high (a setup habit, not a physical limit) now explains reaching for the ball, so
        // m_ballBodyGap followed. 16 -> 15: the backswing plane faults were given their causes (the
        // arms lifting — outside takeaway, disconnection, flying elbow — for steep; the hands working
        // round the body — inside takeaway, pinned arms, a deep trail elbow — for flat), so
        // m_shaftPlaneBackswing went live. Three DTL measures are still held.
        // 15 -> 14 on 2026-10-01: m_pelvisRotRateP6P7 (hip_stall) went live on the two-camera
        // route's pelvisRotationSigned. This fixture is a single-camera capture, so it still
        // produces nothing for it — which the next check (no value from a non-live measure) and the
        // live measure's own absence here both stay honest about.
        // 14 -> 11 on 2026-10-20: the launch monitor became the primary source for what it
        // measures (Mark's decision for the session report). m_clubPathAtImpact, m_launchAngle and
        // m_launchDirection now prefer their lm.* twins, so each has a live rung and is live;
        // m_ballSpeed did the same but is HELD — it produces on a monitor shot and has no corridor,
        // so nothing grades it. Still not live, and still silent here: this fixture has no monitor.
        // 11 -> 10 on 2026-10-22 (posture_metrics_design.md §3.1, §7.4): m_kneeFlexAddress reads the
        // trail knee the down-the-line camera can see and goes live, m_balanceHeelToeAddress goes live
        // on a provisional norm, and the new m_headPitchAddress is HELD until the repeatability gate.
        // 10 -> 11 on 2026-10-22: m_trunkRiseImpact is HELD — it failed validation V1 against the
        // down-the-line bend (docs/research/data/posture/posture_composites_2026-10-22.md).
        // 11 -> 10 the same day: m_headPitchAddress passed V5 (repeatability) and went live.
        // 10 -> 11 on 2026-10-22: m_spineBendChangeTop is HELD until the trunk IMUs can corroborate it —
        // the down-the-line hip-to-shoulder line tips upright at the top with the turn, not the posture
        // (trunk_imu_sequence_grf_design.md §8.8).
        // 11 -> 6 on 2026-11-12 (the planned-metrics promotion): m_shaftDirectionP2 and
        // m_shaftDirectionP4 (W1, ShaftDirectionStage — the fused shaft's bearing) and m_shoulderPlane,
        // m_shoulderAlignment and m_leadKneeFlex (W2, Skeleton3dBodyStage — the two-camera 3-D fit)
        // went live. This fixture is a single-camera capture, so none of the five produces a value
        // here — the producer landing is the only reason the count fell. metric_catalogue_test's
        // planned count moved with them (12 -> 9); diagnostics_catalogue_integrity_test pins no count
        // here and asserts the two zero-sums (no overclaim, no understatement), which still hold.
        // 6 -> 14 on 2026-11-13 (plane stack D5): the eight m_leadElbowFlex_p1…p8 are HELD. leadArmFlexion
        // gained a face-on route, and their corridors came from the wrist sensors' grid; on the library the
        // face-on elbow at the top reads ~37° above address, so they would have graded bent_lead_arm on every
        // camera swing. This fixture carries no leadArmFlexion at all, so none of the eight produces here.
        check(planned == 14, "14 shipped measures are not live yet");   // 16 -> 14 on 2026-10-14: m_ksOrderShare, m_ksTimingSpread went live on the session reducer (scope: session — still no per-swing value, which `wrong` below confirms)
        check(wrong == 0, "…and not one of them produced a value");
    }

    // ── The full stack, per fixture ─────────────────────────────────────────
    const Coverage cRich   = runFixture(rich,   pack, norms, "rich_7iron");
    const Coverage cLm     = runFixture(lm,     pack, norms, "lm_7iron");
    const Coverage cSparse = runFixture(sparse, pack, norms, "sparse_noclub");

    // THE NUMBERS BELOW ARE OBSERVED, NOT AUTHORED. They are what the shipped pack and norm set
    // answer on these three swings today, pinned so that a producer landing, a corridor being
    // authored, or a metric key drifting shows up as a deliberate edit here rather than as silence.
    // Raising them is the point of the next several stages; lowering one without saying so is the
    // regression this test exists to catch.
    std::printf("\nthe measurement\n");
    check(cRich.assessable > 0 && cLm.assessable > 0, "the two rich captures assess something at all");
    // 51 → 53 on 2026-08-12: `top` and `sky`, and this fixture has NO launch monitor at all — so
    // it is worth saying why they became answerable on a camera-only swing. Both are conjunctions
    // (detection: all), and one conjunct that was assessed and did not fire settles an AND whatever
    // the unreadable terms would have said. This capture has no strike height and never will, but
    // it has an attack angle, and an attack angle of -4° is not the upward strike a top requires.
    // "Definitely not a top" is a real answer, and the conjunction reaches it from evidence no
    // single one of its terms could.
    //
    // 53 -> 54 on 2026-09-06 (39542bc), and NOT ONE NEW MEASURE RESOLVED — the resolved count
    // below is unchanged at 39, and the fired count is unchanged at 15. The one is
    // `coming_out_of_it`, which replaced `loss_of_posture`, and it moved from Unavailable to
    // NotFired. The old condition read spine forward bend alone, which is sagittal, needs
    // down-the-line or trunk IMUs and has no producer at all, so it could never be answered
    // either way. The new one is a conjunction of `sig_headRiseDown` (on the new
    // `m_headLiftDown`, which this fixture cannot produce — it is a verbatim corpus copy from an
    // older build with no headLift key) AND `sig_axisTiltImpactHigh` (on `m_axisTiltImpact`,
    // which the fixture does carry). The axis tilt was assessed and did not fire, and one
    // known-false conjunct settles an AND whatever the unreadable term would have said. Same
    // mechanism as `top` and `sky` on 2026-08-12.
    //
    // ⚠ WORTH KNOWING WHAT THAT KIND OF "NO" IS: on a capture that cannot read the other
    // conjunct, this condition can answer NO and never YES — a true head-rise would leave the
    // AND unsettled and report Unavailable. So the negative is sound and the fault's prevalence
    // across unre-analysed swings is structurally understated. The PARTIAL line printed by
    // runFixture() counts these; see the pin below.
    //
    // 54 -> 53 on 2026-09-04, and the one is exactly the deleted content. `weight_back_at_finish`
    // was detected only by `sig_weightBackFinish`, which read `m_pelvisSwayFinish` — pelvisSway at
    // the finish, outside the P1-P7 domain where that projection means anything. The measure and
    // both signals on it are gone, so the condition has no detector and reports Unavailable. Its
    // sibling `off_balance_finish` is NOT in the delta: it kept `sig_offBalanceFinish` on
    // `m_comOverLeadFootFinish`, which this fixture carries, so it is still assessable.
    // 63 -> 62 on 2026-09-12, and the one is `hip_stall`: its only signal read
    // m_pelvisRotRateP6P7, which is now noProducer on a series nothing emits. The condition has no
    // detector left and reports Unavailable — which is the point. It was firing on every shot of
    // every camera-only session off the fold in an unsigned curve, and an Unavailable that says so
    // is worth more than a pattern that is always there.
    // 62 -> 59 on 2026-09-14, and the three are the conditions whose measures came off a quantity
    // that could not carry them. `alignment_open` and `alignment_closed` read m_shoulderAlignment,
    // now noProducer on the planned `shoulderLineYaw`: it had been grading the shoulder line's
    // face-on TILT at address, which the grip sets at ~-10° on every right-hander, so "open" fired
    // on square shoulders (7 of 7 on the 9 Sep session). `deceleration` read m_handSpeedP6P7, a
    // floor under the hands' delivery-to-impact rate — which is negative on every good release
    // (97 of 97 corpus swings) — and now waits on `clubheadPeakLead`. All three report Unavailable
    // with a gapReason that says why, which is the same trade hip_stall made two days earlier.
    // 59 -> 56 on 2026-09-14 (the corridor review), and the three are attack_too_steep,
    // attack_too_shallow and pelvis_sink_backswing. The first two read m_attackAngle, which no longer
    // falls back to the camera's projected attack angle (−65° to +38° over the corpus) and this
    // fixture has no launch monitor; the third moved to m_pelvisSinkTop, noProducer on a belt-line
    // series. `top` and `sky` are NOT in the delta: each is a conjunction with another term this
    // fixture assessed and found false, which settles the AND whatever the attack term would say.
    // 56 -> 55 on 2026-09-23: over_the_top. This fixture is face-on only and the condition now
    // reads the down-the-line hand loop, so it is Unavailable here rather than assessed off a
    // face-on plane delta whose placeholder corridor could never fire in any case.
    // 55 -> 54 on 2026-10-02: sequence_order. It now reads the PLACED trunk nodes' instants, and this
    // face-on fixture places neither, so it is Unavailable — where it used to be "assessed" by
    // comparing two rotation angles, which its Order test cannot read.
    // 54 -> 55 on 2026-10-22: reverse_tilt_at_address, on m_axisTiltAddress — secondaryAxisTilt read
    // at P1, a curve this face-on fixture already carries. None of the three posture composites is
    // assessable here: the impact and top ones read too little of their weight face-on without
    // trunkRise (which this older fixture lacks), and the setup one requires the down-the-line trunk
    // hinge. All three report compositeCoverage, which is the refusal working.
    // 57 -> 56 on 2026-11-13 (plane stack D5): chicken_wing. It reads the lead elbow's fold after impact
    // (leadArmFlexion, a millisecond window after P7) instead of m_leadArmToTorso, and this fixture was
    // analysed before the face-on elbow existed, so it carries no leadArmFlexion; a re-analysis writes it.
    check(cRich.assessable == 56, "rich_7iron: 56 of 192 conditions assessable (observed)");
    // HOW MANY OF THOSE ANSWERS RESTED ON EVIDENCE THE CAPTURE DID NOT HAVE. A conjunction
    // settled by one known-false term is a real negative, but it is a different kind of "no"
    // from one where every term was read, and it can only ever be a no. Pinned because the
    // engine's own comment expects partial settlement to be "the common case by a distance"
    // and on real swings it is not: 3 of 54 here, 2 of 21 on lm_7iron, 0 of 2 on sparse_noclub.
    // If that starts climbing, the panel is answering more and more from less and less.
    // 1 -> 2 on 2026-10-15: casting now prefers the lead-wrist hinge at P6 (the wrist engine's cast
    // rule, detection `first`) and falls back to the lag angle. This face-on fixture carries no wrist
    // sensor, so the hinge is the term it could not read and the lag angle answers, exactly as it
    // did before — the same shape as flying_elbow, whose down-the-line term this fixture lacks.
    // 2 -> 3, STALE SINCE bcc6ee7e (2026-10-18), not moved by anything here: that commit made
    // excessive_lag read the P6 lead-wrist hinge first (detection `first`), exactly as casting did on
    // 2026-10-15, and did not walk this pin. The hinge is the term this face-on fixture cannot read,
    // and the lag angle answers, so excessive_lag joined casting and sky in the partial list (run with
    // PP_COVERAGE_DUMP=1 to see the three). Found and pinned 2026-10-22.
    check(cRich.assessedPartial == 3,
          "rich_7iron: 3 of its answers rest on a term it could not read (observed)");
    // 38 -> 40 with the two new hipLineTilt measures. Both read a curve this fixture ALREADY
    // carries — the reduction samples the series itself at each segmented phase and does not need
    // the producer to have listed that phase — so a swing written by an older build gains them
    // with no re-analysis. The three plumb-bob measures do NOT resolve here and correctly so: the
    // fixture is a verbatim corpus copy and its metrics array has no plumbBobDistance key.
    //
    // 40 -> 39 on 2026-09-04: `m_pelvisSwayFinish` was one of the live measures this fixture
    // resolved, and it was deleted as an out-of-domain reading. Nothing else moved — the three
    // remaining pelvisSway measures all sit inside P1-P7.
    //
    // 53 -> 52 on 2026-09-12: m_pelvisRotRateP6P7 resolved on this fixture and is no longer live.
    // The three other pelvisRotation measures are LEVELS off the magnitude series and are
    // untouched — it is the derivative the fold destroys, not the reading.
    //
    // 52 -> 50 on 2026-09-14: m_shoulderAlignment and m_handSpeedP6P7 both resolved on this fixture
    // and are no longer live — the first is noProducer, the second is gone. m_headSwayBack is NOT
    // in the delta: its norm's sign was mirrored to match the producer, which changes what the
    // value grades as, not whether it resolves.
    //
    // 50 -> 49 on 2026-09-14: m_attackAngle no longer resolves off the camera series.
    // 49 -> 52 on 2026-10-22: m_headLiftImpact, m_pelvisLiftImpact and m_axisTiltAddress read
    // headLift, pelvisLift and secondaryAxisTilt, which this fixture already carries.
    // 52 -> 61 on 2026-10-22: the posture chart's per-position changes from address on the two
    // face-on series this fixture carries — m_headLift_p2/_p3/_p5/_p6/_p8 and
    // m_pelvisLift_p2/_p3/_p5/_p6. The down-the-line rows have no series here.
    check(cRich.measures   == 63, "rich_7iron: 63 live measures resolved (observed)");
    // 12 → 14 on 2026-08-09: sig_launchLow/sig_launchHigh moved onto m_lmLaunchAngle (the
    // measured key this fixture actually carries), so launch_low and launch_high became
    // assessable on an LM-only capture.
    //
    // 14 → 17 on 2026-08-12: thin, chunk and shank stopped being asserted. All three are now
    // threshold signals on the two face-impact readings this fixture already carried, so nothing
    // new had to be captured for them to become answerable — the numbers were always there and the
    // pack simply had no signal reading them. The one finding that fires on this swing is `thin`:
    // 14.1 mm below centre, past the panel's own 10 mm boundary.
    //
    // NOT 19. `pull_hook` and `push_slice` are measured too now, but they read `m_compoundMiss`,
    // which is DERIVED at write time rather than read off the device — and this fixture is a
    // verbatim corpus copy written by an older build, so its metrics array does not contain the
    // key. That is the honest state of every swing already on disk: they gain the two conditions
    // when the reading is next folded in, not before. Regenerating the fixture to paper over it
    // would hide exactly the thing worth knowing.
    // 17 → 21 and 25 → 26 on 2026-08-12, and the single extra MEASURE is the whole story. It is
    // `m_attackAngle`, which now prefers `lm.attackAngle` and falls back to our projected
    // `attackAngle` (Measure::preferKeys). This fixture is device-only: it has never had a bare
    // `attackAngle` and could not have one, so before the ladder the measure resolved nothing —
    // and `attack_too_steep` and `attack_too_shallow` were therefore unanswerable on a swing whose
    // launch monitor had reported the attack angle outright, -2.73°, sitting in the document read
    // by nothing. Those two conditions plus `top` and `sky` are the four.
    //
    // 21 -> 23 and 26 -> 30 on 2026-10-20, the launch monitor first (Measure::preferKeys on the five
    // bare measures with an lm.* twin). The two conditions are in_to_out_path and out_to_in_path:
    // they read m_clubPathAtImpact, whose own `clubPath` producer is planned, so on a swing whose
    // monitor reported path outright (-0.49°) they were unanswerable — the attack-angle story of
    // 2026-08-12 again. The four measures are m_clubPathAtImpact, m_launchAngle, m_launchDirection
    // and m_clubheadSpeedImpact, each now reading its monitor rung. m_ballSpeed resolves too but is
    // held, so it is not counted, and ball_speed_deficit stays Unavailable: no norm to judge it by.
    // 23 -> 25 on 2026-11-08: fade and draw read the spin axis this fixture reports.
    check(cLm.assessable   == 25, "lm_7iron: 25 of 192 conditions assessable (observed)");
    check(cLm.measures     == 30, "lm_7iron: 30 live measures resolved (observed)");
    check(cSparse.assessable == 2, "sparse_noclub: 2 of 192 conditions assessable (observed)");
    check(cSparse.measures   == 1, "sparse_noclub: 1 live measure resolved (observed)");
    check(cRich.assessable > cSparse.assessable,
          "a richer capture assesses strictly more than a degraded one");

    // ── Known groups: the verdict must match the arithmetic ─────────────────
    //
    // Four conditions whose numbers can be worked out on paper from the fixture and the shipped
    // norm. This is the part that cannot be satisfied by a pipeline that runs cleanly and grades
    // everything wrong.
    std::printf("\nknown groups: the verdict against the arithmetic\n");
    {
        const LiveMeasureSource src(rich, pack);
        const LiveDetection     d = detectForSwing(src, pack, norms);

        // m_impactShaftLean = 12.5°, graded at `iron` (inherited by iron_7): mu 8, sigmaLo 4,
        // sigmaHi 6, so the Ideal band runs 4..14. 12.5 is z = +0.75 — inside Ideal, and NEITHER
        // tail may fire.
        //
        // ⚠ IT USED TO BE 41°, AND THIS BLOCK USED TO ASSERT THE FIRING THAT PRODUCED. 41° of
        // forward shaft lean at impact is not a golf swing; 12.5° is an ordinary 7 iron. The old
        // fixture was analysed by a build whose impact shaft-lean was wrong, so what this test
        // pinned was a fault firing on a broken number — a reading that would have told a golfer
        // to fix something they were already doing correctly. Regenerating the fixture from a
        // current run (2026-09-07) replaced it. The not-firing direction is the one worth having
        // here anyway: over-flagging is the failure this file exists to catch.
        const Finding *hi = d.result.find(QStringLiteral("excessive_shaft_lean"));
        const Finding *lo = d.result.find(QStringLiteral("insufficient_shaft_lean"));
        check(hi && hi->state == FindingState::NotFired,
              "12.5° of shaft lean is inside an iron's Ideal band, so the high tail does NOT fire");
        check(lo && lo->state == FindingState::NotFired,
              "…and neither does the low tail of the same axis");
        check(hi && hi->evidence.hasEvidence && near(hi->evidence.value, 12.5, 1e-6),
              "the NotFired finding still carries the 12.5° it was judged on");
        // 14 -> 18 on 2026-09-14: the iron row's tolerance is the INSTRUMENT's now — the tracker's
        // impact lean overshoots 39 hand-marked frames by a median 12° with an sd of 9.5°, so both
        // sides widened to 10 (mu 8 unchanged). The corridor cannot be narrower than the reading.
        check(hi && hi->evidence.hasCorridor && near(hi->evidence.corridorHi, 18.0),
              "…and the corridor it was tested against");

        // A FIRING that carries its own evidence, which the shaft-lean case used to supply.
        // Asserted as the invariant rather than a pinned corridor: `sig_offBalanceFinish` is
        // outsideCorridor on the HIGH side, so whatever band the norm resolves to, a fired
        // finding must carry a value above its own corridorHi — and that value must be the one
        // the source reports for the measure, not a number assembled somewhere else.
        const Finding *bal = d.result.find(QStringLiteral("off_balance_finish"));
        const std::optional<IMeasureValueSource::Value> com =
            src.value(QStringLiteral("m_comOverLeadFootFinish"));
        check(bal && bal->state == FindingState::Fired,
              "a finish 50.5% of a stance width off the lead foot fires off_balance_finish");
        check(bal && com && bal->evidence.hasEvidence && near(bal->evidence.value, com->value, 1e-9),
              "…carrying the very value the source reports for the measure");
        check(bal && bal->evidence.hasCorridor && bal->evidence.value > bal->evidence.corridorHi,
              "…and a high-tail firing sits above its own corridor");

        // m_ballPosition = 45.49% of stance width, graded at `iron`: mu 33, sigma 10. That is
        // OUTSIDE the Ideal band (23..43) and well inside Good (13..53). THE SIGNAL MUST NOT FIRE.
        // Ideal is |z| <= 1, so a detector that fired here would flag a third of any population on
        // every characteristic in the library — the single failure norm_measure_source_test names
        // first and the one only real data can demonstrate.
        const Finding *back = d.result.find(QStringLiteral("ball_back"));
        check(back && back->state == FindingState::NotFired,
              "a ball position outside Ideal but inside Good does NOT fire ball_back");
        check(back && back->evidence.hasEvidence && back->evidence.z > 1.0 && back->evidence.z < 2.0,
              "…and the NotFired finding still records how far out it sat (1 < z < 2)");

        // m_pelvisSwayBack = -28.36% of stance width against mu -5, sigma 7.5: 3.1 tolerances
        // below, past the Watch edge at -27.5, so Action on the low tail.
        const Finding *sway = d.result.find(QStringLiteral("sway"));
        check(sway && sway->state == FindingState::Fired,
              "28.6% of sway off the ball fires `sway` (3.1 sigma below a -5% norm)");
        check(sway && sway->direction == Direction::Low, "…on the low tail, which is the one authored");
    }
    {
        // m_smashFactor = 1.3885 on a 7 iron, a FLOOR: mu 1.38, so anything at or above the
        // aspiration is Ideal by construction and the deficit signal cannot fire. The arithmetic
        // matters because a floor graded as a target would call this reading a fault for being
        // 0.008 above the mean.
        const LiveMeasureSource src(lm, pack);
        const LiveDetection     d = detectForSwing(src, pack, norms);

        const Finding *smash = d.result.find(QStringLiteral("smash_deficit"));
        check(smash && smash->state == FindingState::NotFired,
              "a smash of 1.389 against an iron floor of 1.38 does NOT fire smash_deficit");
        check(smash && smash->evidence.hasCorridor && smash->evidence.highOpen,
              "…and the evidence reports the open high tail rather than a fabricated upper edge");
    }

    // ── #176: a held plain measure does not grade, on the real stack ───────────────────────
    //
    // The issue's own recreation, on the shipped pack and a real swing: the value is produced
    // either way (LiveMeasureSource answers for it), and only the status decides whether a fault is
    // judged on it. `sway` reads m_pelvisSwayBack alone and fires on rich_7iron (above).
    std::printf("\na held measure does not grade (#176)\n");
    {
        const auto withStatus = [&](const char *id, MeasureStatus st) {
            CharacteristicPack k = pack;
            for (Measure &m : k.measures) if (m.id == QLatin1String(id)) m.status = st;
            return k;
        };
        for (MeasureStatus st : { MeasureStatus::Held, MeasureStatus::Planned }) {
            const CharacteristicPack k = withStatus("m_pelvisSwayBack", st);
            const LiveMeasureSource  src(rich, k);
            const LiveDetection      d = detectForSwing(src, k, norms);
            const Finding *sway = d.result.find(QStringLiteral("sway"));
            check(src.value(QStringLiteral("m_pelvisSwayBack")).has_value(),
                  "the sway reading is still produced on rich_7iron");
            check(sway && sway->state == FindingState::Unavailable && sway->refusal == SignalRefusal::NotLive
                      && sway->missingMeasures.value(0) == QLatin1String("m_pelvisSwayBack")
                      && !sway->evidence.hasEvidence,
                  QByteArray("…but with the measure ").append(qPrintable(measureStatusName(st)))
                      .append(", `sway` is not assessable: NotLive, naming it, no value graded").constData());
        }
        {
            const LiveMeasureSource src(rich, pack);
            const LiveDetection d = detectForSwing(src, pack, norms);
            const Finding *sway = d.result.find(QStringLiteral("sway"));
            check(sway && sway->state == FindingState::Fired,
                  "…and the same swing on the shipped (live) status fires `sway` again");
        }

        // The shipped held measure with a value: m_ballSpeed reads the monitor's ball speed on
        // lm_7iron and is held (no corridor yet). Its signal is refused for being held — the true
        // reason — and the launch-monitor measures that ARE live keep grading on the same swing
        // (smash_deficit above). No measure in the pack is `externalDevice`; the monitor's are live.
        {
            const LiveMeasureSource src(lm, pack);
            const LiveDetection d = detectForSwing(src, pack, norms, {}, {}, QString(),
                                                   { QStringLiteral("sig_ballSpeedDeficit") });
            check(src.value(QStringLiteral("m_ballSpeed")).has_value(),
                  "lm_7iron: held m_ballSpeed has the monitor's value");
            check(d.signalOutcomes.size() == 1 && !d.signalOutcomes[0].available
                      && d.signalOutcomes[0].refusal == SignalRefusal::NotLive,
                  "…and sig_ballSpeedDeficit is refused as NotLive, not graded and not 'no range'");
            const Finding *smash = d.result.find(QStringLiteral("smash_deficit"));
            check(smash && smash->state != FindingState::Unavailable,
                  "…while smash_deficit, on a live launch-monitor measure, is still judged");
            int lmNotLive = 0;
            for (const Measure &m : pack.measures) {
                QStringList keys = m.preferKeys;
                keys << m.metricKey;
                const bool lmKey = std::any_of(keys.cbegin(), keys.cend(),
                                               [](const QString &k) { return k.startsWith(QLatin1String("lm.")); });
                if (lmKey && m.status == MeasureStatus::ExternalDevice) ++lmNotLive;
            }
            check(lmNotLive == 0, "no launch-monitor measure is marked externalDevice (they are live, so they grade)");
        }
    }

    // ── The launch monitor is the primary source for what it measures ─────────────────────
    //
    // Mark, 20 Oct 2026: "LM is always the primary source for the metrics it delivers — our
    // estimates will never be better." The two path conditions read m_clubPathAtImpact, which
    // until then read only our own `clubPath` — a planned producer — so in_to_out_path and
    // out_to_in_path were NEVER judged, even on a session where the monitor measured path on every
    // swing (session_report_design.md §14.6). The measure now prefers `lm.clubPath`
    // (Measure::preferKeys), and the three cases below are the whole contract of that ladder, on
    // the real device-only capture and two copies of it edited to carry the other instrument:
    //
    //   the monitor only    → judged, from the monitor        (lm_7iron as recorded)
    //   both instruments    → judged, from the MONITOR, never ours, even when ours would fire
    //   our estimate only   → still judged, from ours          (the fallback a monitor-less golfer has)
    //
    // The estimate is +6° in both edited copies: past the ±4° fault line, so "it fired" and "it
    // did not" tell the two instruments apart, which a value inside the corridor could not.
    std::printf("\nthe launch monitor first: club path\n");
    {
        const QString lmPathKey = QStringLiteral("lm.clubPath");
        // Copy lm_7iron with its metrics array edited, into its own swing directory.
        auto variant = [&](const char *name, bool keepMonitor, bool addOurs) {
            QFile in(QDir(lm).filePath(QStringLiteral("swing.json")));
            if (!in.open(QIODevice::ReadOnly)) return QString();
            QJsonObject doc      = QJsonDocument::fromJson(in.readAll()).object();
            QJsonObject analysis = doc.value(QStringLiteral("analysis")).toObject();
            QJsonArray  metrics  = analysis.value(QStringLiteral("metrics")).toArray();
            QJsonArray  out;
            QJsonObject monitorRow;
            for (const QJsonValue &v : metrics) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("key")).toString() == lmPathKey) {
                    monitorRow = o;
                    if (!keepMonitor) continue;
                }
                out.append(o);
            }
            if (addOurs && !monitorRow.isEmpty()) {
                // Our estimate in the monitor's own shape — one sample at impact — so the only
                // thing that differs between the two rows is the key and the number.
                QJsonObject ours = monitorRow;
                ours.insert(QStringLiteral("key"), QStringLiteral("clubPath"));
                ours.insert(QStringLiteral("label"), QStringLiteral("Club path"));
                QJsonArray  samples = ours.value(QStringLiteral("phaseSamples")).toArray();
                QJsonObject s0      = samples.at(0).toObject();
                s0.insert(QStringLiteral("value"), 6.0);
                samples.replace(0, s0);
                ours.insert(QStringLiteral("phaseSamples"), samples);
                out.append(ours);
            }
            analysis.insert(QStringLiteral("metrics"), out);
            doc.insert(QStringLiteral("analysis"), analysis);
            const QString dst = QDir(tmp.path()).filePath(QLatin1String("live_swings/") + QLatin1String(name));
            QDir().mkpath(dst);
            QFile f(QDir(dst).filePath(QStringLiteral("swing.json")));
            if (!f.open(QIODevice::WriteOnly)) return QString();
            f.write(QJsonDocument(doc).toJson());
            return dst;
        };
        const QString both = variant("lm_path_both", /*keepMonitor*/ true,  /*addOurs*/ true);
        const QString ours = variant("lm_path_ours", /*keepMonitor*/ false, /*addOurs*/ true);
        check(!both.isEmpty() && !ours.isEmpty(), "the two edited copies of lm_7iron staged");

        struct Case { const char *label; QString dir; double value; FindingState inToOut; };
        const Case cases[] = {
            { "monitor only", lm,   -0.494054, FindingState::NotFired },
            { "both",         both, -0.494054, FindingState::NotFired },
            { "ours only",    ours,  6.0,      FindingState::Fired },
        };
        const auto stateWord = [](const Finding *f) {
            if (!f) return "(none)";
            switch (f->state) {
            case FindingState::Fired:    return "fired";
            case FindingState::NotFired: return "not fired";
            default:                     return "unavailable";
            }
        };
        for (const Case &k : cases) {
            const LiveMeasureSource src(k.dir, pack);
            const std::optional<IMeasureValueSource::Value> v =
                src.value(QStringLiteral("m_clubPathAtImpact"));
            const LiveDetection d   = detectForSwing(src, pack, norms);
            const Finding      *in  = d.result.find(QStringLiteral("in_to_out_path"));
            const Finding      *out = d.result.find(QStringLiteral("out_to_in_path"));
            const bool readOk = v && near(v->value, k.value, 1e-6);
            const bool judged = in && out && in->state == k.inToOut && out->state == FindingState::NotFired
                                && in->evidence.hasEvidence && near(in->evidence.value, k.value, 1e-6);
            std::printf("  [%s] %-12s m_clubPathAtImpact = %s, in_to_out_path %s, out_to_in_path %s\n",
                        readOk && judged ? "PASS" : "FAIL", k.label,
                        v ? qPrintable(QString::number(v->value)) : "(none)",
                        stateWord(in), stateWord(out));
            if (!(readOk && judged)) ++g_fail;
        }
    }

    // ── The route a reading came from (trunk_imu_sequence_grf_design.md §10.7, E3) ──────────
    //
    // The ratio test refuses a quotient across two scales, and it can only do that if the route a
    // KS producer stamped on its series survives every hop to the engine: swing.json → grid →
    // sidecar → grid reading → value → measure reading. A hop that dropped it would read as "no
    // route on either half", which the rule PERMITS — so a lost route fails open, silently. Each
    // hop is pinned here on a synthetic swing, since no corpus fixture carries a route yet.
    std::printf("\nroutes carried through\n");
    {
        auto phaseAt = [](Phase ph, qint64 tUs) {
            QJsonObject o;
            o.insert(QStringLiteral("phase"), int(ph));
            o.insert(QStringLiteral("t_us"),  tUs);
            return o;
        };
        // A point-in-time metric, read at impact, as the KS peak speeds are emitted.
        auto atImpact = [](const char *key, double value, const char *route, bool direct) {
            QJsonObject ps;
            ps.insert(QStringLiteral("phase"), int(Phase::Impact));
            ps.insert(QStringLiteral("t_us"),  1'100'000);
            ps.insert(QStringLiteral("value"), value);
            QJsonObject o;
            o.insert(QStringLiteral("key"),          QString::fromLatin1(key));
            o.insert(QStringLiteral("unit"),         QStringLiteral("°/s"));
            o.insert(QStringLiteral("t_us"),         QJsonArray{});
            o.insert(QStringLiteral("value"),        QJsonArray{});
            o.insert(QStringLiteral("phaseSamples"), QJsonArray{ ps });
            if (route[0] != '\0') o.insert(QStringLiteral("routeId"), QString::fromLatin1(route));
            if (direct)            o.insert(QStringLiteral("routeDirect"), true);
            return o;
        };

        QJsonObject an;
        an.insert(QStringLiteral("phases"), QJsonArray{ phaseAt(Phase::Address, 100'000),
                                                        phaseAt(Phase::Top, 400'000),
                                                        phaseAt(Phase::Impact, 1'100'000) });
        an.insert(QStringLiteral("metrics"),
                  QJsonArray{ atImpact("pelvisPeakSpeed", 480.0, "pelvisImu", true),
                              atImpact("thoraxPeakSpeed", 700.0, "thoraxImu", true),
                              atImpact("camThoraxPeakSpeed", 690.0, "faceOn+dtl", false),
                              atImpact("plainSpeed", 500.0, "", false) });

        CharacteristicPack rp;
        auto measure = [&](const char *id, const char *key) {
            Measure m;
            m.id             = QString::fromLatin1(id);
            m.kind           = MeasureKind::Provided;
            m.metricKey      = QString::fromLatin1(key);
            m.unit           = QStringLiteral("°/s");
            m.status         = MeasureStatus::Live;
            m.reducer.kind   = ReducerKind::At;
            m.reducer.anchor = Phase::Impact;
            rp.measures.push_back(m);
        };
        measure("m_pelvis",    "pelvisPeakSpeed");
        measure("m_thorax",    "thoraxPeakSpeed");
        measure("m_camThorax", "camThoraxPeakSpeed");
        measure("m_plain",     "plainSpeed");

        auto ratio = [&](const char *cid, const char *num, const char *den) {
            Signal s;
            s.id        = QStringLiteral("sig_") + QString::fromLatin1(cid);
            s.test      = SignalTest::Ratio;
            s.measures  = { QString::fromLatin1(num), QString::fromLatin1(den) };
            s.direction = Direction::Low;
            s.threshold = 1.22;
            rp.signalDefs.push_back(s);
            Condition c;
            c.id         = QString::fromLatin1(cid);
            c.detectedBy = { s.id };
            c.state      = ConditionState::Active;
            rp.conditions.push_back(c);
        };
        ratio("c_direct",  "m_thorax",    "m_pelvis");   // two Direct rungs: assessed
        ratio("c_mixed",   "m_camThorax", "m_pelvis");   // camera over IMU: two scales

        const SwingPhaseGrid grid = buildPhaseGrid(an);
        const MetricPhaseGrid *gp = grid.metric(QStringLiteral("pelvisPeakSpeed"));
        check(gp && gp->routeId == QStringLiteral("pelvisImu") && gp->routeDirect,
              "the grid reads routeId / routeDirect off the swing.json metric");
        const MetricPhaseGrid *gq = grid.metric(QStringLiteral("plainSpeed"));
        check(gq && gq->routeId.isEmpty() && !gq->routeDirect,
              "…and a metric that states no route has none");

        // The sidecar is a cache of the grid: a route it dropped would fail open on every reload.
        bool guardOk = false;
        const SwingPhaseGrid reloaded =
            loadPhaseGrid(savePhaseGrid(grid, 1234, 5678), 1234, 5678, &guardOk, grid.config);
        const MetricPhaseGrid *rc = reloaded.metric(QStringLiteral("camThoraxPeakSpeed"));
        check(guardOk && rc && rc->routeId == QStringLiteral("faceOn+dtl") && !rc->routeDirect,
              "the sidecar round-trips the route");

        const LiveMeasureSource src(reloaded, rp);
        const auto vp = src.value(QStringLiteral("m_pelvis"));
        const auto vc = src.value(QStringLiteral("m_camThorax"));
        const auto vn = src.value(QStringLiteral("m_plain"));
        check(vp && vp->route == QStringLiteral("pelvisImu") && vp->direct,
              "the value carries a Direct rung's route");
        check(vc && vc->route == QStringLiteral("faceOn+dtl") && !vc->direct,
              "…and an estimated one's");
        check(vn && vn->route.isEmpty() && !vn->direct, "…and none where none was stated");

        LiveMeasureSource noSigma(reloaded, rp);
        noSigma.setWithSigma(false);
        const auto vs = noSigma.value(QStringLiteral("m_pelvis"));
        check(vs && vs->route == QStringLiteral("pelvisImu") && vs->direct,
              "the route is provenance, not σ: carried with the σ switch off");

        const NormMeasureSource graded(src, nullptr);
        const auto mr = graded.read(QStringLiteral("m_thorax"));
        check(mr && mr->route == QStringLiteral("thoraxImu") && mr->direct,
              "the measure reading the engine sees carries the route");

        const DetectionResult d = detect(rp, graded);
        check(d.find(QStringLiteral("c_direct"))
                  && d.find(QStringLiteral("c_direct"))->state == FindingState::NotFired,
              "thorax/pelvis off two Direct rungs is assessed (700/480 = 1.46, above 1.22)");
        check(d.find(QStringLiteral("c_mixed"))
                  && d.find(QStringLiteral("c_mixed"))->state == FindingState::Unavailable,
              "a camera thorax over an IMU pelvis is Unavailable: two scales");
    }

    // ── The takeaway against the set-up (plane_stack_design.md §3.1, D1) ───────────────────
    //
    // m_takeawayPlane is the swing plane at P2 LESS the plane at P1, on the shipped pack and the
    // shipped norms. No corpus fixture carries swingPlaneCurve yet (rich_7iron predates it), so the
    // curve is synthetic: flat plateaus about each phase instant, so the ±15 ms windowed median at
    // each is the plateau's value and the expected numbers are plain arithmetic: 58° at address, the
    // case's value at shaft-parallel back, 66° at lead-arm parallel (a climb of 8° by P3).
    std::printf("\nthe takeaway against the set-up (m_takeawayPlane)\n");
    {
        auto phaseAt = [](Phase ph, qint64 tUs) {
            QJsonObject o;
            o.insert(QStringLiteral("phase"), int(ph));
            o.insert(QStringLiteral("t_us"),  tUs);
            return o;
        };
        // (t ms, value) knots; plateaus of ±40 ms about P1 (100 ms), P2 (300 ms) and P3 (450 ms).
        auto curve = [](double atP1, double atP2, double atP3, bool maskP2) {
            const double knots[][2] = { { 0, atP1 },   { 140, atP1 }, { 260, atP2 }, { 340, atP2 },
                                        { 410, atP3 }, { 490, atP3 }, { 600, atP3 + 4.0 } };
            QJsonArray t, v, valid;
            for (int ms = 0; ms <= 600; ms += 5) {
                double y = knots[0][1];
                for (std::size_t k = 1; k < std::size(knots); ++k)
                    if (ms <= knots[k][0]) {
                        const double f = (ms - knots[k - 1][0]) / (knots[k][0] - knots[k - 1][0]);
                        y = knots[k - 1][1] + f * (knots[k][1] - knots[k - 1][1]);
                        break;
                    }
                t.append(qint64(ms) * 1000);
                v.append(y);
                valid.append((maskP2 && ms >= 200 && ms <= 400) ? 0 : 1);   // bridged round P2
            }
            QJsonObject o;
            o.insert(QStringLiteral("key"),   QStringLiteral("swingPlaneCurve"));
            o.insert(QStringLiteral("unit"),  QStringLiteral("°"));
            o.insert(QStringLiteral("t_us"),  t);
            o.insert(QStringLiteral("value"), v);
            if (maskP2) o.insert(QStringLiteral("valid"), valid);
            return o;
        };
        auto analysis = [&](double atP1, double atP2, double atP3, bool maskP2 = false) {
            QJsonObject an;
            an.insert(QStringLiteral("phases"),
                      QJsonArray{ phaseAt(Phase::Address, 100'000), phaseAt(Phase::ShaftParallelBack, 300'000),
                                  phaseAt(Phase::MidBackswing, 450'000), phaseAt(Phase::Top, 560'000) });
            an.insert(QStringLiteral("metrics"), QJsonArray{ curve(atP1, atP2, atP3, maskP2) });
            return an;
        };
        const auto stateOf = [](const LiveDetection &d, const char *id) {
            const Finding *f = d.result.find(QLatin1String(id));
            return f ? f->state : FindingState::Unavailable;
        };

        const Measure *tm = pack.measure(QStringLiteral("m_takeawayPlane"));
        check(tm && tm->status == MeasureStatus::Live && tm->metricKey == QLatin1String("swingPlaneCurve")
                  && tm->reducer.kind == ReducerKind::Delta && tm->reducer.anchor == Phase::Address,
              "m_takeawayPlane ships live: swingPlaneCurve, delta anchored at P1");

        // THE FIRING EDGES ARE −2 / +3 (Mark, 13 Nov 2026 evening; plane stack D6): sigmaLo 1, sigmaHi 1.5, so
        // under the shipped `standard` policy (a fault fires past the Good band, 2 tolerances out) flat fires
        // below −2 and steep above +3, and the amber corridor the finding is shown against, the norm's claim
        // mu − sigmaLo … mu + sigmaHi, is −1 … +1.5. (Until D6: corridor −2 … +3, firing below −4 / above +6.)
        // Flat: 4.5° under the address plane.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(58.0, 53.5, 66.0)), pack);
            checkValue(src, "m_takeawayPlane",       -4.5);   // P2 − P1
            checkValue(src, "m_shaftPlaneBackswing",  8.0);   // P3 − P1, the climb, unchanged
            checkValue(src, "m_backswingPlaneAngle", 66.0);   // P3 itself: shown, grades nothing
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_takeaway") == FindingState::Fired,
                  "−4.5° under the address plane fires flat_takeaway (past −2)");
            check(stateOf(d, "steep_takeaway") == FindingState::NotFired, "…and not steep_takeaway");
            const Finding *f = d.result.find(QStringLiteral("flat_takeaway"));
            check(f && f->evidence.hasCorridor && near(f->evidence.corridorLo, -1.0) && near(f->evidence.corridorHi, 1.5),
                  "…shown against the asymmetric amber corridor −1 … +1.5 (sigmaLo 1, sigmaHi 1.5)");
            check(!d.result.find(QStringLiteral("backswing_plane_too_flat"))
                      && !d.result.find(QStringLiteral("backswing_plane_too_upright")),
                  "the retired absolute pair is no longer asked");
        }
        // −2.5°, just past the flat edge (Mark's library median is −3.1°, so it now fires on him).
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(58.0, 55.5, 66.0)), pack);
            checkValue(src, "m_takeawayPlane", -2.5);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_takeaway") == FindingState::Fired
                      && stateOf(d, "steep_takeaway") == FindingState::NotFired,
                  "−2.5° is past the −2 firing edge: flat_takeaway fires (it did not before D6)");
        }
        // −1.5°: in the amber band (−2 … −1), so not a fault.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(58.0, 56.5, 66.0)), pack);
            checkValue(src, "m_takeawayPlane", -1.5);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_takeaway") == FindingState::NotFired
                      && stateOf(d, "steep_takeaway") == FindingState::NotFired,
                  "−1.5° is amber, inside the −2 firing edge: neither fires");
        }
        // Just above: +2.5° is amber on the steep side (+1.5 … +3), which reaches further.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(58.0, 60.5, 66.0)), pack);
            checkValue(src, "m_takeawayPlane", 2.5);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_takeaway") == FindingState::NotFired
                      && stateOf(d, "steep_takeaway") == FindingState::NotFired,
                  "+2.5° (just above the plane it was set on) is inside the +3 firing edge: neither fires");
        }
        // Steep: +3.5° above, past the +3 edge.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(58.0, 61.5, 66.0)), pack);
            checkValue(src, "m_takeawayPlane", 3.5);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "steep_takeaway") == FindingState::Fired
                      && stateOf(d, "flat_takeaway") == FindingState::NotFired,
                  "+3.5° above the address plane fires steep_takeaway only (past +3)");
        }
        // The curve bridged round P2 (Chris's masked club track): no takeaway reading, the climb
        // still reads, and both takeaway faults are not assessable rather than graded on interpolation.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(58.0, 55.0, 66.0, true)), pack);
            check(!src.value(QStringLiteral("m_takeawayPlane")).has_value(),
                  "a curve bridged round P2 gives no takeaway reading");
            checkValue(src, "m_shaftPlaneBackswing", 8.0);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_takeaway") == FindingState::Unavailable
                      && stateOf(d, "steep_takeaway") == FindingState::Unavailable,
                  "…so both takeaway faults are not assessable");
        }
    }

    // ── The arm lift at the top and bent_lead_arm's 3-D elbow (plane stack D2) ──────────────────────
    //
    // armLiftTop is ONE reading at the top (a phase sample, no curve), as both producers write it; the
    // shipped norm is mu 5, sigmaLo 8, sigmaHi 7: corridor −3 … +12, firing below −11 (arms_swing_around)
    // and above +19 (arms_lift). leadArmFlexion3d is read at the top; its norm is a ceiling at 15,
    // tolerance 8: amber from 23, firing above 31.
    std::printf("\nthe arm lift at the top (m_armLiftTop) and bent_lead_arm's 3-D elbow (D2)\n");
    {
        const auto phaseAt = [](Phase ph, qint64 tUs) {
            return QJsonObject{ { QStringLiteral("phase"), int(ph) }, { QStringLiteral("t_us"), tUs } };
        };
        const auto atTop = [](const char *key, double value, const char *route) {
            QJsonObject ps{ { QStringLiteral("phase"), int(Phase::Top) }, { QStringLiteral("t_us"), 560'000 },
                            { QStringLiteral("value"), value } };
            QJsonObject o;
            o.insert(QStringLiteral("key"),          QString::fromLatin1(key));
            o.insert(QStringLiteral("unit"),         QStringLiteral("°"));
            o.insert(QStringLiteral("t_us"),         QJsonArray{});
            o.insert(QStringLiteral("value"),        QJsonArray{});
            o.insert(QStringLiteral("phaseSamples"), QJsonArray{ ps });
            o.insert(QStringLiteral("routeId"),      QString::fromLatin1(route));
            return o;
        };
        const auto analysis = [&](const QJsonArray &metrics) {
            QJsonObject an;
            an.insert(QStringLiteral("phases"), QJsonArray{ phaseAt(Phase::Address, 100'000), phaseAt(Phase::Top, 560'000),
                                                            phaseAt(Phase::Impact, 900'000) });
            an.insert(QStringLiteral("metrics"), metrics);
            return an;
        };
        const auto stateOf = [](const LiveDetection &d, const char *id) {
            const Finding *f = d.result.find(QLatin1String(id));
            return f ? f->state : FindingState::Unavailable;
        };
        const Measure *am = pack.measure(QStringLiteral("m_armLiftTop"));
        check(am && am->status == MeasureStatus::Live && am->metricKey == QLatin1String("armLiftTop")
                  && am->reducer.kind == ReducerKind::At && am->reducer.anchor == Phase::Top
                  && am->golferName.contains(QStringLiteral("measured to the elbow")),
              "m_armLiftTop ships live: armLiftTop at P4, its golfer name says it is measured to the elbow");
        struct Case { double v; FindingState around, lift; const char *what; };
        const Case cases[] = {
            { -12.0, FindingState::Fired,    FindingState::NotFired, "−12° (under the shoulder line) fires arms_swing_around (past −11)" },
            {  -5.0, FindingState::NotFired, FindingState::NotFired, "−5° is outside the corridor, inside the Good band: neither fires" },
            {   6.0, FindingState::NotFired, FindingState::NotFired, "+6° (the one-plane-to-two-plane middle) is clean" },
            {  17.0, FindingState::NotFired, FindingState::NotFired, "+17° (a two-plane arm) is outside the corridor, inside the Good band: neither fires" },
            {  20.0, FindingState::NotFired, FindingState::Fired,    "+20° fires arms_lift (past +19)" },
        };
        for (const Case &c : cases) {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{ atTop("armLiftTop", c.v, "dtl") })), pack);
            checkValue(src, "m_armLiftTop", c.v);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "arms_swing_around") == c.around && stateOf(d, "arms_lift") == c.lift, c.what);
        }
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{ atTop("armLiftTop", 20.0, "dtl") })), pack);
            const LiveDetection d = detectForSwing(src, pack, norms);
            const Finding *f = d.result.find(QStringLiteral("arms_lift"));
            check(f && f->evidence.hasCorridor && near(f->evidence.corridorLo, -3.0) && near(f->evidence.corridorHi, 12.0),
                  "…shown against the corridor −3 … +12 (mu 5, sigmaLo 8, sigmaHi 7)");
        }
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{})), pack);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "arms_swing_around") == FindingState::Unavailable && stateOf(d, "arms_lift") == FindingState::Unavailable,
                  "no arm lift ⇒ both arm faults are not assessable");
        }
        // bent_lead_arm: its 3-D signal reads leadElbowBendChange3d (D6); m_leadElbowFlex_p4 stays held (D5), so on a
        // camera swing the 3-D elbow is its one signal.
        const Measure *bm = pack.measure(QStringLiteral("m_leadElbowBendTop3d"));
        const Measure *p4 = pack.measure(QStringLiteral("m_leadElbowFlex_p4"));
        check(bm && bm->status == MeasureStatus::Live && p4 && p4->status == MeasureStatus::Held,
              "m_leadElbowBendTop3d ships live; m_leadElbowFlex_p4 stays held");
        // A CHANGE FROM THE ADDRESS HOLD (D6, Mark 13 Nov 2026 evening): leadElbowBendChange3d, ONE reading at the
        // top that the 3-D body stage derives (the measured top bend less the hold's median), mu 0, tolerance 8:
        // amber from +8, firing above +16.
        check(bm && bm->metricKey == QLatin1String("leadElbowBendChange3d") && bm->reducer.kind == ReducerKind::At
                  && bm->reducer.anchor == Phase::Top && bm->shape == Shape::Ceiling,
              "m_leadElbowBendTop3d reads leadElbowBendChange3d at P4, a ceiling");
        struct Bend { double change; FindingState s; const char *what; };
        const Bend bends[] = {
            { 18.0, FindingState::Fired,    "+18° more bend at the top than in the hold fires bent_lead_arm (past +16)" },
            { 12.7, FindingState::NotFired, "+12.7° (Mark's median) is amber (past +8), not a fault" },
            {  2.0, FindingState::NotFired, "+2° (an arm as straight as at address) is clean" },
            { -3.0, FindingState::NotFired, "−3° (straighter than at address) is clean: a ceiling" },
        };
        for (const Bend &b : bends) {
            QJsonArray ms{ atTop("leadElbowBendChange3d", b.change, "faceOn+dtl") };
            // A face-on leadArmFlexion beside it (bent 40° more at the top than at address, D5's projection):
            // held, it grades nothing.
            QJsonObject fo = atTop("leadArmFlexion", 50.0, "faceOn");
            QJsonArray fps = fo.value(QStringLiteral("phaseSamples")).toArray();
            fps.append(QJsonObject{ { QStringLiteral("phase"), int(Phase::Address) }, { QStringLiteral("t_us"), 100'000 },
                                    { QStringLiteral("value"), 10.0 } });
            fo.insert(QStringLiteral("phaseSamples"), fps);
            ms.append(fo);
            const LiveMeasureSource src(buildPhaseGrid(analysis(ms)), pack);
            checkValue(src, "m_leadElbowBendTop3d", b.change);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "bent_lead_arm") == b.s, b.what);
        }
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{ atTop("leadElbowBendChange3d", 18.0, "faceOn+dtl") })), pack);
            const LiveDetection d = detectForSwing(src, pack, norms);
            const Finding *f = d.result.find(QStringLiteral("bent_lead_arm"));
            check(f && f->evidence.hasCorridor && near(f->evidence.corridorHi, 8.0),
                  "…shown against the ceiling's amber edge, +8° from the address arm");
        }
        // No measured elbow at one end: the stage writes no reading and the Refused body3d.leadElbowUnmeasured,
        // which is the reason bent_lead_arm's measure gives (D6 round 2) — not "not segmented".
        for (const char *code : { "top", "address", "both" }) {
            QJsonObject an = analysis(QJsonArray{ atTop("leadArmFlexion3d", 30.0, "faceOn+dtl") });
            an.insert(QStringLiteral("gatesVersion"), kGatesVersion);
            an.insert(QStringLiteral("gates"), gateRecordsToJson({ GateRecord{
                QStringLiteral("body3d.leadElbowUnmeasured"), QStringLiteral("leadElbowBendChange3d"), GateEffect::Refused,
                QVariantMap{ { QStringLiteral("code"), QString::fromLatin1(code) } }, {}, {},
                { QStringLiteral("leadElbowBendChange3d") } } }));
            const LiveMeasureSource src(buildPhaseGrid(an), pack);
            check(!src.value(QStringLiteral("m_leadElbowBendTop3d")).has_value(),
                  qPrintable(QStringLiteral("%1: no change reading").arg(QLatin1String(code))));
            const LiveMeasureSource::GateCause gc = src.gateCause(QStringLiteral("m_leadElbowBendTop3d"));
            check(gc.gate == QLatin1String("body3d.leadElbowUnmeasured") && gc.clause.endsWith(QStringLiteral("lead elbow not seen by both cameras")),
                  qPrintable(QStringLiteral("%1: …and bent_lead_arm's measure names body3d.leadElbowUnmeasured (\"%2\")")
                                 .arg(QLatin1String(code), gc.clause)));
            check(stateOf(detectForSwing(src, pack, norms), "bent_lead_arm") == FindingState::Unavailable,
                  qPrintable(QStringLiteral("%1: …so bent_lead_arm is not assessable").arg(QLatin1String(code))));
        }
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{})), pack);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "bent_lead_arm") == FindingState::Unavailable,
                  "no measured elbow (and the face-on route held) ⇒ bent_lead_arm is not assessable");
        }
    }

    // ── The club's climb placed in the on-plane window halfway back (plane stack D3) ─────────────────────────────────────────
    //
    // clubSlotTop is ONE reading at P3 (a phase sample, no curve), as club_slot_top.h writes it: 0 on the plane
    // the club was set on at address, 1 on the shoulder plane at address. The shipped norm is mu 0.5, sigma
    // 0.25: corridor 0.25 … 0.75 (amber outside), firing below 0 (flat_backswing_plane, club under plane halfway back)
    // and above 1 (steep_backswing_plane, club above plane halfway back). The climb in degrees, m_shaftPlaneBackswing,
    // stays live and shown, and grades neither.
    std::printf("\nthe club in the on-plane window at P3 (m_clubSlotTop, D3)\n");
    {
        const auto phaseAt = [](Phase ph, qint64 tUs) {
            return QJsonObject{ { QStringLiteral("phase"), int(ph) }, { QStringLiteral("t_us"), tUs } };
        };
        const auto slotAt = [](double value) {
            QJsonObject ps{ { QStringLiteral("phase"), int(Phase::MidBackswing) }, { QStringLiteral("t_us"), 450'000 },
                            { QStringLiteral("value"), value } };
            QJsonObject o;
            o.insert(QStringLiteral("key"),          QStringLiteral("clubSlotTop"));
            o.insert(QStringLiteral("unit"),         QStringLiteral("ratio"));
            o.insert(QStringLiteral("t_us"),         QJsonArray{});
            o.insert(QStringLiteral("value"),        QJsonArray{});
            o.insert(QStringLiteral("phaseSamples"), QJsonArray{ ps });
            o.insert(QStringLiteral("routeId"),      QStringLiteral("dtl"));
            return o;
        };
        // A swing plane climbing 20° from address to P3: in degrees a big climb, but no slot reading beside it.
        const auto climb = [] {
            QJsonArray t, v;
            for (int ms = 0; ms <= 600; ms += 5) {
                t.append(qint64(ms) * 1000);
                v.append(ms <= 140 ? 58.0 : ms >= 410 ? 78.0 : 58.0 + 20.0 * (ms - 140) / 270.0);
            }
            return QJsonObject{ { QStringLiteral("key"), QStringLiteral("swingPlaneCurve") }, { QStringLiteral("unit"), QStringLiteral("°") },
                                { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v } };
        };
        const auto analysis = [&](const QJsonArray &metrics) {
            QJsonObject an;
            an.insert(QStringLiteral("phases"), QJsonArray{ phaseAt(Phase::Address, 100'000), phaseAt(Phase::MidBackswing, 450'000),
                                                            phaseAt(Phase::Top, 560'000) });
            an.insert(QStringLiteral("metrics"), metrics);
            return an;
        };
        const auto stateOf = [](const LiveDetection &d, const char *id) {
            const Finding *f = d.result.find(QLatin1String(id));
            return f ? f->state : FindingState::Unavailable;
        };
        const Measure *sm = pack.measure(QStringLiteral("m_clubSlotTop"));
        check(sm && sm->status == MeasureStatus::Live && sm->metricKey == QLatin1String("clubSlotTop")
                  && sm->reducer.kind == ReducerKind::At && sm->reducer.anchor == Phase::MidBackswing,
              "m_clubSlotTop ships live: clubSlotTop at P3");
        const Signal *sf = pack.signal(QStringLiteral("sig_flatBackswingPlane"));
        const Signal *ss = pack.signal(QStringLiteral("sig_steepBackswingPlane"));
        check(sf && ss && sf->measures == QStringList{ QStringLiteral("m_clubSlotTop") } && sf->direction == Direction::Low
                  && ss->measures == QStringList{ QStringLiteral("m_clubSlotTop") } && ss->direction == Direction::High,
              "the pair's signals read m_clubSlotTop, low and high");
        const Condition *cf = pack.condition(QStringLiteral("flat_backswing_plane"));
        const Condition *cu = pack.condition(QStringLiteral("steep_backswing_plane"));
        check(cf && cu && cf->label == QStringLiteral("Club under plane halfway back") && cu->label == QStringLiteral("Club above plane halfway back")
                  && cf->golfer == QStringLiteral("halfway back, the club sits below the plane you set at address")
                  && cu->golfer == QStringLiteral("halfway back, the club climbs above the line through your trail shoulder"),
              "…relabelled: Club under / above plane halfway back, with the golfer lines");
        struct Case { double v; FindingState under, above; const char *what; };
        const Case cases[] = {
            { -0.2, FindingState::Fired,    FindingState::NotFired, "−0.2 (below the address plane) fires flat_backswing_plane (under plane halfway back)" },
            {  0.1, FindingState::NotFired, FindingState::NotFired, "0.1 (amber, low in the window) fires neither" },
            {  0.5, FindingState::NotFired, FindingState::NotFired, "0.5 (the middle of the window) is clean" },
            {  0.9, FindingState::NotFired, FindingState::NotFired, "0.9 (amber, high in the window) fires neither" },
            {  1.2, FindingState::NotFired, FindingState::Fired,    "1.2 (above the shoulder plane) fires steep_backswing_plane (above plane halfway back)" },
        };
        for (const Case &c : cases) {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{ slotAt(c.v) })), pack);
            checkValue(src, "m_clubSlotTop", c.v);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_backswing_plane") == c.under && stateOf(d, "steep_backswing_plane") == c.above, c.what);
        }
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{ slotAt(1.2) })), pack);
            const LiveDetection d = detectForSwing(src, pack, norms);
            const Finding *f = d.result.find(QStringLiteral("steep_backswing_plane"));
            check(f && f->evidence.hasCorridor && near(f->evidence.corridorLo, 0.25) && near(f->evidence.corridorHi, 0.75),
                  "…shown against the corridor 0.25 … 0.75 (mu 0.5, sigma 0.25)");
        }
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(QJsonArray{ climb() })), pack);
            checkValue(src, "m_shaftPlaneBackswing", 20.0);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "flat_backswing_plane") == FindingState::Unavailable
                      && stateOf(d, "steep_backswing_plane") == FindingState::Unavailable,
                  "a 20° climb with no window reading: m_shaftPlaneBackswing still reads (shown), and the pair is not assessable");
        }
    }

    // ── chicken_wing reads the lead elbow's fold in the 100 ms after impact (plane stack D5) ─────
    //
    // A face-on leadArmFlexion at 15° to impact (500 ms), then rising by `fold` over 60 ms and held.
    // The fold measure is the windowed-mean peak over [P7, P7 + 100 ms] less the ±15 ms median at P7,
    // so on this curve it is exactly `fold`. The norm: ceiling, mu 0, tolerance 5, so it fires past
    // +10 (two tolerances) and is implausible past +90.
    {
        std::printf("\n── chicken_wing (D5) ──\n");
        const auto phaseAt = [](Phase p, qint64 us) {
            return QJsonObject{ { QStringLiteral("phase"), int(p) }, { QStringLiteral("t_us"), us }, { QStringLiteral("conf"), 0.9 } };
        };
        const auto analysis = [&](double atImpact, double fold) {
            QJsonArray t, v;
            for (int ms = 0; ms <= 1000; ++ms) {
                const double y = ms <= 500 ? atImpact : atImpact + fold * std::min(1.0, (ms - 500) / 60.0);
                t.append(qint64(ms) * 1000);
                v.append(y);
            }
            const QJsonObject elbow{ { QStringLiteral("key"), QStringLiteral("leadArmFlexion") }, { QStringLiteral("unit"), QStringLiteral("°") },
                                     { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v },
                                     { QStringLiteral("routeId"), QStringLiteral("faceOn") } };
            return QJsonObject{ { QStringLiteral("phases"), QJsonArray{ phaseAt(Phase::Address, 100'000), phaseAt(Phase::Top, 300'000),
                                                                        phaseAt(Phase::Impact, 500'000), phaseAt(Phase::Finish, 900'000) } },
                                { QStringLiteral("metrics"), QJsonArray{ elbow } } };
        };
        const PhaseGridConfig cfg = withMsWindows({}, msWindowsOf(pack.measures));
        const auto stateOf = [](const LiveDetection &d, const char *id) {
            const Finding *f = d.result.find(QLatin1String(id));
            return f ? f->state : FindingState::Unavailable;
        };
        const Measure *fm = pack.measure(QStringLiteral("m_leadElbowFoldAfterImpact"));
        check(fm && fm->status == MeasureStatus::Live && fm->metricKey == QLatin1String("leadArmFlexion")
                  && fm->reducer.kind == ReducerKind::Extremum && fm->reducer.anchor == Phase::Impact
                  && fm->reducer.windowMs == std::pair<int, int>{ 0, 100 } && fm->shape == Shape::Ceiling,
              "m_leadElbowFoldAfterImpact ships live: leadArmFlexion, max over P7 + 0…100 ms from P7, ceiling");
        const Signal *sg = pack.signal(QStringLiteral("sig_chickenWing"));
        check(sg && sg->measures == QStringList{ QStringLiteral("m_leadElbowFoldAfterImpact") } && sg->direction == Direction::High,
              "sig_chickenWing reads that one measure, high");
        bool allHeld = true;
        for (int p = 1; p <= 8; ++p) {
            const Measure *e = pack.measure(QStringLiteral("m_leadElbowFlex_p%1").arg(p));
            allHeld = allHeld && e && e->status == MeasureStatus::Held && !e->gapReason.isEmpty();
        }
        check(allHeld, "m_leadElbowFlex_p1…p8 are held, each with its reason");
        const Measure *lat = pack.measure(QStringLiteral("m_leadArmToTorso"));
        check(lat && lat->status == MeasureStatus::Live, "m_leadArmToTorso stays live (shown)");

        // A fold of 14°: past the firing edge.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(15.0, 14.0), cfg), pack);
            checkValue(src, "m_leadElbowFoldAfterImpact", 14.0);
            const LiveDetection d = detectForSwing(src, pack, norms);
            check(stateOf(d, "chicken_wing") == FindingState::Fired, "a 14° fold after impact fires chicken_wing (past +10)");
            check(stateOf(d, "bent_lead_arm") != FindingState::Fired && stateOf(d, "locked_lead_arm") != FindingState::Fired,
                  "…and the held elbow measures grade neither bent_lead_arm nor locked_lead_arm");
        }
        // A fold of 7°: amber, not a fault.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(15.0, 7.0), cfg), pack);
            checkValue(src, "m_leadElbowFoldAfterImpact", 7.0);
            check(stateOf(detectForSwing(src, pack, norms), "chicken_wing") == FindingState::NotFired,
                  "a 7° fold (inside two tolerances) does not fire");
        }
        // A straight arm through the ball: clean.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(3.0, 0.0), cfg), pack);
            checkValue(src, "m_leadElbowFoldAfterImpact", 0.0);
            check(stateOf(detectForSwing(src, pack, norms), "chicken_wing") == FindingState::NotFired,
                  "a straight lead arm through impact is clean");
        }
        // A wrist keypoint swept across the elbow (+120°): not a swing, not judged.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(15.0, 120.0), cfg), pack);
            check(stateOf(detectForSwing(src, pack, norms), "chicken_wing") == FindingState::Unavailable,
                  "a 120° fold is implausible (past 90): not assessable rather than fired");
        }
        // THE WINDOW RULE (D5 round 3): the fold rises to +14° by +60 ms, but the frames from +40 to +100 ms
        // are masked (the lead arm hidden). The span is not covered — 60 ms unread against a 30 ms limit —
        // so the fold is NOT ASSESSABLE, with its own reason, never the +7° the remaining frames would say.
        {
            const auto masked = [&](int fromMs, int toMs) {
                QJsonObject an = analysis(15.0, 14.0);
                QJsonArray ms = an.value(QStringLiteral("metrics")).toArray();
                QJsonObject elbow = ms.at(0).toObject();
                QJsonArray valid;
                for (int t = 0; t <= 1000; ++t) valid.append((t >= 500 + fromMs && t <= 500 + toMs) ? 0 : 1);
                elbow.insert(QStringLiteral("valid"), valid);
                ms.replace(0, elbow);
                an.insert(QStringLiteral("metrics"), ms);
                return an;
            };
            const LiveMeasureSource src(buildPhaseGrid(masked(40, 100), cfg), pack);
            check(!src.value(QStringLiteral("m_leadElbowFoldAfterImpact")).has_value(),
                  "a fold whose last 60 ms are hidden is not assessable (not read from what is left)");
            check(src.missingKind(QStringLiteral("m_leadElbowFoldAfterImpact")) == MissingKind::WindowNotCovered,
                  "…with its own reason: too little of the moment could be seen (not an unsegmented phase)");
            check(stateOf(detectForSwing(src, pack, norms), "chicken_wing") == FindingState::Unavailable,
                  "…so chicken_wing is not assessable");
            // 25 ms hidden (+50 … +74) leaves the rest within 30 ms of each other: covered, and read.
            const LiveMeasureSource ok(buildPhaseGrid(masked(50, 74), cfg), pack);
            check(ok.value(QStringLiteral("m_leadElbowFoldAfterImpact")).has_value(), "25 ms hidden: covered, read");
        }
        // The producer's "not judged" record covers the same span the grader reads.
        check(fm && fm->reducer.windowMs
                  && int64_t(fm->reducer.windowMs->second - fm->reducer.windowMs->first) * 1000
                         == pinpoint::tuned::upperBody::kElbowFoldSpanUs
                  && fm->reducer.windowMs->first == 0,
              "m_leadElbowFoldAfterImpact's span is tuned::upperBody::kElbowFoldSpanUs after impact");

        // The reading's own path: with no span asked for the grid has nothing, so a reader that does
        // not name the pack's spans cannot grade it — LiveMeasureSource(dir, pack) names them itself.
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(15.0, 14.0)), pack);
            check(!src.value(QStringLiteral("m_leadElbowFoldAfterImpact")).has_value(),
                  "a grid built without the pack's spans has no fold reading");
        }
    }

    // ── Why there is no P8 (plane stack D6, Club/p8_refusal_gate.h) ────────────────────────────────────────
    //
    // FA bounded P8; where findP8Crossing refuses, the readings that need P8 have nothing to read. The record
    // positions.p8Refused sits on the PHASE (subject position.p8, covers nothing), and a measure whose reducer
    // needs P8 and whose reading has no value there names it — the real reason, not "not segmented".
    std::printf("\nwhy there is no P8 (positions.p8Refused, D6)\n");
    {
        const auto phaseAt = [](Phase ph, qint64 tUs) {
            return QJsonObject{ { QStringLiteral("phase"), int(ph) }, { QStringLiteral("t_us"), tUs } };
        };
        const auto curve = [](const char *key, double base) {
            QJsonArray t, v;
            for (int ms = 0; ms <= 1200; ms += 5) { t.append(qint64(ms) * 1000); v.append(base + 0.01 * ms); }
            return QJsonObject{ { QStringLiteral("key"), QString::fromLatin1(key) }, { QStringLiteral("unit"), QStringLiteral("°") },
                                { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v } };
        };
        // A ladder with no P8 (or with one), the two P8 readings' keys as curves, and the records.
        const auto analysis = [&](bool withP8, const std::vector<GateRecord> &gates) {
            QJsonArray ph{ phaseAt(Phase::Address, 100'000), phaseAt(Phase::Top, 560'000), phaseAt(Phase::Impact, 900'000) };
            if (withP8) ph.append(phaseAt(Phase::ShaftParallelThrough, 1'000'000));
            QJsonObject an;
            an.insert(QStringLiteral("phases"), ph);
            an.insert(QStringLiteral("metrics"), QJsonArray{ curve("leadKneeFlexion", 20.0), curve("leadArmToTorso", 40.0) });
            an.insert(QStringLiteral("gatesVersion"), kGatesVersion);
            an.insert(QStringLiteral("gates"), gateRecordsToJson(gates));
            return an;
        };
        const PositionsConfig pc;
        const std::vector<PhaseEvent> noP8{ PhaseEvent{ Phase::Address, 100'000 }, PhaseEvent{ Phase::Impact, 900'000 } };
        std::vector<PhaseEvent> withP8 = noP8;
        withP8.push_back(PhaseEvent{ Phase::ShaftParallelThrough, 1'000'000 });

        struct Cause { P8Refusal why; const char *code; const char *words; };
        const Cause causes[] = {
            { P8Refusal::NoCrossing,    "noCrossing",    "never seen coming back up through level" },
            { P8Refusal::Late,          "late",          "too late to be the follow-through" },
            { P8Refusal::WrongSense,    "wrongSense",    "going down, not up" },
            { P8Refusal::VerticalStall, "verticalStall", "of upright for" },
        };
        namespace gates = pinpoint::gates;
        const gates::GateEntry *entry = gates::gateEntry(QStringLiteral("positions.p8Refused"));
        check(entry && entry->effects.size() == 1 && entry->effects.front() == GateEffect::Refused,
              "positions.p8Refused is catalogued, Refused");
        for (const Cause &c : causes) {
            const std::optional<GateRecord> r = p8RefusalRecord(uint8_t(c.why), noP8, pc);
            check(r && r->gate == QLatin1String("positions.p8Refused") && r->subject == QLatin1String("position.p8")
                      && r->effect == GateEffect::Refused && r->covers.isEmpty()
                      && r->evidence.value(QStringLiteral("code")).toString() == QLatin1String(c.code),
                  qPrintable(QStringLiteral("%1: a Refused record on the phase, code %1, covering no key").arg(QLatin1String(c.code))));
            const QString sentence = entry ? gates::evidenceText(*entry, r ? r->evidence : QVariantMap{}) : QString();
            check(sentence.startsWith(QStringLiteral("No P8: ")) && sentence.contains(QString::fromUtf8(c.words)),
                  qPrintable(QStringLiteral("%1: its sentence names the cause (\"%2\")").arg(QLatin1String(c.code), sentence)));
            const LiveMeasureSource src(buildPhaseGrid(analysis(false, { *r })), pack);
            for (const char *mid : { "m_leadKneeFlex", "m_leadArmToTorso", "m_leadKneeFlex_p8" }) {
                const QString id = QString::fromLatin1(mid);
                check(!src.value(id).has_value() && src.missingKind(id) == MissingKind::PhaseNotSegmented,
                      qPrintable(QStringLiteral("%1 / %2: no P8, no reading").arg(QLatin1String(c.code), id)));
                const LiveMeasureSource::GateCause gc = src.gateCause(id);
                check(gc.gate == QLatin1String("positions.p8Refused") && gc.subject == QLatin1String("position.p8")
                          && gc.clause == QStringLiteral("P8 (the shaft level after impact) not timed"),
                      qPrintable(QStringLiteral("%1 / %2: …and its reason is the P8 record (\"%3\")").arg(QLatin1String(c.code), id, gc.clause)));
            }
            // The same keys read fine at the positions they do have: nothing is refused there.
            check(src.value(QStringLiteral("m_leadKneeFlex_p4")).has_value(),
                  qPrintable(QStringLiteral("%1: leadKneeFlexion still reads at the top").arg(QLatin1String(c.code))));
        }
        // No record where nothing was refused, where the cause was no impact, or where the ladder has a P8
        // (the IMU segmenter's, say) — and then a missing P8 reading names no phase reason.
        check(!p8RefusalRecord(uint8_t(P8Refusal::None), noP8, pc) && !p8RefusalRecord(uint8_t(P8Refusal::NoImpact), noP8, pc)
                  && !p8RefusalRecord(uint8_t(P8Refusal::Late), withP8, pc),
              "no record for None, for NoImpact, or with a P8 on the ladder");
        {
            const LiveMeasureSource src(buildPhaseGrid(analysis(false, {})), pack);
            (void)src.value(QStringLiteral("m_leadKneeFlex"));
            check(src.gateCause(QStringLiteral("m_leadKneeFlex")).gate.isEmpty(),
                  "with no record, a missing P8 reading names no gate (\"not segmented\" stands)");
        }
        // The persisted names round-trip (analysis.club.diag.p8Refusal).
        bool names = true;
        for (const Cause &c : causes)
            names = names && QLatin1String(shaftP8RefusalName(uint8_t(c.why))) == QLatin1String(c.code)
                 && shaftP8RefusalFromName(QString::fromLatin1(c.code)) == uint8_t(c.why);
        check(names && shaftP8RefusalName(0)[0] == '\0' && shaftP8RefusalFromName(QString()) == 0,
              "the causes' persisted names round-trip, None writes nothing");
    }

    std::printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
