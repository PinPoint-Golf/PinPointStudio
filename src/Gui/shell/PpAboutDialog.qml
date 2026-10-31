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
import QtQuick.Controls
import QtQuick.Layouts
import PinPointStudio
import "update_states.js" as UpdateStates

// About PinPoint Studio — modal dialog showing the icon, "PinPoint Studio for
// <OS>", the version + build stats, and the bundled library versions. It surfaces
// the updater state and lets the user check, download and restart into an update —
// reusing the existing `updateController` (the same surface the Settings → General
// version row drives). Opening the dialog never checks on its own: a check only
// happens when the user presses "Check for updates". Unlike the Settings row the
// update block is shown on every platform — a devbuild or an unsupported build gets
// the badge saying so and a disabled button, rather than a blank gap that reads as a
// missing control. Opened from the header version pill (all platforms) and the macOS
// application menu.
Popup {
    id: root
    objectName: "aboutDialog"

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.gap(24)
    width: Math.min(Theme.sp(480), (parent ? parent.width : Theme.sp(480)) - Theme.sp(48))

    // Live updater state + its badge, in the words update_states.js gives every updater surface
    // ("long": room for "Checking for updates…").
    readonly property string uState: updateController.state
    readonly property var pal: UpdateStates.badge(uState, "long", updateController.progress, Theme)

    background: Rectangle {
        color: Theme.colorSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.colorBorderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.gap(18)

        // ── Identity: icon + wordmark + OS ──────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            Image {
                source:            appInfo.iconSource
                sourceSize.width:  Theme.sp(76)
                sourceSize.height: Theme.sp(76)
                Layout.preferredWidth:  Theme.sp(76)
                Layout.preferredHeight: Theme.sp(76)
                fillMode:          Image.PreserveAspectFit
                smooth:            true
                mipmap:            true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.gap(3)

                PpDisplayText {
                    Layout.fillWidth: true
                    text:       "PinPoint Studio"
                    pixelSize:  Theme.sp(26)
                    wrapMode:   Text.NoWrap
                    elide:      Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text:           qsTr("for %1").arg(appInfo.osName)
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText2
                    wrapMode:       Text.WordWrap
                }
            }
        }

        // ── Version + build provenance ──────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(4)

            Text {
                Layout.fillWidth: true
                text:           qsTr("%1  ·  build %2").arg(appInfo.versionString).arg(appInfo.buildNumber)
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorText
            }
            Text {
                Layout.fillWidth: true
                text:           qsTr("commit %1 · built %2 · Qt %3 · %4")
                                    .arg(appInfo.gitSha).arg(appInfo.buildDate)
                                    .arg(appInfo.qtVersion).arg(appInfo.architecture)
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
            }
        }

        // ── Update section (always shown; inert where updates aren't supported) ──
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(8)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(10)

                // Status badge — hidden in the neutral idle state (the button carries it).
                Rectangle {
                    visible:        root.uState !== "idle"
                    implicitWidth:  badgeText.implicitWidth + Theme.sp(16)
                    implicitHeight: Theme.sp(26)
                    radius:         Theme.radius
                    color:          root.pal.bg
                    border.width:   1
                    border.color:   root.pal.fg

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text:           root.pal.text
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          root.pal.fg
                    }
                }

                Item { Layout.fillWidth: true }

                // Contextual action: Check for updates / Download & install / Restart now.
                // Hidden while a check/download/verify is in flight; present but disabled
                // on a devbuild / unsupported platform, where there is no engine to drive
                // — so the row reads as "nothing to press yet" rather than as a gap.
                PpButton {
                    readonly property string mode: UpdateStates.actionMode(root.uState)
                    visible: mode !== ""
                    enabled: updateController.supported
                    primary: mode === "download" || mode === "restart"
                    label:   UpdateStates.actionLabel(mode, "long")
                    onClicked: {
                        if (mode === "download")     updateController.download()
                        else if (mode === "restart") updateController.relaunch()
                        else                         updateController.checkNow()
                    }
                }
            }

            // Updater status / error line (e.g. "End the session to install").
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: updateController.state === "error" ? updateController.errorString
                                                         : updateController.statusMessage
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          updateController.state === "error" ? Theme.colorWarn : Theme.colorText3
                wrapMode:       Text.WordWrap
            }
        }

        PpDivider { Layout.fillWidth: true }

        // ── Bundled libraries ───────────────────────────────────────────────
        Text {
            text:                qsTr("Libraries")
            font.family:         Theme.fontBody
            font.pixelSize:      Theme.fontSzLabel
            font.letterSpacing:  Theme.trackingLabel
            font.capitalization: Theme.capsFont
            color:               Theme.colorText3
        }

        ScrollView {
            id: depsScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(depsCol.implicitHeight, Theme.sp(210))
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: depsCol
                width:   depsScroll.availableWidth
                spacing: Theme.gap(7)

                Repeater {
                    model: appInfo.dependencies
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.gap(12)

                        Text {
                            text:           modelData.name
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText2
                            Layout.fillWidth: true
                            elide:          Text.ElideRight
                        }
                        Text {
                            text:           modelData.version
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText3
                        }
                    }
                }
            }
        }

        // ── Close ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PpButton {
                label:     qsTr("Close")
                primary:   true
                onClicked: root.close()
            }
        }
    }
}
