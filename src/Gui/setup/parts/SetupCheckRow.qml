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

// One device or check row: status circle, label + sub-text, optional live camera thumbnail, enable
// toggle, Recalibrate button and status chip. Ported verbatim from the retired ScreenSessionWizard.qml's inline
// `component CheckRow` (l.2330–2471) — the camera rows (with their thumbnail Loader), the
// Triangulate checks and, in Stage 5b, the sensor rows all use it.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: cr
    property bool   ok:            false
    property bool   warn:          false  // amber — found but not connected/ready
    property bool   optional:      false  // true = muted colour when hard-failing
    property string label:         ""
    property string subOk:         ""
    property string subWarn:       ""    // shown when warn && !ok
    property string subFail:       ""
    property bool   showRecal:     false
    property bool   recalEnabled:  false
    // Disabled state — muted/greyed, neutral circle, shows subDisabled.
    property bool   disabled:      false
    property string subDisabled:   ""
    // Optional on/off toggle on the right (e.g. enable an IMU for connection).
    property bool   showToggle:    false
    property bool   toggleChecked: true
    signal toggled(bool value)
    // RHS status chip — shown when chipText is non-empty
    property string chipText:      ""
    property color  chipColor:     "transparent"
    property color  chipTextColor: Theme.colorText3
    signal recalibrate()
    // Live camera thumbnail (wizard camera rows) — the row grows to fit it.
    property QtObject thumbInstance: null

    height: thumbInstance !== null ? Theme.sp(76) : Theme.sp(52)
    Behavior on height { NumberAnimation { duration: Theme.durationFast } }

    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 1; color: Theme.colorBorder
    }

    RowLayout {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: Theme.gap(10)

        SetupStatusCircle {
            opacity: cr.disabled ? 0.45 : 1.0
            ok:    !cr.disabled && cr.ok
            warn:  !cr.disabled && !cr.ok && cr.warn
            error: !cr.disabled && !cr.ok && !cr.warn && !cr.optional
        }

        Column {
            Layout.fillWidth: true
            spacing: Theme.gap(2)
            opacity: cr.disabled ? 0.45 : 1.0
            Text {
                text:           cr.label
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText
            }
            Text {
                width:              parent.width
                text:               cr.disabled ? cr.subDisabled
                                                 : (cr.ok ? cr.subOk : (cr.warn ? cr.subWarn : cr.subFail))
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              cr.disabled ? Theme.colorText3
                                           : (cr.ok   ? Theme.colorGood
                                           : (cr.warn ? Theme.colorWarn
                                                      : (cr.optional ? Theme.colorText3
                                                                     : Theme.colorError)))
                elide:              Text.ElideRight
            }
        }

        // Live thumbnail preview — streams as soon as the camera connects.
        // All overlays off; PpCameraFrame handles the RGB/Bayer split and
        // the subscribe/unsubscribe lifecycle. Loader-gated so the many
        // non-camera CheckRows don't each carry a video item.
        Loader {
            active:  cr.thumbInstance !== null
            visible: active
            Layout.preferredWidth:  Theme.sp(108)
            Layout.preferredHeight: Theme.sp(62)
            Layout.alignment:       Qt.AlignVCenter
            sourceComponent: PpCameraFrame {
                instance: cr.thumbInstance
                showPoseOverlay:      false
                showHittingArea:      false
                showPerspectiveBadge: false
                showStatsOverlay:     false
            }
        }

        // Per-row enable toggle (e.g. include/exclude an IMU for connection).
        Row {
            visible:          cr.showToggle
            spacing:          Theme.gap(6)
            Layout.alignment: Qt.AlignVCenter
            Text {
                text:           qsTr("Enable")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          Theme.colorText3
                anchors.verticalCenter: parent.verticalCenter
            }
            SetupTogglePill {
                checked:                cr.toggleChecked
                anchors.verticalCenter: parent.verticalCenter
                onToggled:              (v) => cr.toggled(v)
            }
        }

        PpButton {
            visible:   cr.showRecal
            label:     qsTr("Recalibrate")
            primary:   false
            enabled:   cr.recalEnabled
            onClicked: cr.recalibrate()
        }

        // Status chip — e.g. "Connecting", "Connection Failed", "Connected"
        Rectangle {
            visible:        cr.chipText !== ""
            implicitWidth:  chipLbl.implicitWidth + Theme.sp(14)
            implicitHeight: Theme.sp(20)
            radius:         Theme.sp(4)
            color:          cr.chipColor
            border.width:   1
            border.color:   cr.chipTextColor
            Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

            Text {
                id: chipLbl
                anchors.centerIn:   parent
                text:               cr.chipText
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              cr.chipTextColor
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
        }
    }
}
