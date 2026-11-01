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

// PPCHFINDINGSLEDGER — FINDINGS / WORKING WELL in a This shot section, the ONE ledger the Posture and Wrist sections
// list their conditions in (PpChPostureLedger, PpChWristLedger set it up): the conditions that fired on this shot, or,
// as Working well, those graded clean, in the presenter's order. A row: the mark and its word (the session tier:
// pattern filled, watching a ring — a mark AND a word, never colour alone; a strength ink3), the name, the positions,
// the explanation. Optional parts, by the section:
//   points     the points it cost, at the row's right
//   goTo       the positions as one line, then a single seek ("Go to P7") and the reading; else the positions one by
//              one, the first a seek link ("at P4")
//   pips       the magnitude and the confidence pips
//   more       "more" opens the presenter's more (coaching, protect, linked to, corroborated by) IN PLACE (openId,
//              toggled)
//   canOpen    the name is a link that opens the condition in Session diagnostics when that panel is up (openCondition)
//   protectWhenStrong   a Working well row says its protect line (else its explanation)
// Every word is the presenter's FINDING; nothing is composed here.
// objectNames, from `prefix`: each row <prefix>Finding (+ findingId); its name <prefix>FindingName; its seek
// <prefix>GoTo (goTo). The list's own objectName is the section's.
Column {
    id: lg
    property string prefix: "ch"
    property QtObject roles: null
    property string title: "Findings"
    property var items: []
    property bool strengths: false
    property string emptyText: ""
    property bool interactive: true
    property bool points: false
    property bool goTo: false
    property bool pips: false
    property bool more: false
    property string openId: ""
    property bool canOpen: false                 // Session diagnostics is up: a name opens its condition
    property bool protectWhenStrong: false
    property real spacingU: 10
    signal seek(real us)
    signal toggled(string id)
    signal openCondition(string id)
    spacing: roles ? roles.g(spacingU) : Theme.gap(spacingU)

    PpSectionHead { visible: !!lg.roles; width: lg.width; roles: lg.roles; paged: false; pad: 0; gap: 8
                    title: lg.title; count: (lg.items || []).length > 0 ? String((lg.items || []).length) : ""; countOnBaseline: true }
    Text { visible: !!lg.roles && (lg.items || []).length === 0 && lg.emptyText !== ""; width: lg.width; wrapMode: Text.WordWrap
           text: lg.emptyText; font: lg.roles ? lg.roles.fBody : Qt.font({}); color: lg.roles ? lg.roles.ink2 : "grey" }
    Repeater {
        model: lg.roles ? (lg.items || []) : []
        delegate: Column {
            id: en
            required property var modelData
            objectName: lg.prefix + "Finding"
            readonly property var f: modelData
            readonly property string findingId: f.id || ""
            readonly property bool open: lg.more && findingId !== "" && lg.openId === findingId
            readonly property bool fault: f.severity === "pattern"   // the session tier; filled = a pattern
            readonly property bool opens: lg.interactive && lg.canOpen && en.findingId !== ""
            readonly property var ty: lg.roles
            width: lg.width
            spacing: ty.g(3)
            Rectangle { width: parent.width; height: en.ty.hw; color: en.ty.hair }
            Item {
                width: parent.width; height: Math.max(nm.implicitHeight, en.ty.u(20))
                Row {
                    id: lead
                    spacing: en.ty.g(7)
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: en.ty.u(9); height: width; radius: en.ty.square ? 0 : width / 2
                                color: lg.strengths ? en.ty.ink3 : (en.fault ? en.ty.warn : en.ty.surface)
                                border.width: lg.strengths ? 0 : 2; border.color: en.ty.warn }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: en.f.severityWord || ""
                           font: en.ty.fSmallM; color: lg.strengths ? en.ty.ink3 : en.ty.warn }
                }
                Text { id: nm; objectName: lg.prefix + "FindingName"
                       x: lead.width + en.ty.u(10); width: parent.width - x - (lg.points ? pts.width + en.ty.u(8) : 0); wrapMode: Text.WordWrap
                       anchors.verticalCenter: parent.verticalCenter
                       text: en.f.name || ""; font: en.opens ? en.ty.fBodyL : en.ty.fBodyM; color: en.opens ? en.ty.link : en.ty.ink
                       MouseArea { anchors.fill: parent; enabled: en.opens; cursorShape: Qt.PointingHandCursor; onClicked: lg.openCondition(en.findingId) } }
                Text { id: pts; visible: lg.points; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                       text: en.f.pointsText || ""; font: en.ty.fFigS; color: en.ty.ink2 }
            }
            Flow {
                width: parent.width; spacing: en.ty.g(8)
                // the positions one by one, the first a seek link
                Repeater {
                    model: lg.goTo ? [] : (en.f.positions || [])
                    delegate: Text {
                        required property var modelData
                        required property int index
                        readonly property bool live: lg.interactive && index === 0 && en.f.seekUs >= 0
                        text: (index === 0 ? "at " : "") + modelData; font: live ? en.ty.fSmallL : en.ty.fSmall; color: live ? en.ty.link : en.ty.ink2
                        MouseArea { anchors.fill: parent; enabled: parent.live; cursorShape: Qt.PointingHandCursor; onClicked: lg.seek(en.f.seekUs) }
                    }
                }
                // or the positions as one line, a single seek, and the reading
                Text { visible: lg.goTo && text !== ""; text: lg.goTo ? (en.f.positionsText || "") : ""; font: en.ty.fSmall; color: en.ty.ink2 }
                Text { objectName: lg.goTo ? lg.prefix + "GoTo" : ""
                       readonly property bool live: lg.interactive && en.f.seekUs >= 0
                       visible: lg.goTo && text !== ""
                       text: lg.goTo ? (en.f.seekText || "") : ""; font: live ? en.ty.fSmallL : en.ty.fSmall; color: live ? en.ty.link : en.ty.ink2
                       MouseArea { anchors.fill: parent; enabled: parent.visible && parent.live; cursorShape: Qt.PointingHandCursor; onClicked: lg.seek(en.f.seekUs) } }
                Text { visible: lg.goTo && !!en.f.valueText && en.f.valueText !== "-"; text: "·  " + (en.f.valueText || ""); font: en.ty.fFigS; color: en.ty.ink2 }
                // the magnitude and the confidence pips
                Text { visible: lg.pips && !!en.f.magnitudeText; text: "·  " + (en.f.magnitudeText || ""); font: en.ty.fFigS; color: en.ty.ink2 }
                Text { visible: lg.pips; text: "·  confidence " + (en.f.pips || "") + (en.f.lowConfidence ? " low" : ""); font: en.ty.fSmall; color: en.ty.ink3 }
                Text { visible: lg.more && lg.interactive && (en.f.more || []).length > 0; text: en.open ? "less" : "more"; font: en.ty.fSmallL; color: en.ty.link
                       MouseArea { anchors.fill: parent; enabled: lg.more && lg.interactive; cursorShape: Qt.PointingHandCursor; onClicked: lg.toggled(en.f.id) } }
            }
            Text { readonly property string t: lg.strengths && lg.protectWhenStrong ? (en.f.protect || "") : (en.f.explanation || "")
                   visible: t !== ""; width: en.ty.measureW(parent.width); wrapMode: Text.WordWrap
                   text: t; font: en.ty.fSmall; color: en.ty.ink2 }
            Column {
                visible: en.open
                width: parent.width; spacing: en.ty.g(3); topPadding: en.ty.g(2)
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
