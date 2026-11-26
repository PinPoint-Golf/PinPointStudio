/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// THE LAUNCH MONITOR PANEL'S CORRIDOR JOIN, on the shipped content.
//
// LmSessionModel::gradesFor() runs exactly the chain below — club string → context node →
// (metric, Impact) → measure → norm → Grade — and turns the answer into the one colour that panel
// carries. Every link is already tested somewhere; what is NOT tested anywhere else is the chain
// composed, against the real norm set, for the metrics a launch monitor actually reports. That is
// the thing that decides what a golfer sees, and it can be wrong in two directions that no compiler
// and no rendering test can catch:
//
//   · resolving at the WRONG CONTEXT. The panel is the first surface to grade a per-club corridor
//     from a club NAME rather than from a shot's declared context. Read at `full_swing`, spin rate
//     resolves the `any` row — 5000 +/- 2000 rpm, written to be too wide to ever fire — and the
//     board silently never colours anything. Everything still compiles, every unit test still
//     passes, and the feature does nothing. So the driver / iron / wedge rows are asserted to
//     actually separate: 2600 rpm is a good driver AND a badly bladed wedge, and the panel has to
//     say both.
//   · grading a metric NOBODY AUTHORED A NORM FOR. 18 of the 25 reading measures carry no norm in
//     any context, on purpose — several of them (ball speed, clubhead speed, peak height) are
//     capability rather than quality and should never carry one. The sweep below states which 7 do,
//     so authoring the 8th is a deliberate edit to a test rather than a colour that appears on a
//     golfer's board unannounced.
//
//   cmake --build build/analysis-tests --target lm_corridor_test
//   ctest --test-dir build/analysis-tests -R lm_corridor --output-on-failure

#include "../norms/context_tree.h"
#include "../../Stores/rule_data_install.h"
#include "../../DomainModel/rule_data.h"   // the packs and norms: RuleData's snapshot (refactor step 3.2)
#include "../norms/metric_corridor.h"
#include "../packs/pack_provider.h"

// The panel's own tuning, so the numbers the pack authored can be checked against it rather than
// against a second copy written here — see "one classifier, two readers" at the end of main().
#include "../../LaunchMonitor/inferred/lm_inferred_reads.h"
#include "../engine/characteristic_engine.h"     // evaluateSignal: the engine's own verdict (refactor 6.3b)
#include "../norms/norm_measure_source.h"
#include "../norms/shot_direction_bounds.h"      // the one direction rule's boundaries, off the pack

#include <QHash>
#include <QSet>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <memory>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// The panel's own resolution, in one call: what grade does `value` earn for `metricKey`, read at
// Impact, for a golfer who hit it with `club`? Mirrors gradesFor() line for line — including the
// second lookup for the Norm, which has to be keyed on the measure the CORRIDOR resolved on rather
// than on a fresh guess, or the numbers drawn and the grade applied come from different rows.
static QString gradeFor(const CharacteristicPack &pack, const INormProvider &norms,
                        const QString &club, const QString &metricKey, double value)
{
    const QString     contextId = contextIdForClub(club);
    const GradePolicy policy    = gradePolicyByName(QStringLiteral("standard"));

    const std::optional<MetricCorridor> c =
        corridorForMetricAtPhase(pack, norms, metricKey, Phase::Impact, contextId, policy);
    if (!c)
        return QString();

    const NormResolution res = norms.resolve(c->measureId, contextId);
    if (!res.found())
        return QString();

    const Grade g = res.grade(value, c->shape, policy);
    return g == Grade::NotMeasured ? QString() : gradeName(g);
}

