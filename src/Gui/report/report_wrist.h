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

// THE REPORT'S WRIST PAGE — the map's `wrist` block, cut from the Charts Wrist presenter's SESSION payload
// (charts/charts_wrist_presenter.h wristSessionReportPayload: the numbers and words the Wrist section's Session
// view draws, nothing picked, Auto, the wash the DOMINANT style's band, every sentence counting "swings" — the
// report's word). Pure and header-only, like report_posture.h, so report_model_test proves its shape, caps and
// absence without a session behind it; SessionReportModel feeds it the payload it read and the report's own swing
// numbers. The page mirrors the Posture page's order (head → checkpoints → score → style → what keeps firing →
// repeatable → what moves with the lead wrist → trust).
//
// WHAT IT DOES, AND ONLY THIS:
//   · ABSENT (null) when the payload is not available or carries no banded row (no page, no head). Gated on DATA:
//     any wrist DOF read, a camera-only session included;
//   · keeps the presenter's sentences and figures VERBATIM (kicker, head, the lines, the profile's sub and caption,
//     every median and width, the score run's numbers, the style table, the findings' count words, the
//     repeatability words, the movesWith rows and chance line, the trust lines);
//   · maps each profile row onto the POSTURE row's keys, so ONE QML profile component draws both pages (below);
//   · renumbers the score run's points, the style table's columns and each finding's marks to the REPORT's swing
//     numbers by cleaned swing folder (a shot the report does not number keeps the presenter's ordinal);
//   · caps the lists and words the "N more".
//
//  wrist: {
//    kicker ("6 swings  ·  7 iron  ·  Wrist"), head ("Graded as a Cupped wrist on every swing"),
//    styleLine ("By the resemblance index, most resemble Cupped (6)" | "": the clause alone, the head says the counts),
//    scoreLine ("Wrist score median 84, middle half 78–90" | "No swing's wrist conditions have been graded yet"),
//    profile: { positions: [{ tag, name, available }] (8), rows: [ROW] (≤ kWristRowCap), sub, caption },
//    scoreRun: { caption ("Wrist score, swing by swing (0–100)"), points: [{ swing, value (| null), measured, text ("84" | "-") }],
//                box: BOX (the presenter's, its texts whole points), yRange { lo 0, hi 100 }, ticks [{ v, text }] (the value axis),
//                legend ("A mark per swing, in the order struck; the box is the session. …"),
//                drift: { defined, clear, rText, ciText, line ({ x0, y0, x1, y1 } | null) } },
//    styles: { columns: [{ swing, label ("Swing 3"), short ("3") }] (≤ kWristStyleColumnCap), rows: [{ key, name,
//              cells: [{ text ("84" | "-"), graded }] }] (cells cut with the columns), counts: [{ name, n }], hasIndex,
//              note ("" | "No swing in this session carries a resemblance index, …": then no columns, no rows), more ("" | "N more swings not shown"),
//              legend ("How much each swing resembles each style, %; …" | "" with no index),
//              countsLine ("Graded against: Cupped on 5 swings, Neutral on 1." | "" with no index) },
//    recurring: [{ id, name, severity ("pattern" | "watching"), tier, count, countText ("4 of 6 swings, at P4"),
//                  marks: [{ swing, fired }] (≤ kWristStyleColumnCap) }] (≤ kWristRecurringCap), recurringMore ("" | "N more"),
//    recurringEmpty ("No wrist finding fired on any swing."), recurringLegend ("Each finding, swing by swing: …" | "" with none),
//    repeat ("From P3 on, most repeatable: lead wrist bow / cup at P7 (middle half 2° wide); least: …." | ""),
//    sizeLine ("Six swings is a small session: each box is drawn from 6 values." — carried for completeness: the trust
//              lines say it, and the page does not print it twice),
//    movesWith: { available, head ("What moves with bow / cup P7"), sub, refusal ("" when available), rows: [LEDGER ROW,
//                 the Relations presenter's, unchanged] (≤ kWristMovesWithCap), chanceLine, note } (the refusal alone when
//                 not available),
//    trust: [string] (≤ kWristTrustCap), trustMore ("" | "N more") }
//
//  ROW — THE POSTURE ROW'S KEYS (PpPosturePage's profile reads exactly these), from the presenter's wrist row:
//    key        ← dof ("leadWristFlexExt")              label    ← label ("Lead wrist bow / cup")
//    metricKey  ← key ("hm.leadWristFlexExt")           hueKey   ← key (the metric's named colour paints the wash)
//    camera     ← camera (the SOURCE words: "wrist sensor" | "face-on camera" | "wrist sensor and face-on camera")
//    unit       ← unit ("°")                            poleText ← poleTopText ("↑ closed": which way is plus)
//    yLo, yHi   ← yLo, yHi (the row's scale over boxes and band)   styleText ← "" (posture's trail-knee style)
//    cells[p]   ← { index p, tag (positions[p].tag), text (boxes[p].medianText: "−19°" | "-"), available (n ≥ 3),
//                   wash (points[p].wash: "band" | "none"), banded, bandLo, bandHi (the dominant style's band | null),
//                   outside (boxes[p].outside: the median outside that band), ring (= outside),
//                   ringWord (the pole it fell on: "closed" | "open"; "" inside), warn "", warnShort "" (no ⚠ on a wrist
//                   cell: a refused reading is a "-" and its reason is in trust) }
//    boxes[p]   ← { box, values, n, median, medianText, wide, wideText ("2° wide" | "") }
//    plus, wrist-only: dof, name, sub, source, poleTop, poleBottom, poleTopText, poleBottomText, numbersOnly (false: the
//    Session view draws no numbers-only row — the lead elbow has no corridor and the screen's Session leaves it off)
//  (posture-only keys the wrist row does not carry: present, style, faultHi/faultLo, alt*, warned — the QML reads
//   them as absent.)
#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace pinpoint::report {

