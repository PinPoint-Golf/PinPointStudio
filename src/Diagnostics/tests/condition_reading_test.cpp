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

// The condition → { metricKey, phase } link (condition_reading.h) over the SHIPPED pack. Every
// condition that can fire must name the reading it grades — a key and an instant — or say that it
// is composed and has none; a latent one has no signal and reads nothing. Three known conditions
// are pinned by hand, because a walk that is merely consistent could still be consistently wrong.
// The full table prints as TABLE| lines (build/charts-build/condition_links.md is made from them).
//
//   build/charts-build/build.sh tests condition_reading_test
//   build/charts-build/build.sh ctest condition_reading

#include "../engine/condition_reading.h"
#include "../measures/measure_facets.h"

#include <QFile>

#include <cstdio>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

int main()
{
    std::printf("condition_reading_test\n");

    QFile f(QStringLiteral(PP_CORE_PACK_PATH));
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("  [FAIL] cannot open %s\n", PP_CORE_PACK_PATH);
        return 1;
    }
    const PackLoadResult res = loadPack(f.readAll(), QStringLiteral("core.json"));
    check(res.loaded, "the shipped pack loads");
    const CharacteristicPack &p = res.pack;

    // ── Every condition: a key and an instant, composed, or latent ─────────────────────────────
    {
        int keyed = 0, composedNoKey = 0, latent = 0, empty = 0, unplaced = 0, readings = 0;
        for (const Condition &c : p.conditions) {
            const std::vector<ConditionReading> rs = conditionReadings(p, c.id);
            if (c.detectedBy.isEmpty()) {
                ++latent;
                std::printf("TABLE|%s|%s|latent|||||\n", qPrintable(c.id), qPrintable(c.label));
                continue;
            }
            if (rs.empty()) { ++empty; std::printf("      %s has signals but no reading\n", qPrintable(c.id)); }
            bool anyKey = false;
            for (const ConditionReading &r : rs) {
                ++readings;
                if (!r.found || r.phase < 0) {
                    ++unplaced;
                    std::printf("      %s → %s is not in the pack\n", qPrintable(c.id), qPrintable(r.measureId));
                }
                if (r.hasKey()) anyKey = true;
                else if (r.composed) ++composedNoKey;
                std::printf("TABLE|%s|%s|%s|%s|%s|%s|%s|%s\n", qPrintable(c.id), qPrintable(c.label),
                            qPrintable(r.signalId), qPrintable(r.measureId),
                            r.hasKey() ? qPrintable(r.metricKey) : (r.composed ? "(composed: no key)" : "(no key)"),
                            r.phase >= 0 ? qPrintable(phaseLabel(Phase(r.phase))) : "-",
                            qPrintable(reducerKindName(r.reducer)),
                            qPrintable(r.keyLadder.join(QLatin1Char(','))));
            }
            if (anyKey) ++keyed;
            else if (!rs.empty() && !rs.front().composed)
                std::printf("      %s reads no key and is not composed\n", qPrintable(c.id));
        }
        std::printf("      (%d conditions: %d keyed, %d latent; %d readings, %d composed with no key)\n",
                    int(p.conditions.size()), keyed, latent, readings, composedNoKey);
        check(empty == 0, "every condition with a signal yields at least one reading");
        check(unplaced == 0, "every reading's measure is in the pack and has a phase");
        check(keyed > 0, "the shipped pack's conditions name metric keys");
        int bad = 0;
        for (const Condition &c : p.conditions) {
            if (c.detectedBy.isEmpty()) continue;
            bool ok = false;
            for (const ConditionReading &r : conditionReadings(p, c.id)) ok = ok || r.hasKey() || r.composed;
            if (!ok) ++bad;
        }
        check(bad == 0, "no detectable condition is left with neither a key nor 'composed'");
    }

    // ── The rule: an `at` lands on its anchor, everything else on its window's end ─────────────
    {
        int wrong = 0;
        for (const Measure &m : p.measures) {
            const ConditionReading r = measureReading(p, m.id);
            const Phase want = (m.reducer.kind == ReducerKind::At && m.reducer.anchor) ? *m.reducer.anchor
                                                                                      : m.reducer.window.second;
            if (!r.found || r.phase != int(want)) ++wrong;
        }
        check(wrong == 0, "every measure's phase is its `at` anchor, else its window's end");
        check(!measureReading(p, QString()).found && measureReading(p, QString()).phase == -1,
              "an empty measure id is not found, phase -1");
        check(!measureReading(p, QStringLiteral("m_noSuchMeasure")).found, "an unknown measure id is not found");
        check(conditionReadings(p, QStringLiteral("no_such_condition")).empty(), "an unknown condition reads nothing");
    }

    // ── Three known conditions, by hand ────────────────────────────────────────────────────────
    {
        // Over the top: the hand path loop, read AT the top.
        const std::vector<ConditionReading> ott = conditionReadings(p, QStringLiteral("over_the_top"));
        check(ott.size() == 1, "over the top reads one measure");
        check(!ott.empty() && ott[0].metricKey == QStringLiteral("handPathLoop"), "…handPathLoop");
        check(!ott.empty() && ott[0].phase == int(Phase::Top) && ott[0].reducer == ReducerKind::At, "…at the top (P4)");

        // Early extension: two routes, the pelvis thrust and the spine angle lost, each an extremum
        // over P5→P7, so each lands on impact.
        const std::vector<ConditionReading> ee = conditionReadings(p, QStringLiteral("early_extension"));
        check(ee.size() == 2, "early extension reads two measures");
        check(ee.size() == 2 && ee[0].metricKey == QStringLiteral("pelvisThrust")
                             && ee[1].metricKey == QStringLiteral("spineForwardBend"),
              "…pelvisThrust, then spineForwardBend");
        check(ee.size() == 2 && ee[0].phase == int(Phase::Impact) && ee[1].phase == int(Phase::Impact)
                             && ee[0].reducer == ReducerKind::Extremum,
              "…both an extremum landing on impact (P7)");
        // The shot's own reading follows the ledger's drivingMeasureId; an unknown one falls back.
        check(conditionReadingFor(p, QStringLiteral("early_extension"), QStringLiteral("m_spineBendLossDown")).metricKey
                  == QStringLiteral("spineForwardBend"),
              "a ledger row's driving measure picks its reading");
        check(conditionReadingFor(p, QStringLiteral("early_extension"), QStringLiteral("m_nothing")).metricKey
                  == QStringLiteral("pelvisThrust"),
              "…an unmatched one falls back to the first");

        // Returns steep: the shaft lie's change from address, landing on impact.
        const ConditionReading lie = conditionReadingFor(p, QStringLiteral("lie_steep_at_impact"));
        check(lie.found && lie.metricKey == QStringLiteral("shaftLie"), "returns steep reads shaftLie");
        check(lie.phase == int(Phase::Impact) && lie.reducer == ReducerKind::Delta, "…as a delta landing on impact (P7)");
    }

    std::printf("%s\n", g_fail ? "FAILED" : "all passed");
    return g_fail == 0 ? 0 : 1;
}
