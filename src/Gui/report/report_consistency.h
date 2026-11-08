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

// THE REPORT'S CONSISTENCY PAGE — the map's `consistency` block (docs/design/consistency_design.md §3, Print), cut from
// the Consistency presenter's report payload (charts/charts_consistency_presenter.h consistencySessionReportPayload: the
// tab's words and rows, nothing picked). Pure and header-only, like report_delivery.h, so report_model_test proves its
// shape and absence without a session behind it.
//
// WHAT IT DOES, AND ONLY THIS:
//   · ABSENT (null) when the payload is not available: no cell has a word, no ball line and no shot flagged. Gated on
//     DATA, never on the session's kind;
//   · keeps the presenter's sentences VERBATIM, as plain strings (paper sets no figure runs);
//   · drops the table when no cell has a word (a table of "-" says nothing the head has not), keeping the head;
//   · numbers each flagged shot as the report numbers its swings;
//   · caps the flagged swings and each swing's rows, ball rows first, and words the "N more";
//   · keeps of each row only what paper prints, and says the same-shot note once, not once a swing.
//
//  consistency: {
//    kicker ("Consistency  ·  31 swings  ·  Gap wedge"), head (the headline's text), headKind, headGrades, headFallback (the
//    presenter's headline kind, grades and fallback: the page puts the vocabulary's words in), ball ("" none),
//    empty ("" none), notes: [string],
//    table: { columns: [{ text, short }], rows: [{ id, name, cells }] } | null (PpChMatrix's rectangle; each graded cell's
//    word is the page's, from its `result`),
//    key, keyMarks (the presenter's templates, %1-%7 the vocabularies' words),
//    flagged: [{ swing, shotLabel ("Swing 12"), title ("Swing 12  ·  gap wedge"), lm ("launch monitor: short"), sentence,
//                carry (null | { label, values }: the carry's typicality, a golfer-layer badge), guardNote ("" none),
//                headLabel, groups: [{ name, rows: [ROW] }], more ("3 more readings" | "") }]
//             (≤ kConsistencyReportSwingCap, strike order),
//    flaggedMore ("" | "2 more flagged swings: 41 and 52"), note ("" | the same-shot note) }
//  ROW: { key, label, tag, value ("82 mph"), median ("72"), place, fired, firedWord, cautionText,
//         typicality (null | the row's typicality `values`, for a golfer-layer badge: the word, no figures) }
//  See charts/charts_consistency_presenter.h, THE GRADES, for the templates and the headline's kinds.

#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace pinpoint::report {

// The page's name column is what the eight columns leave, and "Lateral and balance" does not fit it: on paper an area
// name takes "&", the way Home writes it (Mark, 8 Nov 2026). The sentences on the page keep "and".
inline QVariantMap reportConsistencyTable(QVariantMap matrix)
{
    QVariantList rows = matrix.value(QStringLiteral("rows")).toList();
    for (QVariant &row : rows) {
        QVariantMap m = row.toMap();
        m.insert(QStringLiteral("name"), m.value(QStringLiteral("name")).toString().replace(QStringLiteral(" and "), QStringLiteral(" & ")));
        row = m;
    }
    matrix.insert(QStringLiteral("rows"), rows);
    return matrix;
}

constexpr int kConsistencyReportSwingCap = 6;    // flagged swings on the page: a session's fences flag few
constexpr int kConsistencyReportRowCap   = 8;    // rows under one swing, ball first

inline QString consistencyReportSentence(const QVariant &sentence)
{
    return sentence.toMap().value(QStringLiteral("text")).toString();
}

// A row's or a carry's typicality `values` (charts_consistency_presenter.h), or null: paper prints the word, so the
// basis string stays behind.
inline QVariant consistencyReportTypicality(const QVariant &payload)
{
    const QVariantMap p = payload.toMap();
    if (p.value(QStringLiteral("kind")).toString() != QLatin1String("typicality")) return QVariant::fromValue(nullptr);
    return p.value(QStringLiteral("values"));
}

inline QVariantMap consistencyReportRow(const QVariantMap &row)
{
    return { { QStringLiteral("typicality"), consistencyReportTypicality(row.value(QStringLiteral("variability"))) }, { QStringLiteral("key"), row.value(QStringLiteral("key")) }, { QStringLiteral("label"), row.value(QStringLiteral("label")) },
             { QStringLiteral("tag"), row.value(QStringLiteral("tag")) }, { QStringLiteral("value"), row.value(QStringLiteral("valueFigure")) },
             { QStringLiteral("median"), row.value(QStringLiteral("medianText")) }, { QStringLiteral("place"), row.value(QStringLiteral("place")) },
             { QStringLiteral("fired"), row.value(QStringLiteral("fired")).toBool() }, { QStringLiteral("firedWord"), row.value(QStringLiteral("firedWord")) },
             { QStringLiteral("cautionText"), row.value(QStringLiteral("caution")).toBool() ? row.value(QStringLiteral("cautionText")).toString() : QString() } };
}

