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

// A HABIT'S RUN OF READINGS AGAINST ITS CORRIDOR, OVER THE SESSION'S DIAGNOSIS (refactor 5.6c, B5/B6).
//
// What the diagnostics panel's strip and the session report's habit run both draw: each shot's reading of one measure
// on a condition's ruler, and the corridor it is drawn against. Pure and header-only, over the rows and ledgers of the
// owner's applied diagnosis (SessionDiagnostics::diagnosisInput / diagnosis, reached through the Session) and the rule
// data the caller holds (the pack, the norms, the grade policy). ONE RULE: SessionDiagnosticsModel::spreadFor and
// spreadCorridorOf call these, and the report calls them over its Session's diagnosis, so the panel and the paper never
// draw a habit against two corridors. Moved here from the adapter's view code; nothing in them changed.
//
// Included by the panel's adapter and the report model only (not by session_spread.h, which a rules header includes).

#include "../../Diagnostics/derived/session_spread.h"
#include "../../Diagnostics/content/norm_provider.h"
#include "../../Diagnostics/content/characteristic_pack.h"

#include <QHash>
#include <QString>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

// One shot's place on a condition's ruler of one measure: its reading (NaN where there is none to place) and THIS
// measure's verdict on it. A condition read by several measures records each (ConditionRow::readings), so a shot decided
// by a sibling measure still has its place on this ruler; a row from before the readings were recorded holds its driving
// measure alone, and a shot decided by another is not placeable here.
struct SpreadReading {
    double    value = std::numeric_limits<double>::quiet_NaN();
    ShotState state = ShotState::NotAssessable;
};

// Every shot's reading of `measureId` (empty: the row's own value) for `conditionId`, in shot order.
inline std::vector<SpreadReading> spreadReadingsOf(const std::vector<ShotRecord> &shots, const QString &conditionId,
                                                   const QString &measureId)
{
    std::vector<SpreadReading> out(shots.size());
    for (size_t i = 0; i < shots.size(); ++i) {
        const ConditionRow *r = rowFor(shots[i], conditionId);
        if (!r) continue;
        out[i].state = r->state;
        if (r->state == ShotState::NotAssessable) continue;
        if (measureId.isEmpty()) {
            if (std::isfinite(r->value)) out[i].value = r->value;
            continue;
        }
        const std::optional<MeasureRow> m = rowReadingOf(*r, measureId);
        if (!m || !std::isfinite(m->value)) {
            out[i].state = ShotState::NotAssessable;
            continue;
        }
        out[i].state = m->fired ? ShotState::Fired : ShotState::Clean;
        out[i].value = m->value;
    }
    return out;
}

// A composite is read by counts, never drawn by its number (posture_metrics_design.md §8): the pack's word first, then
// the ROWS' — a row graded as a composite carries its parts whatever the pack says today.
inline bool spreadReadsComposite(const std::vector<ShotRecord> &shots, const QString &conditionId, const QString &measureId,
                                 const CharacteristicPack *pack)
{
    if (pack && !measureId.isEmpty())
        if (const Measure *m = pack->measure(measureId); m && m->kind == MeasureKind::Composite) return true;
    for (const ShotRecord &s : shots) {
        const ConditionRow *r = rowFor(s, conditionId);
        if (!r) continue;
        if (r->drivingMeasureId == measureId && r->composite.present()) return true;
        for (const MeasureRow &mr : r->readings)
            if (mr.measureId == measureId && mr.composite.present()) return true;
    }
    return false;
}

// Where the corridor came from, for a caption: the norm that resolved, or the row's stored band.
struct SpreadCorridorSource {
    const Norm *norm   = nullptr;   // the norm that resolved (into the caller's provider), or null
    bool        stored = false;     // no norm drew it: the representative row's stored band did
};

