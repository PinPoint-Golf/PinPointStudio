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

// THE REPORT'S CLUB DELIVERY PAGE — the map's `delivery` block (docs/design/club_delivery_chart_design.md §4.8, stage 4),
// cut from the Charts Club delivery presenter's SESSION payload (charts/charts_delivery_presenter.h
// deliverySessionReportPayload: the numbers and words the Club delivery section's Session view draws, nothing picked,
// the fits over the golfer's day, every sentence counting "swings" — the report's word). Pure and header-only, like
// report_posture.h, so report_model_test proves its shape and absence without a session behind it; SessionReportModel
// feeds it the payload it read, the report's own swing numbers and the axis ticks (paper has no ticker to ask).
//
// WHAT IT DOES, AND ONLY THIS:
//   · ABSENT (null) when the payload is not available or no swing reads the swing plane (no down-the-line club track:
//     no page, as the Posture page has none without a posture row). Gated on DATA, never on the session's kind;
//   · keeps the presenter's sentences and figures VERBATIM (kicker, head, every median and middle half, the varies words,
//     the usual set-up figures, the fit and attack sentences, the pool words, the shape line, the trust lines), with
//     one cut: the hands' trust line ends at "your hands." — paper has no plot to shade;
//   · lays the scatter out in PpChScatter's own shape (the screen's PpChDeliveryFlight does the same in QML): the day's
//     swings with both readings, this session's in ink and numbered as the report numbers its swings, the day's other
//     sessions faint; the padded ranges the screen uses; the fitted line ONLY when the fit holds; and the axis ticks;
//   · numbers the shot-shape marks as the report numbers its swings;
//   · adds the few sentences the screen says by drawing and paper must say in words: the band's sub (or why there is
//     none), the scatter's legend, the grid's sub (what it was read from, and how many swings have no shape), and the
//     ball flight's line when no swing has both readings;
//   · caps the trust lines and words the "N more".
//
//  delivery: {
//    kicker ("10 swings  ·  7-iron  ·  Club delivery"), head (the session's head line),
//    band: { shown (≥ 5 swings with the curve), n, sub ("10 swings with the swing plane curve. …" | why there is no band),
//            positions: [{ tag, name, box: BOX, values: [per shot], medianText ("59°" | "-"), halfText ("±1.5°" | "-") }] (7),
//            lo, hi (the boxes' one scale), varies ("" none) },
//    usual: [{ label ("address" | "shoulders" | "hands steeper coming down"), text ("59°" | "+3°" | "-") }] (3),
//    flight: { text (the fit's sentence, or why there is none; "" when it is the head word for word), attackText (""),
//              pool ("on 20 Oct (3 sessions)" | ""), holds,
//              scatter: { xRange, yRange, points: [{ ordinal (the report's swing | −1 another session), x, y, faint, ring }],
//                         line ({ x0, y0, x1, y1 } | null: only when it holds), clear, caption "", xLabel, yLabel,
//                         xUnit, yUnit, xTicks, yTicks: [{ v, text }] } | null (no swing with both readings),
//              legend ("" with no scatter) },
//    shape: { marks: [{ ordinal (= swing), swing, available, start, curve, word, fromBall, picked: false }],
//             shown (any mark with a shape), line ("Pull, slight fade on 5 of 8 swings" | ""), sub },
//    trust: [string] (≤ kDeliveryReportTrustCap), trustMore ("" | "N more") }
#include "../charts/session_delivery.h"   // kDeliverySessionBandShots: the band's floor, the screen's own

#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace pinpoint::report {

constexpr int kDeliveryReportTrustCap = 12;      // the presenter says at most nine (cameras, hands, pool, three fits, monitor,
                                                 // literature)
constexpr int kDeliveryReportPositions = 7;      // P1..P7

// The axis ticks for a scale: SessionReportModel passes ChartsRelationsPresenter::axisTicks; empty = no ticks.
using DeliveryReportTicks = std::function<QVariantList(double lo, double hi, int maxTicks, const QString &unit)>;