static void checkGrade(const CharacteristicPack &pack, const INormProvider &norms,
                       const char *club, const char *metricKey, double value,
                       const char *want, const char *why)
{
    const QString got = gradeFor(pack, norms, QString::fromLatin1(club),
                                 QString::fromLatin1(metricKey), value);
    const bool    ok  = got == QLatin1String(want);
    std::printf("  [%s] %-9s %-18s %8.2f -> %-8s want %-8s  %s\n",
                ok ? "PASS" : "FAIL", club, metricKey, value,
                got.isEmpty() ? "(none)" : got.toUtf8().constData(), want, why);
    if (!ok) ++g_fail;
}

// What the BOARD does, rather than which of the four bands answered. The panel paints Watch and
// Action and nothing else, so "Ideal", "Good", "no corridor" and "implausible reading" are one
// outcome as far as a golfer is concerned — and a claim about silence should be written the way the
// silence is produced, not pinned to whichever band happens to deliver it this year.
static void checkSilent(const CharacteristicPack &pack, const INormProvider &norms,
                        const char *club, const char *metricKey, double value, const char *why)
{
    const QString got = gradeFor(pack, norms, QString::fromLatin1(club),
                                 QString::fromLatin1(metricKey), value);
    const bool    ok  = got != QLatin1String("watch") && got != QLatin1String("action");
    std::printf("  [%s] %-9s %-18s %8.2f -> %-8s uncoloured   %s\n",
                ok ? "PASS" : "FAIL", club, metricKey, value,
                got.isEmpty() ? "(none)" : got.toUtf8().constData(), why);
    if (!ok) ++g_fail;
}

