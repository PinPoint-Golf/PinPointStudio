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

// A CONDITION'S ROLE (src/Coaching/condition_role_pack.h) — the one projection of the pack's kind and
// hold rule that the work-on list, Home's areas, the shot outcome composer, the session report and
// Home → Your swing read (audit C-06, 14 Nov 2026).
//
//   1. PARITY on the shipped pack: for every condition core.json authors, Your swing's "a fault to
//      work on" (themeConditionInfo().fault) is the work-on list's (workOnClassFor() == Movement),
//      and "what you do well" never praises what the list cannot hold (held); since 14 Nov 2026 it
//      may praise an unheld set-up (Mark: "yes you can praise setup").
//   2. The other four projections are unchanged: every kind, held or not, gives the class, layer,
//      report kind and hold the readers used to compute themselves (the old tables, written out).
//   3. The shipped pack's own cases: a held fault (chicken_wing) is in none of Your swing's lists; an
//      unheld Setup habit can be needs work and praised; the same set-up held is neither.

#include "../../Coaching/condition_role_pack.h"
#include "../../Coaching/work_ons_pack.h"
#include "../../Coaching/swing_areas_pack.h"
#include "../../Coaching/shot_outcomes_pack.h"
#include "../../Coaching/session_report.h"
#include "../../Diagnostics/packs/swing_themes_pack.h"

#include <QFile>

#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// ── The tables each reader used before the role (copied from the code it replaced) ─────────────

static WorkOnClass legacyWorkOnClass(const Condition &c)           // work_ons_pack.h
{
    if (!c.reportHold.isEmpty()) return WorkOnClass::Excluded;
    switch (c.kind) {
    case ConditionKind::Fault:
    case ConditionKind::Setup:    return WorkOnClass::Movement;
    case ConditionKind::Delivery: return WorkOnClass::Delivery;
    default: break;
    }
    return WorkOnClass::Excluded;
}

static ShotConditionLayer legacyLayer(ConditionKind kind)          // shot_outcomes_pack.h
{
    switch (kind) {
    case ConditionKind::Outcome:  return ShotConditionLayer::Ball;
    case ConditionKind::Delivery: return ShotConditionLayer::Club;
    case ConditionKind::Fault:    return ShotConditionLayer::Swing;
    default: break;
    }
    return ShotConditionLayer::Root;
}

static report::FactKind legacyFact(ConditionKind kind)              // session_report.h
{
    using report::FactKind;
    switch (kind) {
    case ConditionKind::Fault:    return FactKind::Fault;
    case ConditionKind::Setup:    return FactKind::Setup;
    case ConditionKind::Delivery: return FactKind::Delivery;
    case ConditionKind::Outcome:  return FactKind::Outcome;
    case ConditionKind::Capacity: return FactKind::Capacity;
    default:                      return FactKind::Other;
    }
}

