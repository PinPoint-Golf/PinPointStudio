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
// TWO CAMERAS, SIDE BY SIDE. When the swing has a down-the-line stream the panel
// shows both frames at once, at the same displayed height and each at its own
// aspect, scrubbing together off ONE playhead. Clicks mark the pane they land in —
// face-on → truth.json, DTL → truth_dtl.json, each in its own pixel space — and the
// pane last clicked wears an accent border, because that is where a keyboard mark,
// an undo or a ball placement will go. The P-positions and the capture conditions
// are instants/properties of the SWING, so they hang off the shared playhead and
// stay in truth.json exactly as before.
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

    // Ball tool: when armed, the next frame click places the stationary ball
    // centre (one click — the ball doesn't move) and disarms. Toggle with 'b'.
    property bool ballMode: false

    // The pane a half-placed club belongs to (0 = face-on, 1 = DTL). Clicking the
    // other pane starts a fresh grip there rather than closing a shaft across two
    // different cameras.
    property int pendingPane: 0

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

    // ── RHS tabs (0 = Positions, 1 = Metadata) ──────────────────────────────────
    property int rhsTab: 0

    // Capture-conditions choices. Display labels are decoupled from the canonical
    // stored values (lowercase, what SwingLab reads from truth.json "meta") so the
    // mapping survives translation. Club is stored verbatim (its own label list).
    readonly property var lightingOpts: [qsTr("Bright"), qsTr("Normal"), qsTr("Dark")]
    readonly property var lightingVals: ["bright", "normal", "dark"]
    readonly property var shaftOpts:    [qsTr("Graphite"), qsTr("Steel")]
    readonly property var shaftVals:    ["graphite", "steel"]
    readonly property var scopeOpts:    [qsTr("Full"), qsTr("Pitch"), qsTr("Chip"), qsTr("Putt")]
    readonly property var scopeVals:    ["full", "pitch", "chip", "putt"]
    readonly property var tempoOpts:    [qsTr("Slow"), qsTr("Normal"), qsTr("Fast")]
    readonly property var tempoVals:    ["slow", "normal", "fast"]
    readonly property var contactOpts:  [qsTr("Ball"), qsTr("Air"), qsTr("Mishit")]
    readonly property var contactVals:  ["ball", "air", "mishit"]
    // The one canonical club vocabulary — shared with the shot-carousel swing-edit
    // popover (ShotListModel.clubOptions / club_vocabulary.h) so both pickers agree.
    readonly property var clubList: markupController.clubOptions

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

    // Mark this panel present on the shared controller while it is on-screen — the
    // panel only exists when the View shows Markup (PpModeStage loads/unloads it),
    // and panelActive narrows that to the visible session screen. The Transit
    // timeline gates its markup diamonds on this, so toggling Markup out of the View
    // (or switching tab/screen) destroys the panel → releases → diamonds vanish.
    property bool _retained: false
    function _syncRetain() {
        if (root.panelActive === root._retained) return
        root._retained = root.panelActive
        if (root._retained) markupController.retainPanel()
        else                markupController.releasePanel()
    }

    onTargetSwingDirChanged: _syncSwing()
    onPanelActiveChanged: { _syncSwing(); _syncRetain() }
    Component.onCompleted: { _syncSwing(); _syncRetain() }
    Component.onDestruction: if (root._retained) markupController.releasePanel()

    focus: true
    Keys.onPressed: function (e) {
        switch (e.key) {
        case Qt.Key_A: case Qt.Key_Left:   markupController.stepFrame(-1); e.accepted = true; break
        case Qt.Key_D: case Qt.Key_Right:  markupController.stepFrame(1);  e.accepted = true; break
        case Qt.Key_Space:                 markupController.stepFrame(markupController.stride); e.accepted = true; break
        case Qt.Key_BracketLeft:           markupController.stepFrame(-markupController.stride); e.accepted = true; break
        case Qt.Key_BracketRight:          markupController.stepFrame(markupController.stride);  e.accepted = true; break
        // Undo / clear / ball act on the ACTIVE pane — the one last clicked, which
        // wears the accent border, so a keystroke never edits the camera the
        // operator is not looking at.
        case Qt.Key_U:
            if (root.pendingGrip) { root.pendingGrip = false; root.repaintPanes() }
            else markupController.clearShaftIn(markupController.activePane)
            e.accepted = true; break
        case Qt.Key_C:   markupController.clearShaftIn(markupController.activePane); e.accepted = true; break
        case Qt.Key_B:
            root.ballMode = !root.ballMode
            if (root.ballMode) root.pendingGrip = false
            root.repaintPanes()
            e.accepted = true; break
        case Qt.Key_S:   markupController.showSkeleton = !markupController.showSkeleton; e.accepted = true; break
        case Qt.Key_Q:   markupController.save();       e.accepted = true; break
        case Qt.Key_0: case Qt.Key_1: case Qt.Key_2: case Qt.Key_3: case Qt.Key_4:
        case Qt.Key_5: case Qt.Key_6: case Qt.Key_7: case Qt.Key_8: case Qt.Key_9:
            root.markEventByKey(String.fromCharCode(e.key)); e.accepted = true; break
        }
    }

    // Both panes repaint together: they share a playhead, so a move in one is a
    // move in the other, and a Canvas only redraws when it is asked to.
    // Guarded: a controller signal can arrive while the panel is still being
    // built (loadSwing runs off the host's binding), and the panes do not exist
    // yet at that point.
    function repaintPanes() {
        if (facePane) facePane.repaint()
        if (dtlPane && dtlPane.visible) dtlPane.repaint()
    }

    // One click handler for both panes — the pane index decides which stream's
    // setter is called, and therefore which sidecar the mark ends up in. The
    // normalised coordinates arrive already mapped against THAT pane's painted
    // image rect, so the DTL frame is never scaled by face-on's dimensions.
    function paneClick(pane, nx, ny) {
        markupController.activePane = pane
        if (root.ballMode) {
            if (pane === 1) markupController.setDtlBall(nx, ny)
            else            markupController.setBall(nx, ny)
            root.ballMode = false
            root.repaintPanes()
            root.forceActiveFocus()
            return
        }
        if (!root.pendingGrip || root.pendingPane !== pane) {
            root.gripNx = nx; root.gripNy = ny
            root.pendingGrip = true; root.pendingPane = pane
            root.repaintPanes()
        } else {
            if (pane === 1) markupController.setDtlShaft(root.gripNx, root.gripNy, nx, ny)
            else            markupController.setShaft(root.gripNx, root.gripNy, nx, ny)
            root.pendingGrip = false
        }
        root.forceActiveFocus()
    }

    Connections {
        target: markupController
        function onFrameChanged() { root.pendingGrip = false; root.repaintPanes() }
        function onDtlFrameChanged() { root.repaintPanes() }
        function onCurrentChanged() { root.pendingGrip = false; root.ballMode = false }
        function onLabelsChanged() { root.repaintPanes() }
        function onPoseChanged() { root.repaintPanes() }
        function onActivePaneChanged() { root.repaintPanes() }
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

            // Frame view (centre) — one pane, or two when the swing has a
            // down-the-line camera. Both panes share the DISPLAYED HEIGHT and keep
            // their own aspect, so the portrait DTL frame is simply the narrower
            // one; when the pair will not fit the width they scale down together
            // rather than scrolling, cropping or opening anything.
            Rectangle {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#000000"

                readonly property real gap:    markupController.hasDtl ? Theme.sp(8) : 0
                readonly property real aFace:  Math.max(0.05, markupController.videoAspect)
                readonly property real aDtl:   markupController.hasDtl ? Math.max(0.05, markupController.dtlAspect) : 0
                readonly property real availW: Math.max(0, width  - Theme.sp(16) - gap)
                readonly property real availH: Math.max(0, height - Theme.sp(16))
                readonly property real paneH:  Math.max(0, Math.min(availH, availW / (aFace + aDtl)))

                Row {
                    anchors.centerIn: parent
                    spacing: stage.gap

                    MlPane {
                        id: facePane
                        paneIndex: 0
                        title: qsTr("FACE-ON")
                        visible: markupController.hasSwing
                        width:  stage.paneH * stage.aFace
                        height: stage.paneH
                        token:      markupController.frameToken
                        frameIdx:   markupController.frameIndex
                        frameTotal: markupController.frameCount
                        frameSec:   markupController.frameSec
                        shaft:      markupController.currentShaft
                        ball:       markupController.ballPoint
                        showPose:   true
                        edges:       root.cocoEdges
                        minConf:     root.kpMinConf
                        pendingGrip: root.pendingGrip
                        pendingPane: root.pendingPane
                        gripNx:      root.gripNx
                        gripNy:      root.gripNy
                        ballMode:    root.ballMode
                        onPaneClicked: function (p, nx, ny) { root.paneClick(p, nx, ny) }
                    }

                    MlPane {
                        id: dtlPane
                        paneIndex: 1
                        title: qsTr("DTL")
                        visible: markupController.hasSwing && markupController.hasDtl
                        width:  stage.paneH * stage.aDtl
                        height: stage.paneH
                        token:      markupController.dtlFrameToken
                        frameIdx:   markupController.dtlFrameIndex
                        frameTotal: markupController.dtlFrameCount
                        frameSec:   markupController.dtlFrameSec
                        shaft:      markupController.dtlShaft
                        ball:       markupController.dtlBallPoint
                        showPose:   false
                        edges:       root.cocoEdges
                        minConf:     root.kpMinConf
                        pendingGrip: root.pendingGrip
                        pendingPane: root.pendingPane
                        gripNx:      root.gripNx
                        gripNy:      root.gripNy
                        ballMode:    root.ballMode
                        onPaneClicked: function (p, nx, ny) { root.paneClick(p, nx, ny) }
                    }
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
            }


            // Controls (right)
            Rectangle {
                Layout.preferredWidth: Theme.sp(496)
                Layout.fillHeight: true
                color: Theme.colorBg

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp(14)
                    spacing: Theme.sp(6)

                    // Tab header — underline tabs, same aesthetic as PpModeStage's
                    // "tabs" arrangement: a contiguous strip, subtle colorBg2 fill on
                    // select/hover, 2px accent underline on the selected tab.
                    Row {
                        spacing: Theme.sp(5)
                        Repeater {
                            model: [qsTr("Positions"), qsTr("Metadata")]
                            delegate: Rectangle {
                                id: tabDel
                                required property string modelData
                                required property int index
                                readonly property bool sel: root.rhsTab === index
                                height: Theme.sp(30); width: tabTxt.implicitWidth + Theme.sp(26)
                                radius: Theme.radius
                                color: sel || tabMa.containsMouse
                                           ? Theme.colorBg2
                                           : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                Rectangle {  // active underline
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height: 2; color: tabDel.sel ? Theme.colorAccent : "transparent"
                                }
                                Text {
                                    id: tabTxt; anchors.centerIn: parent; text: tabDel.modelData
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                    color: tabDel.sel ? Theme.colorText : Theme.colorText3
                                }
                                MouseArea {
                                    id: tabMa
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.rhsTab = tabDel.index; root.forceActiveFocus() }
                                }
                            }
                        }
                    }

                    // Index 0 = Positions, 1 = Metadata. Both stay instantiated.
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: root.rhsTab

                    // ── Positions ───────────────────────────────────────────────
                    ScrollView {
                    id: ctrlScroll
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    // Bind to width, not availableWidth: the latter subtracts the
                    // vertical scrollbar, coupling width↔contentHeight through text
                    // wrapping → contentWidth binding loop. (Matches the app's other
                    // ScrollViews, e.g. ScreenAthleteForm.)
                    contentWidth: width
                    Column {
                    // Reserve a constant right gutter (independent of scrollbar
                    // state, so no binding loop) so an overlay vertical scrollbar
                    // never paints over the right-anchored row controls.
                    width: ctrlScroll.width - Theme.sp(12)
                    spacing: Theme.sp(14)

                    // CLUB / SHAFT
                    Column {
                        width: parent.width; spacing: Theme.sp(8)
                        MlSection { text: qsTr("CLUB / SHAFT") }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: markupController.hasDtl
                                  ? qsTr("Click grip, then clubhead to place the club on this frame — in either pane; each goes to its own file. Then press a P key (1–9, 0=P10) to tag the position.")
                                  : qsTr("Click grip, then clubhead to place the club on this frame, then press a P key (1–9, 0=P10) to tag the position.")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                        }
                        Row {
                            spacing: Theme.sp(8)
                            MlButton { text: qsTr("Undo (u)"); onClicked: {
                                if (root.pendingGrip) { root.pendingGrip = false; root.repaintPanes() }
                                else markupController.clearShaftIn(markupController.activePane) } }
                            MlButton { text: qsTr("Clear (c)"); onClicked: markupController.clearShaftIn(markupController.activePane) }
                        }
                        // Which pane the buttons above (and the keys) act on.
                        Text {
                            visible: markupController.hasDtl
                            text: markupController.activePane === 1
                                  ? qsTr("◆ acting on the DTL pane") : qsTr("◆ acting on the Face-On pane")
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel; color: Theme.colorAccentLight
                        }
                    }

                    // BALL (stationary — mark once)
                    Column {
                        width: parent.width; spacing: Theme.sp(8)
                        MlSection { text: qsTr("BALL") }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: markupController.hasDtl
                                  ? qsTr("The ball is stationary — mark it once per camera (it sits somewhere different in each frame). Press ‘Place ball’, then click the ball centre in either pane; it applies to every frame of that stream.")
                                  : qsTr("The ball is stationary — mark it once. Press ‘Place ball’, then click the ball centre; it applies to every frame of the swing.")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                        }
                        Row {
                            spacing: Theme.sp(8)
                            MlButton {
                                readonly property var _pb: markupController.activePane === 1
                                                           ? markupController.dtlBallPoint : markupController.ballPoint
                                text: root.ballMode ? qsTr("Click the ball…")
                                      : ((_pb && _pb.has) ? qsTr("Move ball (b)") : qsTr("Place ball (b)"))
                                accent: root.ballMode
                                onClicked: { root.ballMode = true; root.pendingGrip = false; root.repaintPanes(); root.forceActiveFocus() }
                            }
                            MlButton {
                                readonly property var _pb: markupController.activePane === 1
                                                           ? markupController.dtlBallPoint : markupController.ballPoint
                                text: qsTr("Clear ball")
                                enabled: _pb && _pb.has
                                onClicked: { markupController.clearBallIn(markupController.activePane); root.forceActiveFocus() }
                            }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            readonly property var _b: markupController.ballPoint
                            visible: _b && _b.has
                            // Guard the whole expression: `visible: false` does NOT
                            // stop the text binding evaluating, so nx/ny must not be
                            // dereferenced when no ball is set (they're undefined).
                            text: (_b && _b.has) ? qsTr("◆ face-on ball at %1, %2").arg(_b.nx.toFixed(3)).arg(_b.ny.toFixed(3)) : ""
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel; color: Theme.colorGood
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            readonly property var _d: markupController.dtlBallPoint
                            visible: _d && _d.has
                            text: (_d && _d.has) ? qsTr("◆ DTL ball at %1, %2").arg(_d.nx.toFixed(3)).arg(_d.ny.toFixed(3)) : ""
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel; color: Theme.colorGood
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

                    // P-POSITIONS — instants of the SWING, off the shared playhead.
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
                                    anchors { left: parent.left; leftMargin: Theme.sp(8); right: jumpChip.left; rightMargin: Theme.sp(6)
                                              bottom: parent.bottom; bottomMargin: Theme.sp(5) }
                                    text: modelData.desc; elide: Text.ElideRight
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
                                }
                                // Timestamp + arrow = the jump affordance. Outlined when
                                // set and highlights on hover, so it reads as "click to
                                // jump to this frame" — distinct from clicking the row
                                // body to set/replace the position.
                                Rectangle {
                                    id: jumpChip
                                    anchors { right: dot.left; rightMargin: Theme.sp(8); verticalCenter: parent.verticalCenter }
                                    height: Theme.sp(24)
                                    width: jumpRow.implicitWidth + Theme.sp(14)
                                    radius: Theme.radius
                                    color: jumpMa.containsMouse ? Theme.colorAccentMid : "transparent"
                                    border.width: ev ? 1 : 0
                                    border.color: jumpMa.containsMouse ? Theme.colorAccent : Theme.colorBorderStrong
                                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                    Row {
                                        id: jumpRow
                                        anchors.centerIn: parent
                                        spacing: Theme.sp(5)
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: ev ? (ev.sec.toFixed(2) + "s") : "—"
                                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
                                            color: !ev ? Theme.colorText3
                                                 : jumpMa.containsMouse ? Theme.colorAccent : Theme.colorText
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: !!ev
                                            text: "→"
                                            font.pixelSize: Theme.fontSzBody; font.bold: true
                                            color: Theme.colorAccent
                                        }
                                    }
                                    MouseArea {
                                        id: jumpMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: !!ev
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: { markupController.setFrameIndex(ev.frame); root.forceActiveFocus() }
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
                        // The DTL pane keeps its own count in its own file, so the
                        // two are never added together or confused for one another.
                        Text {
                            visible: markupController.hasDtl
                            text: qsTr("%1 DTL shaft frames → truth_dtl.json").arg(markupController.dtlShaftCount)
                            elide: Text.ElideRight; width: parent.width
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: Theme.colorText2
                        }
                        Text {
                            visible: markupController.dirty
                            text: qsTr("● unsaved changes")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorWarn
                        }
                    }
                    }
                    }   // ── end Positions ScrollView ───────────────────────────

                    // ── Metadata: capture conditions recorded into truth.json ────
                    ScrollView {
                        id: metaScroll
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        contentWidth: width
                        Column {
                            width: metaScroll.width - Theme.sp(12)
                            spacing: Theme.sp(18)

                            Text {
                                width: parent.width; wrapMode: Text.WordWrap
                                text: qsTr("Capture conditions saved into truth.json for SwingLab. Set what you can verify — leave anything unknown unset.")
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                            }

                            // SWING SCOPE — gates SwingLab's full-swing-only checks.
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("SWING SCOPE") }
                                PpSegmentedControl {
                                    width: parent.width
                                    options: root.scopeOpts
                                    selected: {
                                        var i = root.scopeVals.indexOf(markupController.metaScope)
                                        return i >= 0 ? root.scopeOpts[i] : ""
                                    }
                                    onActivated: function (v) {
                                        var i = root.scopeOpts.indexOf(v)
                                        if (i >= 0) markupController.metaScope = root.scopeVals[i]
                                        root.forceActiveFocus()
                                    }
                                }
                                Text {
                                    width: parent.width; wrapMode: Text.WordWrap
                                    text: qsTr("Partial swings (pitch / chip / putt) skip the full-swing sweep & tempo checks.")
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                                }
                            }

                            // TEMPO
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("TEMPO") }
                                PpSegmentedControl {
                                    width: parent.width
                                    options: root.tempoOpts
                                    selected: {
                                        var i = root.tempoVals.indexOf(markupController.metaTempo)
                                        return i >= 0 ? root.tempoOpts[i] : ""
                                    }
                                    onActivated: function (v) {
                                        var i = root.tempoOpts.indexOf(v)
                                        if (i >= 0) markupController.metaTempo = root.tempoVals[i]
                                        root.forceActiveFocus()
                                    }
                                }
                            }

                            // BALL CONTACT
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("BALL CONTACT") }
                                PpSegmentedControl {
                                    width: parent.width
                                    options: root.contactOpts
                                    selected: {
                                        var i = root.contactVals.indexOf(markupController.metaContact)
                                        return i >= 0 ? root.contactOpts[i] : ""
                                    }
                                    onActivated: function (v) {
                                        var i = root.contactOpts.indexOf(v)
                                        if (i >= 0) markupController.metaContact = root.contactVals[i]
                                        root.forceActiveFocus()
                                    }
                                }
                            }

                            // LIGHTING
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("LIGHTING") }
                                PpSegmentedControl {
                                    width: parent.width
                                    options: root.lightingOpts
                                    selected: {
                                        var i = root.lightingVals.indexOf(markupController.metaLighting)
                                        return i >= 0 ? root.lightingOpts[i] : ""
                                    }
                                    onActivated: function (v) {
                                        var i = root.lightingOpts.indexOf(v)
                                        if (i >= 0) markupController.metaLighting = root.lightingVals[i]
                                        root.forceActiveFocus()
                                    }
                                }
                            }

                            // SHAFT
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("SHAFT") }
                                PpSegmentedControl {
                                    width: parent.width
                                    options: root.shaftOpts
                                    selected: {
                                        var i = root.shaftVals.indexOf(markupController.metaShaft)
                                        return i >= 0 ? root.shaftOpts[i] : ""
                                    }
                                    onActivated: function (v) {
                                        var i = root.shaftOpts.indexOf(v)
                                        if (i >= 0) markupController.metaShaft = root.shaftVals[i]
                                        root.forceActiveFocus()
                                    }
                                }
                            }

                            // CLUB
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("CLUB") }
                                Text {
                                    width: parent.width; wrapMode: Text.WordWrap
                                    text: qsTr("Defaulted from the session; change it here if the club for this swing differs.")
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                                }
                                PpComboBox {
                                    id: clubCombo
                                    width: parent.width
                                    enabled: markupController.hasSwing
                                    model: root.clubList
                                    displayFn: ClubFormat.display
                                    onActivated: function (i) {
                                        markupController.metaClub = root.clubList[i]
                                        root.forceActiveFocus()
                                    }
                                    // Sync imperatively (not a currentIndex binding) so
                                    // picking an item — which sets currentIndex internally
                                    // — doesn't break the link to the controller on the next
                                    // swing load.
                                    function syncFromController() {
                                        var i = root.clubList.indexOf(markupController.metaClub)
                                        currentIndex = i >= 0 ? i : 0
                                    }
                                    Component.onCompleted: syncFromController()
                                    Connections {
                                        target: markupController
                                        function onMetaChanged() { clubCombo.syncFromController() }
                                    }
                                }
                            }

                            // CLUB LEAVES FRAME — explains a legitimately low coverage.
                            Column {
                                width: parent.width; spacing: Theme.sp(8)
                                MlSection { text: qsTr("TRACKING") }
                                MlButton {
                                    text: markupController.metaClubLeavesFrame
                                          ? qsTr("Club leaves frame: yes")
                                          : qsTr("Club leaves frame: no")
                                    accent: markupController.metaClubLeavesFrame
                                    enabled: markupController.hasSwing
                                    onClicked: markupController.metaClubLeavesFrame = !markupController.metaClubLeavesFrame
                                }
                                Text {
                                    width: parent.width; wrapMode: Text.WordWrap
                                    text: qsTr("Flag when the clubhead exits the frame mid-swing, so low shaft coverage isn't read as a tracker failure.")
                                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                                }
                            }
                        }
                    }
                    }   // ── end StackLayout ───────────────────────────────────────
                }       // ── end ColumnLayout ──────────────────────────────────────
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

                // Which sidecars this swing's marks land in — unobtrusive, but always
                // on screen, because the two panes write different files.
                Text {
                    visible: markupController.hasSwing
                    text: markupController.hasDtl ? qsTr("→ truth.json + truth_dtl.json")
                                                  : qsTr("→ truth.json")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel; color: Theme.colorText3
                }
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

    // One camera's frame: the exact still, that stream's OWN labels over it, and a
    // click mapped through the image's painted rect. The mapping is the point —
    // the two cameras are different shapes (face-on landscape, DTL portrait), so a
    // click normalised against the wrong pane would be a wrong pixel in the wrong
    // file. The pane last clicked wears the accent border and is where a keyboard
    // undo / clear / ball lands.
    component MlPane: Rectangle {
        id: pane

        property int    paneIndex: 0        // 0 = face-on (truth.json), 1 = DTL
        property string title: ""
        property int    token: 0            // bumped per decoded still; busts the cache
        property int    frameIdx: 0
        property int    frameTotal: 0
        property real   frameSec: 0
        property var    shaft: null         // this stream's label for this frame
        property var    ball: null          // this stream's stationary ball centre
        property bool   showPose: false     // the recorded pose is face-on only
        // Everything the pane draws is handed to it, so an inline component never
        // has to reach back into the enclosing document's ids to paint a frame.
        property var    edges: []           // COCO skeleton edge list
        property real   minConf: 0.30
        property bool   pendingGrip: false  // a grip is placed, waiting on the head
        property int    pendingPane: 0      // ...in THIS pane, or the other one
        property real   gripNx: 0
        property real   gripNy: 0
        property bool   ballMode: false     // the next click places the ball
        readonly property bool isActive: markupController.activePane === pane.paneIndex

        signal paneClicked(int pane, real nx, real ny)

        function repaint() { paneCanvas.requestPaint() }

        color: "#000000"
        border.width: 1
        border.color: pane.isActive ? Theme.colorAccent : Theme.colorBorder
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        Image {
            id: paneImg
            anchors.fill: parent
            anchors.margins: Theme.sp(4)
            fillMode: Image.PreserveAspectFit
            cache: false
            asynchronous: false
            smooth: true
            // The token is 0 until this pane's first frame decodes (async); only
            // request once a real frame exists, else the provider has no image yet
            // and QML logs "Failed to get image from provider".
            source: (markupController.hasSwing && pane.token > 0)
                    ? "image://markup/" + (pane.paneIndex === 1 ? "dtl/" : "face/") + pane.token
                    : ""

            readonly property real pw: paintedWidth
            readonly property real ph: paintedHeight
            readonly property real px0: (width - paintedWidth) / 2
            readonly property real py0: (height - paintedHeight) / 2
            function toNx(x) { return pw > 0 ? Math.min(1, Math.max(0, (x - px0) / pw)) : 0 }
            function toNy(y) { return ph > 0 ? Math.min(1, Math.max(0, (y - py0) / ph)) : 0 }
            function sx(nx) { return px0 + nx * pw }
            function sy(ny) { return py0 + ny * ph }
        }

        Canvas {
            id: paneCanvas
            anchors.fill: paneImg
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (!markupController.hasSwing) return

                var pose = pane.showPose ? markupController.currentPose : null
                if (pane.showPose && markupController.showSkeleton && pose && pose.has && pose.kp) {
                    var kp = pose.kp
                    ctx.lineWidth = 2
                    ctx.strokeStyle = Qt.rgba(0.50, 0.72, 0.96, 0.80)
                    for (var e = 0; e < pane.edges.length; ++e) {
                        var a = pane.edges[e][0], b = pane.edges[e][1]
                        if (kp[a*3+2] < pane.minConf || kp[b*3+2] < pane.minConf) continue
                        ctx.beginPath()
                        ctx.moveTo(paneImg.sx(kp[a*3]), paneImg.sy(kp[a*3+1]))
                        ctx.lineTo(paneImg.sx(kp[b*3]), paneImg.sy(kp[b*3+1]))
                        ctx.stroke()
                    }
                    ctx.fillStyle = Qt.rgba(0.50, 0.72, 0.96, 0.95)
                    for (var i = 0; i < 17; ++i) {
                        if (kp[i*3+2] < pane.minConf) continue
                        ctx.beginPath()
                        ctx.arc(paneImg.sx(kp[i*3]), paneImg.sy(kp[i*3+1]), 3, 0, 2 * Math.PI)
                        ctx.fill()
                    }
                    if (pose.lead) {
                        var lx = paneImg.sx(pose.lead[0]), ly = paneImg.sy(pose.lead[1])
                        ctx.strokeStyle = Theme.colorAttention; ctx.lineWidth = 2
                        ctx.beginPath(); ctx.arc(lx, ly, 8, 0, 2 * Math.PI); ctx.stroke()
                    }
                    if (pose.trail) {
                        var tx = paneImg.sx(pose.trail[0]), ty = paneImg.sy(pose.trail[1])
                        ctx.strokeStyle = Qt.rgba(0.78, 0.55, 0.96, 0.85); ctx.lineWidth = 2
                        ctx.beginPath(); ctx.arc(tx, ty, 7, 0, 2 * Math.PI); ctx.stroke()
                    }
                }

                var s = pane.shaft
                if (s && s.has) {
                    var gx = paneImg.sx(s.gripNx), gy = paneImg.sy(s.gripNy)
                    var hx = paneImg.sx(s.headNx), hy = paneImg.sy(s.headNy)
                    ctx.strokeStyle = Theme.colorAccent
                    ctx.lineWidth = 2
                    ctx.beginPath(); ctx.moveTo(gx, gy); ctx.lineTo(hx, hy); ctx.stroke()
                    ctx.fillStyle = Theme.colorGood
                    ctx.beginPath(); ctx.arc(gx, gy, 5, 0, 2 * Math.PI); ctx.fill()
                    ctx.fillStyle = Theme.colorError
                    ctx.beginPath(); ctx.arc(hx, hy, 5, 0, 2 * Math.PI); ctx.fill()
                }
                // The half-placed grip belongs to the pane it was clicked in.
                if (pane.pendingGrip && pane.pendingPane === pane.paneIndex) {
                    var pgx = paneImg.sx(pane.gripNx), pgy = paneImg.sy(pane.gripNy)
                    ctx.fillStyle = Theme.colorGood
                    ctx.beginPath(); ctx.arc(pgx, pgy, 5, 0, 2 * Math.PI); ctx.fill()
                    ctx.strokeStyle = Theme.colorGood; ctx.lineWidth = 1
                    ctx.beginPath(); ctx.arc(pgx, pgy, 9, 0, 2 * Math.PI); ctx.stroke()
                }

                // Stationary ball centre (marked once per camera) — a reticle that
                // reads on any background: dark halo + bright ring + centre dot.
                var b2 = pane.ball
                if (b2 && b2.has) {
                    var bx = paneImg.sx(b2.nx), by = paneImg.sy(b2.ny)
                    ctx.lineWidth = 3; ctx.strokeStyle = Qt.rgba(0, 0, 0, 0.55)
                    ctx.beginPath(); ctx.arc(bx, by, 11, 0, 2 * Math.PI); ctx.stroke()
                    ctx.lineWidth = 2; ctx.strokeStyle = Qt.rgba(0.20, 0.95, 0.80, 0.95)
                    ctx.beginPath(); ctx.arc(bx, by, 11, 0, 2 * Math.PI); ctx.stroke()
                    ctx.fillStyle = Qt.rgba(0.20, 0.95, 0.80, 0.95)
                    ctx.beginPath(); ctx.arc(bx, by, 2.5, 0, 2 * Math.PI); ctx.fill()
                }
            }
        }

        MouseArea {
            anchors.fill: paneImg
            enabled: markupController.hasSwing
            cursorShape: Qt.CrossCursor
            onClicked: function (m) {
                pane.paneClicked(pane.paneIndex, paneImg.toNx(m.x), paneImg.toNy(m.y))
            }
        }

        // HUD: which camera, which of ITS frames, and — on the active pane — what
        // the next click will do. Each pane names its own frame index and time, so
        // the pairing the playhead made is visible rather than assumed.
        Rectangle {
            visible: markupController.hasSwing
            anchors { left: paneImg.left; top: paneImg.top; margins: Theme.sp(6) }
            width: hud.width + Theme.sp(16); height: hud.height + Theme.sp(8)
            radius: Theme.radius
            color: Qt.rgba(0, 0, 0, 0.55)
            Row {
                id: hud
                anchors.centerIn: parent
                spacing: Theme.sp(10)
                Text {
                    text: pane.title
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color: pane.isActive ? Theme.colorAccentLight : Theme.colorText3
                }
                Text {
                    text: qsTr("%1 / %2").arg(pane.frameIdx).arg(Math.max(0, pane.frameTotal - 1))
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: "#ffffff"
                }
                Text {
                    text: pane.frameSec.toFixed(3) + "s"
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm; color: Theme.colorAccentLight
                }
                Text {
                    visible: pane.isActive
                    text: pane.ballMode ? qsTr("◇ click ball")
                          : (pane.shaft && pane.shaft.has) ? qsTr("◆ shaft")
                          : ((pane.pendingGrip && pane.pendingPane === pane.paneIndex)
                             ? qsTr("◇ pick head") : qsTr("◇ pick grip"))
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
                    color: pane.ballMode ? Theme.colorAccentLight
                           : (pane.shaft && pane.shaft.has) ? Theme.colorGood : Theme.colorText3
                }
            }
        }
    }

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
