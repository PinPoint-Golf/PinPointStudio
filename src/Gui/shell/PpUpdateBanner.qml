/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

import QtQuick
import QtQuick.Layouts
import PinPointStudio

// Non-modal, dismissible launch banner for the Linux in-app updater (design §5,
// surface A). Reads the global context properties (updateController, appSettings,
// sessionController) directly. Suppressed during a session and for skipped
// versions; the Settings → General Version row remains the always-available
// surface B. Place anchored at the top of the window in Main.qml.
Item {
    id: bannerRoot

    // Local, run-scoped UI state.
    property bool dismissedThisRun: false   // "Later" → hide until next launch
    property bool showNotes:        false   // changelog disclosure
    property bool allowed:          true    // host gate (e.g. off during the wizard)

    readonly property string uState: updateController.state
    readonly property bool offerFresh:
        uState === "available"
        && updateController.latestVersion !== appSettings.skippedUpdateVersion
    // Show for a fresh offer, while a banner-initiated download/verify runs, and
    // when ready to restart — but never over a live session or where the host
    // disallows it.
    readonly property bool shouldShow:
        allowed && updateController.supported && !sessionController.running && !dismissedThisRun
        && (offerFresh || uState === "downloading" || uState === "verifying" || uState === "ready")

    implicitHeight: shouldShow ? card.implicitHeight : 0
    opacity: shouldShow ? 1.0 : 0.0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    onShouldShowChanged: if (!shouldShow) showNotes = false

    readonly property bool isReady:   uState === "ready"
    readonly property bool isWorking: uState === "downloading" || uState === "verifying"

    Rectangle {
        id: card
        width: parent.width
        implicitHeight: contentCol.implicitHeight + Theme.sp(28)
        color:  Theme.colorBg2
        radius: Theme.radius
        border.width: 1
        border.color: bannerRoot.isReady ? Theme.colorGood : Theme.colorAccent

        ColumnLayout {
            id: contentCol
            anchors.fill: parent
            anchors.margins: Theme.sp(14)
            spacing: Theme.sp(8)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(12)

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                    color: bannerRoot.isReady ? Theme.colorGood : Theme.colorAccent
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(2)

                    Text {
                        Layout.fillWidth: true
                        text: bannerRoot.isReady     ? qsTr("Update ready")
                            : bannerRoot.uState === "downloading" ? qsTr("Downloading update…")
                            : bannerRoot.uState === "verifying"   ? qsTr("Verifying update…")
                            : qsTr("Update available — %1").arg(updateController.latestVersion)
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                        elide:          Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: bannerRoot.isReady ? qsTr("Restart to finish installing %1").arg(updateController.latestVersion)
                            : bannerRoot.uState === "downloading" ? qsTr("%1%").arg(Math.round(updateController.progress * 100))
                            : bannerRoot.uState === "verifying"   ? qsTr("Checking signature")
                            : qsTr("You have %1").arg(updateController.currentVersion)
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                        elide:          Text.ElideRight
                    }
                }

                // "What's new" disclosure (offer only, when notes exist).
                Text {
                    visible: bannerRoot.offerFresh && updateController.releaseNotes.length > 0
                    text: bannerRoot.showNotes ? qsTr("Hide notes") : qsTr("What's new")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorAccent
                    TapHandler { onTapped: bannerRoot.showNotes = !bannerRoot.showNotes }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                // Actions per state.
                PpButton {
                    visible: bannerRoot.offerFresh
                    label: qsTr("Skip")
                    onClicked: updateController.skipVersion()
                }
                PpButton {
                    visible: bannerRoot.offerFresh || bannerRoot.isReady
                    label: qsTr("Later")
                    onClicked: {
                        if (bannerRoot.isReady) updateController.installOnNextLaunch()
                        else                    bannerRoot.dismissedThisRun = true
                    }
                }
                PpButton {
                    visible: bannerRoot.offerFresh
                    label: qsTr("Install")
                    primary: true
                    onClicked: updateController.download()
                }
                PpButton {
                    visible: bannerRoot.isReady
                    label: qsTr("Restart now")
                    primary: true
                    onClicked: updateController.relaunch()
                }
            }

            // Indeterminate-free progress bar during download.
            Rectangle {
                Layout.fillWidth: true
                visible: bannerRoot.uState === "downloading"
                height: Theme.sp(4)
                radius: Theme.sp(2)
                color:  Theme.colorBg3
                Rectangle {
                    height: parent.height
                    radius: parent.radius
                    color:  Theme.colorAccent
                    width:  parent.width * Math.max(0, Math.min(1, updateController.progress))
                    Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                }
            }

            // Changelog (Markdown), bounded + clipped.
            Text {
                Layout.fillWidth: true
                Layout.maximumHeight: Theme.sp(140)
                visible: bannerRoot.showNotes && bannerRoot.offerFresh
                         && updateController.releaseNotes.length > 0
                text:           updateController.releaseNotes
                textFormat:     Text.MarkdownText
                wrapMode:       Text.WordWrap
                clip:           true
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color:          Theme.colorText2
            }
        }
    }
}
