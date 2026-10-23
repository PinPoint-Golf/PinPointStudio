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

// PPCHPOSTURELEDGER — FINDINGS / WORKING WELL in This shot's Posture section (docs/design/posture_chart_design.md
// §5.2): the posture conditions that fired on this shot (the departures D1–D7, and the trail knee when it
// straightened), or, as Working well, the posture conditions graded clean whose reading sits inside its band. A
// row: the mark and its word (the session tier: pattern filled, watching a ring — a mark AND a word, never colour
// alone; working ink3), the name — a link that opens the condition in Session diagnostics when that panel is up —
// the positions, "Go to P7" (a seek), and the explanation (Working well: none — its name is its golferWell line; protect is ""). Every word is the
// presenter's FINDING; nothing is composed here.
// objectNames: chPostureFindings / chPostureWorkingWell (by `strengths`); each row chPostureFinding (+ findingId);
// its name chPostureFindingName, its seek chPostureGoTo.
Column {
    id: lg
    objectName: strengths ? "chPostureWorkingWell" : "chPostureFindings"
    property QtObject roles: null
    property string title: "Findings"
    property var items: []
    property bool strengths: false
    property string emptyText: ""
    property bool interactive: true
    property bool canOpen: false                 // Session diagnostics is up: a name opens its condition
    signal seek(real us)
    signal openCondition(string id)
    spacing: roles ? roles.u(9) : 9

    Row {
        visible: !!lg.roles
        spacing: lg.roles ? lg.roles.u(8) : 8
        Text { id: hd; text: lg.title; font: lg.roles ? lg.roles.fSection : Qt.font({}); color: lg.roles ? lg.roles.ink : "black" }
        Text { text: (lg.items || []).length > 0 ? String((lg.items || []).length) : ""; font: lg.roles ? lg.roles.fFigS : Qt.font({})
               color: lg.roles ? lg.roles.ink3 : "grey"; anchors.baseline: hd.baseline }
    }
    Text { visible: !!lg.roles && (lg.items || []).length === 0 && lg.emptyText !== ""; width: lg.width; wrapMode: Text.WordWrap
           text: lg.emptyText; font: lg.roles ? lg.roles.fBody : Qt.font({}); color: lg.roles ? lg.roles.ink2 : "grey" }
    Repeater {
        model: lg.roles ? (lg.items || []) : []
        delegate: Column {
            id: en
            required property var modelData
            objectName: "chPostureFinding"
            readonly property var f: modelData
            readonly property string findingId: f.id || ""
            readonly property bool fault: f.severity === "pattern"
            readonly property bool opens: lg.interactive && lg.canOpen && en.findingId !== ""
            readonly property var ty: lg.roles
            width: lg.width
            spacing: ty.u(3)
            Rectangle { width: parent.width; height: en.ty.hw; color: en.ty.hair }
            Item {
                width: parent.width; height: Math.max(nm.implicitHeight, en.ty.u(20))
                Row {
                    id: lead
                    spacing: en.ty.u(7)
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: en.ty.u(9); height: width; radius: en.ty.square ? 0 : width / 2
                                color: lg.strengths ? en.ty.ink3 : (en.fault ? en.ty.warn : en.ty.surface)
                                border.width: lg.strengths ? 0 : 2; border.color: en.ty.warn }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: en.f.severityWord || ""
                           font: en.ty.fSmallM; color: lg.strengths ? en.ty.ink3 : en.ty.warn }
                }
                Text { id: nm; objectName: "chPostureFindingName"
                       x: lead.width + en.ty.u(10); width: parent.width - x; wrapMode: Text.WordWrap
                       anchors.verticalCenter: parent.verticalCenter
                       text: en.f.name || ""; font: en.opens ? en.ty.fBodyL : en.ty.fBodyM; color: en.opens ? en.ty.link : en.ty.ink
                       MouseArea { anchors.fill: parent; enabled: en.opens; cursorShape: Qt.PointingHandCursor; onClicked: lg.openCondition(en.findingId) } }
            }
            Flow {
                width: parent.width; spacing: en.ty.u(8)
                Text { visible: text !== ""; text: en.f.positionsText || ""; font: en.ty.fSmall; color: en.ty.ink2 }
                Text { objectName: "chPostureGoTo"
                       readonly property bool live: lg.interactive && en.f.seekUs >= 0
                       visible: text !== ""
                       text: en.f.seekText || ""; font: live ? en.ty.fSmallL : en.ty.fSmall; color: live ? en.ty.link : en.ty.ink2
                       MouseArea { anchors.fill: parent; enabled: parent.live; cursorShape: Qt.PointingHandCursor; onClicked: lg.seek(en.f.seekUs) } }
                Text { visible: !!en.f.valueText && en.f.valueText !== "-"; text: "·  " + (en.f.valueText || ""); font: en.ty.fFigS; color: en.ty.ink2 }
            }
            Text { readonly property string t: lg.strengths ? (en.f.protect || "") : (en.f.explanation || "")
                   visible: t !== ""; width: en.ty.measureW(parent.width); wrapMode: Text.WordWrap
                   text: t; font: en.ty.fSmall; color: en.ty.ink2 }
        }
    }
}
