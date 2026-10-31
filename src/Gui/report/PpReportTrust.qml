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

import QtQuick

// "HOW FAR CAN I TRUST THIS?" — the trust note of a report section page (Posture, Wrist, Club delivery), said once and
// open (paper has no fold): the heading, the section's trust lines, then "N more" (`more`). ONE pager atom.
//   columns 1 — the lines one under another, indented, in the small style (the Posture and Wrist pages);
//   columns 2 — two columns across the full width, each under a hairline, in the note style (fMicro at 1.25), split
//               where the columns come out most nearly level (the Club delivery page: Order page's side-by-side idiom,
//               so the whole reading sits on one page — Mark, 29 Oct).
// Every line is the map's. The page sets the objectName (reportPostureTrust, …) and `lineName` names each line
// (reportWristTrustLine, reportDeliveryTrustLine; "" for none). FOLIO LIGHT through `ro`.
Column {
    id: tb
    property QtObject ro: null
    property var lines: []
    property string more: ""
    property int columns: 1
    property string lineName: ""
    property bool atom: true
    readonly property bool two: columns === 2
    readonly property real colGap: 18                   // the Order page's side-by-side columns (PpOrderPage ColsRow)
    readonly property real colW: (width - colGap) / 2
    // two columns: the lines in their order, split where the columns come out most nearly level (by length: the columns
    // share one width and one type, so a line's length stands for its height)
    readonly property int split: {
        const n = lines.length
        if (n < 2) return n
        const len = lines.map(function (t) { return String(t).length + 60 })   // + a line's own padding and ragged end
        const total = len.reduce(function (a, b) { return a + b }, 0)
        let best = 1, bestD = Infinity, acc = 0
        for (let i = 1; i < n; ++i) {
            acc += len[i - 1]
            const d = Math.abs(total - 2 * acc)
            if (d < bestD) { bestD = d; best = i }
        }
        return best
    }
    width: 600

    Item { width: 1; height: tb.two ? 8 : 18 }
    Text { width: parent.width; text: "How far can I trust this?"; font: tb.ro.fH3; color: tb.ro.ink }
    Item { width: 1; height: tb.two ? 5 : 4 }
    // one column: each line indented, in the small style
    Repeater {
        model: tb.two ? [] : tb.lines
        delegate: Text {
            required property var modelData
            objectName: tb.lineName
            width: tb.width; leftPadding: 12; bottomPadding: 3
            text: String(modelData); font: tb.ro.fSmall; color: tb.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25
        }
    }
    // two columns across the full width, each under a hairline, in the report's note style — its smallest, never smaller
    Row {
        visible: tb.two
        spacing: tb.colGap
        Repeater {
            model: tb.two ? [tb.lines.slice(0, tb.split), tb.lines.slice(tb.split)] : []
            delegate: Column {
                required property var modelData
                width: tb.colW; spacing: 4
                Rectangle { width: parent.width; height: tb.ro.hw; color: tb.ro.hair }
                Repeater {
                    model: modelData
                    delegate: Text {
                        required property var modelData
                        objectName: tb.lineName
                        width: tb.colW
                        text: String(modelData); font: tb.ro.fMicro; color: tb.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25
                    }
                }
            }
        }
    }
    Text { width: parent.width; visible: text !== ""; leftPadding: tb.two ? 0 : 12; topPadding: tb.two ? 3 : 0
           text: tb.more; font: tb.ro.fMicro; color: tb.ro.ink3; wrapMode: Text.WordWrap }
}