inline QString deliveryReportMoreWords(int more) { return more > 0 ? QStringLiteral("%1 more").arg(more) : QString(); }

inline QString deliveryReportSwings(int n) { return n == 1 ? QStringLiteral("1 swing") : QStringLiteral("%1 swings").arg(n); }

// `swingNoOfDir`: QDir::cleanPath(swing folder) → the report's swing number (1-based). A shot the report does not number
// keeps the presenter's ordinal.
inline QVariant deliveryReportBlock(const QVariantMap &payload, const QHash<QString, int> &swingNoOfDir,
                                    const DeliveryReportTicks &ticks = {})
{
    const QVariant none = QVariant::fromValue(nullptr);
    if (!payload.value(QStringLiteral("available")).toBool()) return none;
    if (payload.value(QStringLiteral("planeShots")).toInt() <= 0) return none;
    const auto fin = [](const QVariant &v) { return !v.isNull() && v.isValid() && std::isfinite(v.toDouble()); };
    const auto swingOf = [&](const QVariantMap &m) {
        const QString dir = QDir::cleanPath(m.value(QStringLiteral("swingDir")).toString());
        return swingNoOfDir.value(dir, m.value(QStringLiteral("ordinal")).toInt());
    };

    QVariantMap out;
    const QString head = payload.value(QStringLiteral("headText")).toString();
    out.insert(QStringLiteral("kicker"), payload.value(QStringLiteral("kicker")).toString());
    out.insert(QStringLiteral("head"), head);

    // ── the plane at each position: a box per P-position on one scale ──
    {
        const QVariantMap band = payload.value(QStringLiteral("band")).toMap();
        const bool shown = band.value(QStringLiteral("shown")).toBool();
        const int n = band.value(QStringLiteral("n")).toInt();
        QVariantList positions;
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const QVariant &pv : band.value(QStringLiteral("positions")).toList()) {
            if (positions.size() >= kDeliveryReportPositions) break;
            const QVariantMap p = pv.toMap();
            const QVariantMap box = p.value(QStringLiteral("box")).toMap();
            for (const char *k : { "min", "max" }) {
                const QVariant v = box.value(QString::fromLatin1(k));
                if (fin(v)) { lo = std::min(lo, v.toDouble()); hi = std::max(hi, v.toDouble()); }
            }
            const QString med = p.value(QStringLiteral("medianText")).toString();
            const QString half = p.value(QStringLiteral("halfText")).toString();
            positions.append(QVariantMap{ { QStringLiteral("tag"), p.value(QStringLiteral("tag")).toString() },
                                          { QStringLiteral("name"), p.value(QStringLiteral("name")).toString() },
                                          { QStringLiteral("box"), box.isEmpty() ? none : QVariant(box) },
                                          { QStringLiteral("values"), p.value(QStringLiteral("values")).toList() },
                                          { QStringLiteral("medianText"), med.isEmpty() ? QStringLiteral("-") : med },
                                          { QStringLiteral("halfText"), half.isEmpty() ? QStringLiteral("-") : half } });
        }
        if (!std::isfinite(lo)) { lo = 0; hi = 1; }
        const double pad = std::max(0.5, (hi - lo) * 0.06);
        const QString sub = shown
            ? deliveryReportSwings(n) + QStringLiteral(" with the swing plane curve. Under each box, the median and ± half the middle half.")
            : QStringLiteral("The plane at each position is drawn from %1 swings with the swing plane curve; this session has %2.")
                  .arg(pinpoint::charts::kDeliverySessionBandShots).arg(n);
        out.insert(QStringLiteral("band"), QVariantMap{
            { QStringLiteral("shown"), shown }, { QStringLiteral("n"), n }, { QStringLiteral("sub"), sub },
            { QStringLiteral("positions"), shown ? positions : QVariantList{} },
            { QStringLiteral("lo"), lo - pad }, { QStringLiteral("hi"), hi + pad },
            { QStringLiteral("varies"), shown ? payload.value(QStringLiteral("varies")).toMap().value(QStringLiteral("words")).toString() : QString() } });
    }

    // ── the usual set-up: the address line, the shoulder plane, the hands' steepening ──
    {
        const QVariantMap u = payload.value(QStringLiteral("usual")).toMap();
        const auto txt = [&](const char *k) { const QString t = u.value(QString::fromLatin1(k)).toString(); return t.isEmpty() ? QStringLiteral("-") : t; };
        out.insert(QStringLiteral("usual"), QVariantList{
            QVariantMap{ { QStringLiteral("label"), QStringLiteral("address") }, { QStringLiteral("text"), txt("addressText") } },
            QVariantMap{ { QStringLiteral("label"), QStringLiteral("shoulders") }, { QStringLiteral("text"), txt("shouldersText") } },
            QVariantMap{ { QStringLiteral("label"), QStringLiteral("hands steeper coming down") }, { QStringLiteral("text"), txt("steepeningText") } } });
    }

    // ── plane and your ball flight: the day's fit, its sentence and the scatter ──
    {
        const QVariantMap fl = payload.value(QStringLiteral("flight")).toMap();
        const QVariantMap fit = fl.value(QStringLiteral("fit")).toMap();
        const QVariantMap sc = fl.value(QStringLiteral("scatter")).toMap();
        const bool holds = fit.value(QStringLiteral("holds")).toBool();
        QString text = fl.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            text = QStringLiteral("No swing in this session has both the launch monitor's path and the plane, so the plane is not set "
                                  "against the ball flight.");
        if (text == head) text.clear();   // the head says it already

        QVariant scatter = none;
        QString legend;
        QVariantList pts;
        for (const QVariant &pv : sc.value(QStringLiteral("points")).toList()) {
            const QVariantMap p = pv.toMap();
            if (!fin(p.value(QStringLiteral("x"))) || !fin(p.value(QStringLiteral("y")))) continue;
            const bool mine = p.value(QStringLiteral("thisSession")).toBool();
            pts.append(QVariantMap{ { QStringLiteral("ordinal"), mine ? swingOf(p) : -1 },
                                    { QStringLiteral("x"), p.value(QStringLiteral("x")).toDouble() },
                                    { QStringLiteral("y"), p.value(QStringLiteral("y")).toDouble() },
                                    { QStringLiteral("faint"), !mine }, { QStringLiteral("ring"), false } });
        }
        if (!pts.isEmpty() && fin(sc.value(QStringLiteral("xLo"))) && fin(sc.value(QStringLiteral("yLo")))) {
            // the screen's padding (PpChDeliveryFlight): 6 % across, 8 % up, never under half a degree
            const double xl = sc.value(QStringLiteral("xLo")).toDouble(), xh = sc.value(QStringLiteral("xHi")).toDouble();
            const double yl = sc.value(QStringLiteral("yLo")).toDouble(), yh = sc.value(QStringLiteral("yHi")).toDouble();
            const double px = std::max(0.5, (xh - xl) * 0.06), py = std::max(0.5, (yh - yl) * 0.08);
            const QString unit = sc.value(QStringLiteral("unit")).toString().isEmpty() ? QStringLiteral("°") : sc.value(QStringLiteral("unit")).toString();
            const QVariantMap xr{ { QStringLiteral("lo"), xl - px }, { QStringLiteral("hi"), xh + px } };
            const QVariantMap yr{ { QStringLiteral("lo"), yl - py }, { QStringLiteral("hi"), yh + py } };
            int others = 0;
            for (const QVariant &p : pts) if (p.toMap().value(QStringLiteral("faint")).toBool()) ++others;
            scatter = QVariantMap{
                { QStringLiteral("xRange"), xr }, { QStringLiteral("yRange"), yr }, { QStringLiteral("points"), pts },
                { QStringLiteral("line"), holds ? sc.value(QStringLiteral("line")) : none }, { QStringLiteral("clear"), holds },
                { QStringLiteral("caption"), QString() },
                { QStringLiteral("xLabel"), sc.value(QStringLiteral("xLabel")).toString() },
                { QStringLiteral("yLabel"), sc.value(QStringLiteral("yLabel")).toString() },
                { QStringLiteral("xUnit"), unit }, { QStringLiteral("yUnit"), unit },
                { QStringLiteral("xTicks"), ticks ? ticks(xl - px, xh + px, 4, unit) : QVariantList{} },
                { QStringLiteral("yTicks"), ticks ? ticks(yl - py, yh + py, 4, unit) : QVariantList{} } };
            legend = (others > 0 ? QStringLiteral("A dot per swing with both readings: this session's in ink, the day's other sessions faint. ")
                                 : QStringLiteral("A dot per swing with both readings. "))
                   + (holds ? QStringLiteral("The line is your fit.") : QStringLiteral("No line: the fit does not hold."));
        }
        out.insert(QStringLiteral("flight"), QVariantMap{
            { QStringLiteral("text"), text }, { QStringLiteral("attackText"), fl.value(QStringLiteral("attackText")).toString() },
            { QStringLiteral("pool"), fl.value(QStringLiteral("pool")).toMap().value(QStringLiteral("words")).toString() },
            { QStringLiteral("holds"), holds }, { QStringLiteral("scatter"), scatter }, { QStringLiteral("legend"), legend } });
    }

    // ── shot shape: every swing's mark on the 3×3 grid ──
    {
        const QVariantMap sh = payload.value(QStringLiteral("shapes")).toMap();
        QVariantList marks;
        int ball = 0, club = 0, without = 0;
        for (const QVariant &mv : sh.value(QStringLiteral("marks")).toList()) {
            const QVariantMap m = mv.toMap();
            const bool avail = m.value(QStringLiteral("available")).toBool();
            const bool fromBall = m.value(QStringLiteral("fromBall")).toBool();
            if (!avail) ++without; else if (fromBall) ++ball; else ++club;
            const int swing = swingOf(m);
            marks.append(QVariantMap{ { QStringLiteral("ordinal"), swing }, { QStringLiteral("swing"), swing },
                                      { QStringLiteral("available"), avail },
                                      { QStringLiteral("start"), m.value(QStringLiteral("start")).toInt() },
                                      { QStringLiteral("curve"), m.value(QStringLiteral("curve")).toInt() },
                                      { QStringLiteral("word"), m.value(QStringLiteral("word")).toString() },
                                      { QStringLiteral("fromBall"), fromBall }, { QStringLiteral("picked"), false } });
        }
        QString sub;
        if (ball + club == 0) {
            sub = QStringLiteral("The launch monitor gave no start line or curve on this session's swings.");
        } else {
            QStringList from;
            if (ball > 0) from << QStringLiteral("the ball's start and spin axis on %1").arg(deliveryReportSwings(ball));
            if (club > 0) from << QStringLiteral("the club's face and path on %1").arg(deliveryReportSwings(club));
            sub = QStringLiteral("Where the ball started and how it curved, from the launch monitor: ") + from.join(QStringLiteral(", "))
                + QStringLiteral(".")
                + (without > 0 ? QStringLiteral(" %1 no shape from the monitor.").arg(deliveryReportSwings(without) + (without == 1 ? QStringLiteral(" has") : QStringLiteral(" have")))
                               : QString());
        }
        out.insert(QStringLiteral("shape"), QVariantMap{
            { QStringLiteral("marks"), marks }, { QStringLiteral("shown"), ball + club > 0 },
            { QStringLiteral("line"), sh.value(QStringLiteral("line")).toString() }, { QStringLiteral("sub"), sub } });
    }

    // ── how far can I trust this? said once ──
    {
        QStringList trust;
        int more = 0;
        for (QString l : payload.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList()) {
            if (l.trimmed().isEmpty()) continue;
            l.replace(QStringLiteral(", shaded on the plot."), QStringLiteral("."));   // paper has no plot of the curve
            if (trust.size() >= kDeliveryReportTrustCap) ++more;
            else trust << l;
        }
        out.insert(QStringLiteral("trust"), trust);
        out.insert(QStringLiteral("trustMore"), deliveryReportMoreWords(more));
    }
    return out;
}

} // namespace pinpoint::report
