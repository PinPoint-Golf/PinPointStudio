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

#pragma once

#include "../packs/characteristic_pack.h"

#include <QString>
#include <QStringList>

#include <vector>

// WHAT A CONDITION READS: the link from a condition to the metric and the instant it grades
// (docs/design/charts_panel_traces_relations.md §6.4). The pack has always held it, three hops
// deep — Condition::detectedBy names signals, Signal::measures names measures, and a Measure names
// its catalogue key and the reducer that turns the curve into one number — but nothing walked it
// for a reader. The session panel published the measure's phase as a LABEL, and the charts mocks
// joined a fired condition to its reading by equal values, which breaks the first time two
// readings agree. This is the one walk, and SessionDiagnosticsModel's card phase is a call to it.
//
// Pure: the pack in, values out. Header-only, so every target that already links the pack (the
// app, the Diagnostics tests, the tools) has it without a new link line.
//
// THE PHASE is where the reduction LANDS: the anchor of an `at`, the window's end for a delta, a
// rate or an extremum. An extremum's peak is somewhere inside its window and the pack does not
// record where, so its phase is the end of the span searched, not the instant of the peak — the
// reducer kind is returned so a reader can say "peak before impact" rather than "at impact".
//
// THE KEY is the measure's own `metricKey`, else the first of its `preferKeys`. Which rung of the
// ladder answers is a property of the SWING (measureKeyLadder, measure_sample.cpp), so the whole
// ladder travels too; the own key is the one a chart draws, because it is the quantity the corridor
// was authored in and the one every capture that can grade it carries. A COMPOSED measure built
// from facets names no key at all: nothing in a swing produces it, there is no curve to draw, and
// the result says so (`composed`, empty `metricKey`) rather than guessing. A composed measure that
// does name a key (some are composed and resolved through one) is read from that key like any other.
//
// A COMPOSITE (posture_metrics_design.md §6) is the third case, and it follows the composed
// precedent: it has no curve of its own — its number is never drawn — so it names no key
// (`composite`, empty `metricKey` and ladder). It still has an instant: its parts are read at one
// (§4.4), so its phase and reducer are its parts' (landingMeasureOf) — P7 for the impact family, P4
// for the backswing, P1 for the setup — and a card, a work-on and a theme place it like any other.

namespace pinpoint::analysis {

struct ConditionReading {
    QString     signalId;           // the signal that reads it ("" for a bare measure lookup)
    QString     measureId;
    QString     metricKey;          // own key, else the first preferred; "" ⇒ none (composed, or not in the pack)
    QStringList keyLadder;          // every key the measure accepts, best first
    int         phase = -1;         // Phase as its enum value; -1 ⇒ the measure is not in the pack
    ReducerKind reducer = ReducerKind::At;
    bool        composed = false;   // MeasureKind::Composed
    bool        composite = false;  // MeasureKind::Composite: no key; phase and reducer are its parts'
    bool        found = false;      // the measure exists in the pack

    bool hasKey() const { return !metricKey.isEmpty(); }
};

// One measure's reading. An empty or unknown id answers found == false, phase -1.
inline ConditionReading measureReading(const CharacteristicPack &pack, const QString &measureId)
{
    ConditionReading r;
    r.measureId = measureId;
    const Measure *m = measureId.isEmpty() ? nullptr : pack.measure(measureId);
    if (!m) return r;
    r.found     = true;
    r.composed  = m->kind == MeasureKind::Composed;
    r.composite = m->kind == MeasureKind::Composite;
    r.keyLadder = measureKeyLadder(*m);
    r.metricKey = !m->metricKey.isEmpty() ? m->metricKey
                : !m->preferKeys.isEmpty() ? m->preferKeys.first() : QString();
    const Reducer &red = landingMeasureOf(pack, *m)->reducer;   // a composite lands where its parts do
    r.reducer   = red.kind;
    r.phase     = (red.kind == ReducerKind::At && red.anchor) ? int(*red.anchor) : int(red.window.second);
    return r;
}

// Every reading a condition grades, in detectedBy order, one per distinct measure (an order or a
// ratio signal reads two). Empty for a latent condition (no signals), an unknown id, or a signal
// the pack does not hold.
inline std::vector<ConditionReading> conditionReadings(const CharacteristicPack &pack, const QString &conditionId)
{
    std::vector<ConditionReading> out;
    const Condition *c = pack.condition(conditionId);
    if (!c) return out;
    QStringList seen;
    for (const QString &sid : c->detectedBy) {
        const Signal *s = pack.signal(sid);
        if (!s) continue;
        for (const QString &mid : s->measures) {
            if (seen.contains(mid)) continue;
            seen << mid;
            ConditionReading r = measureReading(pack, mid);
            r.signalId = sid;
            out.push_back(r);
        }
    }
    return out;
}

// THE SHOT'S reading: a ledger row names the measure that drove it (drivingMeasureId, the reading
// the verdict rests on when a condition has several routes), so a fired mark sits on that one. An
// empty or unmatched id falls back to the condition's first reading; a condition with none answers
// found == false.
inline ConditionReading conditionReadingFor(const CharacteristicPack &pack, const QString &conditionId,
                                            const QString &drivingMeasureId = QString())
{
    const std::vector<ConditionReading> all = conditionReadings(pack, conditionId);
    for (const ConditionReading &r : all)
        if (!drivingMeasureId.isEmpty() && r.measureId == drivingMeasureId) return r;
    return all.empty() ? ConditionReading{} : all.front();
}

} // namespace pinpoint::analysis