int main()
{
    std::printf("condition_role_test\n");

    // ── 2. Every kind, held and not: the four other readers' answers are today's ─────────────────
    {
        const ConditionKind kinds[] = { ConditionKind::Fault,    ConditionKind::Setup,  ConditionKind::Delivery,
                                        ConditionKind::Outcome,  ConditionKind::Capacity, ConditionKind::Intent,
                                        ConditionKind::Equipment };
        bool workOn = true, layer = true, fact = true, held = true, outcome = true, praise = true, themes = true;
        for (ConditionKind k : kinds)
            for (bool h : { false, true }) {
                Condition c;
                c.id = QStringLiteral("c");
                c.kind = k;
                if (h) c.reportHold = QStringLiteral("not measured reliably");
                const ConditionRole r = conditionRoleFor(c);
                workOn  = workOn  && r.workOn == legacyWorkOnClass(c) && workOnClassFor(c) == legacyWorkOnClass(c);
                layer   = layer   && r.layer == legacyLayer(k);
                fact    = fact    && r.fact == legacyFact(k);
                held    = held    && r.held == h;
                outcome = outcome && r.isOutcome() == (k == ConditionKind::Outcome);
                praise  = praise  && r.praisable == ((k == ConditionKind::Fault || k == ConditionKind::Setup) && !h);
                themes  = themes  && r.isMovementWorkOn() == (legacyWorkOnClass(c) == WorkOnClass::Movement);
            }
        check(workOn,  "the work-on class (list, areas): as before — Fault/Setup movement, Delivery short list, held excluded");
        check(layer,   "the shot outcome layer: as before — held or not, Setup and the rest are Root");
        check(fact,    "the report's kind: as before, held or not");
        check(held,    "held is exactly reportHold");
        check(outcome, "an outcome is exactly ConditionKind::Outcome (the areas' and the composer's test)");
        check(praise,  "praisable: a Fault or Setup that is not held, nothing else (Setup since 14 Nov 2026)");
        check(themes,  "a fault to work on is the list's Movement class, held never");
    }

    // ── The shipped pack ─────────────────────────────────────────────────────────────────────────
    QFile pf(QStringLiteral(PP_CORE_PACK_PATH));
    if (!pf.open(QIODevice::ReadOnly)) {
        std::printf("  [FAIL] cannot open %s\n", PP_CORE_PACK_PATH);
        return 1;
    }
    const PackLoadResult loaded = loadPack(pf.readAll(), QStringLiteral("core.json"));
    check(loaded.parsed, "core.json parses");
    const CharacteristicPack &pack = loaded.pack;
    check(!pack.conditions.empty(), "core.json authors conditions");

    // ── 1. Parity: Your swing and Work-ons answer "can this be a fault to work on" alike ─────────
    const QHash<QString, ThemeConditionInfo> themeInfo = themeConditionInfo(pack, DrillSet{});
    const QHash<QString, AreaConditionInfo>  areaInfo  = areaConditionInfoForPack(pack);
    const report::ReportPack                 reportPk  = report::reportPackOf(pack);
    const ShotOutcomePack                    shotPk    = shotOutcomePackFor(&pack);
    int disagree = 0, praiseBeyondList = 0, readers = 0, held = 0, setupWorkOns = 0;
    for (const Condition &c : pack.conditions) {
        const bool listed = workOnClassFor(c) == WorkOnClass::Movement;
        const auto ti = themeInfo.constFind(c.id);
        if (ti == themeInfo.constEnd() || ti->fault != listed) {
            if (++disagree <= 5)
                std::printf("        %s: Your swing says %s, Work-ons says %s\n", qPrintable(c.id),
                            ti == themeInfo.constEnd() ? "absent" : (ti->fault ? "work on" : "not"),
                            listed ? "work on" : "not");
        }
        if (ti != themeInfo.constEnd() && ti->praisable
            && (!listed || (c.kind != ConditionKind::Fault && c.kind != ConditionKind::Setup)))
            ++praiseBeyondList;

        // Every reader holds the one role's answer for this condition.
        const ConditionRole r = conditionRoleFor(c);
        const report::ReportCondition *rc = reportPk.condition(c.id);
        const bool ok = areaInfo.contains(c.id) && rc && rc->kind == r.fact && rc->held == r.held
                        && shotPk.conditions.contains(c.id) && shotPk.conditions.value(c.id).layer == r.layer;
        if (!ok) ++readers;
        if (r.held) ++held;
        if (c.kind == ConditionKind::Setup && listed) ++setupWorkOns;
    }
    check(disagree == 0, "parity: for every shipped condition, Your swing's fault-to-work-on is Work-ons' Movement class");
    check(praiseBeyondList == 0, "\"what you do well\" praises only an unheld Fault or Setup — never a held reading");
    check(readers == 0, "the report's kind and hold and the composer's layer are the role's, for every shipped condition");
    check(held > 0, "the shipped pack holds at least one condition back (the parity covers the hold rule)");
    check(setupWorkOns > 0, "the shipped pack has Setup habits on the work-on list (the parity covers Setup)");

    // ── 3. The cases the audit named, on the shipped pack's own conditions ──────────────────────
    // A held fault (chicken_wing, "we could not measure your lead arm after impact", when this was
    // written) and an unheld Setup habit (ball_too_far, which Mark's library fixture now lists) —
    // praisable since 14 Nov 2026 (Mark: "yes you can praise setup") — and that set-up held back.
    {
        const Condition *heldFault = nullptr, *setup = nullptr;
        for (const Condition &c : pack.conditions) {
            if (!heldFault && c.kind == ConditionKind::Fault && !c.reportHold.isEmpty()) heldFault = &c;
            if (!setup && c.kind == ConditionKind::Setup && c.reportHold.isEmpty() && !c.detectedBy.isEmpty()) setup = &c;
        }
        check(heldFault != nullptr, "the shipped pack holds a fault back");
        if (heldFault) {
            const ThemeConditionInfo ti = themeInfo.value(heldFault->id);
            std::printf("        held fault: %s\n", qPrintable(heldFault->id));
            check(workOnClassFor(*heldFault) == WorkOnClass::Excluded && !ti.fault && !ti.praisable,
                  "a held fault: not a work-on, and not needs work, a focus or praised in Your swing");
        }
        check(setup != nullptr, "the shipped pack authors a detectable Setup habit");
        if (setup) {
            const ThemeConditionInfo ti = themeInfo.value(setup->id);
            std::printf("        setup habit: %s\n", qPrintable(setup->id));
            check(workOnClassFor(*setup) == WorkOnClass::Movement && ti.fault && ti.praisable,
                  "an unheld Setup habit: a work-on, so needs work (and a focus) in Your swing, and praisable");

            // The same set-up held back, through the pack marshalling Your swing reads.
            CharacteristicPack heldPack = pack;
            for (Condition &c : heldPack.conditions)
                if (c.id == setup->id) c.reportHold = QStringLiteral("not measured reliably");
            const Condition *heldSetup = heldPack.condition(setup->id);
            const ThemeConditionInfo hi = themeConditionInfo(heldPack, DrillSet{}).value(setup->id);
            check(heldSetup && workOnClassFor(*heldSetup) == WorkOnClass::Excluded && !hi.fault && !hi.praisable,
                  "a held Setup: not a work-on, and not needs work, a focus or praised in Your swing");
        }
    }

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
