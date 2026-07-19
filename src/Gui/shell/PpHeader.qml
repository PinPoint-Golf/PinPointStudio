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

Item {
    id: root

    // The screen name shown after the separator. Set by Main.qml.
    property string screenName: ""
    property bool showVersionPill: false
    property bool isFullscreen: false

    // True while a session screen (Swing/Wrist/GRF/Coach) is current — set by
    // Main.qml. Gates the centred DETECT cluster, which only makes sense during
    // a live Capture session.
    property bool sessionScreenActive: false

    // Override-able navigation state — default to navController, Main.qml
    // can replace these to intercept back/forward for wizard step navigation.
    property bool backEnabled:    navController.canGoBack
    property bool forwardEnabled: navController.canGoForward

    signal fullscreenToggleRequested()
    signal backRequested()
    signal forwardRequested()
    // Version pill clicked — Main.qml opens the About PinPoint Studio dialog.
    signal aboutRequested()
    // Close button pressed — Main.qml routes this through window.close() so it
    // shares the onClosing interception (session-active confirm) with the WM
    // close. Never call Qt.quit() here: it bypasses onClosing entirely.
    signal closeRequested()

    implicitHeight: Theme.headerHeight

    Rectangle { anchors.fill: parent; color: Theme.colorSurface }

    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height:  1
        color:   Theme.colorBorderMid
        opacity: Theme.borderOpacityNormal
    }

    RowLayout {
        anchors.fill:        parent
        anchors.leftMargin:  24
        anchors.rightMargin: 16
        spacing:             16

        // Back/forward navigation cluster
        Row {
            spacing:          Theme.sp(4)
            Layout.alignment: Qt.AlignVCenter
            height:           Theme.headerHeight

            Item {
                width:  Theme.sp(28)
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text:             "‹"
                    font.family:      Theme.fontBody
                    font.pixelSize:   Theme.sp(16)
                    color:            root.backEnabled
                                      ? (backHover.containsMouse ? Theme.colorText
                                                                 : Theme.colorText2)
                                      : Theme.colorText3
                    opacity:          root.backEnabled ? 1.0 : 0.4
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast }
                    }
                }

                PpPressable {
                    id:             backHover
                    enabled:        root.backEnabled
                    confirmOnClick: true
                    onClicked:      root.backRequested()
                }
            }

            Item {
                width:  Theme.sp(28)
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text:             "›"
                    font.family:      Theme.fontBody
                    font.pixelSize:   Theme.sp(16)
                    color:            root.forwardEnabled
                                      ? (fwdHover.containsMouse ? Theme.colorText
                                                                : Theme.colorText2)
                                      : Theme.colorText3
                    opacity:          root.forwardEnabled ? 1.0 : 0.4
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast }
                    }
                }

                PpPressable {
                    id:             fwdHover
                    enabled:        root.forwardEnabled
                    confirmOnClick: true
                    onClicked:      root.forwardRequested()
                }
            }
        }

        Rectangle {
            width:   1
            height:  Theme.sp(16)
            color:   Theme.colorBorderMid
            opacity: Theme.borderOpacityNormal
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text:                root.screenName
            font.family:         Theme.fontBody
            font.pixelSize:      Theme.fontSzLabel
            font.letterSpacing:  Theme.trackingLabel
            font.capitalization: Font.AllUppercase
            color:               Theme.colorText3
        }

        Item { Layout.fillWidth: true }

        // Clickable version pill → opens the About PinPoint Studio dialog.
        Item {
            id: versionPill
            Layout.alignment:   Qt.AlignVCenter
            implicitWidth:      versionLabel.implicitWidth + Theme.sp(10)
            implicitHeight:     versionLabel.implicitHeight + Theme.sp(6)
            visible:            root.showVersionPill

            Rectangle {
                anchors.fill:  parent
                color:         versionPillMa.containsMouse ? Theme.colorBg2 : "transparent"
                border.width:  1
                border.color:  versionPillMa.containsMouse ? Theme.colorBorderStrong : Theme.colorBorderMid
                radius:        Theme.radius
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }

            Text {
                id: versionLabel
                anchors.centerIn:    parent
                text:                appSettings.appVersion
                font.family:         Theme.fontData
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingData
                color:               versionPillMa.containsMouse ? Theme.colorText2 : Theme.colorText3
            }

            PpPressable {
                id: versionPillMa
                onClicked: root.aboutRequested()
            }

            ToolTip.visible: versionPillMa.containsMouse
            ToolTip.text:    qsTr("About PinPoint Studio")
            ToolTip.delay:   500
        }

        // Fullscreen toggle button — corner-bracket expand/compress icon
        Item {
            id: fsButton
            Layout.alignment: Qt.AlignVCenter
            implicitWidth:    Theme.sp(28)
            implicitHeight:   Theme.headerHeight

            readonly property color iconColor: fsHover.containsMouse ? Theme.colorText : Theme.colorText3

            Canvas {
                id: fsCanvas
                anchors.centerIn: parent
                width:  Theme.sp(14)
                height: Theme.sp(14)

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = fsButton.iconColor.toString()
                    ctx.lineWidth   = Math.max(1, Math.round(width / 10))
                    ctx.lineCap     = "square"
                    var a = Math.round(width * 0.35)  // arm length
                    var w = width, h = height
                    ctx.beginPath()
                    if (!root.isFullscreen) {
                        // Expand: L-brackets at corners pointing outward
                        ctx.moveTo(a, 0); ctx.lineTo(0, 0); ctx.lineTo(0, a)
                        ctx.moveTo(w-a, 0); ctx.lineTo(w, 0); ctx.lineTo(w, a)
                        ctx.moveTo(0, h-a); ctx.lineTo(0, h); ctx.lineTo(a, h)
                        ctx.moveTo(w-a, h); ctx.lineTo(w, h); ctx.lineTo(w, h-a)
                    } else {
                        // Compress: L-brackets inset, pointing inward
                        ctx.moveTo(0, a); ctx.lineTo(a, a); ctx.lineTo(a, 0)
                        ctx.moveTo(w, a); ctx.lineTo(w-a, a); ctx.lineTo(w-a, 0)
                        ctx.moveTo(0, h-a); ctx.lineTo(a, h-a); ctx.lineTo(a, h)
                        ctx.moveTo(w, h-a); ctx.lineTo(w-a, h-a); ctx.lineTo(w-a, h)
                    }
                    ctx.stroke()
                }

                onWidthChanged:           requestPaint()
                Component.onCompleted:    requestPaint()
            }

            Connections {
                target: root
                function onIsFullscreenChanged() { fsCanvas.requestPaint() }
            }

            Connections {
                target: fsButton
                function onIconColorChanged() { fsCanvas.requestPaint() }
            }

            PpPressable {
                id:        fsHover
                onClicked: root.fullscreenToggleRequested()
            }
        }

        // Close button — only on Linux/Windows in full-screen mode (macOS has the
        // traffic-light controls in the title bar even when the app is full-screened).
        Item {
            id: closeButton
            Layout.alignment: Qt.AlignVCenter
            implicitWidth:    Theme.sp(28)
            implicitHeight:   Theme.headerHeight
            visible:          root.isFullscreen && Qt.platform.os !== "macos"

            Text {
                anchors.centerIn: parent
                text:             "✕"
                font.family:      Theme.fontBody
                font.pixelSize:   Theme.sp(13)
                color:            closeHover.containsMouse ? Theme.colorError : Theme.colorText3
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }

            PpPressable {
                id:        closeHover
                onClicked: root.closeRequested()
            }
        }

        Item { Layout.preferredWidth: Theme.sp(4) }
    }

    // Centre slot of the title bar (moved up from the session toolbar). Both are
    // siblings of the RowLayout so they centre on the bar itself, independent of
    // the left/right clusters; the mid-bar region is otherwise empty. While a
    // shot is being processed the ANALYSING badge takes the slot; otherwise the
    // DETECT cluster (which is also the manual SHOT trigger) holds it. Gated to
    // session screens — Replay/Analyse and non-session screens hide both.
    readonly property bool _analysisActive: shotProcessor.busy && !shotProcessor.isReplaying

    PpAnalysingBadge {
        anchors.centerIn: parent
        visible: root.sessionScreenActive
                 && root._analysisActive
                 && !sessionReviewController.reviewActive
    }

    PpDetectCluster {
        anchors.centerIn: parent
        visible: root.sessionScreenActive
                 && SessionMode.mode === SessionMode.capture
                 && !sessionReviewController.reviewActive
                 && !root._analysisActive   // yields the slot to the ANALYSING badge
    }
}
