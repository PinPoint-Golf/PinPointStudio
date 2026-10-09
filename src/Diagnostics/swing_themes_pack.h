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

// What the swing themes (src/Analysis/swing_themes.h) need from the diagnostics pack, marshalled
// so that no pack type reaches the arithmetic — the same split work_ons.h keeps with WorkOnClass.
// Header-only; links nothing beyond what holding a CharacteristicPack already needs — except the
// drill registry (drill_pack.cpp), which the two functions that name drills read through
// sharedDrillSet() unless the caller passes a DrillSet of its own.
//
//   themeMeasureInfo()       per measure: which way is MORE OF THE FAULT, where in the swing it
//                            is read, and its metricKey
//   themeConditionInfo()     per condition: Fault or not (only faults are listed or praised), its
//                            detection mode and prominence, and its FAMILIES — the metricKey (or
//                            measure id) of the first measure of each detectedBy signal — the
//                            root, so a "Signed" key joins its unsigned family — which is
//                            how "what you do well" keeps clear of what the other layers say —
//                            and, for the focus, its DRILL (the first of Condition::drills the
//                            registry holds) and WHEN (the earliest swing position of those first
//                            measures, by the rule below; 0s ignored)
//   themePhrases()           the golfer's words: Condition::golfer, golferWell and golferWhy,
//                            Measure::golferHigh/golferLow, each measure's family (its
//                            metricKey root) so a sentence does not say one quantity twice, and
//                            each drill's label and instruction
//
// ORIENTATION is decided in this order, and the order is the rule (tools/themes/theme_pca.py
// orientation()): a ceiling corridor says high is the fault (+1) and a floor says low is (−1);
// else, when every signal whose FIRST measure is this one watches one tail, that tail is the
// fault; else `unwatchedTail` names the benign tail, so the other one is the fault; else the
// measure is two-sided and keeps its raw sign (+1, marked).
//
// SWING POSITION is the latest P-position the reducer reads (its anchor, and its window when the
// kind has one), P1..P9 → 1..9. A reducer that names no P-position at all falls back to its named
// checkpoints, transition → 4 and finish → 10; anything else is 0, "not placed".

#include "characteristic.h"
#include "characteristic_pack.h"
#include "drill_pack.h"

#include "../Analysis/swing_themes.h"

#include <QHash>
#include <QSet>
#include <QString>

#include <algorithm>
#include <optional>

