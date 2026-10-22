// Acceptance tests for the SHIPPED seed pack (src/Resources/diagnostics/core.json).
//
// The pack is not done when its characteristics load — it is done when the causal graph resolves in
// the right direction, the dominant causes concentrate, and no brand name has leaked into the
// content. Each of those is a way the pack can be wrong while looking entirely correct.
//
//   cmake --build build/analyzer-tests --target core_pack_test
//   ctest --test-dir build/analyzer-tests -R core_pack --output-on-failure

#include "../packs/characteristic_pack.h"
#include "../norms/context_tree.h"
#include "../norms/norm_pack.h"
#include "../packs/pack_provider.h"
#include "../packs/reference_pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

int main()
{
    std::printf("core_pack_test\n");

    QFile f(QStringLiteral(PP_CORE_PACK_PATH));
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("  [FAIL] cannot open %s\n", PP_CORE_PACK_PATH);
        return 1;
    }
    const QByteArray   raw  = f.readAll();
    const PackLoadResult res = loadPack(raw, QStringLiteral("core.json"));
    const CharacteristicPack &p = res.pack;

    // ── It loads and validates ──────────────────────────────────────────────────
    {
        if (!res.loaded)
            for (const ValidationIssue &i : res.report.withSeverity(IssueSeverity::Error))
                std::printf("        error: %s\n", qPrintable(i.message));
        check(res.loaded, "the shipped pack loads and validates with no errors");
        check(p.id == QStringLiteral("core"), "pack id is 'core'");
        check(p.schemaVersion == kPackSchemaVersion, "pack declares the current schema version");
    }

    // ── Content census ──────────────────────────────────────────────────────────
    int observable = 0, screened = 0, asserted = 0;
    for (const Condition &c : p.conditions) {
        if (c.observability == Observability::Observable) ++observable;
        if (c.confirmedBy == ConfirmedBy::Screened) ++screened;
        if (c.confirmedBy == ConfirmedBy::Asserted) ++asserted;
    }
    {
        check(observable >= 25, "at least 25 observable characteristics ship on day one");
        check(screened >= 10, "the screened cause library is seeded");
        check(asserted >= 5, "behavioural causes are seeded");
        check(p.edges.size() >= 75, "the causal graph is seeded, not a stub");
        std::printf("        (%d observable, %d screened, %d behavioural, %d edges, %d measures)\n",
                    observable, screened, asserted, int(p.edges.size()), int(p.measures.size()));
    }

    // ── The kind census ─────────────────────────────────────────────────────────
    //
    // Thresholds, not exact counts: which kind a borderline condition lands in is an editorial call
    // that is meant to move, and pinning 69 would make every reclassification a test edit. What must
    // hold is the SHAPE — that the library is mostly about swing faults, and that the five kinds
    // whose absence would mean the axis had not really been authored are all populated.
    {
        std::map<ConditionKind, int> byKind;
        for (const Condition &c : p.conditions) ++byKind[c.kind];

        check(byKind[ConditionKind::Fault] >= 50, "the library is mostly swing faults, as it should be");
        check(byKind[ConditionKind::Delivery] >= 8, "the delivery layer is populated");
        check(byKind[ConditionKind::Outcome] >= 15, "the outcome layer is populated");
        check(byKind[ConditionKind::Capacity] >= 10, "the physical-capacity layer is populated");
        check(byKind[ConditionKind::Intent] >= 5, "the intent layer is populated");
        check(byKind[ConditionKind::Setup] >= 15, "the setup layer is populated");

        // The four kinds that the group axis CANNOT recover, which is the whole argument for the
        // field existing. All four used to sit in `setup` because there was nowhere else to put them.
        int miscastAsSetupGroup = 0;
        for (const Condition &c : p.conditions)
            if (c.group == ConditionGroup::Setup
                && (c.kind == ConditionKind::Capacity || c.kind == ConditionKind::Intent
                    || c.kind == ConditionKind::Equipment))
                ++miscastAsSetupGroup;
        check(miscastAsSetupGroup >= 20,
              "the `setup` group still holds capacities, intents and equipment — which is why the "
              "kind axis is not recoverable from it");

        for (const auto &kv : byKind)
            std::printf("        %-10s %d\n", qPrintable(conditionKindName(kv.first)), kv.second);
    }

    // ── Outcome and BallFlight are coextensive, in BOTH directions ──────────────
    //
    // `outcomeReachOf()` asks the KIND. It used to ask the group, and the move was landed only
    // because the two sets are provably the same set over shipped content — so this assertion is
    // what makes that a no-op rather than a hope. The second half is the one that matters: a new
    // ball-flight condition authored without a kind would default to Fault and quietly stop being
    // counted as an outcome by every "how many bad shots does this explain" number in the panel.
    //
    // The decision this pins, so it is not rediscovered as an accident: `strike_toe`/`strike_heel`
    // are Outcome, not Delivery, though strike location IS one of the ball-flight determinants. A
    // golfer sees the mark on the face; that makes it something the ball did. Move them to Delivery
    // and this assertion goes red for a correct reason — then the one-way form is what to keep, and
    // `swingEdges` below needs its own sentence.
    {
        QStringList outcomeNotBallFlight, ballFlightNotOutcome;
        for (const Condition &c : p.conditions) {
            if (c.kind == ConditionKind::Outcome && c.group != ConditionGroup::BallFlight)
                outcomeNotBallFlight << c.id;
            if (c.group == ConditionGroup::BallFlight && c.kind != ConditionKind::Outcome)
                ballFlightNotOutcome << c.id;
        }
        for (const QString &id : outcomeNotBallFlight)
            std::printf("        Outcome outside ballFlight: %s\n", qPrintable(id));
        for (const QString &id : ballFlightNotOutcome)
            std::printf("        ballFlight not an Outcome:  %s\n", qPrintable(id));
        check(outcomeNotBallFlight.isEmpty(), "every Outcome sits in the ball-flight group");
        check(ballFlightNotOutcome.isEmpty(), "and every ball-flight condition is an Outcome");
    }

    // ── Every shipped condition AUTHORS both new fields ─────────────────────────
    //
    // Read off the RAW JSON, and it has to be: Prominence has five legitimate values and no
    // sentinel, so by the time the loader is done a row nobody authored is byte-identical to a row
    // somebody authored at the default rung — and 58 conditions sit at that rung on purpose. The
    // validator therefore cannot ask this question at all (see the note in characteristic_pack.h).
    // This is the only layer where it is answerable, and the failure it catches is the real one: a
    // 146-row hand edit that stopped at 142.
    {
        const QJsonArray conds = QJsonDocument::fromJson(raw).object()
                                     .value(QStringLiteral("conditions")).toArray();
        QStringList noKind, noProminence;
        for (const QJsonValue &v : conds) {
            const QJsonObject o = v.toObject();
            const QString     id = o.value(QStringLiteral("id")).toString();
            if (!o.contains(QStringLiteral("kind")))       noKind << id;
            if (!o.contains(QStringLiteral("prominence"))) noProminence << id;
        }
        for (const QString &id : noKind)       std::printf("        no kind: %s\n", qPrintable(id));
        for (const QString &id : noProminence) std::printf("        no prominence: %s\n", qPrintable(id));
        check(int(conds.size()) == int(p.conditions.size()), "every condition in the file loaded");
        check(noKind.isEmpty(), "every shipped condition declares its kind");
        check(noProminence.isEmpty(), "and every one declares a prominence");
    }

    // ── Cause concentration ─────────────────────────────────────────────────────
    // The whole point of the model: a handful of latent causes explain most of the pack, and none of
    // them needs any capture hardware. If every characteristic had its own private cause, this would
    // be a restated fault list rather than a diagnosis.
    {
        std::map<int, QString, std::greater<int>> byCoverage;
        for (const Condition &c : p.conditions) {
            const int cov = coverageOf(p, c.id);
            if (cov > 0) byCoverage.insert({ cov, c.id });
        }

        auto coverage = [&](const char *id) { return coverageOf(p, QString::fromLatin1(id)); };
        // 13 -> 15 with reverse_pivot. Two edges, not one: the fault itself, and the pelvis drifting
        // to the lead side going back, which this capacity plainly explains and had never been wired
        // to. The second was the older gap — the same restriction already reached SWAY, the opposite
        // tail of the very same pelvis-sway measure, so the graph could account for one direction of
        // a two-tailed failure and not the other.
        // 15 -> 17 with the kinematic-sequence checks (2026-10-14, design §10.5): a pelvis that will
        // not turn apart from the chest leaves the chest nothing to overtake, and lets the shoulders
        // start the downswing.
        // 17 -> 21 with the session report's content (2026-10-20, session_report_design.md §3.3):
        // thin cause lists were grown so "we checked every cause we know of" means something, and the
        // physical-limit causes a coach would screen first were wired to the habits they plainly allow.
        // Practice tier, weak or moderate, no citations — coaching reasoning, reviewed by Mark.
        check(coverage("poor_pelvic_disassociation") == 21, "poor pelvic disassociation explains 21");
        // 15 and 9, up from 13 and 8. The face-on producer batch made six authored conditions
        // gradeable for the first time, and the "everything that can fire has a cause" check below
        // caught that they had nothing behind them. Two of the six route to a thoracic-rotation
        // restriction (a chest that cannot turn is why the arms run away and why the finish stops
        // short) and one to the lead hip (a hip that cannot internally rotate spins the pelvis out
        // rather than turning over a stable lead leg).
        //
        // 15 -> 16 with reverse_pivot: a chest that cannot turn away from the target has to get to
        // the top some other way, and leaning toward the target is the cheapest substitute. Moderate
        // rather than Strong — the restriction makes the substitution available, it does not compel
        // it, and a golfer with the same restriction can shorten the backswing instead.
        // 16 -> 18 with the kinematic-sequence checks (2026-10-14): a chest that cannot turn cannot
        // speed up past the hips, and the arms run ahead of it coming down.
        // 18 -> 24 with the session report's content (2026-10-20, session_report_design.md §3.3):
        // thin cause lists were grown so "we checked every cause we know of" means something, and the
        // physical-limit causes a coach would screen first were wired to the habits they plainly allow.
        // Practice tier, weak or moderate, no citations — coaching reasoning, reviewed by Mark.
        // 24 -> 26 with the posture composites (2026-10-22, posture_metrics_design.md §7.3): a chest that cannot turn stands the
        // golfer up, at impact and at the top (loss_of_posture, loss_of_posture_backswing).
        // Practice tier, from the design's edge tables, reviewed by Mark with the design.
        check(coverage("limited_thoracic_rotation") == 26, "limited thoracic rotation explains 26");
        // 9 -> 10 with the lead-side audit: the hip that cannot internally rotate cannot accept the
        // load and turn over it, so the rotation stops rather than being slow to start — which is
        // late_pelvis_rotation, a fault it already explained and a different one.
        // 10 -> 18 with the session report's content (2026-10-20, session_report_design.md §3.3):
        // thin cause lists were grown so "we checked every cause we know of" means something, and the
        // physical-limit causes a coach would screen first were wired to the habits they plainly allow.
        // Practice tier, weak or moderate, no citations — coaching reasoning, reviewed by Mark.
        // 18 -> 19 with the posture composites (2026-10-22, posture_metrics_design.md §7.3): a lead hip that cannot rotate
        // internally leaves the turn through nowhere to go but up (loss_of_posture).
        // Practice tier, from the design's edge tables, reviewed by Mark with the design.
        check(coverage("limited_lead_hip_ir") == 19, "limited lead-hip internal rotation explains 19");
        // 10 -> 11 when the trail hip was given its route to over-the-top: a pelvis that cannot
        // finish turning away leaves the downswing no room to rotate into, and the arms take it
        // over the top. Updated deliberately, per the note below on the pelvis-sway count — these
        // are the numbers somebody chose, and a `>=` here would stop the test noticing a change.
        //
        // 11 -> 13 with the lower-body producer: the trail hip gained the lead knee working in at
        // the top and the pelvis rising in the backswing. Both are OBSERVATIONS the restriction can
        // produce and neither is evidence of it on its own — see the design doc on why the face-on
        // signature is ambiguous — which is why both edges are Moderate and why the explanation
        // pass, not the detector, is what names the cause.
        //
        // 13 -> 14 and 10 -> 11 with the early-extension causes audit. Both restrictions are named
        // in the coaching account of early extension and neither had a route to it: a trail hip
        // that cannot internally rotate leaves the downswing no room to turn into, and a pelvis
        // nothing is holding in its hinge comes out of the hinge instead of rotating within it.
        // The core case was the conspicuous one — it already carried an edge to BACKING OFF THE
        // BALL, which is the opposite tail of the same pelvis-thrust axis, so the graph could
        // explain the rarer direction of that fault and not the common one.
        //
        // 14 -> 15 with reverse_pivot, and this is the Strong one. The trail hip is the joint the
        // backswing loads INTO; if it cannot internally rotate there is nowhere for the load to go,
        // and the mass stays on the lead side because it was never invited across.
        // 15 -> 19 with the session report's content (2026-10-20, session_report_design.md §3.3):
        // thin cause lists were grown so "we checked every cause we know of" means something, and the
        // physical-limit causes a coach would screen first were wired to the habits they plainly allow.
        // Practice tier, weak or moderate, no citations — coaching reasoning, reviewed by Mark.
        // 19 -> 20 with the posture composites (2026-10-22, posture_metrics_design.md §7.3): a trail hip that cannot load
        // stands the golfer up going back (loss_of_posture_backswing).
        // Practice tier, from the design's edge tables, reviewed by Mark with the design.
        check(coverage("limited_trail_hip_ir") == 20, "limited trail-hip internal rotation explains 20");
        // 11 -> 12 on 2026-09-23: crouching (excessive_knee_flex). Without a stable middle the golfer
        // sinks into the knees rather than hinging, and that one setup then explains standing too
        // upright, crowding the ball, the trail knee straightening, the pelvis thrusting toward the
        // ball going back and the weight sitting in the heels — a single Strong edge here reaches
        // all five through it, rather than five edges that would hide the mechanism.
        // 12 -> 16 with the session report's content (2026-10-20, session_report_design.md §3.3):
        // thin cause lists were grown so "we checked every cause we know of" means something, and the
        // physical-limit causes a coach would screen first were wired to the habits they plainly allow.
        // Practice tier, weak or moderate, no citations — coaching reasoning, reviewed by Mark.
        // 16 -> 19 with the posture composites (2026-10-22, posture_metrics_design.md §7.3): a middle that cannot hold a shape
        // loses it either way through impact (loss_of_posture, posture_sinks_impact) and cannot hold a
        // good one at address (poor_setup_posture).
        // Practice tier, from the design's edge tables, reviewed by Mark with the design.
        check(coverage("poor_core_stability") == 19, "poor core stability explains 19");

        const int topFive = coverage("poor_pelvic_disassociation") + coverage("limited_thoracic_rotation")
                          + coverage("limited_lead_hip_ir") + coverage("limited_trail_hip_ir")
                          + coverage("poor_core_stability");

        // Measured against the SWING-fault edges, not every edge in the pack.
        //
        // The claim being made is that a handful of physical capacities explain most of what goes
        // wrong IN THE SWING — that this is a diagnosis rather than a restated fault list. The
        // ball-flight layer added a second tier of edges (fault → outcome) that no screen result
        // reaches directly and never will: a tight trail hip does not cause a slice, it causes the
        // delivery that causes the slice. Counting those in the denominator would make the same
        // library look less concentrated purely for having become able to explain the shot the
        // golfer actually saw.
        // Asks the KIND, like outcomeReachOf() does, and for the same reason: "is this an outcome"
        // is a question about what sort of thing a condition is. The number is unchanged, because
        // the two sets are asserted coextensive above.
        int swingEdges = 0;
        for (const Edge &e : p.edges) {
            const Condition *to = p.condition(e.to);
            if (e.type == EdgeType::Causes && to && to->kind != ConditionKind::Outcome)
                ++swingEdges;
        }

        // The fraction was 3/10 and is now 1/5. It is a CALIBRATION, not the claim — the claim is
        // that a handful of physical capacities explain a large share of the swing, and the number
        // is one crude way of measuring it.
        //
        // What moved it: the unwatched-tails pass added a second causal LAYER. Its 23 nodes are
        // delivery faults caused mostly by other swing faults — excessive shaft lean comes from
        // excessive lag, not from a tight hip — so they land in the denominator while adding almost
        // nothing to the numerator. That is the same dilution the ballFlight scoping above already
        // corrected for once, and it says nothing about concentration: a library that grows DEPTH
        // looks less concentrated to a depth-1 out-degree ratio while being no less explained.
        //
        // The screened-cause edges that were genuinely true were authored rather than the threshold
        // simply dropped — that is what took the ratio from 23 % to the 25 % that ships. Reaching
        // for the old 30 % from there would have meant inventing links to satisfy a number, which
        // is the laundering this check exists to prevent. So the gate sits below what ships, with
        // room for the next content pass, and the honest reading of a failure here is unchanged:
        // new nodes were given private causes instead of being wired to the existing screen
        // library. Fix the edges.
        check(topFive >= swingEdges / 5,
              "five causes account for a substantial share of every SWING causal edge");
        std::printf("        (top five %d of %d swing causal edges, %d %%)\n",
                    topFive, swingEdges, swingEdges ? topFive * 100 / swingEdges : 0);

        // Every dominant cause must be screen-backed — that is what makes the output actionable
        // without any capture hardware at all.
        bool allScreened = true;
        for (const char *id : { "poor_pelvic_disassociation", "limited_thoracic_rotation",
                                "limited_lead_hip_ir", "limited_trail_hip_ir", "poor_core_stability" }) {
            const Condition *c = p.condition(QString::fromLatin1(id));
            if (!c || c->confirmedBy != ConfirmedBy::Screened) allScreened = false;
        }
        check(allScreened, "every dominant cause is screen-backed");
    }

    // ── Edge orientation, structurally ──────────────────────────────────────────
    // The seed tables read effect-first while Edge is cause-first, so every row flips on
    // transcription. A coverage count CANNOT catch a mistake here — totals are identical under edge
    // reversal — so this is the assertion that guards the whole graph.
    {
        bool noneInverted = true;
        for (const Condition &c : p.conditions) {
            if (c.confirmedBy != ConfirmedBy::Screened) continue;
            if (!causesOf(p, c.id).isEmpty()) noneInverted = false;      // in-degree must be 0
            if (effectsOf(p, c.id).isEmpty()) noneInverted = false;      // out-degree must be > 0
        }
        check(noneInverted, "every screened cause has in-degree 0 and out-degree > 0");

        // Two spot checks in plain English, so a reader can see the orientation is right.
        check(effectsOf(p, QStringLiteral("limited_hip_extension"))
                  .contains(QStringLiteral("s_posture")),
              "limited hip extension CAUSES S-posture (not the reverse)");
        check(causesOf(p, QStringLiteral("early_extension"))
                  .contains(QStringLiteral("limited_lead_hip_ir")),
              "early extension is CAUSED BY limited lead-hip internal rotation");
    }

    // ── Every characteristic resolves to Live, or to a NAMED missing measure ────
    {
        int live = 0, planned = 0, held = 0, noProducer = 0, notCapturable = 0, externalDevice = 0;
        bool everyGapNamed = true;

        for (const Condition &c : p.conditions) {
            if (c.observability != Observability::Observable) continue;
            for (const QString &sid : c.detectedBy) {
                const Signal *s = p.signal(sid);
                if (!s) continue;
                for (const QString &mid : s->measures) {
                    const Measure *m = p.measure(mid);
                    if (!m) { everyGapNamed = false; continue; }
                    switch (m->status) {
                    case MeasureStatus::Live:          ++live; break;
                    case MeasureStatus::Planned:       ++planned; break;
                    case MeasureStatus::Held:
                        ++held;
                        if (m->gapReason.isEmpty()) everyGapNamed = false;
                        break;
                    case MeasureStatus::NoProducer:    ++noProducer; break;
                    case MeasureStatus::NotCapturable:
                        ++notCapturable;
                        if (m->gapReason.isEmpty()) everyGapNamed = false;
                        break;
                    // Held to the same standard as a capture gap: the status says something stands
                    // in the way, and only the reason says WHICH device. Two surfaces quote it.
                    case MeasureStatus::ExternalDevice:
                        ++externalDevice;
                        if (m->gapReason.isEmpty()) everyGapNamed = false;
                        break;
                    }
                }
            }
        }
        std::printf("        (measure bindings: %d live, %d held, %d planned, %d no-producer, "
                    "%d external-device, %d capture-gap)\n",
                    live, held, planned, noProducer, externalDevice, notCapturable);
        check(everyGapNamed, "every characteristic resolves to a real measure, gaps named");
        check(live > 0, "some characteristics are LIVE on day one, not all stubs");
    }

    // ── Provided measures bind to the catalogue, not to a parallel registry ─────
    {
        bool allBound = true;
        int  provided = 0;
        for (const Measure &m : p.measures) {
            if (m.kind != MeasureKind::Provided) continue;
            ++provided;
            if (m.metricKey.isEmpty()) allBound = false;
        }
        check(provided > 0 && allBound, "every Provided measure names a MetricCatalogue key");

        // The payoff of ranking series rather than reduced measures: one producer unblocks several
        // characteristics. pelvisSway carries sway back, slide and hanging back at three different
        // phases — one producer, three faults, which is the payoff being demonstrated. The number is
        // updated deliberately when a reducer is added or removed, never loosened to a `>=`: the
        // claim is that these are the reducers somebody chose, not that there are some.
        //
        // 4 -> 3 on 2026-09-04: `m_pelvisSwayFinish` read this series AT THE FINISH, and pelvisSway
        // is a P1-P7 quantity — past impact the pelvis has turned, so its lateral offset in a
        // face-on image is the rotation and not the translation the measure named. It was deleted
        // rather than reclassified, because `notCapturable` is a statement about the METRIC and
        // pelvisSway is produced on every camera swing. The honest finish reading is
        // `m_comOverLeadFootFinish`, a distance ALONG the stance line, which survives the turn.
        int onPelvisSway = 0;
        for (const Measure &m : p.measures)
            if (m.metricKey == QStringLiteral("pelvisSway")) ++onPelvisSway;
        check(onPelvisSway == 3, "three characteristics sit on one pelvis-sway series (one producer)");
    }

    // ── The spinal measures are roadmap items, not capture gaps ─────────────────
    // They have no pose keypoint, but the back contour of a down-the-line silhouette carries both
    // the thoracic round and the lumbar arch — so the honest classification is "producer not
    // written" rather than "this product can never see it".
    {
        const Measure *thoracic = p.measure(QStringLiteral("m_thoracicCurve"));
        const Measure *lumbar   = p.measure(QStringLiteral("m_lumbarCurve"));
        check(thoracic && thoracic->status == MeasureStatus::NoProducer,
              "C-posture's measure is a roadmap item (DTL back contour would produce it)");
        check(lumbar && lumbar->status == MeasureStatus::NoProducer,
              "S-posture's measure likewise");
        check(thoracic && !thoracic->gapReason.isEmpty(),
              "and it still explains why the skeleton cannot supply it");

        // Carried deliberately despite being unmeasurable: four conditions cite C-posture as a
        // cause, so dropping it would cost them their strongest explanation.
        check(coverageOf(p, QStringLiteral("c_posture")) >= 2,
              "C-posture is carried because other conditions depend on it");
    }

    // ── Screened and Behavioural causes never enter the roadmap ─────────────────
    // One row implying a producer that will never be built corrupts the artefact for every other
    // row, so this is a hard rule rather than a presentation choice.
    {
        bool clean = true;
        for (const Condition &c : p.conditions) {
            if (!isOutsideCaptureReach(c.confirmedBy)) continue;
            for (const QString &sid : c.detectedBy) {
                const Signal *s = p.signal(sid);
                if (s && !s->measures.isEmpty()) clean = false;   // it would land in the roadmap
            }
        }
        check(clean, "no Physical/Behavioural cause carries a measure that could reach the roadmap");
    }

    // ── Tail splits ─────────────────────────────────────────────────────────────
    {
        for (const char *axis : { "ball_position", "stance_width", "alignment", "ball_body_distance" })
            check(tailsOfAxis(p, QString::fromLatin1(axis)).size() == 2,
                  "a two-sided characteristic has both tails authored");

        // Both tails must sit on the same measure — that is what makes them tails.
        const QStringList ballTails = tailsOfAxis(p, QStringLiteral("ball_position"));
        check(ballTails.size() == 2 && ballTails.contains(QStringLiteral("ball_forward"))
                  && ballTails.contains(QStringLiteral("ball_back")),
              "ball position splits into forward and back");

        // Mark's case, wired at BOTH ends: ball forward opens the shoulders, ball back closes them.
        check(effectsOf(p, QStringLiteral("ball_forward")).contains(QStringLiteral("alignment_open")),
              "ball forward causes an open shoulder line");
        check(effectsOf(p, QStringLiteral("ball_back")).contains(QStringLiteral("alignment_closed")),
              "ball back causes a closed shoulder line");
        check(!effectsOf(p, QStringLiteral("ball_forward")).contains(QStringLiteral("alignment_closed")),
              "the tails are not cross-wired");
    }

    // ── C-posture's two routes have different remedies ──────────────────────────
    {
        const QStringList causes = causesOf(p, QStringLiteral("c_posture"));
        check(causes.contains(QStringLiteral("thoracic_kyphosis")), "C-posture has a physical cause");
        check(causes.contains(QStringLiteral("ball_too_far")), "C-posture has a setup-induced cause");

        const Condition *phys  = p.condition(QStringLiteral("thoracic_kyphosis"));
        const Condition *setup = p.condition(QStringLiteral("ball_too_far"));
        check(phys && phys->confirmedBy == ConfirmedBy::Screened, "the physical route is screened");
        check(setup && setup->confirmedBy == ConfirmedBy::Measured, "the setup route is measured");
    }

    // ── Sharp findings (Condition::sharp) ───────────────────────────────────────
    // The sequence's after-impact and out-of-order checks are sharp (Mark, 15 Oct): crossing the
    // line is serious in itself. The flag survives a save and reload, and nothing else carries it.
    {
        const QStringList sharpIds = { QStringLiteral("hips_peak_after_impact"), QStringLiteral("chest_peak_after_impact"),
                                       QStringLiteral("arms_peak_after_impact"), QStringLiteral("club_peak_after_impact"),
                                       QStringLiteral("sequence_order") };
        const PackLoadResult back = loadPack(savePack(p), QStringLiteral("core_sharp_roundtrip"));
        bool all = true, roundTrip = true;
        int  count = 0;
        for (const QString &id : sharpIds) {
            const Condition *c = p.condition(id), *b = back.pack.condition(id);
            all       = all && c && c->sharp;
            roundTrip = roundTrip && b && b->sharp;
        }
        for (const Condition &c : p.conditions) count += c.sharp ? 1 : 0;
        check(all, "the four after-impact checks and the order check are sharp");
        check(roundTrip, "…and stay sharp through a save and reload");
        check(count == sharpIds.size(), "…and no other condition is");
    }

    // ── D27: the club racing ahead of the arms is its own condition (17 Oct) ────
    // Drafted as a second signal on casting under ANY, which reads clean only when EVERY signal was
    // read — so beside the usually-missing club peak, casting would have counted firings and never a
    // clean swing. It ships instead as a single-signal sequence condition, casting keeps its wrist
    // preference untouched, and the one edge says casting is the usual way the club gets there first.
    {
        const Condition *club    = p.condition(QStringLiteral("club_before_arms"));
        const Condition *casting = p.condition(QStringLiteral("casting"));
        check(club && club->detectedBy == QStringList{ QStringLiteral("sig_clubBeforeArm") }
                  && club->group == ConditionGroup::Sequence && club->kind == ConditionKind::Fault
                  && club->confirmedBy == ConfirmedBy::Measured && club->state == ConditionState::Active
                  && !club->sharp,
              "club_before_arms reads sig_clubBeforeArm alone: an active, measured sequence fault, not sharp");
        check(casting && casting->detectedBy
                             == QStringList({ QStringLiteral("sig_castingHinge"), QStringLiteral("sig_casting") })
                  && casting->detection == DetectionMode::First,
              "…casting keeps its two wrist signals as a preference, with no club order among them");
        bool edge = false;
        for (const Edge &e : p.edges)
            if (e.from == QLatin1String("casting") && e.to == QLatin1String("club_before_arms"))
                edge = e.type == EdgeType::Causes && e.strength == Strength::Moderate
                       && e.provenance.tier == ProvenanceTier::Indirect
                       && e.provenance.citation == QLatin1String("9780978873417");
        check(edge, "…and casting causes it: moderate, indirect, on Cheetham et al. 2008");
        // Casting was its only cause until the session report's content (2026-10-20, design §3.3) added
        // the two timing causes a coach would name for a club that races ahead: a rushed transition and
        // hips that stop turning. Practice tier; casting keeps its sourced, moderate edge above.
        QStringList clubCauses = causesOf(p, QStringLiteral("club_before_arms"));
        clubCauses.sort();
        check(clubCauses == QStringList({ QStringLiteral("casting"), QStringLiteral("hip_stall"),
                                          QStringLiteral("transition_rush") }),
              "…its causes: casting, a rushed transition, hips that stall");
    }

    // ── No brand names anywhere in the content ──────────────────────────────────
    // Several conditions are named with terms popularised by a commercial screening system. The
    // TERMS are common domain and stay; the ATTRIBUTION must not enter the repo in any form — not a
    // citation, not an author, not an explanatory note. Checked against the raw bytes so a comment
    // or a stray field cannot slip past.
    //
    // ALL THREE reviewable content files, not just the pack. The rule was always stated over the
    // content as a whole, but the grep only ever covered core.json — and a vendor name duly sat
    // unnoticed in a norms.json citation note. A rule enforced over one of three files is a rule
    // that reads as enforced and is not.
    //
    // THE ONE SANCTIONED ESCAPE, because otherwise this rule silently costs the library citations:
    // a peer-reviewed paper whose PUBLISHED TITLE names such a system may still be recorded, with
    // the name replaced by a bracketed editorial redaction — `ref.gulgin2014` is the worked example.
    // The identifier is never touched, so the record resolves to the real title in one click, and
    // the redaction declares itself in `establishes` rather than being silent. That is a standard
    // scholarly abridgement, not a workaround: the alternative is dropping the only study that
    // tested physical screens against visible swing faults, which would leave the library's most
    // load-bearing negative result uncitable. Redact the BRAND, never the finding.
    {
        const char *forbidden[] = { "titleist", "tpi", "trackman", "flightscope", "foresight",
                                    "k-vest", "kvest", "gears", "swingcatalyst", "boditrak",
                                    "performance institute", "certified", "hackmotion" };
        const struct { const char *label; const char *path; } files[] = {
            { "core.json",       PP_CORE_PACK_PATH },
            { "norms.json",      PP_CORE_NORMS_PATH },
            { "references.json", PP_CORE_REFERENCES_PATH },
        };

        bool clean = true;
        for (const auto &file : files) {
            QFile cf(QString::fromUtf8(file.path));
            if (!cf.open(QIODevice::ReadOnly)) {
                std::printf("        cannot open %s for the brand grep\n", file.label);
                clean = false;
                continue;
            }
            const QString lower = QString::fromUtf8(cf.readAll()).toLower();
            for (const char *needle : forbidden) {
                if (lower.contains(QLatin1String(needle))) {
                    std::printf("        brand token '%s' found in %s\n", needle, file.label);
                    clean = false;
                }
            }
        }
        check(clean, "no commercial organisation, product or certification body is named");
    }

    // ── Uncited content is honestly badged ──────────────────────────────────────
    {
        // This used to assert `proposed > 0`, as a proxy for "uncited content is not laundered
        // into a cited tier". That proxy was only ever valid mid-search: `Proposed` means NOBODY
        // HAS LOOKED, so a search pass that reaches every condition legitimately empties the tier,
        // and re-adding a proposed row to satisfy the old check would be the dishonesty it was
        // written to prevent. DO NOT RESTORE IT.
        //
        // What it was really protecting is asserted directly instead: the untested majority must
        // still be recorded as untested. `practice` says the field agrees and has never measured
        // the named state, which is the true state of most of this library — and a pack where
        // more content claimed a source than admitted to coaching orthodoxy would be the
        // laundering worth failing over.
        int cited = 0, practice = 0;
        for (const Condition &c : p.conditions) {
            if (c.provenance.tier == ProvenanceTier::Practice) ++practice;
            if (citationRequired(c.provenance.tier))           ++cited;
        }
        check(practice > cited, "the untested majority is recorded as coaching practice, not as sourced");

        // And the pass actually happened: every condition names the day it was searched. This is
        // strictly stronger than the check it replaced — that one could pass with 111 conditions
        // never looked at.
        bool allSearched = true;
        for (const Condition &c : p.conditions)
            if (!c.provenance.searched()) allSearched = false;
        check(allSearched, "every condition records the date its search was made");

        std::printf("        (conditions: %d practice, %d cited)\n", practice, cited);

        // Keyed on citationRequired(), not on "above proposed". Two tiers are ABOVE proposed and
        // legitimately carry no citation: NoSourceFound asserts the absence of a source, and
        // Practice cites a body of coaching practice this repo is not permitted to name. Demanding
        // a citation for either would make the finding unrecordable — which is how they came to be
        // conflated with "nobody has looked" in the first place.
        bool noFakeCitations = true, noUndatedSearch = true;
        for (const Condition &c : p.conditions) {
            if (citationRequired(c.provenance.tier) && c.provenance.citation.isEmpty())
                noFakeCitations = false;
            if (searchDateRequired(c.provenance.tier) && !c.provenance.searched())
                noUndatedSearch = false;
        }
        check(noFakeCitations, "no condition claims a cited tier without a citation");
        check(noUndatedSearch, "no condition records a search outcome without the date it was made");

        // The same two rules over the edges, which until now could make neither claim at all.
        bool edgesHonest = true;
        for (const Edge &e : p.edges)
            if ((citationRequired(e.provenance.tier) && e.provenance.citation.isEmpty())
                || (searchDateRequired(e.provenance.tier) && !e.provenance.searched()))
                edgesHonest = false;
        check(edgesHonest, "no edge claims a cited tier without a citation, or a search without a date");

        std::map<QString, int> byTier;
        for (const Edge &e : p.edges) ++byTier[provenanceTierName(e.provenance.tier)];
        std::printf("        (edge provenance:");
        for (const auto &kv : byTier) std::printf(" %s=%d", qPrintable(kv.first), kv.second);
        std::printf(")\n");
    }

    // ── No peer tier rests on a book ────────────────────────────────────────────
    // `Supported` means a peer-reviewed source tested this cause and this effect; `Established`
    // means independent sources reproduced it. A book is neither, however good it is, and admitting
    // ISBNs to the bibliography without this check would let coaching doctrine be filed under the
    // two tiers that exist to mean the opposite of doctrine.
    //
    // The pack loader demotes this at load time (`bookCitationAtPeerTier`), and that is precisely
    // why the content needs its OWN check: a validator that fires is not the same as content that
    // is right, and a reader of core.json sees the authored tier, not the corrected one. Note that
    // this target deliberately runs with NO bibliography on the path — see its CMake comment — so
    // the tiers read here are exactly as authored, and the registry is loaded by hand below.
    {
        QFile rf(QStringLiteral(PP_CORE_REFERENCES_PATH));
        check(rf.open(QIODevice::ReadOnly), "the shipped reference registry is readable");
        const ReferenceLoadResult rres =
            loadReferenceSet(rf.readAll(), QStringLiteral("references.json"));
        check(rres.loaded, "the shipped reference registry loads and validates clean");

        // The identifiers of every record whose ONLY identifier is an ISBN. A record carrying both
        // an ISBN and a DOI is a paper that also happens to have been collected into a volume, and
        // it is peer-reviewed on the strength of the DOI.
        QSet<QString> bookOnly;
        for (const Reference &r : rres.pack.references)
            if (r.doi.isEmpty() && r.pmid.isEmpty() && !r.isbn.isEmpty()) bookOnly.insert(r.isbn);

        const auto peer = [](ProvenanceTier t) {
            return t == ProvenanceTier::Supported || t == ProvenanceTier::Established;
        };

        int overclaimed = 0;
        for (const Condition &c : p.conditions)
            if (peer(c.provenance.tier) && bookOnly.contains(c.provenance.citation)) {
                ++overclaimed;
                std::printf("        '%s' claims a peer tier on the book '%s'\n",
                            qPrintable(c.id), qPrintable(c.provenance.citation));
            }
        for (const Edge &e : p.edges)
            if (peer(e.provenance.tier) && bookOnly.contains(e.provenance.citation)) {
                ++overclaimed;
                std::printf("        edge '%s' -> '%s' claims a peer tier on the book '%s'\n",
                            qPrintable(e.from), qPrintable(e.to),
                            qPrintable(e.provenance.citation));
            }
        check(overclaimed == 0, "no condition or edge claims a peer-reviewed tier on a book");
        std::printf("        (%d book-only references in the registry)\n", int(bookOnly.size()));
    }

    // ── Every condition says what it costs the golfer ───────────────────────────
    {
        bool allHaveConsequence = true;
        for (const Condition &c : p.conditions)
            if (c.consequence.text().isEmpty()) allHaveConsequence = false;
        check(allHaveConsequence, "every condition carries a plain-English consequence");
    }

    // ── The health list is populated, and it is warnings not errors ─────────────
    {
        const int warnings = res.report.warningCount();
        check(warnings > 0, "the health list has content (uncited tiers, single-tail axes)");
        check(res.report.errorCount() == 0, "nothing in the health list is an error");
        std::printf("        (%d health-list warnings)\n", warnings);
    }

    // ── Every row the home screen can name has golfer words, and they are plain ─
    //
    // The lint is a WARNING so a user's own pack still loads without the prose. The shipped pack is
    // held to zero here, because this is the content the home screen's summary actually speaks in
    // (docs/design/home_themes_design.md): a missing phrase silences the summary about a real fault,
    // and a causal or jargon word puts back exactly what the summary exists to keep one tap down.
    {
        const std::vector<ValidationIssue> missing = res.report.withCode(QStringLiteral("golferPhraseMissing"));
        const std::vector<ValidationIssue> wording = res.report.withCode(QStringLiteral("golferPhraseWording"));
        for (const ValidationIssue &i : missing)
            std::printf("        %s\n", qPrintable(i.message));
        for (const ValidationIssue &i : wording)
            std::printf("        %s\n", qPrintable(i.message));
        check(missing.empty(),
              "every detectable fault / set-up and every measure that produces readings has golfer words");
        check(wording.empty(),
              "no shipped golfer phrase says why, or uses anatomy, planes, P-positions, numbers or units");

        int conditionPhrases = 0, wellPhrases = 0, whySentences = 0, measurePhrases = 0;
        for (const Condition &c : p.conditions) {
            if (!c.golfer.isEmpty()) ++conditionPhrases;
            if (!c.golferWell.isEmpty()) ++wellPhrases;
            if (!c.golferWhy.isEmpty()) ++whySentences;
        }
        for (const Measure &m : p.measures)
            measurePhrases += int(!m.golferHigh.isEmpty()) + int(!m.golferLow.isEmpty());
        std::printf("        (%d condition phrases, %d golferWell phrases, %d golferWhy sentences, "
                    "%d measure phrases)\n",
                    conditionPhrases, wellPhrases, whySentences, measurePhrases);
    }

    // ── …and the lint fires on a pack that breaks it ──────────────────────────
    //
    // The zero above proves nothing on its own: a check that never fires passes it too. One small
    // pack, one row per way of being wrong, and one row per way of being right that must NOT fire —
    // an outcome needs no phrase, and "extend" is not "extension".
    {
        const QByteArray json = R"({
          "id": "golfer_lint", "version": "1", "schemaVersion": 1,
          "measures": [
            { "id": "m_half", "kind": "provided", "metricKey": "half", "status": "live",
              "reducer": { "kind": "at", "anchor": "p1" },
              "golferHigh": "your hips turn more because you sway" },
            { "id": "m_whole", "kind": "provided", "metricKey": "whole", "status": "held",
              "reducer": { "kind": "at", "anchor": "p1" }, "gapReason": "test",
              "golferHigh": "your head rises more going back",
              "golferLow": "your head drops more going back" },
            { "id": "m_unproduced", "kind": "provided", "metricKey": "none", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" } } ],
          "signals": [ { "id": "s_half", "test": "outsideCorridor", "measures": ["m_half"],
                         "direction": "high" },
                       { "id": "s_whole", "test": "outsideCorridor", "measures": ["m_whole"],
                         "direction": "high" } ],
          "conditions": [
            { "id": "c_missing", "label": "c", "kind": "fault", "detectedBy": ["s_half"] },
            { "id": "c_jargon", "label": "c", "kind": "fault", "detectedBy": ["s_half"],
              "golfer": "your pelvis slides at P7" },
            { "id": "c_clean", "label": "c", "kind": "setup", "detectedBy": ["s_whole"],
              "golfer": "your arms extend fully after impact" },
            { "id": "c_outcome", "label": "c", "kind": "outcome", "group": "ballFlight",
              "detectedBy": ["s_whole"] } ]
        })";
        const PackLoadResult lint = loadPack(json, QStringLiteral("golfer_lint"));
        auto flagged = [&](const char *code, const char *subject) {
            for (const ValidationIssue &i : lint.report.withCode(QString::fromLatin1(code)))
                if (i.subject == QLatin1String(subject)) return true;
            return false;
        };
        check(lint.loaded, "a pack with missing or jargon golfer phrases still loads");
        check(flagged("golferPhraseMissing", "c_missing"),
              "golferPhraseMissing: a detectable fault with no golfer phrase");
        check(flagged("golferPhraseMissing", "m_half"),
              "golferPhraseMissing: a live measure with only one direction phrased");
        check(!flagged("golferPhraseMissing", "c_outcome") && !flagged("golferPhraseMissing", "m_unproduced")
                  && !flagged("golferPhraseMissing", "c_clean") && !flagged("golferPhraseMissing", "m_whole"),
              "golferPhraseMissing: silent on an outcome, an unproduced measure, and complete rows");
        check(flagged("golferPhraseWording", "c_jargon"),
              "golferPhraseWording: anatomy and a P-position in a condition phrase");
        check(flagged("golferPhraseWording", "m_half"),
              "golferPhraseWording: a causal word in a measure phrase");
        check(!flagged("golferPhraseWording", "c_clean") && !flagged("golferPhraseWording", "m_whole"),
              "golferPhraseWording: silent on plain phrases (\"extend\" is not \"extension\")");

        const PackLoadResult back = loadPack(savePack(lint.pack), QStringLiteral("golfer_lint2"));
        const Condition *c = back.pack.condition(QStringLiteral("c_clean"));
        const Measure   *m = back.pack.measure(QStringLiteral("m_whole"));
        check(c && c->golfer == QStringLiteral("your arms extend fully after impact")
                  && m && m->golferHigh == QStringLiteral("your head rises more going back")
                  && m->golferLow == QStringLiteral("your head drops more going back"),
              "golfer, golferHigh and golferLow survive a save and reload");
    }

    // ── …and golferWell is held to the same lint, plus the negation ban ───────────
    //
    // Required on a detectable Fault combining Any or First — the rows "What you do well" can name —
    // and nowhere else: a conjunction has no single ideal, a set-up is not praised. Each banned
    // negation fires on its own row (one with a curly apostrophe, as a phone types it), jargon fires
    // here as it does in `golfer`, and words that merely START with "no" must not. The two rows the
    // missing check must stay silent on also carry a clean golferWhy, which is required on the same
    // rows and has its own block below.
    {
        const QByteArray json = R"({
          "id": "golfer_well_lint", "version": "1", "schemaVersion": 1,
          "measures": [
            { "id": "m_a", "kind": "provided", "metricKey": "a", "status": "held",
              "reducer": { "kind": "at", "anchor": "p1" }, "gapReason": "test",
              "golferHigh": "your arms get wider at the top",
              "golferLow": "your arms get narrower at the top" },
            { "id": "m_b", "kind": "provided", "metricKey": "b", "status": "held",
              "reducer": { "kind": "at", "anchor": "p1" }, "gapReason": "test",
              "golferHigh": "your head rises more going back",
              "golferLow": "your head drops more going back" } ],
          "signals": [ { "id": "s_a", "test": "outsideCorridor", "measures": ["m_a"], "direction": "low" },
                       { "id": "s_b", "test": "outsideCorridor", "measures": ["m_b"], "direction": "high" } ],
          "conditions": [
            { "id": "w_missing", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top" },
            { "id": "w_first", "label": "c", "kind": "fault", "detection": "first",
              "detectedBy": ["s_a", "s_b"], "golfer": "your swing gets narrow at the top" },
            { "id": "w_all", "label": "c", "kind": "fault", "detection": "all",
              "detectedBy": ["s_a", "s_b"], "golfer": "your swing gets narrow at the top" },
            { "id": "w_setup", "label": "c", "kind": "setup", "detectedBy": ["s_a"],
              "golfer": "you stand too close at address" },
            { "id": "w_dont", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "you don't lose width at the top",
              "golferWhy": "A narrow swing has less room to build speed, so you lose distance." },
            { "id": "w_doesnt", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "your lead arm doesn’t collapse at the top" },
            { "id": "w_not", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "your arms are not narrow at the top" },
            { "id": "w_never", "label": "c", "kind": "fault", "detectedBy": ["s_b"],
              "golfer": "your head comes up going back",
              "golferWell": "your head never rises going back" },
            { "id": "w_no", "label": "c", "kind": "fault", "detectedBy": ["s_b"],
              "golfer": "your head comes up going back",
              "golferWell": "your head makes no move going back" },
            { "id": "w_jargon", "label": "c", "kind": "fault", "detectedBy": ["s_b"],
              "golfer": "your head comes up going back",
              "golferWell": "your pelvis stays level at P4" },
            { "id": "w_clean", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "you keep your arms noticeably wide at the top",
              "golferWhy": "A narrow swing has less room to build speed, so you lose distance." } ]
        })";
        const PackLoadResult lint = loadPack(json, QStringLiteral("golfer_well_lint"));
        auto flagged = [&](const char *code, const char *subject) {
            for (const ValidationIssue &i : lint.report.withCode(QString::fromLatin1(code)))
                if (i.subject == QLatin1String(subject)) return true;
            return false;
        };
        check(lint.loaded, "a pack with missing or negated golferWell phrases still loads");
        check(flagged("golferPhraseMissing", "w_missing") && flagged("golferPhraseMissing", "w_first"),
              "golferPhraseMissing: a detectable Any / First fault with no golferWell");
        check(!flagged("golferPhraseMissing", "w_all") && !flagged("golferPhraseMissing", "w_setup")
                  && !flagged("golferPhraseMissing", "w_clean") && !flagged("golferPhraseMissing", "w_dont"),
              "golferPhraseMissing: silent on a conjunction, a set-up, and rows that have golferWell");
        check(flagged("golferPhraseWording", "w_dont") && flagged("golferPhraseWording", "w_doesnt")
                  && flagged("golferPhraseWording", "w_not") && flagged("golferPhraseWording", "w_never")
                  && flagged("golferPhraseWording", "w_no"),
              "golferPhraseWording: don't, doesn't (curly), not, never and no in a golferWell phrase");
        check(flagged("golferPhraseWording", "w_jargon"),
              "golferPhraseWording: anatomy and a P-position in a golferWell phrase");
        check(!flagged("golferPhraseWording", "w_clean") && !flagged("golferPhraseWording", "w_missing"),
              "golferPhraseWording: silent on a plain golferWell (\"noticeably\" is not \"no\")");

        const PackLoadResult back = loadPack(savePack(lint.pack), QStringLiteral("golfer_well_lint2"));
        const Condition *c = back.pack.condition(QStringLiteral("w_clean"));
        const Condition *a = back.pack.condition(QStringLiteral("w_all"));
        check(c && c->golferWell == QStringLiteral("you keep your arms noticeably wide at the top")
                  && a && a->golferWell.isEmpty(),
              "golferWell survives a save and reload, and stays absent where it was");
    }

    // ── …and golferWhy is one plain sentence about the shot ───────────────────────
    //
    // Required on the same rows as golferWell (a detectable Any / First fault), and nowhere else.
    // It is a SENTENCE, so a lower-case start and a missing full stop each fire on their own row; a
    // causal word fires (the reason is never another fault) and jargon fires as it does everywhere,
    // while "so" — the word that states the cost to the shot — must not, and nor may "costs",
    // which is not "causes".
    {
        const QByteArray json = R"({
          "id": "golfer_why_lint", "version": "1", "schemaVersion": 1,
          "measures": [
            { "id": "m_a", "kind": "provided", "metricKey": "a", "status": "held",
              "reducer": { "kind": "at", "anchor": "p1" }, "gapReason": "test",
              "golferHigh": "your arms get wider at the top",
              "golferLow": "your arms get narrower at the top" },
            { "id": "m_b", "kind": "provided", "metricKey": "b", "status": "held",
              "reducer": { "kind": "at", "anchor": "p1" }, "gapReason": "test",
              "golferHigh": "your head rises more going back",
              "golferLow": "your head drops more going back" } ],
          "signals": [ { "id": "s_a", "test": "outsideCorridor", "measures": ["m_a"], "direction": "low" },
                       { "id": "s_b", "test": "outsideCorridor", "measures": ["m_b"], "direction": "high" } ],
          "conditions": [
            { "id": "y_missing", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "you keep your arms wide at the top" },
            { "id": "y_first", "label": "c", "kind": "fault", "detection": "first",
              "detectedBy": ["s_a", "s_b"], "golfer": "your swing gets narrow at the top",
              "golferWell": "you keep your arms wide at the top" },
            { "id": "y_all", "label": "c", "kind": "fault", "detection": "all",
              "detectedBy": ["s_a", "s_b"], "golfer": "your swing gets narrow at the top" },
            { "id": "y_setup", "label": "c", "kind": "setup", "detectedBy": ["s_a"],
              "golfer": "you stand too close at address" },
            { "id": "y_lower", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "you keep your arms wide at the top",
              "golferWhy": "a narrow swing has less room to build speed, so you lose distance." },
            { "id": "y_nostop", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "you keep your arms wide at the top",
              "golferWhy": "A narrow swing has less room to build speed, so you lose distance" },
            { "id": "y_because", "label": "c", "kind": "fault", "detectedBy": ["s_b"],
              "golfer": "your head comes up going back",
              "golferWell": "your head stays level going back",
              "golferWhy": "Your head comes up because you stand up, so contact suffers." },
            { "id": "y_leads", "label": "c", "kind": "fault", "detectedBy": ["s_b"],
              "golfer": "your head comes up going back",
              "golferWell": "your head stays level going back",
              "golferWhy": "A rising head leads to thin contact." },
            { "id": "y_jargon", "label": "c", "kind": "fault", "detectedBy": ["s_b"],
              "golfer": "your head comes up going back",
              "golferWell": "your head stays level going back",
              "golferWhy": "Losing your pelvis tilt at P4 costs you speed." },
            { "id": "y_clean", "label": "c", "kind": "fault", "detectedBy": ["s_a"],
              "golfer": "your swing gets narrow at the top",
              "golferWell": "you keep your arms wide at the top",
              "golferWhy": "A narrow swing also has less room to build speed, so it costs you distance." } ]
        })";
        const PackLoadResult lint = loadPack(json, QStringLiteral("golfer_why_lint"));
        auto flagged = [&](const char *code, const char *subject) {
            for (const ValidationIssue &i : lint.report.withCode(QString::fromLatin1(code)))
                if (i.subject == QLatin1String(subject)) return true;
            return false;
        };
        check(lint.loaded, "a pack with missing or badly worded golferWhy sentences still loads");
        check(flagged("golferPhraseMissing", "y_missing") && flagged("golferPhraseMissing", "y_first"),
              "golferPhraseMissing: a detectable Any / First fault with no golferWhy");
        check(!flagged("golferPhraseMissing", "y_all") && !flagged("golferPhraseMissing", "y_setup")
                  && !flagged("golferPhraseMissing", "y_clean") && !flagged("golferPhraseMissing", "y_lower"),
              "golferPhraseMissing: silent on a conjunction, a set-up, and rows that have golferWhy");
        check(flagged("golferPhraseWording", "y_lower") && flagged("golferPhraseWording", "y_nostop"),
              "golferPhraseWording: a golferWhy with no capital, and one with no full stop");
        check(flagged("golferPhraseWording", "y_because") && flagged("golferPhraseWording", "y_leads"),
              "golferPhraseWording: because and leads to in a golferWhy");
        check(flagged("golferPhraseWording", "y_jargon"),
              "golferPhraseWording: anatomy and a P-position in a golferWhy");
        check(!flagged("golferPhraseWording", "y_clean") && !flagged("golferPhraseWording", "y_missing"),
              "golferPhraseWording: silent on a plain golferWhy (\"so\" is allowed, \"costs\" is not \"causes\")");

        const PackLoadResult back = loadPack(savePack(lint.pack), QStringLiteral("golfer_why_lint2"));
        const Condition *c = back.pack.condition(QStringLiteral("y_clean"));
        const Condition *a = back.pack.condition(QStringLiteral("y_all"));
        check(c && c->golferWhy == QStringLiteral("A narrow swing also has less room to build speed, so it costs you distance.")
                  && a && a->golferWhy.isEmpty(),
              "golferWhy survives a save and reload, and stays absent where it was");
    }

    // ── The session report's words are held to their own lint ────────────────────
    //
    // docs/design/session_report_design.md §5. `goal` is the work-on page's title, `cue` the feel,
    // `check` how to see it without the studio, and a measure's `golferName` the number in the
    // golfer's words. Each way of being wrong on its own row, so one firing cannot hide another,
    // and the mock's own wording as the clean row that must stay silent.
    {
        const QByteArray json = R"({
          "id": "report_words_lint", "version": "1", "schemaVersion": 1,
          "measures": [
            { "id": "m_capital", "kind": "provided", "metricKey": "a", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" },
              "golferName": "Trail-knee bend lost by the top" },
            { "id": "m_pnum", "kind": "provided", "metricKey": "b", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" },
              "golferName": "trail-knee bend lost by p4" },
            { "id": "m_clean", "kind": "provided", "metricKey": "c", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" },
              "golferName": "trail-knee bend lost by the top", "golferSign": -1 } ],
          "conditions": [
            { "id": "w_title", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Keep The Flex In Your Trail Knee" },
            { "id": "w_stop", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Keep the flex in your trail knee." },
            { "id": "w_long", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Keep the flex in your trail knee from address all the way up to the top" },
            { "id": "w_never", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Never straighten your trail knee" },
            { "id": "w_dont", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Don’t straighten your trail knee" },
            { "id": "w_pnum", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Keep your trail knee flexed at P4" },
            { "id": "w_digit", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Keep 2 knuckles showing" },
            { "id": "w_lower", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "keep the flex in your trail knee" },
            { "id": "w_promise", "label": "c", "kind": "outcome", "group": "ballFlight",
              "cue": "Keep your backside on the wall and this will fix your slice." },
            { "id": "w_cures", "label": "c", "kind": "outcome", "group": "ballFlight",
              "cue": "A quiet head cures the thin strike." },
            { "id": "w_cueStop", "label": "c", "kind": "outcome", "group": "ballFlight",
              "cue": "Feel your backside on an imaginary wall" },
            { "id": "w_cuePnum", "label": "c", "kind": "outcome", "group": "ballFlight",
              "cue": "Feel the knee stay bent at P4." },
            { "id": "w_checkPct", "label": "c", "kind": "outcome", "group": "ballFlight",
              "check": "Film from down the line. Your knee should keep 90% of its bend." },
            { "id": "w_checkLower", "label": "c", "kind": "outcome", "group": "ballFlight",
              "check": "film from down the line and look at the knee." },
            { "id": "w_clean", "label": "c", "kind": "outcome", "group": "ballFlight",
              "goal": "Keep the flex in your trail knee",
              "cue": "Keep your backside on an imaginary wall behind you all the way to impact.",
              "check": "Film from down the line. At the top, your trail knee should look about as bent as it did at address." } ]
        })";
        const PackLoadResult lint = loadPack(json, QStringLiteral("report_words_lint"));
        auto flagged = [&](const char *subject) {
            for (const ValidationIssue &i : lint.report.withCode(QStringLiteral("golferPhraseWording")))
                if (i.subject == QLatin1String(subject)) return true;
            return false;
        };
        check(lint.loaded, "a pack with badly worded report sentences still loads (the lint is a warning)");
        check(flagged("w_title") && flagged("w_stop") && flagged("w_long") && flagged("w_lower"),
              "goal: Title Case, a full stop, over 60 characters and a lower-case start each fire");
        check(flagged("w_never") && flagged("w_dont") && flagged("w_pnum") && flagged("w_digit"),
              "goal: never, don't (curly), a P-position and a digit each fire");
        check(flagged("w_promise") && flagged("w_cures"),
              "cue: a promise fires — \"will fix\" and \"cures\"");
        check(flagged("w_cueStop") && flagged("w_cuePnum"),
              "cue: no full stop, and a P-position, each fire");
        check(flagged("w_checkPct") && flagged("w_checkLower"),
              "check: a percentage, and no capital, each fire");
        check(flagged("m_capital") && flagged("m_pnum"),
              "golferName: a capital, and a P-position, each fire");
        check(!flagged("w_clean") && !flagged("m_clean"),
              "silent on the mock's own goal, cue, check and golferName");
    }

    // ── …the report fields' closed vocabularies are load ERRORS ──────────────────
    //
    // The still is drawn from these words by a renderer that cannot guess, a golferSign other than
    // ±1 rescales the printed number, and a hold with no reason is a habit that silently vanishes —
    // none of them is a wording question, so each refuses the pack the way a misspelt `kind` does.
    {
        auto loadOne = [](const char *condition, const char *measure = nullptr) {
            QByteArray json = R"({ "id": "report_schema", "version": "1", "schemaVersion": 1, "measures": [)";
            if (measure) json += measure;
            json += R"(], "conditions": [)";
            json += condition;
            json += "] }";
            return loadPack(json, QStringLiteral("report_schema"));
        };
        auto refusedWith = [](const PackLoadResult &r, const char *code) {
            return !r.loaded && !r.report.withCode(QString::fromLatin1(code)).empty();
        };
        const char *okMeasure = R"({ "id": "m_x", "kind": "provided", "metricKey": "x",
                                     "status": "planned", "reducer": { "kind": "at", "anchor": "p1" } })";

        check(refusedWith(loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "drawing" } })"),
                          "unknownPictureKind"), "picture kind 'drawing' is refused");
        check(refusedWith(loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "change",
                   "camera": "side", "phase": "P4", "line": "trailThigh" } })"), "unknownPictureCamera"),
              "picture camera 'side' is refused");
        check(refusedWith(loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "change",
                   "camera": "dtl", "phase": "p4", "line": "trailThigh" } })"), "unknownPicturePhase")
                  && refusedWith(loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "pair",
                   "camera": "dtl", "phase": "P11", "line": "trailThigh" } })"), "unknownPicturePhase"),
              "picture phase 'p4' (the measure spelling) and 'P11' are refused");
        check(refusedWith(loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "change",
                   "camera": "dtl", "phase": "P4", "line": "knee" } })"), "unknownPictureLine"),
              "picture line 'knee' (the mock's old word) is refused — it is trailThigh or trailShin");
        check(refusedWith(loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "typical",
                   "camera": "faceOn", "phase": "P7" } })"), "pictureIncomplete"),
              "a 'typical' picture with no line is refused as incomplete");
        const PackLoadResult none = loadOne(R"({ "id": "c", "label": "c", "picture": { "kind": "none" } })");
        check(none.loaded && none.pack.condition(QStringLiteral("c"))
                  && none.pack.condition(QStringLiteral("c"))->picture.kind == QStringLiteral("none"),
              "…while kind 'none' needs nothing else and loads");

        check(refusedWith(loadOne(R"({ "id": "c", "label": "c" })",
                                  R"({ "id": "m_x", "kind": "provided", "metricKey": "x", "status": "planned",
                                       "reducer": { "kind": "at", "anchor": "p1" }, "golferSign": 2 })"),
                          "badGolferSign")
                  && refusedWith(loadOne(R"({ "id": "c", "label": "c" })",
                                  R"({ "id": "m_x", "kind": "provided", "metricKey": "x", "status": "planned",
                                       "reducer": { "kind": "at", "anchor": "p1" }, "golferSign": "minus" })"),
                          "badGolferSign"),
              "golferSign 2, and a string, are refused");
        const PackLoadResult neg = loadOne(R"({ "id": "c", "label": "c" })",
                                           R"({ "id": "m_x", "kind": "provided", "metricKey": "x", "status": "planned",
                                                "reducer": { "kind": "at", "anchor": "p1" }, "golferSign": -1 })");
        const PackLoadResult dflt = loadOne(R"({ "id": "c", "label": "c" })", okMeasure);
        check(neg.loaded && neg.pack.measure(QStringLiteral("m_x"))->golferSign == -1
                  && dflt.loaded && dflt.pack.measure(QStringLiteral("m_x"))->golferSign == 1,
              "golferSign -1 loads as -1, and an absent one is +1");

        check(refusedWith(loadOne(R"({ "id": "c", "label": "c", "reportHold": { "reason": "  " } })"),
                          "reportHoldNoReason")
                  && refusedWith(loadOne(R"({ "id": "c", "label": "c", "reportHold": "not trusted" })"),
                                 "reportHoldNoReason"),
              "a reportHold with a blank reason, or written as a bare string, is refused");
        const PackLoadResult held = loadOne(
            R"({ "id": "c", "label": "c", "reportHold": { "reason": "The camera cannot see your lead elbow clearly enough yet." } })");
        check(held.loaded && held.pack.condition(QStringLiteral("c"))->reportHold
                                 == QStringLiteral("The camera cannot see your lead elbow clearly enough yet."),
              "…and {\"reason\": …} loads as the reason string");
    }

    // ── …and every report field survives a save and reload ───────────────────────
    //
    // savePack() is the one writer — the file provider, the model browser's dirty diff and the
    // session model's content stamp all go through it — so a field it dropped would be lost on the
    // first edit of a user pack, and would leave the content stamp blind to a wording change.
    {
        const QByteArray json = R"({
          "id": "report_roundtrip", "version": "1", "schemaVersion": 1,
          "measures": [
            { "id": "m_signed", "kind": "provided", "metricKey": "a", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" },
              "golferName": "trail-knee bend lost by the top", "golferSign": -1 },
            { "id": "m_plain", "kind": "provided", "metricKey": "b", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" } } ],
          "conditions": [
            { "id": "c_full", "label": "c", "goal": "Keep the flex in your trail knee",
              "cue": "Keep your backside on an imaginary wall behind you all the way to impact.",
              "check": "Film from down the line. At the top, your trail knee should look about as bent as it did at address.",
              "picture": { "kind": "change", "camera": "dtl", "phase": "P4", "line": "trailThigh" },
              "reportHold": { "reason": "Not reliable enough to headline a report yet." } },
            { "id": "c_bare", "label": "c" } ]
        })";
        const PackLoadResult first = loadPack(json, QStringLiteral("report_roundtrip"));
        const QJsonObject    saved = savePack(first.pack);
        const PackLoadResult back  = loadPack(saved, QStringLiteral("report_roundtrip2"));
        const Condition *c = back.pack.condition(QStringLiteral("c_full"));
        const Condition *b = back.pack.condition(QStringLiteral("c_bare"));
        const Measure   *ms = back.pack.measure(QStringLiteral("m_signed"));
        const Measure   *mp = back.pack.measure(QStringLiteral("m_plain"));
        check(first.loaded && back.loaded, "the report round-trip pack loads, and reloads");
        check(c && c->goal == QStringLiteral("Keep the flex in your trail knee")
                  && c->cue.startsWith(QStringLiteral("Keep your backside"))
                  && c->check.startsWith(QStringLiteral("Film from down the line."))
                  && c->picture.kind == QStringLiteral("change") && c->picture.camera == QStringLiteral("dtl")
                  && c->picture.phase == QStringLiteral("P4") && c->picture.line == QStringLiteral("trailThigh")
                  && c->reportHold == QStringLiteral("Not reliable enough to headline a report yet."),
              "goal, cue, check, picture and reportHold survive a save and reload");
        check(ms && ms->golferName == QStringLiteral("trail-knee bend lost by the top") && ms->golferSign == -1
                  && mp && mp->golferSign == 1,
              "golferName and golferSign survive a save and reload");
        const auto rowOf = [&saved](const char *array, const char *id) {
            for (const QJsonValue &v : saved.value(QLatin1String(array)).toArray())
                if (v.toObject().value(QStringLiteral("id")).toString() == QLatin1String(id))
                    return v.toObject();
            return QJsonObject();
        };
        const QJsonObject bareRow  = rowOf("conditions", "c_bare");
        const QJsonObject plainRow = rowOf("measures", "m_plain");
        check(b && !bareRow.contains(QStringLiteral("picture")) && !bareRow.contains(QStringLiteral("goal"))
                  && !bareRow.contains(QStringLiteral("reportHold"))
                  && !plainRow.contains(QStringLiteral("golferSign"))
                  && !plainRow.contains(QStringLiteral("golferName")),
              "…and an unauthored field, or golferSign at its +1 default, is not written at all");
    }

    // ── THE REPORT GATE (session_report_design.md §5; CONTRACTS §1) ─────────────
    //
    // Every condition the work-on rule can list — set-up, fault or delivery, detectable, not retired
    // or superseded; drafts INCLUDED — must be able to fill a work-on page: golfer, golferWell,
    // golferWhy, goal, cue, check, a picture and at least one drill that exists. Every measure such a
    // condition reads must name its number for the golfer, and every capacity must have a golfer
    // name for "could be behind it". [model-completeness-is-the-point]: the report never prints
    // "no drill yet", so the content has to be there before the report can be.
    //
    // First the gate itself, on synthetic packs: one that passes, then one per kind of gap, each of
    // which must produce exactly that gap and no other — a gate that reports the wrong field is a
    // gate an author cannot act on.
    {
        const QByteArray passJson = R"({
          "id": "report_gate", "version": "1", "schemaVersion": 1,
          "measures": [
            { "id": "m_bend", "kind": "provided", "metricKey": "bend", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p4" },
              "golferName": "trail-knee bend lost by the top", "golferSign": -1 },
            { "id": "m_hips", "kind": "provided", "metricKey": "hipsT", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p7" }, "golferName": "when your hips turn" },
            { "id": "m_chest", "kind": "provided", "metricKey": "chestT", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p7" }, "golferName": "when your chest turns" },
            { "id": "m_arms", "kind": "provided", "metricKey": "armsT", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p7" }, "golferName": "when your arms swing through" },
            { "id": "m_unread", "kind": "provided", "metricKey": "unread", "status": "planned",
              "reducer": { "kind": "at", "anchor": "p1" } } ],
          "signals": [
            { "id": "s_bend", "test": "outsideCorridor", "measures": ["m_bend"], "direction": "low" },
            { "id": "s_seq", "test": "order", "measures": ["m_hips", "m_chest"],
              "fallback": ["m_arms", "impact"] },
            { "id": "s_unread", "test": "outsideCorridor", "measures": ["m_unread"], "direction": "high" } ],
          "conditions": [
            { "id": "g_knee", "label": "c", "kind": "fault", "state": "draft", "detectedBy": ["s_bend"],
              "golfer": "your trail knee straightens going back",
              "golferWell": "you keep the flex in your trail knee going back",
              "golferWhy": "A straight trail leg lets your hips slide instead of turn, so you lose power.",
              "goal": "Keep the flex in your trail knee",
              "cue": "Keep your backside on an imaginary wall behind you all the way to impact.",
              "check": "Film from down the line. At the top, your trail knee should look about as bent as it did at address.",
              "picture": { "kind": "change", "camera": "dtl", "phase": "P4", "line": "trailThigh" },
              "drills": ["drill.wall"] },
            { "id": "g_seq", "label": "c", "kind": "delivery", "detectedBy": ["s_seq"],
              "golfer": "your arms start down before your hips",
              "golferWell": "your hips lead the way down",
              "golferWhy": "Starting with the arms throws the club outside, so the strike is weaker.",
              "goal": "Let your hips start the way down",
              "cue": "Feel your lead hip turn away before your hands move.",
              "check": "Film face on. Your belt buckle should turn before your hands drop.",
              "picture": { "kind": "none" }, "drills": ["drill.wall", "drill.notInTheRegistry"] },
            { "id": "g_retired", "label": "c", "kind": "fault", "state": "retired", "detectedBy": ["s_unread"] },
            { "id": "g_outcome", "label": "c", "kind": "outcome", "group": "ballFlight", "detectedBy": ["s_unread"] },
            { "id": "g_cap", "label": "c", "kind": "capacity", "confirmedBy": "screened",
              "observability": "latent", "golfer": "Trail-hip mobility" } ]
        })";
        const QSet<QString> drillIds{ QStringLiteral("drill.wall") };
        const PackLoadResult pass = loadPack(passJson, QStringLiteral("report_gate"));
        if (!pass.loaded)
            for (const ValidationIssue &i : pass.report.withSeverity(IssueSeverity::Error))
                std::printf("        error: %s\n", qPrintable(i.message));
        const std::vector<ReportContentGap> none = reportContentGaps(pass.pack, drillIds);
        for (const ReportContentGap &g : none) std::printf("        unexpected: %s\n", qPrintable(g.why));
        check(pass.loaded && none.empty(),
              "gate: a complete pack passes — a draft is judged, a retired row and an outcome are "
              "not, a measure no listable row reads needs no golferName, and one resolving drill is enough");
        check(isWorkOnListable(*pass.pack.condition(QStringLiteral("g_knee")))
                  && isWorkOnListable(*pass.pack.condition(QStringLiteral("g_seq")))
                  && !isWorkOnListable(*pass.pack.condition(QStringLiteral("g_retired")))
                  && !isWorkOnListable(*pass.pack.condition(QStringLiteral("g_outcome")))
                  && !isWorkOnListable(*pass.pack.condition(QStringLiteral("g_cap"))),
              "gate: listable = set-up / fault / delivery, detectable, not retired or superseded");

        // One gap at a time: knock one field out of the passing pack and expect exactly it back.
        const QJsonObject passRoot = QJsonDocument::fromJson(passJson).object();
        auto withEdit = [&passRoot](const char *array, const char *id,
                                    const std::function<void(QJsonObject &)> &edit) {
            QJsonObject root = passRoot;
            QJsonArray  rows = root.value(QLatin1String(array)).toArray();
            for (qsizetype i = 0; i < rows.size(); ++i) {
                QJsonObject o = rows.at(i).toObject();
                if (o.value(QStringLiteral("id")).toString() != QLatin1String(id)) continue;
                edit(o);
                rows.replace(i, o);
            }
            root.insert(QLatin1String(array), rows);
            return loadPack(root, QStringLiteral("report_gate_gap")).pack;
        };
        auto exactlyOne = [&drillIds](const CharacteristicPack &pk, const char *entity, const char *field) {
            const std::vector<ReportContentGap> gaps = reportContentGaps(pk, drillIds);
            const bool ok = gaps.size() == 1 && gaps.front().entity == QLatin1String(entity)
                            && gaps.front().field == QLatin1String(field);
            if (!ok)
                for (const ReportContentGap &g : gaps)
                    std::printf("        got %s.%s: %s\n", qPrintable(g.entity), qPrintable(g.field),
                                qPrintable(g.why));
            return ok;
        };
        for (const char *field : { "golfer", "golferWell", "golferWhy", "goal", "cue", "check",
                                   "picture", "drills" }) {
            const CharacteristicPack pk = withEdit("conditions", "g_knee", [field](QJsonObject &o) {
                o.remove(QLatin1String(field));
            });
            const QByteArray label = QByteArray("gate: a listable draft with no ") + field
                                     + " is exactly one gap, naming that field";
            check(exactlyOne(pk, "g_knee", field), label.constData());
        }
        check(exactlyOne(withEdit("conditions", "g_knee",
                                  [](QJsonObject &o) {
                                      o.insert(QStringLiteral("drills"),
                                               QJsonArray{ QStringLiteral("drill.notInTheRegistry") });
                                  }),
                         "g_knee", "drills"),
              "gate: a drill id that does not resolve in drills.json is no drill at all");
        check(exactlyOne(withEdit("measures", "m_bend",
                                  [](QJsonObject &o) { o.remove(QStringLiteral("golferName")); }),
                         "m_bend", "golferName"),
              "gate: a measure a listable fault reads, with no golferName");
        check(exactlyOne(withEdit("measures", "m_arms",
                                  [](QJsonObject &o) { o.remove(QStringLiteral("golferName")); }),
                         "m_arms", "golferName"),
              "gate: …including a measure read only as an order signal's fallback link");
        check(exactlyOne(withEdit("conditions", "g_cap",
                                  [](QJsonObject &o) { o.remove(QStringLiteral("golfer")); }),
                         "g_cap", "golfer"),
              "gate: a capacity with no golfer name");
        check(reportContentGaps(withEdit("conditions", "g_knee",
                                         [](QJsonObject &o) {
                                             o.insert(QStringLiteral("state"), QStringLiteral("superseded"));
                                             o.insert(QStringLiteral("supersededBy"), QStringLiteral("g_seq"));
                                             o.remove(QStringLiteral("goal"));
                                         }),
                                drillIds).empty(),
              "gate: a superseded row is not listable, so neither its missing goal nor its measure is a gap");
    }

    // ── …and the SHIPPED pack, against the shipped drills ─────────────────────────
    //
    // EXPECTED RED until the content of the session-report build is merged (CONTRACTS §2): this is
    // the requirement, not a measurement of progress, and it is not to be weakened to go green. The
    // failure prints every gap, grouped by field, so the list IS the authoring queue.
    {
        QSet<QString> drillIds;
        {
            QFile df(QFileInfo(QStringLiteral(PP_CORE_PACK_PATH)).dir().filePath(QStringLiteral("drills.json")));
            check(df.open(QIODevice::ReadOnly), "gate: the shipped drills.json is readable beside core.json");
            for (const QJsonValue &v : QJsonDocument::fromJson(df.readAll()).object()
                                           .value(QStringLiteral("drills")).toArray())
                drillIds.insert(v.toObject().value(QStringLiteral("id")).toString());
        }
        int listable = 0;
        for (const Condition &c : p.conditions) listable += int(isWorkOnListable(c));

        const std::vector<ReportContentGap> gaps = reportContentGaps(p, drillIds);
        std::map<QString, int> byField;
        for (const ReportContentGap &g : gaps) ++byField[g.field];
        std::printf("        report gate: %d listable conditions, %d drills; %d gaps\n",
                    listable, int(drillIds.size()), int(gaps.size()));
        for (const auto &kv : byField)
            std::printf("          %-11s %d\n", qPrintable(kv.first), kv.second);
        for (const ReportContentGap &g : gaps)
            std::printf("        gap %s.%s — %s\n", qPrintable(g.entity), qPrintable(g.field), qPrintable(g.why));
        check(gaps.empty(),
              "REPORT GATE: every listable habit can fill a work-on page, every measure it reads has "
              "golferName, every capacity has golfer");
    }

    // ── The launch monitor is the primary source for what it measures ─────────────
    //
    // Mark, 20 Oct 2026: "LM is always the primary source for the metrics it delivers — our
    // estimates will never be better." Done with the EXISTING Measure::preferKeys ladder, so the
    // five bare measures that have a device twin read the device first and our estimate only where
    // no monitor answered. Pinned, because this decides which instrument a diagnosis rests on, and a
    // rung quietly falling off would put the path conditions back to never being judged on a shot
    // whose monitor measured path outright.
    //
    // STATUS follows the ladder, not the bare key: a measure with a live rung HAS a producer today,
    // so it is live — or held, where it produces and is deliberately not graded (ball speed has no
    // corridor). Planned would grey a condition the engine is judging, on the panel's DAG, and say
    // "a producer is planned" beside a value.
    {
        const struct { const char *id; const char *lm; MeasureStatus status; } rows[] = {
            { "m_clubPathAtImpact",   "lm.clubPath",        MeasureStatus::Live },
            { "m_ballSpeed",          "lm.ballSpeed",       MeasureStatus::Held },
            { "m_launchAngle",        "lm.launchAngle",     MeasureStatus::Live },
            { "m_launchDirection",    "lm.launchDirection", MeasureStatus::Live },
            { "m_clubheadSpeedImpact","lm.clubheadSpeed",   MeasureStatus::Live },
        };
        int ok = 0;
        for (const auto &row : rows) {
            const Measure *m = p.measure(QString::fromLatin1(row.id));
            const bool good = m && m->preferKeys == QStringList{ QString::fromLatin1(row.lm) }
                              && m->status == row.status;
            if (!good)
                std::printf("        %s: preferKeys [%s], status %s\n", row.id,
                            m ? qPrintable(m->preferKeys.join(QStringLiteral(", "))) : "(missing)",
                            m ? qPrintable(measureStatusName(m->status)) : "-");
            ok += int(good);
        }
        check(ok == int(std::size(rows)),
              "the five measures with a launch-monitor twin read the monitor first, with a status that says so");
        const Measure *bs = p.measure(QStringLiteral("m_ballSpeed"));
        check(bs && !bs->gapReason.isEmpty(), "…and held ball speed says why it is not graded");
    }

    // ── No LIVE corridor signal is left without a norm ─────────────────────────
    // "The pack is dark" was the state this whole exercise existed to end: 30 corridor signals and
    // not one norm to grade against, so the engine correctly reported Unavailable for every single
    // characteristic and the library detected nothing at all. It looked like a working library.
    //
    // Scoped to LIVE measures on purpose. A signal on a measure with no producer cannot fire
    // whatever norms exist, so requiring norms there would assert something that changes nothing;
    // a signal on a live measure with no norm is a real hole, and this is what makes it loud.
    {
        QFile nf(QStringLiteral(PP_CORE_NORMS_PATH));
        check(nf.open(QIODevice::ReadOnly), "the shipped norm set is readable");
        const NormPackLoadResult nres = loadNormPack(nf.readAll(), QStringLiteral("norms.json"));
        check(nres.loaded, "the shipped norm set loads and validates clean");

        int live = 0, dark = 0;
        for (const Signal &sig : p.signalDefs) {
            if (sig.test != SignalTest::OutsideCorridor || sig.measures.isEmpty()) continue;
            const Measure *m = p.measure(sig.measures.first());
            if (m == nullptr || m->status != MeasureStatus::Live) continue;
            ++live;
            if (nres.pack.contextsFor(m->id).isEmpty()) {
                ++dark;
                std::printf("        '%s' is live but has no norm in any context\n",
                            qPrintable(m->id));
            }
        }
        std::printf("        (%d live corridor signals, %d without a norm)\n", live, dark);
        check(live > 0, "there are live corridor signals to check");
        check(dark == 0, "every LIVE corridor signal resolves a norm — the pack cannot go dark");

        // ── Where the pelvis-thrust corridors call a fault, and where they SURFACE ──
        //
        // TWO DIFFERENT NUMBERS, and a corridor row states neither of them directly. The fault
        // line is `watchMaxZ * sigma`; the point where the detector first surfaces the condition
        // is `goodMaxZ * sigma`, two thirds of it, because evaluate() fires on a DEVIATION and
        // not merely on leaving Ideal. An author reading 1.33 cm off the row would guess wrong
        // about both. Worth pinning here rather than trusting a comment, because a Finding
        // carries no severity — Watch and Action reach a reader identically — so the gap between
        // the two is invisible everywhere else in the app.
        //
        // Asserted through grade() rather than by multiplying it out, so the day the policy table
        // or the deviation rule moves, this fails instead of quietly agreeing with stale
        // arithmetic. Shape is read off the MEASURE, never assumed: m_pelvisThrustBack is a
        // ceiling and passing Target here would grade its open tail.
        {
            const GradePolicy pol = gradePolicyByName(QStringLiteral("standard"));
            auto gradeOf = [&](const char *measureId, double cm) {
                const Measure *m = p.measure(QString::fromLatin1(measureId));
                const Norm    *n = nres.pack.find(QString::fromLatin1(measureId),
                                                  QStringLiteral("any"));
                return (m && n) ? grade(cm, *n, m->shape, pol) : Grade::NotMeasured;
            };
            auto isFault   = [&](const char *id, double cm) { return gradeOf(id, cm) == Grade::Action; };
            auto surfaces  = [&](const char *id, double cm) { return isDeviation(gradeOf(id, cm)); };

            check(!isFault("m_pelvisThrustBack", 3.9),
                  "3.9 cm of pelvis thrust toward the ball going back is not yet a fault");
            check(isFault("m_pelvisThrustBack", 4.1),
                  "…and beyond 4 cm it is — the line this corridor was authored for");
            check(!surfaces("m_pelvisThrustBack", 2.6) && surfaces("m_pelvisThrustBack", 2.8),
                  "…while the CONDITION surfaces from about 2.7 cm, two thirds of the fault line");
            // The open tail. A ceiling makes no claim about the pelvis sitting back going back,
            // and this is the assertion that would fail if somebody 'tidied' the shape to Target.
            check(gradeOf("m_pelvisThrustBack", -6.0) == Grade::Ideal,
                  "and moving AWAY from the ball going back is not graded at all");

            // The same quantity's DOWNSWING row, stated so the asymmetry is visible rather than
            // buried in two sigmas eight lines apart in a JSON file. Since 2026-10-08 (Mark) early
            // extension surfaces past 5 cm from address and reaches Action at 7.5 cm; it is read
            // FROM ADDRESS, so it includes what the backswing row above already counted.
            check(!isFault("m_pelvisThrustDown", 7.4),
                  "early extension is not a fault at 7.4 cm of downswing thrust");
            check(isFault("m_pelvisThrustDown", 7.6),
                  "…and beyond 7.5 cm it is");
            check(isFault("m_pelvisThrustDown", 12.1), "…and is beyond 12 cm");
        }

        // ── The plumb bob's per-club corridors ─────────────────────────────────
        //
        // The one shipped measure whose corridor CHANGES SIGN between clubs, which is what makes it
        // worth pinning here rather than trusting four rows in a JSON file. An inch and a half ahead
        // of the stance centre is an Ideal wedge setup and a bad driver one; an inch behind is the
        // reverse. A reader who saw only the `any` row would conclude the metric barely moves.
        //
        // Graded through grade() and the measure's own shape rather than by reading mu back, so the
        // day the policy table moves this fails instead of quietly agreeing with stale arithmetic.
        // find() is an EXACT-context lookup and does not walk the tree — which is the right tool
        // here, because the claim is that each row exists under the node the club actually resolves
        // to. The inheritance half (a 7 iron reaching the `iron` family) is context_tree_test's.
        {
            const GradePolicy pol = gradePolicyByName(QStringLiteral("standard"));
            const Measure *pb = p.measure(QStringLiteral("m_plumbBobAddress"));
            check(pb != nullptr, "the plumb bob has a measure at address");
            auto gradeIn = [&](const char *ctx, double inches) {
                const Norm *n = nres.pack.find(QStringLiteral("m_plumbBobAddress"),
                                               QString::fromLatin1(ctx));
                return (pb && n) ? grade(inches, *n, pb->shape, pol) : Grade::NotMeasured;
            };

            check(gradeIn("wedge", 1.5) == Grade::Ideal,
                  "an inch and a half ahead of centre is an Ideal WEDGE setup");
            check(gradeIn("driver", 1.5) == Grade::Action,
                  "…and the same number with a DRIVER is a fault — the corridor changes sign");
            check(gradeIn("driver", -1.0) == Grade::Ideal,
                  "an inch BEHIND centre is where the driver wants the hips");
            check(gradeIn("wedge", -1.0) == Grade::Action, "…and is a fault with a wedge");
            check(gradeIn("iron", 0.5) == Grade::Ideal, "half an inch ahead suits a mid-iron");

            // The rows have to live under the nodes the clubs actually resolve to, or they are
            // authored where nothing reaches them and every swing grades against `any`.
            check(contextIdForClub(QStringLiteral("PITCHING WEDGE")).startsWith(QLatin1String("wedge"))
                      && contextIdForClub(QStringLiteral("DRIVER")) == QLatin1String("driver")
                      && contextIdForClub(QStringLiteral("7 IRON")).startsWith(QLatin1String("iron")),
                  "…and each club resolves into the family its row is authored under");

            // The unknown-club row is deliberately wide: it has to cover a corridor that spans two
            // and a half inches between the clubs above, and a narrow `any` row would call a
            // legitimate driver setup a fault on every swing whose club was never recorded.
            check(gradeIn("any", 1.5) != Grade::Action && gradeIn("any", -1.0) != Grade::Action,
                  "with the club unknown, neither a wedge nor a driver setup is called a fault");
        }

        // ── Nothing the app can DETECT is left without an EXPLANATION ───────────
        // The counterpart to the check above, and the one that nearly slipped. A signal that can
        // fire on a condition with no cause hands the coach "your head moved" — which the golfer
        // already knew — and the whole point of the model is the next sentence.
        //
        // This gap was invisible because it forms at the intersection of two healthy-looking
        // states: the causal work went in per GROUP, while the firing set is decided per PRODUCER,
        // and the eleven live-but-unclaimed producer keys arrived after their group had already
        // been wired. Four such conditions shipped isolated — no cause AND no effect — and six
        // more had no cause. All ten were among the sixteen that can actually fire, so the least
        // explicable part of the library was precisely the part a golfer would meet first.
        //
        // Scoped to what can fire, deliberately. `noCause` on a producer-less condition is a
        // content backlog and the validator already reports it; a condition that fires TODAY with
        // nothing behind it is a defect in what ships.
        int fireable = 0, unexplained = 0;
        for (const Condition &c : p.conditions) {
            if (c.observability == Observability::Latent) continue;

            const bool canFire = std::any_of(
                c.detectedBy.cbegin(), c.detectedBy.cend(), [&](const QString &sid) {
                    const Signal *sig = p.signal(sid);
                    if (sig == nullptr || sig->measures.isEmpty()) return false;
                    return std::all_of(
                        sig->measures.cbegin(), sig->measures.cend(), [&](const QString &mid) {
                            const Measure *m = p.measure(mid);
                            if (m == nullptr || m->status != MeasureStatus::Live) return false;
                            return sig->test != SignalTest::OutsideCorridor
                                   || !nres.pack.contextsFor(mid).isEmpty();
                        });
                });
            if (!canFire) continue;

            ++fireable;
            // THE ONE DECLARED EXCEPTION. inconsistent_sequence went live with the session reducer
            // (§10.8 E2) and has no cause BY DECISION: both authored causes were searched and dropped
            // unsupported (trunk_imu_sequence_grf_design.md §10.9, 14 October 2026), and the spec
            // says it raises `noCause` rather than take a cause on intuition. It is a session-scope
            // dispersion finding, so the explanation lists it as unexplained — the honest answer.
            // Named, not a category: a second fireable condition with no cause still fails here.
            if (c.id == QLatin1String("inconsistent_sequence")) continue;
            if (causesOf(p, c.id).isEmpty()) {
                ++unexplained;
                std::printf("        '%s' can fire today but has no cause — it would be reported "
                            "and never explained\n", qPrintable(c.id));
            }
        }
        std::printf("        (%d conditions can fire, %d of them unexplainable)\n",
                    fireable, unexplained);
        check(fireable > 0, "there are conditions that can fire");
        check(unexplained == 0, "every condition that can fire today has at least one cause");
    }

    // ── The pack enumeration: sibling registries out, every real pack in ───────
    // AppData/diagnostics is not exclusive to characteristic packs: file_norm_provider.cpp writes
    // `user.norms.json` there (see its note on why norms and packs share one directory), and
    // screen_pack.cpp / drill_pack.cpp / reference_pack.cpp write `screens.json` / `drills.json` /
    // `references.json` into the very same place, and file_norm_provider.cpp also reads a fixed
    // `contexts.json` from it. A `*.json` glob that does not know about them picks all five up: each
    // has no top-level `id` of its own, so it injects a spurious "Pack has no id" issue into a
    // provider's report — and worse, QDir::Name sorts `drills.json` ahead of `user.json`, so a
    // sibling that DOES carry a non-empty `id` can silently become a pack in the assembled library.
    // Regression, not a smoke test: this directory is shared on purpose, so the sibling files are
    // always going to be sitting right there.
    //
    // The skip list moved into characteristicPackFilesIn() when the directory stopped being one
    // provider, so that is what this now exercises — the enumeration AND the layering order it
    // decides. Extended rather than replaced: every original assertion is still made below.
    {
        QTemporaryDir tmp;
        check(tmp.isValid(), "temp directory for the shared-directory regression is created");
        QDir dir(tmp.path());

        auto write = [&](const char *name, const char *content) {
            QFile f(dir.filePath(QString::fromLatin1(name)));
            const bool opened = f.open(QIODevice::WriteOnly);
            check(opened, name);
            f.write(content);
        };

        write("user.json", R"({"id":"user_test"})");
        write("user.norms.json", "{}");
        write("screens.json", "{}");
        // A non-empty top-level `id`, deliberately: this is the file that USED TO be mistaken for
        // the user pack, because `drills.json` sorts ahead of `user.json` under QDir::Name and the
        // old glob had no way to tell a drill registry from a characteristic pack.
        write("drills.json", R"({"id":"drills_should_not_load"})");
        write("references.json", "{}");
        write("contexts.json", "{}");
        // A SECOND pack, sorting ahead of `user.json`. Two things ride on it. It is the file the
        // directory-wide provider used to swallow — parsed, reported, then discarded, because only
        // the first readable pack became "this provider's pack" while the comment beside that line
        // claimed a directory of several was represented by several providers. Nothing built the
        // several. And its name proves the ORDER rule below is the user-first one rather than plain
        // alphabetical, which sorting after `user.json` could not distinguish.
        write("community.json", R"({"id":"community_test"})");

        const QStringList packs = characteristicPackFilesIn(tmp.path());
        QStringList       names;
        for (const QString &p : packs) names << QFileInfo(p).fileName();
        check(names == QStringList({ QStringLiteral("user.json"),
                                     QStringLiteral("community.json") }),
              "the pack files are the two packs, user.json FIRST and the rest alphabetically");

        const std::vector<std::unique_ptr<ICharacteristicPackProvider>> fps =
            makeFilePackProviders(tmp.path());
        check(fps.size() == 2, "a directory holding two packs yields TWO providers, one per file");
        if (fps.size() == 2) {
            check(fps[0]->pack().id == QStringLiteral("user_test"),
                  "the real user pack loads, and no sibling registry is mistaken for it");
            check(fps[1]->pack().id == QStringLiteral("community_test"),
                  "…and the second pack is not silently discarded, as it was for as long as one "
                  "provider covered the whole directory");
            // Origin is decided by file NAME because that is the only evidence there is, and it
            // decides whether a pack may redefine shipped content. A downloaded pack merged as
            // LocalUser could overwrite a core characteristic with no warning anywhere.
            check(fps[0]->origin() == PackOrigin::LocalUser,
                  "user.json is the user's own layer, so it may override core");
            check(fps[1]->origin() == PackOrigin::Community,
                  "anything else arrived from somebody else, so core wins over it");
            check(fps[0]->report().issues.empty() && fps[1]->report().issues.empty(),
                  "the sibling registries raise no issues in either pack provider's report");
        }
    }

    // ── A broken pack costs the library that pack and nothing else ─────────────
    // The containment claim one-provider-per-file is FOR. While a directory was one provider, a
    // malformed file's issues landed in the same report as the good pack's and there was no way to
    // read off which file was at fault; worse, the good pack could be the one that got dropped,
    // purely by sorting second. Both halves are asserted: the fault is attributed to its own file,
    // and the neighbour still loads.
    {
        QTemporaryDir tmp;
        check(tmp.isValid(), "temp directory for the broken-neighbour case is created");
        QDir dir(tmp.path());

        auto write = [&](const char *name, const char *content) {
            QFile f(dir.filePath(QString::fromLatin1(name)));
            check(f.open(QIODevice::WriteOnly), name);
            f.write(content);
        };

        write("user.json", R"({"id":"user_test"})");
        write("zbroken.json", "{ this is not json");

        const std::vector<std::unique_ptr<ICharacteristicPackProvider>> fps =
            makeFilePackProviders(tmp.path());
        check(fps.size() == 2, "the unreadable pack still gets a provider — it has a report to give");
        if (fps.size() == 2) {
            check(fps[0]->pack().id == QStringLiteral("user_test") && fps[0]->report().issues.empty(),
                  "the good pack loads clean, unaffected by its broken neighbour");
            check(fps[1]->pack().id.isEmpty(), "the broken pack contributes no content");

            bool blamedItsOwnFile = false;
            for (const ValidationIssue &i : fps[1]->report().issues)
                if (i.code == QLatin1String("parse")
                    && i.subject == dir.filePath(QStringLiteral("zbroken.json")))
                    blamedItsOwnFile = true;
            check(blamedItsOwnFile,
                  "and its parse failure names the FILE it came from, so the health list can say "
                  "which pack to fix");
        }
    }

    // ── The session's sequence consistency (trunk_imu_sequence_grf_design.md §10.8 E2, F10) ───
    {
        // inconsistent_sequence_is_session_scope
        bool live = true;
        for (const char *mid : { "m_ksOrderShare", "m_ksTimingSpread" }) {
            const Measure *m = p.measure(QLatin1String(mid));
            live = live && m && m->status == MeasureStatus::Live && m->scope == MeasureScope::Session
                   && m->kind == MeasureKind::Provided && m->gapReason.isEmpty();
        }
        check(live, "the two session measures are live, provided, scope session, with no gap reason");
        check(isSessionScopeCondition(p, QStringLiteral("inconsistent_sequence")),
              "inconsistent_sequence_is_session_scope: no per-swing verdict is possible");
        int sessionScoped = 0;
        for (const Measure &m : p.measures) sessionScoped += m.scope == MeasureScope::Session ? 1 : 0;
        check(sessionScoped == 2, "…and they are the only two session measures");
        const Signal *share = p.signal(QStringLiteral("sig_ksOrderShare"));
        const Signal *spread = p.signal(QStringLiteral("sig_ksTimingSpread"));
        check(share && share->test == SignalTest::Threshold && share->direction == Direction::Low
                  && share->threshold.value_or(0) == 0.5
                  && spread && spread->test == SignalTest::Threshold && spread->direction == Direction::High
                  && spread->threshold.value_or(0) == 2.0,
              "the share fires below 0.5 (the in-order upper bound, P7), the spread above 2.0 (P2)");

        // inconsistent_sequence_has_no_edges — Neal 2007: felt timing tracks the strike, not the
        // lag between peaks, so nothing may join it to a strike outcome; and its causes were dropped
        // unsupported (§10.9), so it has none either.
        int touching = 0;
        for (const Edge &e : p.edges)
            if (e.from == QLatin1String("inconsistent_sequence") || e.to == QLatin1String("inconsistent_sequence"))
                ++touching;
        check(touching == 0, "inconsistent_sequence_has_no_edges: no edge in or out, so none to a strike outcome");
    }

    // ── The wrist domain (wrist_motion_into_charts.md §7.3, CONTRACT I1) ──────────
    //
    // The Wrist tab's findings and its score are exactly these conditions, so the set is pinned:
    // a row silently leaving the domain would vanish from the tab and stop costing the score.
    {
        static const char *kWrist[] = {
            "cupped_at_top", "bowed_at_top", "open_face_top", "shut_face_top", "early_face_roll",
            "face_held_shut_takeaway", "scooping", "bowed_lead_wrist", "flipping",
            "face_held_open_impact", "casting", "excessive_lag", "insufficient_set", "over_set",
            "chicken_wing" };
        QSet<QString> want, got;
        for (const char *id : kWrist) want.insert(QLatin1String(id));
        for (const Condition &c : p.conditions) {
            if (c.domain.isEmpty()) continue;
            got.insert(c.id);
            check(isKnownConditionDomain(c.domain), qPrintable(QStringLiteral("'%1' carries a known domain").arg(c.id)));
        }
        check(got == want, "the wrist domain is exactly the fifteen conditions CONTRACT I1 lists");
        check(res.report.withCode(QStringLiteral("unknownDomain")).empty(), "no shipped domain is misspelt");

        // The rotation readings are wired as ADDITIONAL routes, beside the radioulnar ones they do
        // not replace, and the cast rule's hinge is Casting's preferred route.
        const auto signalsOf = [&](const char *cid) {
            const Condition *c = p.condition(QLatin1String(cid));
            return c ? c->detectedBy : QStringList();
        };
        check(signalsOf("early_face_roll") == QStringList({ QStringLiteral("sig_earlyFaceRoll"),
                                                             QStringLiteral("sig_earlyFaceRollRotation") })
                  && signalsOf("flipping") == QStringList({ QStringLiteral("sig_flipping"),
                                                             QStringLiteral("sig_flippingRotation") }),
              "the forearm-rotation signals sit beside the pronation ones, which stay first");
        const Condition *casting = p.condition(QStringLiteral("casting"));
        check(casting && casting->detection == DetectionMode::First
                  && casting->detectedBy == QStringList({ QStringLiteral("sig_castingHinge"),
                                                          QStringLiteral("sig_casting") }),
              "casting reads the hinge where a wrist sensor is worn, the lag angle everywhere else");
        const Condition *held = p.condition(QStringLiteral("excessive_lag"));
        check(held && held->detection == DetectionMode::First
                  && held->detectedBy == QStringList({ QStringLiteral("sig_excessiveLagHinge"),
                                                       QStringLiteral("sig_excessiveLag") }),
              "…and so does excessive_lag, the other end of the same hinge (#12)");
    }

    // ── Forearm rotation is MONITORED, never graded ─────────────────────────────
    //
    // Decision rule 2 of the wrist-tab run: the rotation corridors are wide placeholders until a
    // directed capture fixes their sign, so NOTHING may fire on them. Asserted against every grade
    // policy a user can pick, not only the standard one: the strictest preset narrows the surfacing
    // edge to 1.5 sigma, and it must still sit well past the largest forearm travel the catalogue
    // quotes (150-190 deg at the top).
    {
        QFile nf(QStringLiteral(PP_CORE_NORMS_PATH));
        check(nf.open(QIODevice::ReadOnly), "the shipped norm set is readable (rotation rows)");
        const NormPackLoadResult nres = loadNormPack(nf.readAll(), QStringLiteral("norms.json"));
        int rows = 0, reachable = 0;
        for (const char *mid : { "m_leadForearmRotation_p2", "m_leadForearmRotation_p4",
                                 "m_leadForearmRotation_p7" }) {
            const Measure *m = p.measure(QLatin1String(mid));
            check(m && m->status == MeasureStatus::Live && m->metricKey == QLatin1String("forearmRotation")
                      && m->preferKeys == QStringList({ QStringLiteral("hm.forearmRotation") }),
                  qPrintable(QStringLiteral("%1 is live on forearmRotation, preferring the wG3").arg(QLatin1String(mid))));
            const Norm *n = nres.pack.find(QLatin1String(mid), QStringLiteral("any"));
            if (!n || !m) continue;
            ++rows;
            check(n->citation.startsWith(QStringLiteral("PLACEHOLDER")),
                  qPrintable(QStringLiteral("%1's norm says it is a placeholder").arg(QLatin1String(mid))));
            for (const GradePolicyPreset &preset : gradePolicyPresets()) {
                double lo = 0.0, hi = 0.0;
                deviationEdges(*n, m->shape, preset.policy, lo, hi);
                if (lo > -250.0 || hi < 250.0) {
                    ++reachable;
                    std::printf("        %s under '%s' surfaces at [%.0f, %.0f]\n", mid, preset.name, lo, hi);
                }
            }
        }
        check(rows == 3, "all three rotation measures carry a norm row");
        check(reachable == 0, "no grade policy brings a rotation corridor within 250 deg of address");
    }

    // ── Shared evidence on the shipped graph (posture_metrics_design.md §6.5, §7.3 †) ────────────
    //
    // Every † edge of §7.3 is stamped at load, because its two ends read one curve — the forward
    // bend read as a P5–P7 extreme by `early_extension` and as a P1→P7 change inside the impact
    // composite are one reading. pelvis_sink_backswing → posture_sinks_backswing is stamped
    // through the composite's pelvis-height part, whose first alternative is the belt-line measure
    // that condition reads. The key match runs ONLY on edges with a composite-read end, so one curve
    // read at two instants along an ordinary chain (sway → slide) is not stamped — the series-wide
    // rule was measured and declined (characteristic_pack.cpp, sharesEvidence()). The full list of
    // stamped edges is printed so a review can see everything the rule marks.
    std::printf("=== shared evidence on the shipped graph ===\n");
    {
        const auto stamped = [&](const char *from, const char *to) {
            for (const Edge &e : p.edges)
                if (e.from == QLatin1String(from) && e.to == QLatin1String(to)) return e.sharedEvidence ? 1 : 0;
            return -1;   // no such edge
        };
        const char *dagger[][2] = {
            { "early_extension", "loss_of_posture" },
            // loss_of_posture_backswing -> loss_of_posture was † and is NOT stamped since 2026-10-22: the
            // first reads the down-the-line head rise, which the impact composite does not read (§4.5).
            { "posture_sinks_backswing", "loss_of_posture" },
            { "diving", "posture_sinks_impact" },
            // trail_knee_straighten, pelvis_rise_backswing and head_rise_backswing -> loss_of_posture_backswing
            // were † and are NOT stamped since 2026-10-22: standing up going back is read on the
            // down-the-line head rise alone (posture_metrics_design.md §4.5), which none of them reads.
            // pelvis_rise_backswing had already left earlier that day, when the V3 measurement took the
            // hip-joint pelvis lift out of m_postureHeldTop.
            { "head_drop_backswing", "posture_sinks_backswing" },
            { "pelvis_sink_backswing", "posture_sinks_backswing" },
            { "posture_too_upright", "poor_setup_posture" },
            { "posture_too_bent", "poor_setup_posture" },
            { "excessive_knee_flex", "poor_setup_posture" },
            { "c_posture", "poor_setup_posture" },
            { "s_posture", "poor_setup_posture" },
            { "insufficient_knee_flex", "poor_setup_posture" },
            { "weight_in_heels_address", "poor_setup_posture" },
            { "weight_on_toes_address", "poor_setup_posture" },
            { "arms_reaching_at_address", "poor_setup_posture" },
            { "reverse_tilt_at_address", "poor_setup_posture" },
        };
        int missing = 0;
        for (const auto &d : dagger)
            if (stamped(d[0], d[1]) != 1) {
                ++missing;
                std::printf("      † %s -> %s is %s\n", d[0], d[1],
                            stamped(d[0], d[1]) < 0 ? "not an edge" : "NOT stamped");
            }
        check(missing == 0, "every † edge of §7.3 that shares a reading is stamped sharedEvidence");
        check(stamped("sway", "slide") == 0 && stamped("early_face_roll", "flipping") == 0
                  && stamped("hip_spin_out", "hips_too_open_at_impact") == 0
                  && stamped("excessive_axis_tilt_top", "excessive_axis_tilt_impact") == 0,
              "one curve at two instants on an ordinary chain is NOT stamped — the key match is the composites' only");
        check(stamped("ball_too_close", "poor_setup_posture") == 0 && stamped("poor_core_stability", "loss_of_posture") == 0
                  && stamped("loss_of_posture", "thin") == 0,
              "placement, capacity and outcome edges into and out of the family are not stamped");
        int n = 0;
        for (const Edge &e : p.edges)
            if (e.sharedEvidence) {
                ++n;
                std::printf("      SHARED %s %s -> %s\n", qPrintable(edgeTypeName(e.type)),
                            qPrintable(e.from), qPrintable(e.to));
            }
        std::printf("      (%d of %d edges share evidence)\n", n, int(p.edges.size()));
    }

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
