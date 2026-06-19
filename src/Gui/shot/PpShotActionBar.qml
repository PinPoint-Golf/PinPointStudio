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

// PpShotActionBar — the scope-aware action header of the shot carousel. ONE
// surface for the two action scopes that survived the PpShotPanel removal:
//
//   · FOCUSED SHOT (the swing on the stage)  — Export · Re-analyse · Move to trash
//   · FILTERED SET ("N of M shown")          — folded in as the "all N shown ▾"
//                                              menu, or the whole bar in Capture
//
// Three states fall out of (summary.valid, filterActive):
//   1. focus + filter — identity left; this-shot actions; "all N shown ▾" control
//   2. focus, no filter — identity left; this-shot actions only
//   3. no focus, filter — set descriptor left; the actions act on the SET
//
// Re-analyse is single-shot only in v1, so it appears ONLY in the focused-shot
// action group — there is deliberately no "Re-analyse all shown" row in the menu
// and no Re-analyse button in the set-scope (state 3) layout. The reanalyseShown()
// signal is declared for API symmetry but never emitted yet (reserved).
//
// Host-injected: the bar owns no model lookups. Pure bindings — handlers only
// forward a signal or open/close the menu; no computation lives here.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // ── Public API (host-injected) ──────────────────────────────────────────
    // Focused-shot metadata from ShotListModel.shotSummary(); { valid:false } when none.
    property var    summary:      ({})
    property bool   filterActive: false
    // The filtered counts for "N of M shown" and the menu tails.
    property int    visibleCount: 0
    property int    sourceCount:  0
    // Human filter descriptor (e.g. "50–100 · 4★"), "" if none.
    property string filterLabel:  ""

    // Focused-shot actions.
    signal exportShot()
    signal reanalyseShot()
    signal trashShot()
    // Filtered-set actions ("all shown"). reanalyseShown() is reserved — never
    // emitted in v1 (re-analyse is single-shot only); kept for API symmetry.
    signal exportShown()
    signal reanalyseShown()
    signal trashShown()

    // ── Derived scope state (pure bindings) ─────────────────────────────────
    readonly property bool _hasFocus:  root.summary && root.summary.valid === true
    readonly property bool _hasFilter: root.filterActive
    readonly property bool _showAllCtl: root._hasFocus && root._hasFilter

    implicitHeight: Theme.sp(36)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.sp(9)

        // ── LEFT · focused identity  (#N · club · time · ★ · score) ─────────
        Row {
            id: identityRow
            Layout.alignment: Qt.AlignVCenter
            visible: root._hasFocus
            spacing: Theme.sp(7)

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           "#" + (root.summary.ordinal !== undefined ? root.summary.ordinal : "")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           root.summary.club !== undefined ? root.summary.club : ""
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           root.summary.timestampLabel !== undefined ? root.summary.timestampLabel : ""
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText2
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
            PpStarRating {
                anchors.verticalCenter: parent.verticalCenter
                value:       root.summary.rating !== undefined ? root.summary.rating : 0
                interactive: false
                starSize:    Theme.fontSzBody2
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
            PpQualityPill {
                anchors.verticalCenter: parent.verticalCenter
                score: root.summary.score !== undefined ? root.summary.score : 0
            }
        }

        // ── LEFT · set descriptor  (N of M shown · filter) ──────────────────
        Row {
            id: setRow
            Layout.alignment: Qt.AlignVCenter
            visible: !root._hasFocus
            spacing: Theme.sp(7)

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           qsTr("%1 of %2 shown").arg(root.visibleCount).arg(root.sourceCount)
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.filterLabel !== ""
                text: "·"; font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible:        root.filterLabel !== ""
                text:           root.filterLabel
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText2
            }
        }

        Item { Layout.fillWidth: true }   // grow — pushes the actions to the right cap

        // ── RIGHT · actions ─────────────────────────────────────────────────
        // Export acts on the focused shot (state 1/2) or the whole set (state 3).
        PpButton {
            Layout.alignment: Qt.AlignVCenter
            label: qsTr("Export")
            glyph: "⤓"
            onClicked: root._hasFocus ? root.exportShot() : root.exportShown()
        }
        // Re-analyse — focused shot only (single-shot in v1); hidden in set scope.
        PpButton {
            Layout.alignment: Qt.AlignVCenter
            visible: root._hasFocus
            label: qsTr("Re-analyse")
            glyph: "↻"
            onClicked: root.reanalyseShot()
        }
        // Destructive — "Move to trash" (this shot) / "Move all shown to trash" (set).
        PpButton {
            Layout.alignment: Qt.AlignVCenter
            destructive: true
            label: root._hasFocus ? qsTr("Move to trash") : qsTr("Move all shown to trash")
            glyph: "🗑"
            onClicked: root._hasFocus ? root.trashShot() : root.trashShown()
        }

        // Hairline before the "all N shown" control (state 1 only).
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            visible: root._showAllCtl
            implicitWidth: 1
            implicitHeight: Theme.sp(20)
            color: Theme.colorBorderMid
            opacity: 0.7
        }

        // ── "all N shown ▾" — the relocated, re-scoped bulk menu (state 1) ───
        Rectangle {
            id: allCtl
            Layout.alignment: Qt.AlignVCenter
            visible: root._showAllCtl
            implicitWidth:  allRow.implicitWidth + Theme.sp(20)
            implicitHeight: Theme.sp(28)
            radius: Theme.radius
            color:  allMenu.opened || allMa.containsMouse ? Theme.colorBg3 : "transparent"
            border.width: 1
            border.color: allMenu.opened || allMa.containsMouse
                              ? Theme.colorBorderMid : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            Row {
                id: allRow
                anchors.centerIn: parent
                spacing: Theme.sp(7)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text:           qsTr("all %1 shown").arg(root.visibleCount)
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody2
                    color:          allMenu.opened || allMa.containsMouse
                                        ? Theme.colorText : Theme.colorText2
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text:           "▾"
                    font.family:    Theme.fontSymbol
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                }
            }
            PpPressable {
                id: allMa
                held: allMenu.opened       // stay grown while the menu is up
                onClicked: allMenu.opened ? allMenu.close() : allMenu.open()
            }

            // Opens UPWARD over the stage (dock convention), right-aligned to the control.
            Popup {
                id: allMenu
                parent: allCtl
                y: -height - Theme.sp(4)
                x: allCtl.width - width
                padding: Theme.sp(5)
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                contentWidth: Math.max(allExportRow.implicitWidth, allTrashRow.implicitWidth)
                              + Theme.sp(28)
                background: Rectangle {
                    color: Theme.colorSurface; radius: Theme.radiusLg
                    border.width: 1; border.color: Theme.colorBorderStrong
                }

                contentItem: Column {

                    Rectangle {   // Export all shown
                        width: parent.width
                        height: Theme.sp(34)
                        radius: Theme.radius
                        color: allExportMa.containsMouse ? Theme.colorBg2 : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        Row {
                            id: allExportRow
                            anchors { left: parent.left; leftMargin: Theme.sp(10)
                                      verticalCenter: parent.verticalCenter }
                            spacing: Theme.sp(10)
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "⤓"
                                font.family: Theme.fontSymbol
                                font.pixelSize: Theme.fontSzBody
                                color: Theme.colorText2
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Export all shown")
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontSzBody
                                color: Theme.colorText
                            }
                        }
                        Text {   // count tail
                            anchors { right: parent.right; rightMargin: Theme.sp(10)
                                      verticalCenter: parent.verticalCenter }
                            text:           qsTr("%1 shots").arg(root.visibleCount)
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                        }
                        PpPressable {
                            id: allExportMa
                            hoverScale: 1.0       // full-width menu item — press-dip only
                            onClicked: { allMenu.close(); root.exportShown() }
                        }
                    }

                    Rectangle {   // separator — destructive action in its own group
                        width: parent.width - Theme.sp(12)
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 1
                        color: Theme.colorBorderMid
                        opacity: Theme.borderOpacityNormal
                    }

                    Rectangle {   // Move all shown to trash (soft, recoverable)
                        width: parent.width
                        height: Theme.sp(34)
                        radius: Theme.radius
                        color: allTrashMa.containsMouse ? Theme.colorWarnLight : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        Row {
                            id: allTrashRow
                            anchors { left: parent.left; leftMargin: Theme.sp(10)
                                      verticalCenter: parent.verticalCenter }
                            spacing: Theme.sp(10)
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "🗑"
                                font.family: Theme.fontSymbol
                                font.pixelSize: Theme.fontSzBody
                                color: Theme.colorWarn
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Move all shown to trash")
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontSzBody
                                color: Theme.colorWarn
                            }
                        }
                        Text {   // count tail
                            anchors { right: parent.right; rightMargin: Theme.sp(10)
                                      verticalCenter: parent.verticalCenter }
                            text:           qsTr("%1 shots").arg(root.visibleCount)
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorWarn
                            opacity:        0.7
                        }
                        PpPressable {
                            id: allTrashMa
                            hoverScale: 1.0       // full-width menu item — press-dip only
                            onClicked: { allMenu.close(); root.trashShown() }
                        }
                    }
                }
            }
        }
    }
}
