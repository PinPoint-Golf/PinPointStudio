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

// Swing-edit popover content — the focused shot's editable metadata: club,
// rating and note. Presentational (bindings only, no model lookups): the host
// (PpShotCarousel) injects `summary` (ShotListModel::shotSummary) and the
// `clubOptions` list, and wires the edit signals back to the active model's
// setClub / setRating / setNote. Replaces the club + rating + note fields of
// the retired PpShotPanel; video and metrics now live on the Review stage.

import QtQuick
import PinPointStudio

Item {
    id: root

    // Focused-shot metadata from ShotListModel::shotSummary(); { valid:false } when none.
    property var summary:     ({})
    // The golf-bag club list backing the picker (ShotListModel.clubOptions).
    property var clubOptions: []

    signal clubChosen(string club)
    signal rated(int value)
    signal noteChanged(string text)
    signal closeRequested()

    implicitWidth:  Theme.sp(288)
    implicitHeight: content.implicitHeight + Theme.sp(27)

    // Re-seed the editable controls from `summary` (the combo index and the note
    // field aren't plain bindings — the combo drives the model on select, and the
    // note field owns its text while editing). Called on load, when the host
    // rebinds `summary` (focus changed / write-through refreshed it), and when the
    // options arrive. The star rating is a pure read binding, so it needs no sync.
    function _sync() {
        const idx = root.clubOptions.indexOf(root.summary.club)
        clubCombo.currentIndex = idx >= 0 ? idx : 0
        noteField.text = (root.summary && root.summary.note !== undefined) ? root.summary.note : ""
    }
    Component.onCompleted:  _sync()
    onSummaryChanged:       _sync()
    onClubOptionsChanged:   _sync()

    Column {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: Theme.sp(14); rightMargin: Theme.sp(14); topMargin: Theme.sp(13) }

        Item {   // header: EDIT SWING · Done
            width: parent.width; height: doneText.implicitHeight
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text:           qsTr("EDIT SWING")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:          Theme.colorText3
            }
            Text {
                id: doneText
                anchors.right: parent.right
                text:           qsTr("Done")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          doneMa.containsMouse ? Qt.lighter(Theme.colorAccent, 1.08)
                                                     : Theme.colorAccent
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                PpPressable {
                    id: doneMa
                    anchors.margins: -Theme.sp(4)
                    onClicked:       root.closeRequested()
                }
            }
        }

        Item { width: 1; height: Theme.sp(3) }

        Text {   // subtitle — #N · hh:mm:ss
            width: parent.width
            text:  (root.summary.ordinal !== undefined ? "#" + root.summary.ordinal : "")
                   + (root.summary.timestampLabel ? "   ·   " + root.summary.timestampLabel : "")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText2
            elide:          Text.ElideRight
        }

        Item { width: 1; height: Theme.sp(15) }

        Text {
            text:           qsTr("CLUB")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorText3
        }
        Item { width: 1; height: Theme.sp(7) }
        PpComboBox {
            id: clubCombo
            width: parent.width
            model: root.clubOptions
            onActivated: (i) => root.clubChosen(root.clubOptions[i])
        }

        Item { width: 1; height: Theme.sp(15) }

        Text {
            text:           qsTr("RATING")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorText3
        }
        Item { width: 1; height: Theme.sp(7) }
        PpStarRating {
            interactive: true
            value:       root.summary.rating !== undefined ? root.summary.rating : 0
            starSize:    Theme.sp(22)
            spacing:     Theme.sp(5)
            onRated:     (n) => root.rated(n)
        }

        Item { width: 1; height: Theme.sp(15) }

        Text {
            text:           qsTr("NOTE")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorText3
        }
        Item { width: 1; height: Theme.sp(7) }
        PpTextField {
            id: noteField
            width: parent.width
            placeholderText:   qsTr("Add a note…")
            onEditingFinished: root.noteChanged(text)
        }
    }
}
