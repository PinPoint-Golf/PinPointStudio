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

// KINDS — "what kinds of swing were there, and what tells them apart?" (design §2.4, B + A; D7). Analyse
// only: Replay has no Kinds form and reads Session there (the body's rule). How weak the structure is comes
// FIRST, in the presenter's words (silhouette; "no structure" then the one split, labelled a split, not kinds);
// then the members and the most typical shot of each, what else separates them (each kind's box on one scale,
// d against the d chance gives), each kind's pooled curve with each shot's own measured stretches under it,
// A's curve-shape groups as a second grouping, and the method, fit and alignment notes.
// objectName: chKindsView.
Column {
    id: kv
    objectName: "chKindsView"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    property real pageH: 500
    readonly property var kd: body ? body.rKinds : ({})
    readonly property bool available: !!kd && kd.available === true
    spacing: roles ? roles.u(12) : 12

    PpChText { objectName: "chKindsWhy"; visible: !kv.available && !!kv.body && present; roles: kv.roles; sentence: kv.body && kv.body.rStatus ? kv.body.rStatus.refusal : null
               font: kv.roles ? kv.roles.fBody : Qt.font({}); tone: kv.roles ? kv.roles.ink2 : "grey" }
    Column {
        objectName: "chKindsHead"
        visible: kv.available
        width: kv.width; spacing: kv.roles ? kv.roles.u(4) : 4
        Text { visible: !kv.cast && !!kv.body && !!kv.body.rStatus.kicker; text: kv.body ? (kv.body.rStatus.kicker || "") : ""; font: kv.roles ? kv.roles.fKicker : Qt.font({}); color: kv.roles ? kv.roles.ink3 : "grey" }
        PpChText { roles: kv.roles; sentence: kv.kd.head; measure: false; font: kv.roles ? kv.roles.fTitle : Qt.font({}); tone: kv.roles ? kv.roles.ink : "black"; lineHeight: 1.08 }
        PpChText { roles: kv.roles; sentence: kv.kd.standfirst; font: kv.roles ? kv.roles.fStand : Qt.font({}); lineHeight: kv.roles ? kv.roles.r.leading : 1 }
    }
    PpChText { visible: kv.available && present; roles: kv.roles; sentence: kv.kd.splitLine; font: kv.roles ? kv.roles.fBodyM : Qt.font({}); tone: kv.roles ? kv.roles.ink : "black" }
    PpChKindTable {
        visible: kv.available
        width: kv.width; roles: kv.roles; brush: kv.brush; interactive: kv.interactive
        kinds: kv.available ? kv.kd : null
        onShotActivated: (o) => { if (kv.body) kv.body.pickOrdinal(o) }
    }
    PpChText { visible: kv.available && present; roles: kv.roles; sentence: kv.kd.separation ? kv.kd.separation.note : null; tone: kv.roles ? kv.roles.ink3 : "grey" }
    PpSectionHead { visible: kv.available && (kv.kd.curves || []).length > 0; width: kv.width; roles: kv.roles
                    title: "Each kind's curve"; sub: "one curve per kind, fitted to the pooled samples of all its shots" }
    Flow {
        id: cf
        visible: kv.available
        width: kv.width; spacing: kv.roles ? kv.roles.u(20) : 20
        readonly property int cols: kv.roles ? kv.roles.columns(width, kv.roles.isPrint ? kv.roles.ch.kindCurveEm : kv.roles.flows.kindCurve, kv.roles.u(20)) : 1
        Repeater {
            model: kv.available ? (kv.kd.curves || []) : []
            delegate: Column {
                required property var modelData
                width: Math.floor((cf.width - (cf.cols - 1) * kv.roles.u(20)) / cf.cols); spacing: kv.roles.u(4)
                Row {
                    spacing: kv.roles.u(5)
                    Rectangle { anchors.verticalCenter: kl.verticalCenter; width: kv.roles.u(kv.roles.ch.swatch); height: width; radius: kv.roles.square ? 0 : width / 2; color: kv.roles.hue(modelData.key) }
                    Text { id: kl; text: modelData.label; font: kv.roles.fSmallM; color: kv.roles.ink }
                }
                PpChKindCurve { width: parent.width; roles: kv.roles; brush: kv.brush; ticker: kv.body ? kv.body.relationsP : null; curve: modelData }
            }
        }
    }
    PpNotes { visible: kv.available && !!kv.kd.fitNote; width: kv.width; roles: kv.roles; text: kv.roles ? kv.roles.esc(kv.kd.fitNote || "") : "" }
    // A's curve-shape groups: a second grouping, said with its members
    Column {
        objectName: "chKindShapes"
        visible: kv.available && !!kv.kd.shapes && kv.kd.shapes.found === true
        width: kv.width; spacing: kv.roles ? kv.roles.u(4) : 4
        PpChText { roles: kv.roles; sentence: kv.kd.shapes ? kv.kd.shapes.sentence : null; font: kv.roles ? kv.roles.fBody : Qt.font({}) }
        Repeater {
            model: kv.available && kv.kd.shapes ? (kv.kd.shapes.groups || []) : []
            delegate: Flow {
                required property var modelData
                width: kv.width; spacing: kv.roles.u(10)
                Text { text: modelData.label; font: kv.roles.fSmallM; color: kv.roles.ink2 }
                Repeater {
                    model: modelData.ordinals || []
                    delegate: Text {
                        required property var modelData
                        readonly property bool pk: !!kv.brush && kv.brush.isPicked(modelData)
                        text: String(modelData); font: pk ? kv.roles.fBodyM : kv.roles.fFigS
                        color: pk || (!!kv.brush && kv.brush.hoveredShot === modelData) ? kv.roles.ink : kv.roles.ink2
                        Rectangle { visible: parent.pk; anchors.fill: parent; anchors.margins: -kv.roles.u(2); color: kv.roles.pickWash; z: -1 }
                        MouseArea { anchors.fill: parent; enabled: kv.interactive; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onContainsMouseChanged: if (kv.brush) kv.brush.hoverShot(parent.modelData, containsMouse)
                                    onClicked: if (kv.body) kv.body.pickOrdinal(parent.modelData) }
                    }
                }
            }
        }
    }
    PpNotes { visible: kv.available && !!kv.kd.methodNote; width: kv.width; roles: kv.roles; text: kv.roles ? kv.roles.esc(kv.kd.methodNote || "") : "" }
    PpChText { visible: kv.available && present; roles: kv.roles; sentence: kv.kd.alignNote; tone: kv.roles ? kv.roles.ink3 : "grey" }
}
