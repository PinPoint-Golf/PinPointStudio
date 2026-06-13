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

// Persistent session toolbar (Command-Bar direction): session clock + the one
// session-global Capture verb + two count-badged device pills. Each pill opens a
// Popup hosting the relevant device panel. Reusable across all four mode screens
// (Wrist first). Calibration is handled entirely INSIDE the panels — the toolbar
// no longer routes a calibrate request anywhere.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    implicitHeight: Theme.sp(60)

    // Session type of the hosting screen (SessionController::Type, 0–3).
    // Set by ScreenSessionMode / ScreenWrist; passed to sessionController.start()
    // so the session lock knows which type owns the running session.
    property int sessionType: -1

    // Mode-switch labels, indexed by SessionMode.mode (capture/review/analyse).
    readonly property var modeNames: [qsTr("Capture"), qsTr("Review"), qsTr("Analyse")]

    // Live capture truth — mirrors the actual EventBuffer state (same source as
    // the Resource Monitor). cameraManager.bufferState notifies on every net
    // transition, including those caused by IMU register/deregister (forwarded
    // via applyCaptureIntent in main.cpp).
    readonly property bool captureLive: cameraManager.bufferState === "capturing"

    // ── Aggregate device state (drives pill badges + colours) ───────────────
    readonly property int  camTotal:     cameraManager.cameraList.length
    readonly property int  camConnected: cameraManager.instances.length
    // True when a connected camera is not "fixed in place" — i.e. it still needs
    // (stereo) calibration this session. Mirrors PpCameraPanel.needsCalibration;
    // stays lit until the camera is calibrated/fixed.
    readonly property bool camNeedsAttention: {
        var _dep  = cameraManager.instances
        var list  = cameraManager.cameraList
        var fixed = appSettings.cameraFixedInPlace
        for (var i = 0; i < list.length; ++i)
            if (list[i].selected && fixed[list[i].cameraKey] !== true) return true
        return false
    }

    readonly property int  imuTotal:     imuManager.imuDeviceList.length
    readonly property int  imuConnected: imuManager.imuCount   // connected instance count
    // True when at least one connected IMU is not yet *successfully* calibrated →
    // attention. A sensor counts as calibrated only when anatCalibrated AND the
    // mount check passes (same thresholds the calibration flow uses: deviation
    // ≤ 15°, gravity ≤ 25°). anatCalibrated alone flips true at the phase-1 arm-down
    // capture, before phase-2 mount validation — so a failed calibration must keep
    // the attention indicator lit, not clear it.
    function _imuCalibratedOk(inst) {
        return inst && inst.anatCalibrated
            && inst.mountDeviationDeg    <= 15.0
            && inst.mountGravityErrorDeg <= 25.0
    }
    readonly property bool imuNeedsAttention: {
        var _dep = imuManager.instances
        var list = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i) {
            var inst = imuManager.instanceFor(list[i].id)
            if (inst && inst.imuConnected && !_imuCalibratedOk(inst)) return true
        }
        return false
    }

    // Lowest battery % across all connected IMUs (−1 when none report a level),
    // sourced from the manager's aggregate so it tracks live battery updates.
    // A reading below 50% raises a toolbar warning naming the level.
    readonly property int  imuLowestBattery: imuManager.lowBatteryPercent
    readonly property bool imuBatteryLow:    imuLowestBattery >= 0 && imuLowestBattery < 50

    Rectangle {
        anchors.fill: parent
        color: Theme.aesthetic === "instrument" ? Theme.colorBg2 : Theme.colorSurface
    }
    Rectangle {  // bottom hairline
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
    }

    RowLayout {
        anchors { fill: parent; leftMargin: Theme.sp(16); rightMargin: Theme.sp(14) }
        spacing: Theme.sp(12)

        // ── Review strip — replaces the whole capture cluster while a saved ──
        // session is loaded (device pills stay; see the !reviewActive gates on
        // captureBtn / clock / End / SHOT below).
        Rectangle {
            id: reviewStrip
            visible: sessionReviewController.reviewActive
            Layout.alignment: Qt.AlignVCenter
            implicitWidth:  reviewRow.implicitWidth + Theme.sp(28)
            implicitHeight: Theme.sp(40)
            radius: Theme.radius
            color: Theme.colorAccentLight
            border.width: 1; border.color: Theme.colorAccentMid

            Row {
                id: reviewRow
                anchors.centerIn: parent
                spacing: Theme.sp(10)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("REVIEWING")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro; color: Theme.colorAccent
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: sessionReviewController.activeDayLabel
                          + (sessionReviewController.activeTimeLabel
                                 ? " · " + sessionReviewController.activeTimeLabel : "")
                          + (sessionReviewController.activeClubMix
                                 ? " — " + sessionReviewController.activeClubMix : "")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                    color: Theme.colorText
                }
            }
        }

        // ── Resume live session — leaves review, back to the live model ─────
        Rectangle {
            id: resumeBtn
            visible: sessionReviewController.reviewActive
            Layout.alignment: Qt.AlignVCenter
            implicitWidth:  resumeRow.implicitWidth + Theme.sp(24)
            implicitHeight: Theme.sp(32)
            radius: Theme.radius
            color: resumeMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
            border.width: 1; border.color: Theme.colorBorderMid
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            Row {
                id: resumeRow
                anchors.centerIn: parent
                spacing: Theme.sp(7)
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.sp(7); height: Theme.sp(7); radius: Theme.sp(3.5)
                    color: Theme.colorError
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Resume live session")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText2
                }
            }
            MouseArea {
                id: resumeMa
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sessionReviewController.resumeLive()
            }
        }

        // ── Capture (session-global) — far left, always first ───────────────
        Rectangle {
            id: captureBtn
            visible: !sessionReviewController.reviewActive
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: captureLbl.implicitWidth + Theme.sp(28)
            implicitHeight: Theme.sp(40)
            radius: Theme.radius
            color: root.captureLive ? Theme.colorErrorLight : Theme.colorAccent
            border.width: root.captureLive ? 1 : 0
            border.color: Theme.colorError
            Row {
                id: captureLbl
                anchors.centerIn: parent
                spacing: Theme.sp(8)
                Rectangle {
                    width: Theme.sp(9); height: Theme.sp(9); radius: Theme.sp(4.5)
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.captureLive ? Theme.colorError
                                            : (Theme.dark ? Theme.colorBg : "#FFFFFF")
                }
                Text {
                    text: root.captureLive ? qsTr("Stop") : qsTr("Capture")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                    color: root.captureLive ? Theme.colorError
                                            : (Theme.dark ? Theme.colorBg : "#FFFFFF")
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (root.captureLive) {
                        cameraManager.stopCapture()
                    } else {
                        if (!sessionController.running) sessionController.start(root.sessionType)
                        cameraManager.startCapture()
                    }
                }
            }
        }

        // ── Session clock (alongside Capture, to its right) ─────────────────
        Column {
            visible: !sessionReviewController.reviewActive
            spacing: Theme.sp(2)
            Layout.alignment: Qt.AlignVCenter
            Text {
                text: qsTr("SESSION")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
            }
            Row {
                spacing: Theme.sp(8)
                Rectangle {
                    width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.captureLive ? Theme.colorError : Theme.colorText3
                    SequentialAnimation on opacity {
                        running: root.captureLive && !Theme.reduceMotion
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: Theme.durationSlow }
                        NumberAnimation { to: 1.0; duration: Theme.durationSlow }
                    }
                }
                Text {
                    text: sessionController.elapsedLabel
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzData
                    color: Theme.colorText
                }
            }
        }

        // ── End Session (groups with the session controls) ──────────────────
        // Ghost button, only while a session is running. Confirmed via a small
        // anchored popup — the session clock can't be resumed once ended.
        Rectangle {
            id: endBtn
            visible: sessionController.running && !sessionReviewController.reviewActive
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: endLbl.implicitWidth + Theme.sp(24)
            implicitHeight: Theme.sp(40)
            radius: Theme.radius
            color: endMa.containsMouse ? Theme.colorBg2 : "transparent"
            border.width: 1
            border.color: Theme.colorBorderMid
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            Text {
                id: endLbl
                anchors.centerIn: parent
                text: qsTr("End Session")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText2
            }
            MouseArea {
                id: endMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: endPopup.opened ? endPopup.close() : endPopup.open()
            }

            Popup {
                id: endPopup
                y: endBtn.height + Theme.sp(10)
                x: 0
                padding: Theme.sp(14)
                margins: Theme.sp(8)
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                background: Rectangle {
                    color: Theme.colorSurface; radius: Theme.radiusLg
                    border.width: 1; border.color: Theme.colorBorderStrong
                }
                contentItem: Column {
                    spacing: Theme.sp(10)
                    Text {
                        text: qsTr("End session?")
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                        color: Theme.colorText
                    }
                    Row {
                        spacing: Theme.sp(8)
                        Rectangle {
                            width: confirmLbl.implicitWidth + Theme.sp(20)
                            height: Theme.sp(30); radius: Theme.radius
                            color: confirmMa.containsMouse ? Theme.colorErrorLight : "transparent"
                            border.width: 1; border.color: Theme.colorError
                            Text {
                                id: confirmLbl
                                anchors.centerIn: parent; text: qsTr("End")
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                color: Theme.colorError
                            }
                            MouseArea {
                                id: confirmMa
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    endPopup.close()
                                    cameraManager.stopCapture()
                                    sessionController.endSession()
                                    // Ending the session releases the devices:
                                    // cameras stop + deselect, IMUs disconnect
                                    // (BLE battery). The next session's wizard
                                    // reconnects what it needs.
                                    cameraManager.disconnectAll()
                                    imuManager.disconnectAll()
                                }
                            }
                        }
                        Rectangle {
                            width: cancelLbl.implicitWidth + Theme.sp(20)
                            height: Theme.sp(30); radius: Theme.radius
                            color: cancelMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
                            border.width: 1; border.color: Theme.colorBorderMid
                            Text {
                                id: cancelLbl
                                anchors.centerIn: parent; text: qsTr("Cancel")
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                color: Theme.colorText2
                            }
                            MouseArea {
                                id: cancelMa
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: endPopup.close()
                            }
                        }
                    }
                }
            }
        }

        Item {   // centre the SHOT button (live only)
            Layout.fillWidth: true
            visible: !sessionReviewController.reviewActive
        }

        // ── Analysing badge — visible while the shot processor works ────────
        // (post-roll + analysis + export; hidden again once the replay runs,
        // which has its own REPLAY badge on the camera frames). One contained
        // chip, SHOT-button footprint: label + elapsed time on the top line,
        // progress bar beneath. The bar fills with
        // shotProcessor.analysisProgress (frames processed) and carries a
        // sweeping sheen + twinkles at the leading edge — the one deliberately
        // showy moment in the toolbar. All motion gated on Theme.reduceMotion;
        // the plain filling bar remains.
        Rectangle {
            id: analysingBox
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Theme.sp(128)
            implicitHeight: Theme.sp(40)
            radius: Theme.radius
            color: Theme.colorBg2
            border.width: 1
            border.color: Theme.colorBorderMid
            visible: shotProcessor.busy && !shotProcessor.isReplaying
                     && !sessionReviewController.reviewActive

            readonly property bool sparkling: visible && !Theme.reduceMotion

            // Elapsed wall-time since the shot started processing (post-roll
            // included). Hidden for the first second so instant analyses never
            // flash a "0s".
            property int elapsedS: 0
            readonly property string elapsedLabel: elapsedS >= 60
                ? Math.floor(elapsedS / 60) + ":" + String(elapsedS % 60).padStart(2, "0")
                : elapsedS + "s"
            onVisibleChanged: if (visible) elapsedS = 0
            Timer {
                running: analysingBox.visible
                interval: 1000; repeat: true
                onTriggered: analysingBox.elapsedS++
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - Theme.sp(24)
                spacing: Theme.sp(5)

                Item {   // label left, elapsed time right
                    width: parent.width
                    height: analysingLbl.implicitHeight

                    Text {
                        id: analysingLbl
                        anchors.left: parent.left
                        text: qsTr("ANALYSING")
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText3
                    }

                    Text {
                        anchors.right: parent.right
                        visible: analysingBox.elapsedS > 0
                        text: analysingBox.elapsedLabel
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color: Theme.colorText3
                        opacity: 0.7
                    }
                }

                Item {
                    id: analyseBar
                    width: parent.width
                    height: Theme.sp(4)

                    Rectangle {   // track
                        anchors.fill: parent
                        radius: height / 2
                        color: Theme.colorBg3
                    }

                    Rectangle {   // fill — never narrower than its own end caps
                        id: analyseFill
                        width: Math.max(height, parent.width * shotProcessor.analysisProgress)
                        height: parent.height
                        radius: height / 2
                        color: Theme.colorAccent
                        clip: true
                        Behavior on width { NumberAnimation { duration: Theme.durationNormal } }

                        Rectangle {   // sheen sweeping the filled portion only (parent clips)
                            id: analyseSheen
                            width: Theme.sp(22); height: parent.height
                            visible: analysingBox.sparkling
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.55) }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                            NumberAnimation on x {
                                running: analysingBox.sparkling
                                loops: Animation.Infinite
                                from: -analyseSheen.width
                                to: analyseBar.width
                                duration: 1100
                            }
                        }
                    }

                    // Twinkles riding the fill's leading edge, staggered so
                    // they read as sparkle rather than a blinking cluster —
                    // offsets tightened to stay inside the chip.
                    Repeater {
                        model: [ { dx: -2, dy: -5, period: 900  },
                                 { dx: -9, dy:  4, period: 1300 },
                                 { dx:  4, dy: -1, period: 700  } ]
                        delegate: Text {
                            required property var modelData
                            x: analyseFill.width + modelData.dx - implicitWidth / 2
                            y: analyseBar.height / 2 + modelData.dy - implicitHeight / 2
                            text: "✦"
                            font.family: Theme.fontSymbol
                            font.pixelSize: Theme.fontSzMicro - 1
                            color: Theme.colorAccent
                            opacity: 0
                            scale: 0.6
                            SequentialAnimation on opacity {
                                running: analysingBox.sparkling
                                loops: Animation.Infinite
                                PauseAnimation  { duration: modelData.period * 0.4 }
                                NumberAnimation { to: 1.0; duration: modelData.period * 0.3
                                                  easing.type: Easing.OutQuad }
                                NumberAnimation { to: 0.0; duration: modelData.period * 0.3
                                                  easing.type: Easing.InQuad }
                            }
                            SequentialAnimation on scale {
                                running: analysingBox.sparkling
                                loops: Animation.Infinite
                                PauseAnimation  { duration: modelData.period * 0.4 }
                                NumberAnimation { to: 1.15; duration: modelData.period * 0.3 }
                                NumberAnimation { to: 0.6;  duration: modelData.period * 0.3 }
                            }
                        }
                    }
                }
            }
        }

        // ── SHOT (manual shot trigger) — centred between timer and pills ────
        // Armed only while the buffer is capturing; the central
        // shotController.triggerShot() gate matches, so a disarmed click is a
        // no-op even if the binding lags.
        Rectangle {
            id: shotBtn
            visible: !sessionReviewController.reviewActive
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: shotLbl.implicitWidth + Theme.sp(28)
            implicitHeight: Theme.sp(40)
            radius: Theme.radius
            color: shotMa.containsMouse && shotController.armed ? Theme.colorBg3
                                                                : Theme.colorBg2
            border.width: 1
            border.color: Theme.colorBorderMid
            opacity: shotController.armed ? 1.0 : 0.35
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            Text {
                id: shotLbl
                anchors.centerIn: parent
                text: qsTr("SHOT")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText
            }

            // Brief accent flash on fire.
            SequentialAnimation {
                id: shotFlash
                ColorAnimation {
                    target: shotBtn; property: "border.color"
                    to: Theme.colorAccent; duration: Theme.durationFast
                }
                ColorAnimation {
                    target: shotBtn; property: "border.color"
                    to: Theme.colorBorderMid; duration: Theme.durationSlow
                }
            }

            MouseArea {
                id: shotMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: shotController.armed
                cursorShape: shotController.armed ? Qt.PointingHandCursor
                                                  : Qt.ArrowCursor
                onClicked: {
                    shotController.triggerShot()
                    if (!Theme.reduceMotion) shotFlash.restart()
                }
            }
        }

        // ── Shot-detection indicators — one dot per detection modality ─────
        // Live feedback on the multi-modal detector: IMU impact + acoustic
        // onset today, Ball/vision reserved (always dim until its producer
        // lands). Tiers: glow = armed & listening, dim = auto-detect on but
        // not armed, even-more-dim = auto-detect off or modality unavailable.
        // A detection flashes the dot green and decays back over 2 s.
        Row {
            id: detectCluster
            visible: !sessionReviewController.reviewActive
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.sp(8)

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("DETECT")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.sp(10)
                DetectDot { id: imuDot;  available: imuManager.imuCount > 0 }
                DetectDot { id: acDot;   available: true }
                // Ball/vision dot (design §8.3): amber while running on the
                // generic (uncalibrated) detector or when lighting has drifted
                // from the calibration envelope; steady green core while the
                // ball is present at the hitting area. The shot flash joins
                // when the Source::Ball candidate feed lands.
                DetectDot {
                    id: ballDot
                    readonly property QtObject foInst: {
                        var insts = cameraManager.instances
                        for (var i = 0; i < insts.length; ++i)
                            if (insts[i].perspective === CameraInstance.FaceOn)
                                return insts[i]
                        return null
                    }
                    available: foInst !== null && foInst.ballEnabled
                    baseColor: foInst !== null
                               && (!foInst.ballCalibrated || foInst.ballDrifting)
                               ? Theme.colorAttention : Theme.colorAccent
                    presence:  foInst !== null && foInst.ballPresent
                }
            }
        }

        Item { Layout.fillWidth: true }   // push device pills to the right

        // ── View · subtle vertical divider sets the layout cluster apart ────
        // Always present alongside the View/Cameras/IMUs cluster (the device
        // pills are never gated on review state, and neither is View).
        PpDivider {
            orientation: Qt.Vertical
            Layout.preferredHeight: Theme.sp(28)
            Layout.alignment: Qt.AlignVCenter
        }

        // ── Mode switch — primary layout control (Capture/Review/Analyse) ─────
        // The activity axis. Review is never blocked: with no focused swing the
        // stage shows its empty-state. Selecting a mode never stops live capture
        // (that is the data-source axis, owned by SessionReviewController).
        PpSegmentedControl {
            id: modeSwitch
            Layout.preferredWidth: Theme.sp(220)
            Layout.alignment: Qt.AlignVCenter
            solid: false
            options:  root.modeNames
            selected: root.modeNames[SessionMode.mode]
            onActivated: (value) => {
                var i = root.modeNames.indexOf(value)
                if (i === SessionMode.review)       SessionMode.showReview()
                else if (i === SessionMode.analyse) SessionMode.enterAnalyse()
                else                                SessionMode.enterCapture()
            }
        }

        // ── View pill — edits the CURRENT mode's saved layout ─────────────────
        ViewPill {
            id: viewPill
            label: root.modeNames[SessionMode.mode]
            active: viewPopup.opened
            onClicked: {
                camPopup.close(); imuPopup.close()
                viewPopup.opened ? viewPopup.close() : viewPopup.open()
            }
        }

        // ── Cameras pill ────────────────────────────────────────────────────
        DevicePill {
            id: camPill
            glyph: "◫"                 // ◫
            title: qsTr("Cameras")
            count: root.camConnected
            valueText: root.camTotal === 0 ? qsTr("none")
                        : root.camNeedsAttention ? qsTr("calibrate")
                        : qsTr("%1 of %2").arg(root.camConnected).arg(root.camTotal)
            ledColor: root.camNeedsAttention ? Theme.colorAttention
                       : root.camConnected > 0 ? Theme.colorGood : Theme.colorText3
            attention: root.camNeedsAttention
            onClicked: { imuPopup.close(); viewPopup.close(); camPopup.opened ? camPopup.close() : camPopup.open() }
        }

        // ── IMUs pill ───────────────────────────────────────────────────────
        // Low battery takes priority over the calibrate hint in the value line —
        // a dying sensor is time-critical, and the message names the level so the
        // user knows how low (e.g. "battery 32%"). Critical (≤20%) reads red.
        DevicePill {
            id: imuPill
            glyph: "⦿"                 // ⦿
            title: qsTr("IMUs")
            count: root.imuConnected
            valueText: root.imuTotal === 0 ? qsTr("none")
                        : root.imuBatteryLow ? qsTr("battery %1%").arg(root.imuLowestBattery)
                        : root.imuNeedsAttention ? qsTr("calibrate")
                        : qsTr("%1 connected").arg(root.imuConnected)
            ledColor: root.imuBatteryLow ? (root.imuLowestBattery <= 20 ? Theme.colorError : Theme.colorWarn)
                       : root.imuNeedsAttention ? Theme.colorAttention
                       : root.imuConnected > 0 ? Theme.colorGood : Theme.colorText3
            attention: root.imuNeedsAttention && !root.imuBatteryLow
            warn: root.imuBatteryLow
            warnColor: root.imuLowestBattery <= 20 ? Theme.colorError : Theme.colorWarn
            onClicked: { camPopup.close(); viewPopup.close(); imuPopup.opened ? imuPopup.close() : imuPopup.open() }
        }
    }

    // ── Popups host the reusable panels; positioned under their pills ───────
    // margins clamp the popup within the window; the panel's implicitHeight
    // drives the popup height so it grows when a panel enters calibrate mode.
    Popup {
        id: viewPopup
        parent: viewPill
        y: viewPill.height + Theme.sp(10)
        x: viewPill.width - width
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpViewPanel { }
    }

    Popup {
        id: camPopup
        parent: camPill
        y: camPill.height + Theme.sp(10)
        x: camPill.width - width
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpCameraPanel {}
    }

    Popup {
        id: imuPopup
        parent: imuPill
        y: imuPill.height + Theme.sp(10)
        x: imuPill.width - width
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpImuPanel {}
    }

    // ── Detection-dot flashes ──────────────────────────────────────────────
    // Each detector firing flashes its dot green; DetectDot.triggerFlash()
    // self-gates on the dot being armed (capturing + auto-detect on), so a
    // detector firing while disarmed leaves the dot at its dim tier. Acoustic
    // onsets are emitted on the audio thread — the queued QML connection
    // marshals them onto the GUI thread.
    Connections {
        target: imuManager
        function onImpactDetected(estImpactUs, confidence) { imuDot.triggerFlash() }
    }
    Connections {
        target: controller   // TranscriptionController owns the acoustic detector
        function onImpactDetected(estImpactUs, confidence) { acDot.triggerFlash() }
    }

    // ── Shot-detection indicator dot ────────────────────────────────────────
    // One per detection modality (see the DETECT cluster). Brightness tiers:
    // even-more-dim (auto-detect off, or unavailable — e.g. the Ball
    // placeholder / no IMU connected), dim (armed pipeline idle), glow (armed
    // & listening, with a breathing halo). A detection flashes the dot green
    // and decays to the base tier over 2 s. Call triggerFlash() to fire it.
    component DetectDot: Item {
        id: dd
        property bool   available: true
        property real   flash:     0     // 1 just after a detection → 0
        // Core/halo colour tier — Theme.colorAttention marks a modality that
        // is running but needs attention (e.g. ball detection uncalibrated).
        property color  baseColor: Theme.colorAccent
        // Steady-good core while the modality's subject is present (ball seen
        // at the hitting area). Armed tier only.
        property bool   presence:  false

        readonly property bool _live:  available && appSettings.autoDetectSwing
        readonly property bool armed:  _live && shotController.armed
        readonly property real _alpha: armed ? 1.0 : (_live ? 0.38 : 0.14)

        implicitWidth: Theme.sp(8); implicitHeight: Theme.sp(8)

        function triggerFlash() {
            if (!armed) return
            _decay.stop(); _rmHold.stop()
            flash = 1
            if (Theme.reduceMotion) _rmHold.restart(); else _decay.restart()
        }

        // Breathing halo — armed only.
        Rectangle {
            anchors.centerIn: parent
            width: Theme.sp(15); height: width; radius: width / 2
            visible: dd.armed
            color: Qt.rgba(dd.baseColor.r, dd.baseColor.g, dd.baseColor.b, 0.22)
            SequentialAnimation on scale {
                running: dd.armed && !Theme.reduceMotion
                loops: Animation.Infinite
                NumberAnimation { to: 1.3; duration: Theme.durationSlow }
                NumberAnimation { to: 1.0; duration: Theme.durationSlow }
            }
        }
        // Core dot — colour tier fades between states.
        Rectangle {
            anchors.fill: parent; radius: width / 2
            color: dd.presence && dd.armed
                   ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.95)
                   : Qt.rgba(dd.baseColor.r, dd.baseColor.g, dd.baseColor.b, dd._alpha)
            Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
        }
        // Green detection flash, decays to reveal the core tier beneath.
        Rectangle {
            anchors.fill: parent; radius: width / 2
            color: Theme.colorGood
            opacity: dd.flash
        }
        NumberAnimation { id: _decay; target: dd; property: "flash"; to: 0; duration: 2000 }
        Timer { id: _rmHold; interval: 2000; onTriggered: dd.flash = 0 }   // reduce-motion: hold then clear
    }

    // ── View pill — lighter sibling of DevicePill (no badge; shows the active
    // mode and a chevron; accent ring while its popup is open) ────────────────
    component ViewPill: Rectangle {
        property string label: ""
        property bool   active: false
        signal clicked()

        Layout.alignment: Qt.AlignVCenter
        implicitWidth: vpRow.implicitWidth + Theme.sp(24)
        implicitHeight: Theme.sp(44)
        radius: Theme.radius
        color: vpMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
        border.width: 1
        border.color: active ? Theme.colorAccentMid : Theme.colorBorderMid
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        RowLayout {
            id: vpRow
            anchors { fill: parent; leftMargin: Theme.sp(11); rightMargin: Theme.sp(11) }
            spacing: Theme.sp(11)
            Item {
                Layout.preferredWidth: Theme.sp(34); Layout.preferredHeight: Theme.sp(34)
                Layout.alignment: Qt.AlignVCenter
                Rectangle {
                    anchors.fill: parent; radius: Theme.radius; color: Theme.colorSurface
                    Text {
                        anchors.centerIn: parent; text: "▦"   // ▦ — swap for project icon set
                        font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(18)
                        color: active ? Theme.colorAccent : Theme.colorText2
                    }
                }
            }
            Column {
                Layout.alignment: Qt.AlignVCenter; spacing: Theme.sp(2)
                Text {
                    text: qsTr("VIEW"); font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                }
                Row {
                    spacing: Theme.sp(4)
                    Text {
                        text: label
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"  // ▾
                        font.pixelSize: Theme.fontSzMicro; color: Theme.colorText2
                    }
                }
            }
        }
        MouseArea {
            id: vpMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked()
        }
    }

    // ── Inline pill component ───────────────────────────────────────────────
    component DevicePill: Rectangle {
        property string glyph:     ""
        property string title:     ""
        property string valueText: ""
        property int    count:     0
        property color  ledColor:  Theme.colorText3
        property bool   attention: false   // tints the value text amber (e.g. "calibrate")
        property bool   warn:      false   // overrides the value-text tint (e.g. low battery)
        property color  warnColor: Theme.colorWarn
        signal clicked()

        Layout.alignment: Qt.AlignVCenter
        implicitWidth: pillRow.implicitWidth + Theme.sp(24)
        implicitHeight: Theme.sp(44)
        radius: Theme.radius
        color: pillMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
        border.width: 1; border.color: Theme.colorBorderMid
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        RowLayout {
            id: pillRow
            anchors { fill: parent; leftMargin: Theme.sp(11); rightMargin: Theme.sp(13) }
            spacing: Theme.sp(11)

            Item {  // glyph + count badge
                Layout.preferredWidth: Theme.sp(34); Layout.preferredHeight: Theme.sp(34)
                Layout.alignment: Qt.AlignVCenter
                Rectangle {
                    anchors.fill: parent; radius: Theme.radius; color: Theme.colorSurface
                    Text {
                        anchors.centerIn: parent; text: glyph
                        font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(18)
                        color: Theme.colorText2
                    }
                }
                Rectangle {
                    anchors { right: parent.right; top: parent.top
                              rightMargin: -Theme.sp(5); topMargin: -Theme.sp(5) }
                    width: Theme.sp(19); height: Theme.sp(19); radius: width / 2
                    color: ledColor
                    Text {
                        anchors.centerIn: parent; text: count
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        color: Theme.dark ? Theme.colorBg : "#FFFFFF"
                    }
                }
            }
            Column {
                Layout.alignment: Qt.AlignVCenter; spacing: Theme.sp(2)
                Text {
                    text: title; font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                }
                Text {
                    text: valueText; font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color: warn ? warnColor
                         : attention ? Theme.colorAttention : Theme.colorText
                }
            }
        }
        MouseArea {
            id: pillMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked()
        }
    }
}
