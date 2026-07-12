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

// Toolbar Club selector panel. Lists the CURRENT athlete's bag (canonical ids,
// vocabulary order) and writes the pick to SessionController.activeClub — the one
// source of truth read at shot-join time (shot_processor.cpp). The active club is
// seeded upstream (Home pick → wizard → activeClub, else the athlete's default at
// start()); this panel just surfaces and overrides it. Clubs whose record carries
// retro-band tape (non-empty bandCentersMm) show a tape marker, so a taped/
// instrumented club is visually distinct from a passive one.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // Emitted after a pick so the host popup can close.
    signal requestClose()

    implicitWidth:  Theme.sp(258)
    implicitHeight: col.implicitHeight + Theme.sp(26)

    // Current selection: the session's active club, else the athlete's default so
    // a not-yet-started session still highlights a sensible row. Reactive to
    // activeClub / athlete / bag edits.
    readonly property string currentClub: {
        void athleteController.athletes
        return sessionController.activeClub !== ""
            ? sessionController.activeClub
            : athleteController.effectivePrimaryClub(athleteController.currentUuid)
    }

    // The athlete's bag, in vocabulary order. Empty when there is no athlete or an
    // emptied bag → the panel shows its empty-state hint.
    readonly property var clubModel: {
        void athleteController.athletes
        if (!athleteController.hasCurrentAthlete) return []
        var bag   = athleteController.clubsFor(athleteController.currentUuid)
        var vocab = athleteController.clubOptions()
        var ids   = Object.keys(bag)
        ids.sort(function (a, b) { return vocab.indexOf(a) - vocab.indexOf(b) })
        return ids
    }

    // Retro-band centres for a club (empty = untaped). void athletes keeps callers
    // reactive to club edits.
    function _bands(id) {
        void athleteController.athletes
        if (!id || !athleteController.hasCurrentAthlete) return []
        var rec = athleteController.clubsFor(athleteController.currentUuid)[id]
        var c = rec && rec.bandCentersMm
        return (c && c.length) ? c : []
    }

    Column {
        id: col
        anchors { fill: parent; margins: Theme.sp(13) }
        spacing: Theme.sp(10)

        Text {
            text: qsTr("CLUB")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
        }

        // Empty state — no bag for this athlete.
        Text {
            visible: root.clubModel.length === 0
            width: parent.width
            text: qsTr("No clubs configured for this athlete.\nAdd clubs in the athlete profile.")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorText3
            wrapMode: Text.WordWrap
        }

        // Club rows — one selectable row per bag club, taped ones marked.
        Column {
            width: parent.width
            spacing: Theme.sp(3)

            Repeater {
                model: root.clubModel

                delegate: Rectangle {
                    id: clubRow
                    required property string modelData
                    readonly property bool sel:   modelData === root.currentClub
                    readonly property var  bands: root._bands(modelData)
                    readonly property bool taped: bands.length > 0

                    width:  parent.width
                    height: Theme.sp(34)
                    radius: Theme.radius
                    // Selected: accent wash + accent border. Hover (unselected): faint
                    // bg fill (alpha-ramped → no flash) + accentMid border — the
                    // chip/tile language shared with the View panel cards.
                    color: sel                 ? Theme.colorAccentLight
                         : rowMa.containsMouse ? Theme.colorBg2
                         :                        Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                    border.width: 1
                    border.color: sel                 ? Theme.colorAccent
                                : rowMa.containsMouse ? Theme.colorAccentMid
                                :                        Theme.colorBorderMid
                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                    RowLayout {
                        anchors { fill: parent; leftMargin: Theme.sp(11); rightMargin: Theme.sp(10) }
                        spacing: Theme.sp(8)

                        Text {
                            text: ClubFormat.display(clubRow.modelData)
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                            color: clubRow.sel ? Theme.colorAccent : Theme.colorText
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Item { Layout.fillWidth: true }

                        // Tape marker: a small stack of band ticks + label, only when
                        // the club record carries retro bands. Reads green (colorGood),
                        // the same "instrumented / ready" hue the device LEDs use.
                        Row {
                            visible: clubRow.taped
                            spacing: Theme.sp(5)
                            Layout.alignment: Qt.AlignVCenter

                            Row {   // band ticks (up to 4 shown)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.sp(2)
                                Repeater {
                                    model: Math.min(clubRow.bands.length, 4)
                                    Rectangle {
                                        width: Theme.sp(2); height: Theme.sp(13); radius: 1
                                        color: Theme.colorGood
                                    }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("taped")
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                                font.letterSpacing: Theme.trackingMicro
                                color: Theme.colorGood
                            }
                        }
                    }

                    PpPressable {
                        id: rowMa
                        onClicked: {
                            sessionController.activeClub = clubRow.modelData
                            root.requestClose()
                        }
                    }
                }
            }
        }
    }
}
