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

// ONE SHOT'S WRIST CHECKPOINT PROFILE, AS PART OF ITS FINDINGS (DEC-123; refactor step 3.8-ii, J-58).
//
// The wrist-pattern identification is an analysis of the shot, and its result is part of the shot's findings
// (OBJ-SHOT-RESULTS) through the one diagnostic model (Mark, 14 Nov 2026; DEC-123). fireFindings() computes it
// (wrist_profile_assess.h: the wrist engine samples each wrist DOF at the eight checkpoints, takes the change from
// address and grades each cell against the norm band of each style), ShotRecord carries it, and diagnostics.json keeps
// it per shot row (FILE-SESS-03, DEC-135; diagnostic_ledger.h schema 16). The Wrist chart and the report's Wrist page
// read it from there and grade nothing themselves.
//
// WHAT IT HOLDS, per DOF the shot carries (in the engine's DOF order): its key, its confidence, the Δ from address at
// each checkpoint (NaN: not measured), and the band and RAG of each cell under EACH of the three styles (Neutral,
// Bowed, Cupped), so a style chosen by hand on the Wrist chart is a pick, never a re-grade. `resolved` is the style
// Auto resolved this swing to — the wrist pattern. The checkpoint times are NOT here: they are the shot's phases
// (wristCheckpoints(), the adapter's own table), read by the view on its own clock.
//
// Values and their JSON only (Qt Core): included by Coaching/diagnostic_ledger.h for ShotRecord, so it names no engine
// type.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int kWristProfilePositions = 8;   // P1..P8 (kNumPos)
inline constexpr int kWristProfileStyles    = 3;   // 0 Neutral · 1 Bowed · 2 Cupped

struct WristProfileGrade {
    QString rag = QStringLiteral("none");   // "green" | "amber" | "red" | "ref" | "none"
    bool    banded = false;
    double  lo = std::numeric_limits<double>::quiet_NaN();
    double  hi = std::numeric_limits<double>::quiet_NaN();
    friend bool operator==(const WristProfileGrade &a, const WristProfileGrade &b)
    {
        const auto same = [](double x, double y) { return (std::isnan(x) && std::isnan(y)) || x == y; };
        return a.rag == b.rag && a.banded == b.banded && same(a.lo, b.lo) && same(a.hi, b.hi);
    }
};

struct WristProfileRow {
    QString dof;          // dofName(): "leadWristRadUln" | "leadWristFlexExt" | "leadForearmRot" | "trailWristFlexExt" | …
    QString metricKey;    // the series key it was read from ("hm.leadWristFlexExt")
    double  confidence = 0.0;
    std::array<double, kWristProfilePositions> deltaDeg{};   // NaN: not measured at that checkpoint
    std::array<std::array<WristProfileGrade, kWristProfilePositions>, kWristProfileStyles> grades{};
    WristProfileRow() { deltaDeg.fill(std::numeric_limits<double>::quiet_NaN()); }
    friend bool operator==(const WristProfileRow &a, const WristProfileRow &b)
    {
        for (int p = 0; p < kWristProfilePositions; ++p) {
            const double x = a.deltaDeg[size_t(p)], y = b.deltaDeg[size_t(p)];
            if (!((std::isnan(x) && std::isnan(y)) || x == y)) return false;
        }
        return a.dof == b.dof && a.metricKey == b.metricKey && a.confidence == b.confidence && a.grades == b.grades;
    }
};

struct WristProfile {
    bool present = false;   // false: the shot carries no wrist DOF (or its findings predate schema 16)
    int  resolved = 0;      // the style Auto resolved the swing to (0..2)
    std::vector<WristProfileRow> rows;
    friend bool operator==(const WristProfile &a, const WristProfile &b)
    {
        return a.present == b.present && a.resolved == b.resolved && a.rows == b.rows;
    }
    friend bool operator!=(const WristProfile &a, const WristProfile &b) { return !(a == b); }
};

// ── JSON (the shot row's `wrist` key, FILE-SESS-03 schema 16) ─────────────────────────────────────────────────
// { resolved, rows: [{ dof, key, conf, d: [8 × Δ | null], g: [3 styles × [8 × [rag, lo | null, hi | null]]] }] }
// A cell is banded exactly when its lo and hi are both numbers.

inline QJsonValue wristNumberOrNull(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null); }

inline QJsonObject wristProfileToJson(const WristProfile &p)
{
    QJsonArray rows;
    for (const WristProfileRow &r : p.rows) {
        QJsonArray d, g;
        for (double x : r.deltaDeg) d.append(wristNumberOrNull(x));
        for (const auto &style : r.grades) {
            QJsonArray cells;
            for (const WristProfileGrade &c : style)
                cells.append(QJsonArray{ c.rag, c.banded ? wristNumberOrNull(c.lo) : QJsonValue(QJsonValue::Null),
                                         c.banded ? wristNumberOrNull(c.hi) : QJsonValue(QJsonValue::Null) });
            g.append(cells);
        }
        rows.append(QJsonObject{ { QStringLiteral("dof"), r.dof }, { QStringLiteral("key"), r.metricKey },
                                 { QStringLiteral("conf"), r.confidence }, { QStringLiteral("d"), d }, { QStringLiteral("g"), g } });
    }
    return QJsonObject{ { QStringLiteral("resolved"), p.resolved }, { QStringLiteral("rows"), rows } };
}

// An absent or malformed block reads as no profile (present false), never as a half one.
inline WristProfile wristProfileFromJson(const QJsonValue &v)
{
    WristProfile p;
    if (!v.isObject()) return p;
    const QJsonObject o = v.toObject();
    const QJsonArray rows = o.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty()) return p;
    const auto num = [](const QJsonValue &x) { return x.isDouble() ? x.toDouble() : std::numeric_limits<double>::quiet_NaN(); };
    for (const QJsonValue &rv : rows) {
        const QJsonObject ro = rv.toObject();
        WristProfileRow r;
        r.dof        = ro.value(QStringLiteral("dof")).toString();
        r.metricKey  = ro.value(QStringLiteral("key")).toString();
        r.confidence = ro.value(QStringLiteral("conf")).toDouble();
        const QJsonArray d = ro.value(QStringLiteral("d")).toArray();
        const QJsonArray g = ro.value(QStringLiteral("g")).toArray();
        if (r.dof.isEmpty() || d.size() != kWristProfilePositions || g.size() != kWristProfileStyles) return WristProfile{};
        for (int i = 0; i < kWristProfilePositions; ++i) r.deltaDeg[size_t(i)] = num(d.at(i));
        for (int s = 0; s < kWristProfileStyles; ++s) {
            const QJsonArray cells = g.at(s).toArray();
            if (cells.size() != kWristProfilePositions) return WristProfile{};
            for (int i = 0; i < kWristProfilePositions; ++i) {
                const QJsonArray c = cells.at(i).toArray();
                WristProfileGrade &gr = r.grades[size_t(s)][size_t(i)];
                gr.rag = c.at(0).toString(QStringLiteral("none"));
                gr.lo = num(c.at(1));
                gr.hi = num(c.at(2));
                gr.banded = std::isfinite(gr.lo) && std::isfinite(gr.hi);
            }
        }
        p.rows.push_back(std::move(r));
    }
    p.resolved = std::clamp(o.value(QStringLiteral("resolved")).toInt(), 0, kWristProfileStyles - 1);
    p.present  = true;
    return p;
}

} // namespace pinpoint::analysis
