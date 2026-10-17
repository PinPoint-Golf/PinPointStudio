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
import PinPointStudio

// PPCHKEYFIGURES — the key figures under a heavy rule (B's KeyFigs; design §2.1 "KeyFigures with B's place
// words"): a label with the metric's swatch, the figure with its unit, and a quiet foot that places it in
// the session ("median 72 · 3rd highest"). A warn figure is set in warn AND carries its word ("late",
// "out of its corridor") — never colour alone. Wraps n → n/2 → n/4, each figure at least 10 em
// (flows.keyFigure). Items are ChartsRelationsPresenter.thisShot.keyFigures.items, shown as given.
// objectName: chKeyFigures; each figure chKeyFigure (+ readingId).
Item {
    id: kf
    objectName: "chKeyFigures"
    property QtObject roles: null
    property var items: []                       // [{ readingId, key, label, figure, unit, foot, warn, warnWord }]
    property bool atom: true
    readonly property var keyKinds: []
    // a figure a gate refused, bounded or withheld (item.trustSubject): its foot opens the reason
    signal trustClicked(string subject)
    readonly property int n: (items || []).length
    readonly property int fit: roles ? roles.columns(width, roles.flows.keyFigure, 0) : 1
    readonly property int cols: Math.max(1, fit >= n ? n : fit >= Math.ceil(n / 2) ? Math.ceil(n / 2) : Math.ceil(n / 4))
    readonly property real cw: width / cols
    readonly property real ch: roles ? roles.u(58) : 58
    visible: n > 0
    implicitHeight: n > 0 && roles ? roles.r.figureRuleTop + Math.ceil(n / cols) * ch + roles.u(2) : 0
    height: implicitHeight
    Rectangle { visible: kf.n > 0; width: parent.width; height: kf.roles ? kf.roles.r.figureRuleTop : 1; color: kf.roles ? kf.roles.rule : "black" }
    Repeater {
        model: kf.roles ? kf.items : []
        delegate: Item {
            id: it
            objectName: "chKeyFigure"
            required property var modelData
            required property int index
            readonly property string readingId: modelData.readingId || ""
            readonly property string key: modelData.key || ""
            readonly property int col: index % kf.cols
            readonly property int row: Math.floor(index / kf.cols)
            x: col * kf.cw; y: kf.roles.r.figureRuleTop + row * kf.ch; width: kf.cw; height: kf.ch
            readonly property real lx: col > 0 ? kf.roles.u(12) : 0
            Rectangle { visible: it.col > 0; width: kf.roles.hw; height: parent.height - kf.roles.u(14); y: kf.roles.u(7); color: kf.roles.hair }
            Row {
                x: it.lx; y: kf.roles.u(6); spacing: kf.roles.u(5)
                width: parent.width - x - kf.roles.u(4)
                Rectangle { visible: it.key !== ""; anchors.verticalCenter: lb.verticalCenter; width: kf.roles.u(kf.roles.ch.swatch); height: width
                            radius: kf.roles.square ? 0 : width / 2; color: kf.roles.hue(it.key) }
                Text { id: lb; width: parent.width - kf.roles.u(12); wrapMode: Text.WordWrap; text: it.modelData.label; font: kf.roles.fLabel; color: kf.roles.ink3 }
            }
            Row {
                x: it.lx; y: kf.roles.u(21); spacing: kf.roles.u(4)
                // a figure a gate refused (item.gated) is the gate's words, quiet, in the link colour; it opens the reason
                Text { id: fg; text: it.modelData.figure + (it.modelData.gated ? " ›" : "")
                       font: it.modelData.gated ? kf.roles.fBody : kf.roles.fFigL
                       color: it.modelData.gated ? kf.roles.link : it.modelData.warn ? kf.roles.warn : kf.roles.ink
                       MouseArea { enabled: !!it.modelData.gated && !!it.modelData.trustSubject; anchors.fill: parent; anchors.margins: -2
                                   cursorShape: Qt.PointingHandCursor; onClicked: kf.trustClicked(it.modelData.trustSubject) } }
                Text { text: it.modelData.unit || ""; font: kf.roles.fSmall; color: kf.roles.ink3; anchors.baseline: fg.baseline }
                Text { visible: it.modelData.warn && !!it.modelData.warnWord; text: it.modelData.warnWord || ""; font: kf.roles.fSmall; color: kf.roles.warn; anchors.baseline: fg.baseline }
            }
            Text { id: foot; x: it.lx; y: kf.roles.u(42); width: parent.width - x - kf.roles.u(4); wrapMode: Text.WordWrap
                   readonly property bool why: !!it.modelData.trustSubject
                   objectName: why ? "chKeyFigureWhy:" + it.modelData.trustSubject : ""
                   text: (it.modelData.foot || "") + (why && !it.modelData.gated ? "  ›" : "")
                   font: kf.roles.fSmall; color: why && !it.modelData.gated ? kf.roles.link : kf.roles.ink3
                   MouseArea { enabled: foot.why && !it.modelData.gated; anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor
                               onClicked: kf.trustClicked(it.modelData.trustSubject) } }
        }
    }
}
