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

// Markup — a view-menu stage panel for in-app ground-truth labelling of the
// currently-focused swing (load → edit → save). Scrub frame-accurately, click
// grip then head to lay the club, press a P key (1–9, 0=P10) to tag a position;
// existing truth.json is loaded for editing and re-saved in place.
//
// Self-contained: it decodes its own exact frames via MarkupController
// (cv::VideoCapture → image://markup), independent of the replay stage.
// MarkupController is a singleton, so only the ACTIVE host screen's panel drives
// it — `panelActive` gates loadSwing so hidden-screen copies stay inert.
Item {
    id: root

    property int    sessionType: -1
    // Focused swing dir (replay, else carousel selection) — host binds this.
    property string targetSwingDir: ""
    // True only for the visible session screen's panel (host passes _screenActive).
    property bool   panelActive: true

    // grip→head picking is two clicks; the first is held here until the second.
    property bool pendingGrip: false
    property real gripNx: 0
    property real gripNy: 0

    readonly property var pDefs: [
        { key: "1", name: "p1",  label: "P1",  desc: "Address" },
        { key: "2", name: "p2",  label: "P2",  desc: "Club parallel (back)" },
        { key: "3", name: "p3",  label: "P3",  desc: "Lead arm parallel (back)" },
        { key: "4", name: "p4",  label: "P4",  desc: "Top" },
        { key: "5", name: "p5",  label: "P5",  desc: "Lead arm parallel (down)" },
        { key: "6", name: "p6",  label: "P6",  desc: "Shaft parallel (down)" },
        { key: "7", name: "p7",  label: "P7",  desc: "Impact" },
        { key: "8", name: "p8",  label: "P8",  desc: "Shaft parallel (follow)" },
        { key: "9", name: "p9",  label: "P9",  desc: "Arm parallel (follow)" },
        { key: "0", name: "p10", label: "P10", desc: "Finish" }
    ]

    readonly property var cocoEdges: [
        [5,7],[7,9],[6,8],[8,10],[5,6],[5,11],[6,12],[11,12],
        [11,13],[13,15],[12,14],[14,16],[0,5],[0,6],[0,1],[0,2],[1,3],[2,4]
    ]
    readonly property real kpMinConf: 0.30

    function markEventByKey(k) {
        for (var i = 0; i < pDefs.length; ++i)
            if (pDefs[i].key === k) { markupController.setEvent(pDefs[i].name); return }
    }
    function pComplete() {
        var n = 0
        for (var i = 0; i < pDefs.length; ++i) {
            var e = markupController.events[pDefs[i].name]
            if (e && e.hasClub) ++n
        }
        return n
    }

    // Only the active screen's panel loads into the shared controller (loadSwing
    // is a no-op when the dir is unchanged, so edits survive re-binding).
    function _syncSwing() {
        if (root.panelActive && root.targetSwingDir !== "")
            markupController.loadSwing(root.targetSwingDir)
    }
    onTargetSwingDirChanged: _syncSwing()
    onPanelActiveChanged: _syncSwing()
    Component.onCompleted: _syncSwing()

    focus: true
    Keys.onPressed: function (e) {
        switch (e.key) {
        case Qt.Key_A: case Qt.Key_Left:   markupController.stepFrame(-1); e.accepted = true; break
        case Qt.Key_D: case Qt.Key_Right:  markupController.stepFrame(1);  e.accepted = true; break
        case Qt.Key_Space:                 markupController.stepFrame(markupController.stride); e.accepted = true; break
        case Qt.Key_BracketLeft:           markupController.stepFrame(-markupController.stride); e.accepted = true; break
        case Qt.Key_BracketRight:          markupController.stepFrame(markupController.stride);  e.accepted = true; break
        case Qt.Key_U:
            if (root.pendingGrip) { root.pendingGrip = false; overlay.requestPaint() }
            else markupController.clearShaft()
            e.accepted = true; break
        case Qt.Key_C:   markupController.clearShaft(); e.accepted = true; break
        case Qt.Key_S:   markupController.showSkeleton = !markupController.showSkeleton; e.accepted = true; break
        case Qt.Key_Q:   markupController.save();       e.accepted = true; break
        case Qt.Key_0: case Qt.Key_1: case Qt.Key_2: case Qt.Key_3: case Qt.Key_4:
        case Qt.Key_5: case Qt.Key_6: case Qt.Key_7: case Qt.Key_8: case Qt.Key_9:
            root.markEventByKey(String.fromCharCode(e.key)); e.accepted = true; break
        }
    }

    Connections {
        target: markupController
        function onFrameChanged() { root.pendingGrip = false; overlay.requestPaint() }
        function onCurrentChanged() { root.pendingGrip = false }
        function onLabelsChanged() { overlay.requestPaint() }
        function onPoseChanged() { overlay.requestPaint() }
        function onMessage(text) { toast.show(text) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Compact header ──────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(40)
            color: Theme.colorBg2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.sp(14); anchors.rightMargin: Theme.sp(12)
                spacing: Theme.sp(12)
                Text {
                    text: qsTr("MARKUP")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                }
                Text {
                    Layout.fillWidth: true
                    text: markupController.hasSwing ? markupController.currentSwingName : qsTr("— no swing —")
                    elide: Text.ElideRight
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorText2
                }
                Text {
                    text: qsTr("%1/10 P").arg(root.pComplete())
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: Theme.colorText2
                }
                MlButton {
                    text: markupController.dirty ? qsTr("● Save (q)") : qsTr("Saved")
                    accent: markupController.dirty
                    enabled: markupController.hasSwing
                    onClicked: markupController.save()
                }
            }
        }

        // ── Body: frame view + controls ─────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Frame view (center)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#000000"

                Image {
                    id: frameImg
                    anchors.fill: parent
                    anchors.margins: Theme.sp(8)
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    asynchronous: false
                    smooth: true
                    // frameToken is 0 until the first frame decodes (async); only
                    // request once a real frame exists, else the provider has no
                    // image yet and QML logs "Failed to get image from provider".
                    source: (markupController.hasSwing && markupController.frameToken > 0)
                            ? "image://markup/" + markupController.frameToken : ""

                    readonly property real pw: paintedWidth
                    readonly property real ph: paintedHeight
                    readonly property real px0: (width - paintedWidth) / 2
                    readonly property real py0: (height - paintedHeight) / 2
                    function toNx(x) { return pw > 0 ? Math.min(1, Math.max(0, (x - px0) / pw)) : 0 }
                    function toNy(y) { return ph > 0 ? Math.min(1, Math.max(0, (y - py0) / ph)) : 0 }
                    function sx(nx) { return px0 + nx * pw }
                    function sy(ny) { return py0 + ny * ph }
                }

                Text {
                    anchors.centerIn: parent
                    visible: !markupController.hasSwing
                    width: parent.width * 0.8
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Select a shot in the carousel to load it for labelling.")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorText3
                }

                Canvas {
                    id: overlay
                    anchors.fill: frameImg
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        if (!markupController.hasSwing) return

                        var pose = markupController.currentPose
                        if (markupController.showSkeleton && pose && pose.has && pose.kp) {
                            var kp = pose.kp
                            ctx.lineWidth = 2
                            ctx.strokeStyle = Qt.rgba(0.50, 0.72, 0.96, 0.80)
                            for (var e = 0; e < root.cocoEdges.length; ++e) {
                                var a = root.cocoEdges[e][0], b = root.cocoEdges[e][1]
                                if (kp[a*3+2] < root.kpMinConf || kp[b*3+2] < root.kpMinConf) continue
                                ctx.beginPath()
                                ctx.moveTo(frameImg.sx(kp[a*3]), frameImg.sy(kp[a*3+1]))
                                ctx.lineTo(frameImg.sx(kp[b*3]), frameImg.sy(kp[b*3+1]))
                                ctx.stroke()
                            }
                            ctx.fillStyle = Qt.rgba(0.50, 0.72, 0.96, 0.95)
                            for (var i = 0; i < 17; ++i) {
                                if (kp[i*3+2] < root.kpMinConf) continue
                                ctx.beginPath()
                                ctx.arc(frameImg.sx(kp[i*3]), frameImg.sy(kp[i*3+1]), 3, 0, 2 * Math.PI)
                                ctx.fill()
                            }
                            if (pose.lead) {
                                var lx = frameImg.sx(pose.lead[0]), ly = frameImg.sy(pose.lead[1])
                                ctx.strokeStyle = Theme.colorAttention; ctx.lineWidth = 2
                                ctx.beginPath(); ctx.arc(lx, ly, 8, 0, 2 * Math.PI); ctx.stroke()
                            }
                            if (pose.trail) {
                                var tx = frameImg.sx(pose.trail[0]), ty = frameImg.sy(pose.trail[1])
                                ctx.strokeStyle = Qt.rgba(0.78, 0.55, 0.96, 0.85); ctx.lineWidth = 2
                                ctx.beginPath(); ctx.arc(tx, ty, 7, 0, 2 * Math.PI); ctx.stroke()
                            }
                        }

                        var s = markupController.currentShaft
                        if (s && s.has) {
                            var gx = frameImg.sx(s.gripNx), gy = frameImg.sy(s.gripNy)
                            var hx = frameImg.sx(s.headNx), hy = frameImg.sy(s.headNy)
                            ctx.strokeStyle = Theme.colorAccent
                            ctx.lineWidth = 2
                            ctx.beginPath(); ctx.moveTo(gx, gy); ctx.lineTo(hx, hy); ctx.stroke()
                            ctx.fillStyle = Theme.colorGood
                            ctx.beginPath(); ctx.arc(gx, gy, 5, 0, 2 * Math.PI); ctx.fill()
                            ctx.fillStyle = Theme.colorError
                            ctx.beginPath(); ctx.arc(hx, hy, 5, 0, 2 * Math.PI); ctx.fill()
                        }
                        if (root.pendingGrip) {
                            var pgx = frameImg.sx(root.gripNx), pgy = frameImg.sy(root.gripNy)
                            ctx.fillStyle = Theme.colorGood
                            ctx.beginPath(); ctx.arc(pgx, pgy, 5, 0, 2 * Math.PI); ctx.fill()
                            ctx.strokeStyle = Theme.colorGood; ctx.lineWidth = 1
                            ctx.beginPath(); ctx.arc(pgx, pgy, 9, 0, 2 * Math.PI); ctx.stroke()
                        }
                    }
                }

                MouseArea {
                    anchors.fill: frameImg
                    enabled: markupController.hasSwing
                    cursorShape: Qt.CrossCursor
                    onClicked: function (m) {
                        var nx = frameImg.toNx(m.x), ny = frameImg.toNy(m.y)
                        if (!root.pendingGrip) {
                            root.gripNx = nx; root.gripNy = ny; root.pendingGrip = true
                            overlay.requestPaint()
                        } else {
                            markupController.setShaft(root.gripNx, root.gripNy, nx, ny)
                            root.pendingGrip = false
                        }
                        root.forceActiveFocus()
                    }
                }

                // HUD: frame index / time, top-left.
                Rectangle {
                    visible: markupController.hasSwing
                    anchors { left: frameImg.left; top: frameImg.top; margins: Theme.sp(6) }
                    width: hud.width + Theme.sp(16); height: hud.height + Theme.sp(8)
                    radius: Theme.radius
                    color: Qt.rgba(0, 0, 0, 0.55)
                    Row {
                        id: hud
                        anchors.centerIn: parent
                        spacing: Theme.sp(10)
                        Text {
                            text: qsTr("frame %1 / %2").arg(markupController.frameIndex).arg(Math.max(0, markupController.frameCount - 1))
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: "#ffffff"
                        }
                        Text {
                            text: markupController.frameSec.toFixed(3) + "s"
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: Theme.colorAccentLight
                        }
                        Text {
                            text: (markupController.currentShaft && markupController.currentShaft.has) ? qsTr("◆ shaft") : (root.pendingGrip ? qsTr("◇ pick head") : qsTr("◇ pick grip"))
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
                            color: (markupController.currentShaft && markupController.currentShaft.has) ? Theme.colorGood : Theme.colorText3
                        }
                    }
                }
            }

            // Controls (right)
            Rectangle {
                Layout.preferredWidth: Theme.sp(248)
                Layout.fillHeight: true
                color: Theme.colorBg
                ScrollView {
                    id: ctrlScroll
                    anchors.fill: parent
                    anchors.margins: Theme.sp(14)
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    // Bind to width, not availableWidth: the latter subtracts the
                    // vertical scrollbar, coupling width↔contentHeight through text
                    // wrapping → contentWidth binding loop. (Matches the app's other
                    // ScrollViews, e.g. ScreenAthleteForm.)
                    contentWidth: width
                    Column {
                    width: ctrlScroll.width
                    spacing: Theme.sp(14)

                    // CLUB / SHAFT
                    Column {
                        width: parent.width; spacing: Theme.sp(8)
                        MlSection { text: qsTr("CLUB / SHAFT") }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: qsTr("Click grip, then clubhead to place the club on this frame, then press a P key (1–9, 0=P10) to tag the position.")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                        }
                        Row {
                            spacing: Theme.sp(8)
                            MlButton { text: qsTr("Undo (u)"); onClicked: {
                                if (root.pendingGrip) { root.pendingGrip = false; overlay.requestPaint() }
                                else markupController.clearShaft() } }
                            MlButton { text: qsTr("Clear (c)"); onClicked: markupController.clearShaft() }
                        }
                    }

                    // POSE OVERLAY
                    Column {
                        width: parent.width; spacing: Theme.sp(8)
                        MlSection { text: qsTr("POSE OVERLAY") }
                        MlButton {
                            text: markupController.showSkeleton ? qsTr("Skeleton: on (s)") : qsTr("Skeleton: off (s)")
                            accent: markupController.showSkeleton && markupController.poseAvailable
                            enabled: markupController.poseAvailable
                            onClicked: markupController.showSkeleton = !markupController.showSkeleton
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: markupController.poseAvailable
                                  ? qsTr("Blue = body skeleton · amber ring = lead hand · purple = trail hand. Reference only — how the grip/head relate to the skeleton.")
                                  : qsTr("No recorded pose in this swing.")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                        }
                    }

                    // STEP STRIDE
                    Column {
                        width: parent.width; spacing: Theme.sp(8)
                        MlSection { text: qsTr("STEP STRIDE") }
                        Row {
                            spacing: Theme.sp(8)
                            MlButton { text: "−"; onClicked: markupController.stride = markupController.stride - 1 }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("every %1 fr").arg(markupController.stride)
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody; color: Theme.colorText
                                width: Theme.sp(92); horizontalAlignment: Text.AlignHCenter
                            }
                            MlButton { text: "+"; onClicked: markupController.stride = markupController.stride + 1 }
                        }
                    }

                    // P-POSITIONS
                    Column {
                        width: parent.width; spacing: Theme.sp(5)
                        MlSection { text: qsTr("P-POSITIONS") }
                        Repeater {
                            model: root.pDefs
                            delegate: Rectangle {
                                readonly property var ev: markupController.events[modelData.name]
                                readonly property bool complete: !!(ev && ev.hasClub)
                                width: parent.width; height: Theme.sp(42)
                                radius: Theme.radius
                                color: complete ? Theme.colorAccentMid : (ev ? Theme.colorBg3 : Theme.colorBg2)
                                border.width: 1
                                border.color: complete ? Theme.colorAccent : (ev ? Theme.colorWarn : Theme.colorBorder)

                                Text {
                                    id: keyHint
                                    anchors { left: parent.left; leftMargin: Theme.sp(8); top: parent.top; topMargin: Theme.sp(6) }
                                    text: modelData.key
                                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
                                }
                                Text {
                                    anchors { left: keyHint.right; leftMargin: Theme.sp(8); top: parent.top; topMargin: Theme.sp(4) }
                                    text: modelData.label
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; color: Theme.colorText
                                }
                                Text {
                                    anchors { left: parent.left; leftMargin: Theme.sp(8); right: timeLbl.left; rightMargin: Theme.sp(6)
                                              bottom: parent.bottom; bottomMargin: Theme.sp(5) }
                                    text: modelData.desc; elide: Text.ElideRight
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
                                }
                                Text {
                                    id: timeLbl
                                    anchors { right: dot.left; rightMargin: Theme.sp(8); top: parent.top; topMargin: Theme.sp(4) }
                                    text: ev ? (ev.sec.toFixed(2) + "s") : "—"
                                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
                                    color: ev ? Theme.colorAccentLight : Theme.colorText3
                                    MouseArea {
                                        anchors.fill: parent; anchors.margins: -Theme.sp(4); enabled: !!ev
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (ev) markupController.setFrameIndex(ev.frame)
                                    }
                                }
                                Text {
                                    id: dot
                                    anchors { right: clr.left; rightMargin: Theme.sp(8); verticalCenter: parent.verticalCenter }
                                    text: complete ? "●" : (ev ? "○" : "·")
                                    font.pixelSize: Theme.fontSzBody
                                    color: complete ? Theme.colorGood : (ev ? Theme.colorWarn : Theme.colorText3)
                                }
                                Text {
                                    id: clr
                                    anchors { right: parent.right; rightMargin: Theme.sp(8); verticalCenter: parent.verticalCenter }
                                    text: ev ? "✕" : ""
                                    font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzBody; color: Theme.colorText3
                                    MouseArea {
                                        anchors.fill: parent; anchors.margins: -Theme.sp(5); enabled: !!ev
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: markupController.clearEvent(modelData.name)
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent; z: -1
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { markupController.setEvent(modelData.name); root.forceActiveFocus() }
                                }
                            }
                        }
                    }

                    // THIS SWING
                    Column {
                        width: parent.width; spacing: Theme.sp(4)
                        MlSection { text: qsTr("THIS SWING") }
                        Text {
                            text: qsTr("%1 / 10 P-positions · %2 shaft frames").arg(root.pComplete()).arg(markupController.shaftCount)
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: Theme.colorText2
                        }
                        Text {
                            visible: markupController.dirty
                            text: qsTr("● unsaved changes")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorWarn
                        }
                    }
                    }
                }
            }
        }

        // ── Transport / status ──────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(44)
            color: Theme.colorBg2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.sp(14); anchors.rightMargin: Theme.sp(14)
                spacing: Theme.sp(10)

                MlButton { text: "⏮"; enabled: markupController.hasSwing; onClicked: markupController.setFrameIndex(0) }
                MlButton { text: "◀ a"; enabled: markupController.hasSwing; onClicked: markupController.stepFrame(-1) }
                Slider {
                    Layout.fillWidth: true
                    enabled: markupController.hasSwing && markupController.frameCount > 1
                    from: 0; to: 1
                    value: markupController.frameCount > 1
                           ? markupController.frameIndex / (markupController.frameCount - 1) : 0
                    onMoved: markupController.seekFraction(value)
                }
                MlButton { text: "d ▶"; enabled: markupController.hasSwing; onClicked: markupController.stepFrame(1) }
                MlButton { text: "⏭"; enabled: markupController.hasSwing; onClicked: markupController.setFrameIndex(markupController.frameCount - 1) }

                Text {
                    text: qsTr("a/d step · space stride · 1–9/0 = P1–P10 · u undo · s skeleton · q save")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                    Layout.maximumWidth: Theme.sp(320); elide: Text.ElideRight
                }
            }
        }
    }

    // Toast for controller messages (save / decode feedback).
    Rectangle {
        id: toast
        function show(t) { toastText.text = t; opacity = 1; toastTimer.restart() }
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: Theme.sp(60) }
        width: toastText.width + Theme.sp(28); height: toastText.height + Theme.sp(16)
        radius: Theme.radiusLg
        color: Theme.colorSurface
        border.width: 1; border.color: Theme.colorBorder
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        Text {
            id: toastText
            anchors.centerIn: parent
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorText
        }
        Timer { id: toastTimer; interval: 2200; onTriggered: toast.opacity = 0 }
    }

    // ── Local components ───────────────────────────────────────────────────────
    component MlSection: Text {
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
    }

    component MlButton: Rectangle {
        id: btn
        property string text: ""
        property bool   accent: false
        signal clicked()
        implicitWidth: lbl.width + Theme.sp(18)
        implicitHeight: Theme.sp(28)
        radius: Theme.radius
        opacity: enabled ? 1.0 : 0.4
        color: accent ? Theme.colorAccent
             : bMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
        border.width: 1
        border.color: accent ? Theme.colorAccent : Theme.colorBorder
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Text {
            id: lbl
            anchors.centerIn: parent
            text: btn.text
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: btn.accent ? "#ffffff" : Theme.colorText
        }
        MouseArea {
            id: bMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: btn.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: { btn.clicked(); root.forceActiveFocus() }
        }
    }
}