namespace pinpoint::analysis {

namespace themes {

inline int pPositionOf(Phase p)
{
    switch (p) {
    case Phase::Address:              return 1;
    case Phase::ShaftParallelBack:    return 2;
    case Phase::MidBackswing:         return 3;
    case Phase::Top:                  return 4;
    case Phase::ArmParallelDown:      return 5;
    case Phase::Delivery:             return 6;
    case Phase::Impact:               return 7;
    case Phase::ShaftParallelThrough: return 8;
    case Phase::FollowThrough:        return 9;
    default:                          break;
    }
    return 0;
}

inline int checkpointPositionOf(Phase p)
{
    if (p == Phase::Transition) return 4;
    if (p == Phase::Finish)     return 10;
    return 0;
}

inline int whenOf(const Reducer &r)
{
    std::vector<Phase> phases;
    if (r.anchor) phases.push_back(*r.anchor);
    if (reducerUsesWindow(r.kind)) { phases.push_back(r.window.first); phases.push_back(r.window.second); }
    int when = 0;
    for (Phase p : phases) when = std::max(when, pPositionOf(p));
    if (when) return when;
    for (Phase p : phases) when = std::max(when, checkpointPositionOf(p));
    return when;
}

} // namespace themes

inline QHash<QString, ThemeMeasureInfo> themeMeasureInfo(const CharacteristicPack &pack)
{
    // The tails watched by the signals whose FIRST measure each measure is.
    QHash<QString, QSet<int>> watched;      // 0 = High, 1 = Low
    for (const Signal &s : pack.signalDefs) {
        if (s.measures.isEmpty() || !s.direction) continue;
        watched[s.measures.front()].insert(*s.direction == Direction::High ? 0 : 1);
    }

    QHash<QString, ThemeMeasureInfo> out;
    for (const Measure &m : pack.measures) {
        ThemeMeasureInfo info;
        info.id        = m.id;
        info.metricKey = m.metricKey;
        info.when      = themes::whenOf(m.reducer);
        info.twoSided  = false;
        const QSet<int> dirs = watched.value(m.id);
        if (m.shape == Shape::Ceiling)                                     info.sign = 1.0;
        else if (m.shape == Shape::Floor)                                  info.sign = -1.0;
        else if (dirs == QSet<int>{ 0 })                                   info.sign = 1.0;
        else if (dirs == QSet<int>{ 1 })                                   info.sign = -1.0;
        else if (m.unwatchedTail && *m.unwatchedTail == Direction::High)   info.sign = -1.0;
        else if (m.unwatchedTail && *m.unwatchedTail == Direction::Low)    info.sign = 1.0;
        else { info.sign = 1.0; info.twoSided = true; }
        out.insert(m.id, info);
    }
    return out;
}

inline QHash<QString, ThemeConditionInfo> themeConditionInfo(const CharacteristicPack &pack,
                                                             const DrillSet &drills = sharedDrillSet())
{
    // measure id -> its metricKey's root (themeFamilyRoot: pelvisRotationSigned is pelvisRotation),
    // or its own id when it has none; and its swing position.
    QHash<QString, QString> family;
    QHash<QString, int>     when;
    for (const Measure &m : pack.measures) {
        const QString root = themeFamilyRoot(m.metricKey);
        family.insert(m.id, root.isEmpty() ? m.id : root);
        when.insert(m.id, themes::whenOf(m.reducer));
    }
    QSet<QString> drillIds;
    for (const Drill &d : drills.drills) drillIds.insert(d.id);

    QHash<QString, ThemeConditionInfo> out;
    for (const Condition &c : pack.conditions) {
        ThemeConditionInfo info;
        info.id         = c.id;
        info.fault      = c.kind == ConditionKind::Fault;
        info.detection  = c.detection == DetectionMode::All   ? kThemeDetectionAll
                        : c.detection == DetectionMode::First ? kThemeDetectionFirst
                        :                                       kThemeDetectionAny;
        info.prominence = int(c.prominence);    // Rare 0 .. Ubiquitous 4, the enum's order
        for (const QString &sid : c.detectedBy) {
            const Signal *s = pack.signal(sid);
            if (!s || s->measures.isEmpty()) continue;
            const QString &first = s->measures.front();
            const QString f = family.value(first, first);
            if (!info.families.contains(f)) info.families.append(f);
            const int w = when.value(first);
            if (w > 0 && (info.when == 0 || w < info.when)) info.when = w;
        }
        for (const QString &id : c.drills)
            if (drillIds.contains(id)) { info.drill = id; break; }
        out.insert(c.id, info);
    }
    return out;
}

// The phrases are copied out, so the result outlives the pack and the drill set it came from.
inline ThemePhrases themePhrases(const CharacteristicPack &pack, const DrillSet &drills = sharedDrillSet())
{
    QHash<QString, QString> golfer, well, why, high, low, family;
    for (const Condition &c : pack.conditions) {
        golfer.insert(c.id, c.golfer);
        well.insert(c.id, c.golferWell);
        why.insert(c.id, c.golferWhy);
    }
    QHash<QString, ThemeDrill> drill;
    for (const Drill &d : drills.drills)
        if (!drill.contains(d.id)) drill.insert(d.id, ThemeDrill{ d.label, d.instruction });
    for (const Measure &m : pack.measures) {
        high.insert(m.id, m.golferHigh);
        low.insert(m.id, m.golferLow);
        family.insert(m.id, themeFamilyRoot(m.metricKey));   // a signed key is its unsigned family
    }
    ThemePhrases ph;
    ph.condition = [golfer](const QString &id) { return golfer.value(id); };
    ph.measure   = [high, low](const QString &id, bool isHigh) { return isHigh ? high.value(id) : low.value(id); };
    ph.family    = [family](const QString &id) { return family.value(id); };
    ph.well      = [well](const QString &id) { return well.value(id); };
    ph.why       = [why](const QString &id) { return why.value(id); };
    ph.drill     = [drill](const QString &id) { return drill.value(id); };
    return ph;
}

} // namespace pinpoint::analysis
