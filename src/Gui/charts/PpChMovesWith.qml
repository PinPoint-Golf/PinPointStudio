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

// PPCHMOVESWITH — "Moves with …" (design §2.3): every other reading ranked by |r| with the Up reading across
// the session (ChartsRelationsPresenter relations.ledger): r (ink when it clears chance, ink3 inside it), its
// 95 % interval DRAWN on a −1…1 rule, n, the slope in units, and the pair as a sparkline. A same-quantity pair
// carries ‡ and no rank (labelled, not ranked as a finding). A row puts that reading across the scatter.
//   column      tier  below it folds to
//   rank, name  1     (line 1)
//   r           1     (line 1)
//   interval    2     line 2, its text "(.37 to .91)"
//   n, slope    3     line 2, "n 15 · +1.3 mph per °"
//   the pair    4     the scatter above draws the selected pair
// objectNames: chMovesWith; each row chMovesRow (+ readingId).
Column {
    id: cl
    objectName: "chMovesWith"
    property QtObject roles: null
    property var rows: []
    property string acrossId: ""
    property bool interactive: true
    property bool atom: false
    readonly property var keyKinds: []
    signal rowActivated(string readingId)
    readonly property real em: roles ? roles.em : 14
    readonly property bool showIv: roles ? roles.shows(2, width) : true
    readonly property bool showNs: roles ? roles.shows(3, width) : true
    readonly property bool showSp: roles ? roles.shows(4, width) : false
    readonly property real gap: roles ? roles.u(12) : 12
    readonly property real rankW: Math.round(1.8 * em)
    readonly property real rW: Math.round(3.4 * em)
    readonly property real ivW: Math.round(10 * em)
    readonly property real nW: Math.round(2.4 * em)
    readonly property real slW: Math.round(10 * em)
    readonly property real spW: Math.round(6 * em)
    readonly property real nameW: Math.max(8 * em, width - rankW - rW - gap * 2 - (showIv ? ivW + gap : 0) - (showNs ? nW + slW + 2 * gap : 0) - (showSp ? spW + gap : 0))

    Item {
        width: cl.width; height: cl.roles ? cl.roles.u(22) : 22
        Rectangle { width: parent.width; height: cl.roles ? cl.roles.r.figureRuleTop : 1; color: cl.roles ? cl.roles.rule : "black" }
        Row {
            y: cl.roles ? cl.roles.u(6) : 6; spacing: cl.gap
            Item { width: cl.rankW; height: 1 }
            Text { width: cl.nameW; text: "Reading"; font: cl.roles.fLabel; color: cl.roles.ink3 }
            Text { width: cl.rW; horizontalAlignment: Text.AlignRight; text: "r"; font: cl.roles.fLabelUnit; color: cl.roles.ink3 }
            Text { visible: cl.showIv; width: cl.ivW; horizontalAlignment: Text.AlignHCenter; text: "95 % interval"; font: cl.roles.fLabel; color: cl.roles.ink3 }
            Text { visible: cl.showNs; width: cl.nW; horizontalAlignment: Text.AlignRight; text: "n"; font: cl.roles.fLabelUnit; color: cl.roles.ink3 }
            Text { visible: cl.showNs; width: cl.slW; text: "Slope"; font: cl.roles.fLabel; color: cl.roles.ink3 }
            Text { visible: cl.showSp; width: cl.spW; text: "The pair"; font: cl.roles.fLabel; color: cl.roles.ink3 }
        }
    }
    Repeater {
        model: cl.roles ? cl.rows : []
        delegate: Item {
            id: row
            objectName: "chMovesRow"
            required property var modelData
            readonly property string readingId: modelData.readingId
            readonly property bool on: modelData.readingId === cl.acrossId
            readonly property bool two: !cl.showIv || !cl.showNs
            readonly property real line1: Math.max(cl.roles.u(24), nm.implicitHeight + cl.roles.u(8))
            width: cl.width; height: line1 + (two ? cl.roles.u(15) : 0) + cl.roles.u(2)
            Rectangle { anchors.fill: parent; visible: ma.containsMouse && cl.interactive; color: cl.roles.rowHover }
            Rectangle { anchors.fill: parent; visible: row.on && !cl.roles.isPrint; color: cl.roles.pickWash }
            Row {
                y: cl.roles.u(4); spacing: cl.gap
                Text { width: cl.rankW; horizontalAlignment: Text.AlignRight; text: row.modelData.rank > 0 ? String(row.modelData.rank) : ""; font: cl.roles.fFigS; color: cl.roles.ink3; anchors.baseline: nm.baseline }
                Text {
                    id: nm; width: cl.nameW; wrapMode: Text.WordWrap; textFormat: Text.StyledText
                    leftPadding: cl.roles.u(cl.roles.ch.swatch) + cl.roles.u(5)
                    text: cl.roles.esc(row.modelData.label) + (row.modelData.same ? " " + cl.roles.esc(row.modelData.sameMark || "") : "")
                          + "  <font color=\"" + cl.roles.hex(cl.roles.ink3) + "\">" + cl.roles.esc(row.modelData.tag || "") + "</font>"
                    font: row.on ? cl.roles.fBodyM : cl.roles.fBody; color: cl.roles.ink
                    Rectangle { y: Math.round(nm.baselineOffset - height - nm.font.pixelSize * 0.18); width: cl.roles.u(cl.roles.ch.swatch); height: width
                                radius: cl.roles.square ? 0 : width / 2; color: cl.roles.hue(row.modelData.key) }
                }
                Text { width: cl.rW; horizontalAlignment: Text.AlignRight; anchors.baseline: nm.baseline; text: row.modelData.rText; font: cl.roles.fFig
                       color: row.modelData.clear ? cl.roles.ink : cl.roles.ink3 }
                Item {
                    visible: cl.showIv; width: cl.ivW; height: cl.roles.u(16)
                    function px(v) { return Math.round((v + 1) / 2 * width) }
                    Rectangle { width: parent.width; height: cl.roles.hw; y: Math.round(parent.height / 2); color: cl.roles.hair }
                    Rectangle { x: Math.round(parent.width / 2); width: cl.roles.hw; height: parent.height; color: cl.roles.ink3; opacity: 0.6 }
                    Rectangle {
                        visible: row.modelData.lo !== null && row.modelData.hi !== null
                        x: parent.px(row.modelData.lo); width: Math.max(1, parent.px(row.modelData.hi) - parent.px(row.modelData.lo))
                        y: Math.round(parent.height / 2) - 1; height: cl.roles.isPrint ? 1.5 : 2
                        color: row.modelData.clear ? cl.roles.ink2 : cl.roles.ink3
                    }
                    PpMark { visible: row.modelData.r !== null; roles: cl.roles; fired: false; cx: parent.px(row.modelData.r || 0); cy: Math.round(parent.height / 2) }
                }
                Text { visible: cl.showNs; width: cl.nW; horizontalAlignment: Text.AlignRight; anchors.baseline: nm.baseline; text: String(row.modelData.n); font: cl.roles.fFigS; color: cl.roles.ink2 }
                Text { visible: cl.showNs; width: cl.slW; anchors.baseline: nm.baseline; text: row.modelData.slopeText || "-"; font: cl.roles.fFigS; color: cl.roles.ink2; wrapMode: Text.WordWrap }
                // the pair as a sparkline scatter: each shot one dot, on each reading's session range (pair.xRange / yRange)
                Canvas {
                    visible: cl.showSp; width: cl.spW; height: cl.roles.u(18)
                    readonly property var pr: row.modelData.pair
                    readonly property var deps: [pr, cl.roles.ink2, width]
                    onDepsChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        if (!pr || !pr.xs) return
                        const ok = function (v) { return v !== null && v !== undefined && isFinite(v) }
                        const xr = pr.xRange || {}, yr = pr.yRange || {}
                        const x0 = xr.lo, x1 = xr.hi, y0 = yr.lo, y1 = yr.hi
                        if (!ok(x0) || !ok(x1) || !ok(y0) || !ok(y1)) return
                        ctx.fillStyle = cl.roles.ink2
                        for (let i = 0; i < pr.xs.length; ++i) {
                            if (!ok(pr.xs[i]) || !ok(pr.ys[i])) continue
                            const xx = 2 + (pr.xs[i] - x0) / Math.max(1e-9, x1 - x0) * (width - 4)
                            const yy = height - 2 - (pr.ys[i] - y0) / Math.max(1e-9, y1 - y0) * (height - 4)
                            ctx.fillRect(Math.round(xx) - 1, Math.round(yy) - 1, 2.5, 2.5)
                        }
                    }
                }
            }
            Row {
                visible: row.two
                x: cl.rankW + cl.gap; y: row.line1 - cl.roles.u(3); spacing: cl.roles.u(10)
                Text { visible: !cl.showIv; text: row.modelData.ciText || ""; font: cl.roles.fFigLabel; color: cl.roles.ink3 }
                Text { visible: !cl.showNs; text: "n"; font: cl.roles.fSmall; color: cl.roles.ink3 }
                Text { visible: !cl.showNs; text: String(row.modelData.n); font: cl.roles.fFigLabel; color: cl.roles.ink3 }
                Text { visible: !cl.showNs; text: row.modelData.slopeText || ""; font: cl.roles.fFigLabel; color: cl.roles.ink3 }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: cl.roles.hw; color: cl.roles.hair }
            MouseArea { id: ma; anchors.fill: parent; enabled: cl.interactive; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cl.rowActivated(row.modelData.readingId) }
        }
    }
}