// "2 more flagged swings: 41 and 52".
inline QString consistencyReportMoreSwings(const QStringList &swings)
{
    if (swings.isEmpty()) return QString();
    const int n = int(swings.size());
    const QString list = n == 1 ? swings.first() : QStringList(swings.mid(0, n - 1)).join(QStringLiteral(", ")) + QStringLiteral(" and ") + swings.last();
    return (n == 1 ? QStringLiteral("1 more flagged swing: ") : QStringLiteral("%1 more flagged swings: ").arg(n)) + list;
}

// `swingNoOfDir`: QDir::cleanPath(swing folder) → the report's swing number (1-based). A shot the report does not number
// keeps the presenter's ordinal.
inline QVariant consistencyReportBlock(const QVariantMap &payload, const QHash<QString, int> &swingNoOfDir)
{
    if (!payload.value(QStringLiteral("available")).toBool()) return QVariant::fromValue(nullptr);

    QStringList notes;
    for (const QVariant &n : payload.value(QStringLiteral("notes")).toList()) notes << consistencyReportSentence(n);

    QVariantList flagged;
    QStringList past;
    QString note;
    for (const QVariant &sv : payload.value(QStringLiteral("flagged")).toList()) {
        const QVariantMap shot = sv.toMap();
        const int swing = swingNoOfDir.value(QDir::cleanPath(shot.value(QStringLiteral("swingDir")).toString()), shot.value(QStringLiteral("ordinal")).toInt());
        if (flagged.size() >= kConsistencyReportSwingCap) { past << QString::number(swing); continue; }

        // ball first: the presenter's groups run ball, club, swing, so the cap falls on the swing's own readings
        QVariantList groups;
        int kept = 0, total = 0;
        for (const QVariant &gv : shot.value(QStringLiteral("groups")).toList()) {
            const QVariantMap group = gv.toMap();
            const QVariantList rows = group.value(QStringLiteral("rows")).toList();
            total += int(rows.size());
            QVariantList out;
            for (const QVariant &r : rows) {
                if (kept >= kConsistencyReportRowCap) break;
                out.append(consistencyReportRow(r.toMap()));
                ++kept;
            }
            if (!out.isEmpty()) groups.append(QVariantMap{ { QStringLiteral("name"), group.value(QStringLiteral("name")) }, { QStringLiteral("rows"), out } });
        }
        const int more = total - kept;
        const QString label = QStringLiteral("Swing %1").arg(swing);
        const QString club = shot.value(QStringLiteral("club")).toString().toLower();
        flagged.append(QVariantMap{
            { QStringLiteral("swing"), swing }, { QStringLiteral("shotLabel"), label },
            { QStringLiteral("title"), club.isEmpty() ? label : label + QStringLiteral("  ·  ") + club },
            { QStringLiteral("lm"), shot.value(QStringLiteral("lm")) },
            { QStringLiteral("sentence"), consistencyReportSentence(shot.value(QStringLiteral("sentence"))) },
            { QStringLiteral("carry"), shot.value(QStringLiteral("carry")).toMap().isEmpty() ? QVariant::fromValue(nullptr)
                                       : QVariant(QVariantMap{ { QStringLiteral("label"), shot.value(QStringLiteral("carry")).toMap().value(QStringLiteral("label")) },
                                                               { QStringLiteral("values"), consistencyReportTypicality(shot.value(QStringLiteral("carry"))) } }) },
            { QStringLiteral("guardNote"), shot.value(QStringLiteral("guardNote")) },
            { QStringLiteral("headLabel"), shot.value(QStringLiteral("headLabel")) },
            { QStringLiteral("groups"), groups },
            { QStringLiteral("more"), more > 0 ? (more == 1 ? QStringLiteral("1 more reading") : QStringLiteral("%1 more readings").arg(more)) : QString() } });
        if (note.isEmpty()) note = shot.value(QStringLiteral("note")).toString();
    }

    const bool table = payload.value(QStringLiteral("table")).toBool();
    return QVariantMap{
        { QStringLiteral("kicker"), payload.value(QStringLiteral("kicker")) },
        { QStringLiteral("head"), consistencyReportSentence(payload.value(QStringLiteral("headline"))) },
        { QStringLiteral("headKind"), payload.value(QStringLiteral("headline")).toMap().value(QStringLiteral("kind")).toString() },
        { QStringLiteral("headGrades"), payload.value(QStringLiteral("headline")).toMap().value(QStringLiteral("grades")).toList() },
        { QStringLiteral("headFallback"), payload.value(QStringLiteral("headline")).toMap().value(QStringLiteral("fallback")).toString() },
        { QStringLiteral("ball"), consistencyReportSentence(payload.value(QStringLiteral("ball"))) },
        { QStringLiteral("empty"), payload.value(QStringLiteral("empty")) },
        { QStringLiteral("notes"), notes },
        { QStringLiteral("table"), table ? QVariant(reportConsistencyTable(payload.value(QStringLiteral("matrix")).toMap())) : QVariant::fromValue(nullptr) },
        { QStringLiteral("key"), table ? payload.value(QStringLiteral("key")).toString() : QString() },
        { QStringLiteral("keyMarks"), table ? payload.value(QStringLiteral("keyMarks")).toString() : QString() },
        { QStringLiteral("flagged"), flagged },
        { QStringLiteral("flaggedMore"), consistencyReportMoreSwings(past) },
        { QStringLiteral("note"), note } };
}

} // namespace pinpoint::report
