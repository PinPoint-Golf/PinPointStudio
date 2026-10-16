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

// PPCHWRISTLEDGER — FINDINGS / WORKING WELL in This shot's wrist section (W6, W7): the wrist-domain diagnostics conditions
// that fired on this shot (or read clean on every tail: a strength), in the presenter's order. A row: the mark
// and its word (the session tier: pattern filled, watching a ring — a mark AND a word, never colour alone; a strength ink3), the name,
// the points it cost, the positions (the first a seek link), the magnitude and the confidence pips, the
// explanation; "more" opens coaching (or protect), linked to and corroborated by IN PLACE. Every word is the
// presenter's FINDING; nothing is composed here.
// objectNames: chWristFindings / chWristStrengths (by `strengths`); each row chWristFinding (+ findingId).
Column {
    id: lg
    objectName: strengths ? "chWristStrengths" : "chWristFindings"
    property QtObject roles: null
    property string title: "Findings"
    property var items: []
    property bool strengths: false
    property string openId: ""
    property string emptyText: ""
    property bool interactive: true
    signal seek(real us)
    signal toggled(string id)
    spacing: roles ? roles.u(10) : 10

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
            objectName: "chWristFinding"
            readonly property var f: modelData
            readonly property string findingId: f.id
            readonly property bool open: lg.openId === f.id
            readonly property bool fault: f.severity === "pattern"   // the session tier; filled = a pattern
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
                Text { id: nm; x: lead.width + en.ty.u(10); width: parent.width - x - pts.width - en.ty.u(8); wrapMode: Text.WordWrap
                       anchors.verticalCenter: parent.verticalCenter
                       text: en.f.name || ""; font: en.ty.fBodyM; color: en.ty.ink }
                Text { id: pts; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                       text: en.f.pointsText || ""; font: en.ty.fFigS; color: en.ty.ink2 }
            }
            Flow {
                width: parent.width; spacing: en.ty.u(8)
                Repeater {
                    model: en.f.positions || []
                    delegate: Text {
                        required property var modelData
                        required property int index
                        readonly property bool live: lg.interactive && index === 0 && en.f.seekUs >= 0
                        text: (index === 0 ? "at " : "") + modelData; font: live ? en.ty.fSmallL : en.ty.fSmall; color: live ? en.ty.link : en.ty.ink2
                        MouseArea { anchors.fill: parent; enabled: parent.live; cursorShape: Qt.PointingHandCursor; onClicked: lg.seek(en.f.seekUs) }
                    }
                }
                Text { visible: !!en.f.magnitudeText; text: "·  " + (en.f.magnitudeText || ""); font: en.ty.fFigS; color: en.ty.ink2 }
                Text { text: "·  confidence " + (en.f.pips || "") + (en.f.lowConfidence ? " low" : ""); font: en.ty.fSmall; color: en.ty.ink3 }
                Text { visible: lg.interactive && (en.f.more || []).length > 0; text: en.open ? "less" : "more"; font: en.ty.fSmallL; color: en.ty.link
                       MouseArea { anchors.fill: parent; enabled: lg.interactive; cursorShape: Qt.PointingHandCursor; onClicked: lg.toggled(en.f.id) } }
            }
            Text { visible: !!en.f.explanation; width: en.ty.measureW(parent.width); wrapMode: Text.WordWrap
                   text: en.f.explanation || ""; font: en.ty.fSmall; color: en.ty.ink2 }
            Column {
                visible: en.open
                width: parent.width; spacing: en.ty.u(3); topPadding: en.ty.u(2)
                Repeater {
                    model: en.open ? (en.f.more || []) : []
                    delegate: Text {
                        required property var modelData
                        width: en.ty.measureW(en.width); wrapMode: Text.WordWrap; textFormat: Text.StyledText
                        text: "<b>" + en.ty.esc(modelData.k) + "</b>  " + en.ty.esc(modelData.v); font: en.ty.fSmall; color: en.ty.ink2
                    }
                }
            }
        }
    }
}