int main()
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    pinpoint::installProcessRuleData();   // this process's packs and norms (refactor step 3.2)
    const std::unique_ptr<ICharacteristicPackProvider> packProv = makeCharacteristicPackProvider();
    const std::shared_ptr<const INormProvider>         norms    = sharedNormProvider();
    const CharacteristicPack                          &pack     = packProv->pack();

    std::printf("=== the launch monitor panel's corridor join ===\n\n");
    check(!pack.measures.empty(), "the shipped characteristic pack loaded");
    check(!norms->norms().norms.empty(), "the shipped norm set loaded");

    // ── which readings can be graded at all ─────────────────────────────────
    //
    // Read at a real club context, because that is the only way the panel ever asks. The list is
    // stated rather than counted: "7 of them" would still pass if one metric quietly swapped for
    // another, and WHICH readings a board is allowed to colour is the whole of the user-visible
    // behaviour here.
    std::printf("\n-- which lm.* readings carry a corridor (context: iron_7) --\n");
    QStringList gradable;
    QStringList lmKeys;
    for (const Measure &m : pack.measures) {
        if (!m.metricKey.startsWith(QLatin1String("lm.")))
            continue;
        if (lmKeys.contains(m.metricKey))
            continue;
        lmKeys << m.metricKey;
        if (corridorForMetricAtPhase(pack, *norms, m.metricKey, Phase::Impact,
                                     QStringLiteral("iron_7")))
            gradable << m.metricKey;
    }
    gradable.sort();

    const QStringList want{
        QStringLiteral("lm.attackAngle"),
        QStringLiteral("lm.carryDistance"),   QStringLiteral("lm.clubPath"),
        QStringLiteral("lm.clubheadSpeed"),   QStringLiteral("lm.faceToPath"),      QStringLiteral("lm.launchAngle"),
        QStringLiteral("lm.launchDirection"), QStringLiteral("lm.smashFactor"),
        QStringLiteral("lm.spinAxis"),        QStringLiteral("lm.spinRate"),
        QStringLiteral("lm.strikeLocation"),
    };
    check(lmKeys.size() >= 25, "the pack still describes the launch monitor's readings");
    // lm.launchAngle joined 2026-08-09: sig_launchLow/High moved onto m_lmLaunchAngle and its
    // norm rows were mirrored from m_launchAngle, so the measured reading now grades.
    //
    // lm.attackAngle joined 2026-08-12, and by a DIFFERENT ROUTE worth understanding, because it is
    // the first key here that grades without anybody authoring a norm for it. `m_attackAngle` now
    // PREFERS `lm.attackAngle` over our projected `attackAngle` (Measure::preferKeys), and the
    // metric-to-measure join walks the whole ladder — so the device's reading resolves the corridor
    // that was already authored for the quantity, mu and sigma per club, with no second row.
    //
    // That is the intended consequence rather than a side effect: it is one quantity measured two
    // ways, so one corridor is the correct number of corridors, and the board colouring the
    // measured attack angle is the thing a golfer with the device should have been getting all
    // along. It also means a norm edit can never leave the two instruments grading differently,
    // which is exactly the drift the mirrored launchAngle rows above have to be kept in step by hand
    // to avoid.
    //
    // lm.clubheadSpeed joined 2026-10-20 by the attack-angle route, and it is the one this header
    // warns about, so it is said out loud: the launch monitor became the primary source for every
    // quantity it measures (Mark's decision, for the session report), and m_clubheadSpeedImpact
    // now prefers `lm.clubheadSpeed`. That measure carries the per-club, per-cohort FLOOR the
    // speed rows were set by rule (Ideal from 5% under mu, Action from 10% under), so the board now
    // colours a slow measured clubhead the way the charts already coloured a slow estimated one. One
    // quantity, one corridor. Ball speed did NOT join: m_ballSpeed prefers `lm.ballSpeed` too, but
    // it has no norm in any context, so the tile stays uncoloured. Since #190 the join lists
    // m_lmClubheadSpeed (the reading's own measure) first; it has no norm, so the walk goes on to
    // m_clubheadSpeedImpact's floor rows, and the board grades as before. If clubhead speed should stay
    // silent on this board, the fix is in the panel (skip capability readings), not in the ladder.
    check(gradable == want,
          "exactly the eleven authored readings resolve a corridor; the rest stay silent");
    if (gradable != want)
        std::printf("       got: %s\n", gradable.join(QStringLiteral(", ")).toUtf8().constData());

    // ── the club decides the corridor, and it has to ────────────────────────
    //
    // The load-bearing assertion in this file. One spin rate, three clubs, three different verdicts:
    // if the panel ever resolved at the default context instead of the club's, every line here would
    // come back Ideal off the deliberately-inert `any` row and the board would go quiet.
    std::printf("\n-- one reading, three clubs (spin rate) --\n");
    checkGrade(pack, *norms, "Driver", "lm.spinRate", 2600, "ideal",
               "the driver row's own centre");
    checkGrade(pack, *norms, "7-iron", "lm.spinRate", 2600, "watch",
               "2.3 SD under the iron row — a bladed 7 iron, not a good drive");
    checkGrade(pack, *norms, "Sand Wedge", "lm.spinRate", 2600, "action",
               "and off a wedge it is 3.2 SD out, which is the red one");
    checkGrade(pack, *norms, "7-iron", "lm.spinRate", 6000, "ideal",
               "the iron row's own centre");
    checkGrade(pack, *norms, "Driver", "lm.spinRate", 6000, "action",
               "…and a driver spinning that hard is losing 40 yards");

    // The inverse, and the reason gradesFor() declines to grade at all when the club is unknown:
    // the `any` row is authored WIDE ENOUGH NEVER TO FIRE, so a board that fell back to it would
    // show a golfer nothing while looking like it was working. Silence is the same outcome, said
    // honestly — and the panel's rule costs nothing, which is what these two lines prove.
    std::printf("\n-- an unknown club falls to a row that grades nothing --\n");
    checkSilent(pack, *norms, "", "lm.spinRate", 2600,
                "the `any` spin row spans driver to wedge…");
    checkSilent(pack, *norms, "", "lm.spinRate", 9000,
                "…so nothing a monitor reports can ever leave it");

    // ── the two delivery corridors, and where their fault lines sit ─────────
    //
    // Both are authored as a SIGMA rather than as an explicit monitor band, so the numbers below are
    // `watchMaxZ * sigma` and not something written down anywhere. That is deliberate — monitorLo /
    // monitorHi exist only to preserve migrated content and the corridor editor cannot touch them,
    // so a row using them would be a corridor a user is locked out of adjusting. The cost is that
    // the fault line MOVES with the grade policy, which is exactly what the rest of the pack does;
    // these assertions pin it under Standard, which is what ships.
    std::printf("\n-- club path: the fault line is +/-4 degrees --\n");
    checkSilent(pack, *norms, "7-iron", "lm.clubPath", 2.5,
                "inside: a couple of degrees is a swing, not a fault");
    checkGrade(pack, *norms, "7-iron", "lm.clubPath", 3.5, "watch",
               "the run-up to the limit is amber, not silence");
    checkGrade(pack, *norms, "7-iron", "lm.clubPath", 4.5, "action",
               "past four degrees out-to-in");
    checkGrade(pack, *norms, "Driver", "lm.clubPath", -4.5, "action",
               "…and symmetrically the other way");

    // The same corridor read through the OTHER instrument. It resolves nothing today — the optical
    // producer is planned — but the rows must agree, because a corridor is a fact about the swing
    // and not about what measured it. This is the assertion that catches the two drifting apart.
    std::printf("\n-- and the optical route grades it identically --\n");
    {
        const QString ctx = contextIdForClub(QStringLiteral("7-iron"));
        const GradePolicy pol = gradePolicyByName(QStringLiteral("standard"));
        const NormResolution lm  = norms->resolve(QStringLiteral("m_lmClubPath"), ctx);
        const NormResolution opt = norms->resolve(QStringLiteral("m_clubPathAtImpact"), ctx);
        check(lm.found() && opt.found(), "both club path rows resolve");
        const bool same = lm.found() && opt.found()
                          && lm.norm->mu == opt.norm->mu
                          && lm.norm->sigmaLo == opt.norm->sigmaLo
                          && lm.norm->sigmaHi == opt.norm->sigmaHi;
        check(same, "m_lmClubPath and m_clubPathAtImpact state the SAME corridor");
        check(lm.found() && opt.found()
              && lm.grade(4.5, Shape::Target, pol) == opt.grade(4.5, Shape::Target, pol),
              "…so one shot cannot grade two ways depending on the instrument");
    }

    // ── each reading's corridor comes from its OWN measure (#190) ───────────
    //
    // The rows above agree, so the band a golfer sees cannot tell which one the board read. The
    // corridor editor can: an edit to the launch monitor's own measure has to move the board's band,
    // and before #190 an estimate's measure that merely listed the reading on its ladder
    // (m_clubPathAtImpact prefers `lm.clubPath`) answered first, by pack order. The ids are written
    // out here, not read back from the join, because the join is what is under test. A reading whose
    // own measure carries no norm still falls through to the estimate's row: clubhead speed is the case.
    std::printf("\n-- each lm.* reading's corridor comes from its own measure --\n");
    {
        const QString ctx = contextIdForClub(QStringLiteral("7-iron"));
        const auto from = [&](const char *key, const char *want, const char *why) {
            const std::optional<MetricCorridor> c =
                corridorForMetricAtPhase(pack, *norms, QString::fromLatin1(key), Phase::Impact, ctx);
            const QString got = c ? c->measureId : QStringLiteral("(none)");
            const bool    ok  = got == QLatin1String(want);
            std::printf("  [%s] %-18s -> %-22s want %-22s  %s\n", ok ? "PASS" : "FAIL", key,
                        got.toUtf8().constData(), want, why);
            if (!ok) ++g_fail;
        };
        from("lm.clubPath",       "m_lmClubPath",         "the board's own path row, not m_clubPathAtImpact's");
        from("lm.launchAngle",    "m_lmLaunchAngle",      "the row sig_launchLow/High fire on");
        from("lm.launchDirection","m_lmLaunchDirection",  "the row sig_pull/push fire on");
        from("clubPath",          "m_clubPathAtImpact",   "our own path still reads the estimate's row");
        from("lm.clubheadSpeed",  "m_clubheadSpeedImpact","m_lmClubheadSpeed has no norm, so the floor rows answer");
        const std::vector<const Measure *> path = measuresForMetricAtPhase(pack, QStringLiteral("lm.clubPath"), Phase::Impact);
        check(path.size() == 2 && path[0]->id == QLatin1String("m_lmClubPath")
                  && path[1]->id == QLatin1String("m_clubPathAtImpact"),
              "the join lists the reading's own measure first, then the measure it is a rung of");
    }

    std::printf("\n-- face to path: the fault line is +/-3 degrees --\n");
    checkSilent(pack, *norms, "7-iron", "lm.faceToPath", 1.8,
                "inside: the face and the path disagree, but not by much");
    // Sigma 1 -> 1.5 on 2026-09-14: the row's own citation put the fault line at ±3°, but with
    // the engine firing beyond 2σ a sigma of 1 fired at 2°, which graded a quarter to a third of
    // a good amateur's corpus shots as a miss. The edge now sits where the citation said.
    checkSilent(pack, *norms, "7-iron", "lm.faceToPath", 2.5,
                "two and a half degrees is inside the fault line the row itself states");
    checkGrade(pack, *norms, "7-iron", "lm.faceToPath", 3.5, "watch",
               "a face three and a half degrees across its own path — past the line");
    checkGrade(pack, *norms, "7-iron", "lm.faceToPath", 5.0, "action",
               "five degrees across the path is a slice or a hook, not a shape");

    // THE CASE THAT CANNOT BE GRADED HERE, stated so nobody later reads the silence as a bug. Both
    // of these are face 2 degrees closed to path; the first is a push-draw that finishes near the
    // target and the second is a pull-hook. A corridor keys on ONE measure, so it says the same
    // thing about both — correctly, because "the face disagrees with the path by 2 degrees" is true
    // of both. Which of them cost the golfer a shot is a question about the PAIR, and it needs
    // either a derived measure or a two-measure signal test; the engine's `detectedBy` is an OR, so
    // it cannot be an AND of two corridor signals either.
    //
    // THAT DERIVED MEASURE NOW EXISTS — `m_compoundMiss`, graded by a threshold rather than a
    // corridor — and the two lines below are still correct about face-to-path, which is why they
    // stay. What changed is that the pair is no longer unaskable, only unaskable OF THIS MEASURE.
    std::printf("\n-- the interaction a single corridor cannot see --\n");
    checkSilent(pack, *norms, "7-iron", "lm.faceToPath", -2.0,
                "path +4, face -2: a push-draw. Face to path alone cannot know that");
    checkSilent(pack, *norms, "7-iron", "lm.faceToPath", -2.0,
                "path  0, face -2: a pull-hook. Same reading, same grade, worse shot");

    // ── a one-sided measure keeps its open tail ─────────────────────────────
    //
    // Smash factor is the only Floor-shaped measure PinPoint ships, and the panel prints it on both
    // the tiles board and the headline strip. An implementation that graded it two-sided would paint
    // the best strike of a golfer's session red — which is the single most embarrassing thing this
    // feature could do.
    std::printf("\n-- smash factor is a floor, not a window --\n");
    checkGrade(pack, *norms, "Driver", "lm.smashFactor", 1.48, "ideal",
               "the driver's own centre");
    checkSilent(pack, *norms, "Driver", "lm.smashFactor", 1.54,
                "above it is better, never worse: the high side is open");
    checkGrade(pack, *norms, "Driver", "lm.smashFactor", 1.20, "action",
               "well below it is a genuinely poor strike");
    // Beyond the physical cap the norm calls the reading implausible, and gradesFor() drops it
    // rather than showing Action. A mis-tracked ball is a fault in the kit, and this panel must not
    // print a fault in the kit as a fault in the swing.
    checkSilent(pack, *norms, "Driver", "lm.smashFactor", 1.90,
                "past the restitution limit it is a bad reading, not a bad shot");

    // ── ONE CLASSIFIER, TWO READERS ─────────────────────────────────────────
    //
    // The panel names a shot "Thin" from `LmInferenceTuning`; the pack fires `thin` from a number
    // typed into core.json. They are the same judgement about the same millimetres, and until this
    // check existed nothing stopped them drifting apart — a coach could then be shown a card
    // reading "Thin" beside a diagnosis that did not mention it, on one shot, from one device.
    //
    // The C++ struct is the authoring point and the JSON follows it, rather than the other way
    // round, for the ordinary reason: the struct is where the numbers are explained, and a number
    // is only defensible next to its reason. This test is what makes the JSON copy safe.
    std::printf("\n-- the pack's thresholds match the panel's tuning --\n");
    {
        const LmInferenceTuning t;
        struct Want { const char *signalId; double value; const char *why; };
        const Want wants[] = {
            { "sig_thin",      -t.severeVMm, "the panel's fat/thin boundary, below centre" },
            { "sig_chunk",      t.severeVMm, "…and above it" },
            { "sig_shank",      t.hoselMm,   "the hosel" },
            // Not a tuning number: the compound miss is stated AS A RATIO against the two
            // boundaries, so 1.0 is the definition of the metric rather than a judgement about
            // golf. Asserted here anyway because it is the contract the producer's arithmetic
            // rests on — normalise differently and these two signals mean something else.
            { "sig_pullHook",  -1.0,         "both halves cleared their own threshold, leftward" },
            { "sig_pushSlice",  1.0,         "…and rightward" },
        };
        for (const Want &w : wants) {
            const Signal *s = pack.signal(QString::fromLatin1(w.signalId));
            const bool    ok = s != nullptr && s->test == SignalTest::Threshold
                               && s->threshold.has_value()
                               && std::abs(*s->threshold - w.value) < 1e-9;
            std::printf("  [%s] %-14s %8.2f  %s\n", ok ? "PASS" : "FAIL", w.signalId,
                        (s != nullptr && s->threshold) ? *s->threshold : 0.0, w.why);
            if (!ok) ++g_fail;
        }
    }

    // ── ONE DIRECTION RULE (refactor 6.3b, audit F-05 / G-01) ───────────────
    //
    // The card, Club delivery and the report name a shot's start and curve by lmShapeClass; the factors and
    // Tendencies by the conditions this pack fires. They are one rule only if lmShapeClass's boundaries ARE the
    // pack's and it answers as the engine does. So: the shipped defaults equal what shotDirectionBounds reads off the
    // shipped pack under the standard policy; a non-standard policy moves them; and, on a grid straddling every edge,
    // lmShapeClass's cell and severity equal what evaluateSignal fires, at both policies.
    std::printf("\n-- one direction rule: the pack's corridors --\n");
    {
        const QString ctx = contextIdForClub(QStringLiteral("7-iron"));
        const GradePolicy standard = gradePolicyByName(QStringLiteral("standard"));
        const GradePolicy strict   = gradePolicyByName(QStringLiteral("strict"));
        const ShotDirectionBounds shipped;
        const ShotDirectionBounds got = shotDirectionBounds(pack, *norms, ctx, standard);
        check(got.startLo == shipped.startLo && got.startHi == shipped.startHi, "start corridor: the defaults are the shipped pack's (±4)");
        check(got.axisLo == shipped.axisLo && got.axisHi == shipped.axisHi, "spin-axis corridor: the defaults are the shipped pack's (±10)");
        check(got.fadeFrom == shipped.fadeFrom && got.fadeTo == shipped.fadeTo
                  && got.drawFrom == shipped.drawFrom && got.drawTo == shipped.drawTo,
              "fade / draw: the defaults are the shipped signals' thresholds");
        check(got.pushSlice == shipped.pushSlice && got.pullHook == shipped.pullHook, "compound: the defaults are the shipped signals'");
        const ShotDirectionBounds tight = shotDirectionBounds(pack, *norms, ctx, strict);
        check(std::abs(tight.startHi - 2.0 * strict.goodMaxZ) < 1e-9 && std::abs(tight.axisHi - 5.0 * strict.goodMaxZ) < 1e-9,
              "the strict policy moves the corridor edges (start 3, axis 7.5)");
        check(tight.fadeFrom == shipped.fadeFrom, "…and leaves the authored fade threshold where it is (EDGE: it does not follow the policy)");

        // The engine's verdict on one shot: its readings through a NormMeasureSource at the 7 iron's context.
        struct Values final : IMeasureValueSource {
            QHash<QString, double> v;
            std::optional<Value> value(const QString &id) const override
            {
                const auto it = v.constFind(id);
                if (it == v.constEnd()) return std::nullopt;
                Value out;
                out.value = *it;
                return out;
            }
        };
        int compared = 0, disagreed = 0;
        for (const GradePolicy &policy : { standard, strict }) {
            const ShotDirectionBounds b = shotDirectionBounds(pack, *norms, ctx, policy);
            for (double start : { -6.0, -4.001, -4.0, -3.0, -2.5, -1.5, -1.0, 0.0, 1.0, 2.5, 3.0, 4.0, 4.001, 6.0 })
                for (double axis : { -12.0, -10.001, -10.0, -7.5, -6.0, -5.0, -4.001, -4.0, -2.0, 0.0, 2.0, 4.0, 4.001,
                                     5.0, 6.0, 7.5, 9.0, 10.0, 10.001, 12.0 }) {
                    Values vals;
                    vals.v.insert(QStringLiteral("m_lmLaunchDirection"), start);
                    vals.v.insert(QStringLiteral("m_spinAxis"), axis);
                    vals.v.insert(QStringLiteral("m_compoundMiss"),
                                  lmCompoundMiss(start, axis, std::optional<double>(), std::optional<double>(),
                                                 std::optional<double>()).value);
                    const NormMeasureSource src(vals, norms, ctx, policy, &pack);
                    const auto fired = [&](const char *id) { return evaluateSignal(pack, QString::fromLatin1(id), src).fired; };
                    int window = fired("sig_pull") ? 0 : fired("sig_push") ? 2 : 1;
                    int curve = 1;
                    bool severe = false;
                    if (fired("sig_slice"))      { curve = 2; severe = true; }
                    else if (fired("sig_hook"))  { curve = 0; severe = true; }
                    else if (fired("sig_fadeBeyondStraight") && fired("sig_fadeShortOfSlice")) curve = 2;   // detection: all
                    else if (fired("sig_drawBeyondStraight") && fired("sig_drawShortOfHook"))  curve = 0;
                    if (fired("sig_pushSlice"))      { window = 2; curve = 2; severe = true; }
                    else if (fired("sig_pullHook"))  { window = 0; curve = 0; severe = true; }
                    const LmShapeClass k = lmShapeClass(start, axis, std::optional<double>(), LmCurvature(), false, b);
                    ++compared;
                    if (!k.has || k.windowIdx != window || k.curveIdx != curve || k.severe != severe) {
                        ++disagreed;
                        std::printf("  [FAIL] start %.3f axis %.3f: engine (%d,%d,%d) rule (%d,%d,%d)\n", start, axis,
                                    window, curve, int(severe), k.windowIdx, k.curveIdx, int(k.severe));
                    }
                }
        }
        check(compared > 500 && disagreed == 0, "lmShapeClass names every shot of the grid as the engine fires it, at both policies");
        std::printf("      (%d shots, %d disagreements)\n", compared, disagreed);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail,
                g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
