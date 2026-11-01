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

// PpToolPill — THE standard toolbar item: a glyph tile, a micro label, the current
// value and a ▾ affordance, with the shared hover/press motion. Was an inline
// `component ViewPill` private to PpSessionToolbar; extracted here so surfaces
// OUTSIDE the toolbar can present the same control rather than a lookalike that
// drifts. Every toolbar pill — View, Motion, Club, Cameras, IMUs — is this
// component.
//
// Anchoring convention for the popup a pill owns (see PpSessionToolbar):
//     parent: <pill>;  y: <pill>.height + Theme.sp(10);  x: <pill>.width - width
// Parenting to the pill is what makes the popup track it — positioning against a
// coordinate computed in some ancestor's space silently misaligns.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: toolPill
    property string label: ""
    property bool   active: false
    // Generalized for MotionPill (Phase 4): icon glyph + micro label above
    // the value line, defaulting to the original View pill's values.
    property string glyph:      "▦"
    property string microLabel: Theme.caps(qsTr("View"))
    // Optional corner dot on the glyph tile (ClubPill uses it as the taped-club
    // marker). Off by default so View/Motion pills are unaffected.
    property bool   badge:      false
    property color  badgeColor: Theme.colorGood
    // A device pill (Cameras, IMUs): its state as a device-vocabulary badge at the tile's corner
    // ("ok" a check, "off" the dashed ring, "attention"/"error" a target in that tone), the value
    // inked in the warning tones too, so the tone is never the only signal. "" for none.
    property string status:     ""
    // The ▾ affordance. A device pill drops it (the badge says what the pill is about).
    property bool   chevron:    true
    signal clicked()

    readonly property color statusTone: Theme.toneColor(status)

    Layout.alignment: Qt.AlignVCenter
    implicitWidth: vpRow.implicitWidth + Theme.sp(24)
    implicitHeight: Theme.sp(44)
    radius: Theme.radius
    color: vpMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
    border.width: 1
    border.color: toolPill.active             ? Theme.colorAccent
                : vpMa.containsMouse ? Theme.colorAccentMid
                :                      Theme.colorBorderMid

    // Hover/press motion — the home-tile / shot-card language adapted to a
    // toolbar pill: a subtle scale (no vertical lift — a pill must not float
    // out of the bar) that grows on hover, holds while its popup is open, and
    // dips on press (PpPressable, held by `active`). Both colour endpoints are
    // opaque (bg2↔bg3), so unlike the home card there is no tint-flash to guard
    // against.
    Behavior on color        { ColorAnimation  { duration: Theme.durationFast } }
    Behavior on border.color { ColorAnimation  { duration: Theme.durationFast } }

    RowLayout {
        id: vpRow
        anchors { fill: parent; leftMargin: Theme.gap(11); rightMargin: toolPill.chevron ? Theme.gap(11) : Theme.gap(13) }
        spacing: Theme.gap(11)
        Item {
            Layout.preferredWidth: Theme.sp(34); Layout.preferredHeight: Theme.sp(34)
            Layout.alignment: Qt.AlignVCenter
            Rectangle {
                anchors.fill: parent; radius: Theme.radius; color: Theme.colorSurface
                Text {
                    anchors.centerIn: parent; text: toolPill.glyph
                    font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(18)
                    color: toolPill.active ? Theme.colorAccent : Theme.colorText2
                }
            }
            Rectangle {   // optional taped-club marker dot
                visible: toolPill.badge
                anchors { right: parent.right; top: parent.top
                          rightMargin: -Theme.sp(3); topMargin: -Theme.sp(3) }
                width: Theme.sp(11); height: Theme.sp(11); radius: width / 2
                color: toolPill.badgeColor
                border.width: 1; border.color: Theme.colorBg2
            }
            // A disc of the pill's own fill under the status badge cuts it out of
            // the tile's corner, so its faint tint reads the same over both.
            Rectangle {
                visible: toolPill.status !== ""
                anchors { right: parent.right; top: parent.top
                          rightMargin: -Theme.sp(6); topMargin: -Theme.sp(6) }
                width: Theme.sp(20); height: width; radius: width / 2
                color: toolPill.color
                PpBadge {
                    anchors.centerIn: parent
                    size: Theme.sp(18)
                    kind: toolPill.status === "ok"  ? "check"
                        : toolPill.status === "off" ? "unconfirmed"
                        :                    "target"
                    tone: toolPill.statusTone
                }
            }
        }
        Column {
            Layout.alignment: Qt.AlignVCenter; spacing: Theme.gap(2)
            PpMicro {
                text: toolPill.microLabel
            }
            Row {
                spacing: Theme.gap(4)
                Text {
                    text: toolPill.label
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: toolPill.status === "attention" || toolPill.status === "error" ? toolPill.statusTone : Theme.colorText
                }
                Text {
                    visible: toolPill.chevron
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"  // ▾
                    font.pixelSize: Theme.fontSzMicro; color: Theme.colorText2
                }
            }
        }
    }
    PpPressable {
        id: vpMa
        held: toolPill.active
        onClicked: toolPill.clicked()
    }
}