// THE CORRIDOR A CONDITION'S RUN IS DRAWN AGAINST, at the focus shot `fi` (an index into `shots`; -1 none).
// WHICH CONTEXT'S NORM: the focus shot's, when it read this condition — that is the swing the ringed dot is, and its
// corridor is the one it was graded against. Otherwise the session's commonest, so a strip over a one-club session does
// not change its bands because the newest swing happened to be unmeasurable. The norm for that measure and context as
// it grades today; else the stored band of THIS measure's reading; then THE CONDITION'S TAIL of it (#16), as the rows
// recorded it. `measure` empty: the ledger's driving measure.
inline SpreadCorridor spreadCorridorOfRows(const std::vector<ShotRecord> &shots, const ConditionLedger &l, int fi,
                                           const QString &measure, const INormProvider *norms,
                                           const CharacteristicPack *pack, const GradePolicy &policy,
                                           SpreadCorridorSource *source = nullptr)
{
    const QString measureId = measure.isEmpty() ? l.drivingMeasureId : measure;
    auto usable = [&](const ConditionRow *r) {
        if (!r || r->state == ShotState::NotAssessable) return false;
        if (measureId.isEmpty()) return std::isfinite(r->value);
        const std::optional<MeasureRow> m = rowReadingOf(*r, measureId);
        return m && std::isfinite(m->value);
    };

    const ConditionRow *rep = nullptr;
    if (fi >= 0 && fi < int(shots.size())) {
        const ConditionRow *r = rowFor(shots[size_t(fi)], l.id);
        if (usable(r)) rep = r;
    }
    if (!rep) {
        QHash<QString, int> seen;
        int best = 0;
        for (const ShotRecord &s : shots) {
            const ConditionRow *r = rowFor(s, l.id);
            if (!usable(r)) continue;
            const int k = ++seen[r->contextId];
            if (k > best) { best = k; rep = r; }
        }
    }

    SpreadCorridor c;
    if (norms && !measureId.isEmpty()) {
        Shape shape = Shape::Target;
        if (pack)
            if (const Measure *m = pack->measure(measureId)) shape = m->shape;
        const NormResolution res = norms->resolve(measureId, rep ? rep->contextId : QString());
        if (res.found()) {
            c = spreadCorridorFromNorm(*res.norm, shape, policy);
            if (source) source->norm = res.norm;
        }
    }
    if (!c.known && rep) {
        // The stored band of THIS measure's reading, not the driving one's.
        ConditionRow band = *rep;
        if (!measureId.isEmpty())
            if (const std::optional<MeasureRow> m = rowReadingOf(*rep, measureId)) {
                band.drivingMeasureId = m->measureId;
                band.value            = m->value;
                band.corridorLo       = m->corridorLo;
                band.corridorHi       = m->corridorHi;
                band.z                = m->z;
                band.corridorShape    = m->corridorShape;
            }
        c = spreadCorridorFromRow(band, policy);
        if (c.known && source) source->stored = true;
    }
    if (rep && !measureId.isEmpty())
        if (const std::optional<MeasureRow> m = rowReadingOf(*rep, measureId))
            c = spreadCorridorOnSide(c, m->firingSide);
    return c;
}

// THE HABIT'S RUN, the numbers its strip and run chart draw: each shot's reading on the driving measure (or `measure`),
// the corridor at the focus shot, and the axis over every placed reading (its decimals are the figures'). A composite
// (spreadReadsComposite) carries no readings, no corridor and no axis: it is read by counts.
struct HabitSpread {
    QString                    measureId, unit;   // unit: the pack's measure's ("" when the pack has none)
    bool                       composite = false;
    SpreadCorridor             corridor;
    SpreadAxis                 axis;
    std::vector<SpreadReading> readings;          // per shot of the rows, in order (empty for a composite)
};

inline HabitSpread habitSpreadOf(const std::vector<ShotRecord> &shots, const ConditionLedger &l, int fi,
                                 const QString &measure, const INormProvider *norms, const CharacteristicPack *pack,
                                 const GradePolicy &policy)
{
    HabitSpread h;
    h.measureId = measure.isEmpty() ? l.drivingMeasureId : measure;
    if (pack && !h.measureId.isEmpty())
        if (const Measure *m = pack->measure(h.measureId)) h.unit = m->unit;
    h.composite = spreadReadsComposite(shots, l.id, h.measureId, pack);
    if (h.composite) return h;
    h.readings = spreadReadingsOf(shots, l.id, h.measureId);
    std::vector<double> placed;
    for (const SpreadReading &r : h.readings)
        if (std::isfinite(r.value)) placed.push_back(r.value);
    h.corridor = spreadCorridorOfRows(shots, l, fi, h.measureId, norms, pack, policy);
    h.axis     = spreadAxisFor(h.corridor, placed);
    return h;
}

// THE SESSION-SCOPE CONDITION THE SESSION'S SEQUENCE READING SPEAKS FOR (B6): the first session-scope ledger of the
// diagnosis, in its order — the session reduction appends them in detect()'s order, which the panel's readout names the
// first of ("" when the pack has none).
inline QString sessionScopeConditionOf(const std::vector<ConditionLedger> &ledgers)
{
    for (const ConditionLedger &l : ledgers)
        if (l.sessionScope) return l.id;
    return QString();
}

} // namespace pinpoint::analysis
