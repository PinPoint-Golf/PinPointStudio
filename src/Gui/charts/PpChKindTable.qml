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

// PPCHKINDTABLE — Kinds' members and what else separates them (design §2.4, D7): the kinds' members in shot
// order (a number brushes that shot everywhere and a click puts it in the carousel; the picked one in its
// column wash), with the most typical shot named; a neutral "not measured" set when the presenter has one;
// then the separation table — each reading's two medians, d (ink when it clears the d chance gives at these
// sizes, ink3 inside it) and the two kinds' boxes on one scale, the first kind above, this shot ringed in its
// own kind. Same-quantity rows carry ‡. Everything is ChartsRelationsPresenter.kinds'.
//   column        tier  below it folds to
//   name, d       1     (line 1)
//   medians, unit 3     line 2, "faster 1630 · slower 1204 °/s"
//   boxes         1     (line 1, narrower)
// objectNames: chKindMembers, chKindTable; each separation row chKindRow (+ readingId).
Column {
    id: kt
    property QtObject roles: null
    property var brush: null
    property var kinds: null
    property bool interactive: true
    property bool atom: false
    readonly property var keyKinds: ["box", "ring"]
    signal shotActivated(int ordinal)
    spacing: roles ? roles.u(14) : 14
    readonly property var groups: kinds && kinds.groups ? kinds.groups : []
    readonly property var names: kinds && kinds.names ? kinds.names : ["", ""]
    readonly property var sep: kinds && kinds.separation ? kinds.separation : null
    readonly property real em: roles ? roles.em : 14
    readonly property bool cols: roles ? roles.shows(3, width) : true
    readonly property real gap: roles ? roles.u(12) : 12
    readonly property real medW: Math.round(4.8 * em)
    readonly property real unitW: Math.round(3.6 * em)
    readonly property real dW: Math.round(3 * em)
    readonly property real boxW: Math.round((roles && roles.sizeClass(width) === "narrow" ? 9 : roles && roles.atLeast(width, "medium") ? 20 : 13) * em)
    readonly property real nameW: Math.max(7 * em, width - boxW - dW - 2 * gap - (cols ? 2 * medW + unitW + 3 * gap : 0))

    // ── the members ──
    Column {
        objectName: "chKindMembers"
        width: kt.width; spacing: kt.roles ? kt.roles.u(4) : 4
        visible: kt.groups.length > 0
        Rectangle { width: parent.width; height: kt.roles ? kt.roles.r.figureRuleTop : 1; color: kt.roles ? kt.roles.rule : "black" }
        Repeater {
            model: kt.roles ? kt.groups : []
            delegate: Item {
                required property var modelData
                width: kt.width; height: Math.max(kt.roles.u(26), fw.height + kt.roles.u(8))
                Row {
                    id: kn; y: kt.roles.u(5); width: Math.round(8 * kt.em); spacing: kt.roles.u(5)
                    Text { text: modelData.name; font: kt.roles.fBodyM; color: modelData.neutral ? kt.roles.ink2 : kt.roles.ink }
                    Text { text: String(modelData.n); font: kt.roles.fFig; color: kt.roles.ink3 }
                }
                Flow {
                    id: fw; x: kn.width + kt.roles.u(12); y: kt.roles.u(5); width: parent.width - x; spacing: kt.roles.u(12)
                    Repeater {
                        model: modelData.members || []
                        delegate: Text {
                            required property var modelData
                            readonly property int no: modelData.ordinal
                            readonly property bool pk: modelData.picked === true
                            text: String(no); font: pk ? kt.roles.fBodyM : kt.roles.fFig
                            color: (kt.brush && kt.brush.hoveredShot === no) || pk ? kt.roles.ink : kt.roles.ink2
                            opacity: kt.brush ? kt.brush.shotOpacity(no, kt.roles.dim) : 1
                            Rectangle { visible: parent.pk || (!!kt.brush && kt.brush.hoveredShot === parent.no); anchors.fill: parent; anchors.margins: -kt.roles.u(3)
                                        color: parent.pk ? kt.roles.pickWash : kt.roles.hoverWash; z: -1 }
                            MouseArea { anchors.fill: parent; enabled: kt.interactive; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onContainsMouseChanged: if (kt.brush) kt.brush.hoverShot(parent.no, containsMouse)
                                        onClicked: kt.shotActivated(parent.no) }
                        }
                    }
                    Text { visible: !!modelData.medoidText; text: modelData.medoidText || ""; font: kt.roles.fSmall; color: kt.roles.ink3 }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: kt.roles.hw; color: kt.roles.hair }
            }
        }
    }

    // ── what else separates them ──
    Column {
        objectName: "chKindTable"
        width: kt.width
        visible: !!kt.sep && (kt.sep.rows || []).length > 0
        Item {
            width: kt.width; height: kt.roles ? kt.roles.u(22) : 22
            Rectangle { width: parent.width; height: kt.roles ? kt.roles.r.figureRuleTop : 1; color: kt.roles ? kt.roles.rule : "black" }
            Row {
                y: kt.roles ? kt.roles.u(6) : 6; spacing: kt.gap
                Text { width: kt.nameW; text: "Reading"; font: kt.roles.fLabel; color: kt.roles.ink3 }
                Text { visible: kt.cols; width: kt.medW; horizontalAlignment: Text.AlignRight; text: kt.names[0] || ""; font: kt.roles.fLabel; color: kt.roles.ink3 }
                Text { visible: kt.cols; width: kt.medW; horizontalAlignment: Text.AlignRight; text: kt.names[1] || ""; font: kt.roles.fLabel; color: kt.roles.ink3 }
                Text { visible: kt.cols; width: kt.unitW; text: "Unit"; font: kt.roles.fLabel; color: kt.roles.ink3 }
                Text { width: kt.dW; horizontalAlignment: Text.AlignRight; text: "d"; font: kt.roles.fLabelUnit; color: kt.roles.ink3 }
                Text { width: kt.boxW; horizontalAlignment: Text.AlignHCenter; text: "the two kinds"; font: kt.roles.fLabel; color: kt.roles.ink3 }
            }
        }
        Repeater {
            model: kt.roles && kt.sep ? kt.sep.rows : []
            delegate: Item {
                id: row
                objectName: "chKindRow"
                required property var modelData
                readonly property string readingId: modelData.readingId
                readonly property string key: modelData.key || ""
                readonly property real line1: Math.max(kt.roles.u(32), nm.implicitHeight + kt.roles.u(8))
                width: kt.width; height: line1 + (kt.cols ? 0 : kt.roles.u(14)) + kt.roles.u(2)
                Row {
                    y: kt.roles.u(4); spacing: kt.gap
                    Text { id: nm; width: kt.nameW; wrapMode: Text.WordWrap; textFormat: Text.StyledText
                           leftPadding: kt.roles.u(kt.roles.ch.swatch) + kt.roles.u(5)
                           text: kt.roles.esc(row.modelData.label) + (row.modelData.same ? " " + kt.roles.esc(row.modelData.sameMark || "") : "")
                                 + "  <font color=\"" + kt.roles.hex(kt.roles.ink3) + "\">" + kt.roles.esc(row.modelData.tag || "") + "</font>"
                           font: kt.roles.fBody; color: kt.roles.ink
                           Rectangle { y: Math.round(nm.baselineOffset - height - nm.font.pixelSize * 0.18); width: kt.roles.u(kt.roles.ch.swatch); height: width
                                       radius: kt.roles.square ? 0 : width / 2; color: kt.roles.hue(row.key) } }
                    Text { visible: kt.cols; width: kt.medW; horizontalAlignment: Text.AlignRight; anchors.baseline: nm.baseline; text: row.modelData.medAText; font: kt.roles.fFig; color: kt.roles.ink }
                    Text { visible: kt.cols; width: kt.medW; horizontalAlignment: Text.AlignRight; anchors.baseline: nm.baseline; text: row.modelData.medBText; font: kt.roles.fFig; color: kt.roles.ink }
                    Text { visible: kt.cols; width: kt.unitW; anchors.baseline: nm.baseline; text: row.modelData.unitText || ""; font: kt.roles.fSmall; color: kt.roles.ink3 }
                    Text { width: kt.dW; horizontalAlignment: Text.AlignRight; anchors.baseline: nm.baseline; text: row.modelData.dText; font: kt.roles.fFig
                           color: row.modelData.clear ? kt.roles.ink : kt.roles.ink3 }
                    Column {
                        width: kt.boxW
                        PpChBoxCell { width: parent.width; height: kt.roles.u(12); markScale: 0.85; roles: kt.roles; brush: kt.brush
                                      box: row.modelData.boxA; values: row.modelData.valuesA; lo: row.modelData.scale.lo; hi: row.modelData.scale.hi
                                      color: kt.roles.hue(row.key); interactive: kt.interactive; onShotActivated: (o) => kt.shotActivated(o) }
                        PpChBoxCell { width: parent.width; height: kt.roles.u(12); markScale: 0.85; roles: kt.roles; brush: kt.brush
                                      box: row.modelData.boxB; values: row.modelData.valuesB; lo: row.modelData.scale.lo; hi: row.modelData.scale.hi
                                      color: kt.roles.hue(row.key); interactive: kt.interactive; onShotActivated: (o) => kt.shotActivated(o) }
                    }
                }
                Row {
                    visible: !kt.cols
                    y: row.line1 - kt.roles.u(4); spacing: kt.roles.u(6)
                    Text { text: kt.names[0] || ""; font: kt.roles.fSmall; color: kt.roles.ink3 }
                    Text { text: row.modelData.medAText; font: kt.roles.fFigLabel; color: kt.roles.ink3 }
                    Text { text: "·"; font: kt.roles.fSmall; color: kt.roles.ink3 }
                    Text { text: kt.names[1] || ""; font: kt.roles.fSmall; color: kt.roles.ink3 }
                    Text { text: row.modelData.medBText; font: kt.roles.fFigLabel; color: kt.roles.ink3 }
                    Text { text: row.modelData.unitText || ""; font: kt.roles.fSmall; color: kt.roles.ink3 }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: kt.roles.hw; color: kt.roles.hair }
            }
        }
    }
}