constexpr int kWristRowCap = 5;              // hinge, bow / cup, forearm roll, trail wrist (+ one spare): the banded DOFs
constexpr int kWristStyleColumnCap = 120;    // swings in the style table and in each finding's marks
constexpr int kWristRecurringCap = 8;        // the pack's wrist conditions (six today)
constexpr int kWristMovesWithCap = 7;        // movesWith(…, 4, 3): four readings, then three launch-monitor ones
constexpr int kWristTrustCap = 16;

inline QString wristMoreWords(int more, const char *noun = nullptr)
{
    if (more <= 0) return QString();
    if (!noun) return QStringLiteral("%1 more").arg(more);
    return QStringLiteral("%1 more %2 not shown").arg(more).arg(QString::fromLatin1(noun) + (more == 1 ? QString() : QStringLiteral("s")));
}

inline QString wristSentenceText(const QVariant &v)
{
    if (v.typeId() == QMetaType::QString) return v.toString();
    return v.toMap().value(QStringLiteral("text")).toString();
}

// `swingNoOfDir`: QDir::cleanPath(swing folder) → the report's swing number (1-based).
inline QVariant wristReportBlock(const QVariantMap &payload, const QHash<QString, int> &swingNoOfDir)
{
    const QVariant none = QVariant::fromValue(nullptr);
    if (!payload.value(QStringLiteral("available")).toBool()) return none;
    const auto swingOf = [&](const QVariantMap &m) {
        return swingNoOfDir.value(QDir::cleanPath(m.value(QStringLiteral("swingDir")).toString()), m.value(QStringLiteral("ordinal")).toInt());
    };

    // the profile: the presenter's rows on the posture row's keys
    QVariantList positions;
    for (const QVariant &p : payload.value(QStringLiteral("positions")).toList()) {
        const QVariantMap pm = p.toMap();
        positions.append(QVariantMap{ { QStringLiteral("tag"), pm.value(QStringLiteral("tag")) },
                                      { QStringLiteral("name"), pm.value(QStringLiteral("name")) },
                                      { QStringLiteral("available"), pm.value(QStringLiteral("available")) } });
    }
    QVariantList rows;
    for (const QVariant &rv : payload.value(QStringLiteral("rows")).toList()) {
        if (rows.size() >= kWristRowCap) break;
        const QVariantMap r = rv.toMap();
        if (r.value(QStringLiteral("numbersOnly")).toBool()) continue;
        const QVariantList points = r.value(QStringLiteral("points")).toList(), boxes = r.value(QStringLiteral("boxes")).toList();
        QVariantList cells, bx;
        for (int p = 0; p < 8; ++p) {
            const QVariantMap pt = points.value(p).toMap(), b = boxes.value(p).toMap();
            const bool outside = b.value(QStringLiteral("outside")).toBool();
            const QString tag = positions.value(p).toMap().value(QStringLiteral("tag")).toString();
            cells.append(QVariantMap{
                { QStringLiteral("index"), p }, { QStringLiteral("tag"), tag.isEmpty() ? QStringLiteral("P%1").arg(p + 1) : tag },
                { QStringLiteral("text"), b.value(QStringLiteral("medianText"), QStringLiteral("-")).toString() },
                { QStringLiteral("available"), !b.value(QStringLiteral("median")).isNull() && b.contains(QStringLiteral("median")) },
                { QStringLiteral("wash"), pt.value(QStringLiteral("wash"), QStringLiteral("none")).toString() },
                { QStringLiteral("banded"), pt.value(QStringLiteral("banded")).toBool() },
                { QStringLiteral("bandLo"), pt.value(QStringLiteral("bandLo"), none) }, { QStringLiteral("bandHi"), pt.value(QStringLiteral("bandHi"), none) },
                { QStringLiteral("outside"), outside }, { QStringLiteral("ring"), outside },
                { QStringLiteral("ringWord"), b.value(QStringLiteral("ringWord")).toString() },
                { QStringLiteral("warn"), QString() }, { QStringLiteral("warnShort"), QString() } });
            QVariantMap bo;
            for (const char *k : { "box", "values", "n", "median", "medianText", "wide", "wideText" }) bo.insert(QString::fromLatin1(k), b.value(QString::fromLatin1(k)));
            bx.append(bo);
        }
        QVariantMap row{
            { QStringLiteral("key"), r.value(QStringLiteral("dof")) }, { QStringLiteral("label"), r.value(QStringLiteral("label")).toString() },
            { QStringLiteral("metricKey"), r.value(QStringLiteral("key")) }, { QStringLiteral("hueKey"), r.value(QStringLiteral("key")) },
            { QStringLiteral("camera"), r.value(QStringLiteral("camera")).toString() }, { QStringLiteral("unit"), r.value(QStringLiteral("unit")).toString() },
            { QStringLiteral("poleText"), r.value(QStringLiteral("poleTopText")).toString() }, { QStringLiteral("styleText"), QString() },
            { QStringLiteral("yLo"), r.value(QStringLiteral("yLo")) }, { QStringLiteral("yHi"), r.value(QStringLiteral("yHi")) },
            { QStringLiteral("outCount"), r.value(QStringLiteral("outCount")) }, { QStringLiteral("cells"), cells }, { QStringLiteral("boxes"), bx },
            { QStringLiteral("numbersOnly"), false } };
        for (const char *k : { "dof", "name", "sub", "source", "poleTop", "poleBottom", "poleTopText", "poleBottomText" })
            row.insert(QString::fromLatin1(k), r.value(QString::fromLatin1(k)));
        rows.append(row);
    }
    if (rows.isEmpty()) return none;

    QVariantMap out;
    out.insert(QStringLiteral("kicker"), payload.value(QStringLiteral("kicker")).toString());
    out.insert(QStringLiteral("head"), wristSentenceText(payload.value(QStringLiteral("head"))));
    out.insert(QStringLiteral("styleLine"), payload.value(QStringLiteral("styleLine")).toString());
    out.insert(QStringLiteral("scoreLine"), payload.value(QStringLiteral("scoreLine")).toString());
    out.insert(QStringLiteral("profile"), QVariantMap{ { QStringLiteral("positions"), positions }, { QStringLiteral("rows"), rows },
                                                       { QStringLiteral("sub"), payload.value(QStringLiteral("sub")).toString() },
                                                       { QStringLiteral("caption"), payload.value(QStringLiteral("caption")).toString() } });

    // the score, swing by swing
    {
        const QVariantMap run = payload.value(QStringLiteral("scoreRun")).toMap();
        QVariantList pts;
        for (const QVariant &v : run.value(QStringLiteral("points")).toList()) {
            const QVariantMap m = v.toMap();
            const QVariant val = m.value(QStringLiteral("value"));
            const bool measured = m.value(QStringLiteral("measured")).toBool() && !val.isNull();
            pts.append(QVariantMap{ { QStringLiteral("swing"), swingOf(m) }, { QStringLiteral("value"), measured ? val : none },
                                    { QStringLiteral("measured"), measured },
                                    { QStringLiteral("text"), measured ? QString::number(qRound(val.toDouble())) : QStringLiteral("-") } });
        }
        const QVariantMap d = run.value(QStringLiteral("drift")).toMap();
        out.insert(QStringLiteral("scoreRun"), QVariantMap{
            { QStringLiteral("caption"), run.value(QStringLiteral("caption")).toString() }, { QStringLiteral("points"), pts },
            { QStringLiteral("box"), run.value(QStringLiteral("box")) }, { QStringLiteral("yRange"), run.value(QStringLiteral("yRange")) },
            { QStringLiteral("ticks"), run.value(QStringLiteral("ticks")).toList() },
            { QStringLiteral("legend"), QStringLiteral("A mark per swing, in the order struck; the box is the session. A short stub on the foot: a swing with no score.") },
            { QStringLiteral("drift"), QVariantMap{ { QStringLiteral("defined"), d.value(QStringLiteral("defined")).toBool() },
                                                    { QStringLiteral("clear"), d.value(QStringLiteral("clear")).toBool() },
                                                    { QStringLiteral("rText"), d.value(QStringLiteral("rText")).toString() },
                                                    { QStringLiteral("ciText"), d.value(QStringLiteral("ciText")).toString() },
                                                    { QStringLiteral("line"), d.value(QStringLiteral("line"), none) } } } });
    }

    // the style table, every swing (to the cap)
    {
        const QVariantMap st = payload.value(QStringLiteral("styles")).toMap();
        const QVariantList allCols = st.value(QStringLiteral("columns")).toList();
        QVariantList cols;
        for (const QVariant &v : allCols) {
            if (cols.size() >= kWristStyleColumnCap) break;
            const QVariantMap m = v.toMap();
            const int sw = swingOf(m);
            cols.append(QVariantMap{ { QStringLiteral("swing"), sw }, { QStringLiteral("label"), QStringLiteral("Swing %1").arg(sw) },
                                     { QStringLiteral("short"), QString::number(sw) } });
        }
        QVariantList srows;
        for (const QVariant &v : st.value(QStringLiteral("rows")).toList()) {
            const QVariantMap m = v.toMap();
            srows.append(QVariantMap{ { QStringLiteral("key"), m.value(QStringLiteral("key")) }, { QStringLiteral("name"), m.value(QStringLiteral("name")) },
                                      { QStringLiteral("cells"), m.value(QStringLiteral("cells")).toList().mid(0, cols.size()) } });
        }
        // paper's own words for the table: what the figures are, and the styles graded against as its foot
        const bool hasIndex = st.value(QStringLiteral("hasIndex")).toBool() && !srows.isEmpty();
        QStringList countBits;
        for (const QVariant &v : st.value(QStringLiteral("counts")).toList()) {
            const QVariantMap m = v.toMap();
            const int n = m.value(QStringLiteral("n")).toInt();
            countBits << (countBits.isEmpty() ? QStringLiteral("%1 on %2 swing%3").arg(m.value(QStringLiteral("name")).toString()).arg(n).arg(n == 1 ? QString() : QStringLiteral("s"))
                                              : QStringLiteral("%1 on %2").arg(m.value(QStringLiteral("name")).toString()).arg(n));
        }
        out.insert(QStringLiteral("styles"), QVariantMap{
            { QStringLiteral("columns"), cols }, { QStringLiteral("rows"), srows }, { QStringLiteral("counts"), st.value(QStringLiteral("counts")) },
            { QStringLiteral("hasIndex"), st.value(QStringLiteral("hasIndex")).toBool() }, { QStringLiteral("note"), st.value(QStringLiteral("note")).toString() },
            { QStringLiteral("more"), wristMoreWords(int(allCols.size() - cols.size()), "swing") },
            { QStringLiteral("legend"), hasIndex ? QStringLiteral("How much each swing resembles each style, %; in bold, the style it was graded against.") : QString() },
            { QStringLiteral("countsLine"), hasIndex && !countBits.isEmpty() ? QStringLiteral("Graded against: ") + countBits.join(QStringLiteral(", ")) + QLatin1Char('.') : QString() } });
    }

    // what keeps firing
    {
        const QVariantList all = payload.value(QStringLiteral("recurring")).toList();
        QVariantList rec;
        for (const QVariant &v : all) {
            if (rec.size() >= kWristRecurringCap) break;
            const QVariantMap m = v.toMap();
            QVariantList marks;
            for (const QVariant &mv : m.value(QStringLiteral("marks")).toList()) {
                if (marks.size() >= kWristStyleColumnCap) break;
                const QVariantMap mk = mv.toMap();
                marks.append(QVariantMap{ { QStringLiteral("swing"), swingOf(mk) }, { QStringLiteral("fired"), mk.value(QStringLiteral("fired")).toBool() } });
            }
            QVariantMap o;
            for (const char *k : { "id", "name", "severity", "tier", "count", "countText" }) o.insert(QString::fromLatin1(k), m.value(QString::fromLatin1(k)));
            o.insert(QStringLiteral("marks"), marks);
            rec.append(o);
        }
        out.insert(QStringLiteral("recurring"), rec);
        out.insert(QStringLiteral("recurringMore"), wristMoreWords(int(all.size() - rec.size())));
        out.insert(QStringLiteral("recurringEmpty"), payload.value(QStringLiteral("recurringEmpty")).toString());
        out.insert(QStringLiteral("recurringLegend"), rec.isEmpty() ? QString() : QStringLiteral("Each finding, swing by swing: a filled mark where it fired, a hollow one where it did not."));
    }

    out.insert(QStringLiteral("repeat"), payload.value(QStringLiteral("repeat")).toString());
    out.insert(QStringLiteral("sizeLine"), payload.value(QStringLiteral("sizeLine")).toString());

    // what moves with the lead wrist at impact
    {
        const QVariantMap mw = payload.value(QStringLiteral("movesWith")).toMap();
        if (mw.isEmpty()) {
            out.insert(QStringLiteral("movesWith"), none);
        } else {
            QVariantList mrows;
            for (const QVariant &v : mw.value(QStringLiteral("rows")).toList()) {
                if (mrows.size() >= kWristMovesWithCap) break;
                mrows.append(v);
            }
            const bool ok = mw.value(QStringLiteral("available")).toBool();
            out.insert(QStringLiteral("movesWith"), QVariantMap{
                { QStringLiteral("available"), ok }, { QStringLiteral("head"), wristSentenceText(mw.value(QStringLiteral("head"))) },
                { QStringLiteral("sub"), mw.value(QStringLiteral("sub")).toString() },
                { QStringLiteral("refusal"), ok ? QString() : wristSentenceText(mw.value(QStringLiteral("refusal"))) },
                { QStringLiteral("rows"), mrows },
                { QStringLiteral("chanceLine"), wristSentenceText(mw.value(QStringLiteral("chance")).toMap().value(QStringLiteral("line"))) },
                { QStringLiteral("note"), wristSentenceText(mw.value(QStringLiteral("note"))) } });
        }
    }

    const QStringList allTrust = payload.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
    QStringList trust;
    int trustMore = 0;
    for (const QString &l : allTrust) {
        if (l.trimmed().isEmpty()) continue;
        if (trust.size() >= kWristTrustCap) ++trustMore;
        else trust << l;
    }
    out.insert(QStringLiteral("trust"), trust);
    out.insert(QStringLiteral("trustMore"), wristMoreWords(trustMore));
    return out;
}

} // namespace pinpoint::report
