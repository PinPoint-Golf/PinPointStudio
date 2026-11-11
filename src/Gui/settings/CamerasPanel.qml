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
import QtQuick.Layouts
import QtMultimedia
import PinPointStudio

SettingsPanelBase {
    id: root
    contentSpacing: Theme.gap(16)

    // Index of the camera row whose ROI panel is currently open (-1 = none).
    property int openRoiIndex: -1
    // Which camera row has its impact TUNING section open; -1 = none. Same
    // pattern as openRoiIndex, and for the same reason: a settings write can
    // rebuild the rows, and a per-row flag would close the section under the
    // operator's cursor on every chip click.
    property int openTuningIndex: -1
    // Index of the camera whose ball-detection (hitting area + calibration)
    // panel is expanded; -1 = none. Same pattern as openRoiIndex.
    property int openBallIndex: -1
    // True when opening the ball panel connected the camera itself — closing
    // the panel then restores the disconnected state (settings should not
    // silently change connection state).
    property bool ballAutoConnected: false

    function closeBallPanel() {
        var idx = openBallIndex
        if (idx === -1) return
        openBallIndex = -1
        if (ballAutoConnected) {
            ballAutoConnected = false
            cameraManager.setSelected(idx, false)
        }
    }

    // The width a positioner's shown children need on one line: their implicit
    // widths plus the spacing between them (a Repeater, which has no width,
    // is skipped, as the positioner skips it). A wrapping chip Flow is given
    // min(this, the row's width): one line when it fits, wrapped when not, and
    // never wider than the card. Read from implicit widths only, so it never
    // depends on the width it sets.
    function oneLineWidth(positioner) {
        var w = 0, n = 0
        for (var i = 0; i < positioner.children.length; ++i) {
            var c = positioner.children[i]
            if (c.visible && c.implicitWidth > 0) { w += c.implicitWidth; n++ }
        }
        return n > 0 ? Math.ceil(w + (n - 1) * positioner.spacing) : 0
    }

    // ── Impact camera mode (impact_camera_design.md §10.2) ───────────────────
    //
    // A mode is a crop SIZE plus a rate; the crop's position is placed in the
    // crop editor like any other crop. The exposure is a third, separate chip
    // because it is what makes the mode usable (≤ 70 µs, or the head is a
    // streak), and its default is the design's.
    //
    // ⚠ Plain values only — no camData, no camRow — because writing
    // appSettings.cameraRoi rebuilds every camera row synchronously (see the
    // crop button's note) and the calling delegate's context is gone by the
    // time the write returns. These live on the panel root, which survives,
    // and cameraRoi is written LAST.
    readonly property double impactDefaultExposureUs: 70

    function applyImpactMode(cameraKey, camIndex, maxW, maxH, mode, liveInstance) {
        if (!mode || !(maxW > 0) || !(maxH > 0)) return
        var w = Math.min(1.0, mode.w / maxW)
        var h = Math.min(1.0, mode.h / maxH)
        // The impact strip is the impact ROLE's crop, kept apart from the crop
        // the camera uses in every other role (camera_roi.h): writing it over
        // that one sent an ex-impact camera back to DTL as a 640x240 strip.
        var roiKey = cameraKey + "#impact"
        var roiMap = appSettings.cameraRoi
        var cur = roiMap[roiKey]
        // Keep the operator's placement when the new size still fits there;
        // otherwise centre it.
        var x = (cur && cur.x + w <= 1.0) ? cur.x : (1.0 - w) / 2.0
        var y = (cur && cur.y + h <= 1.0) ? cur.y : (1.0 - h) / 2.0
        var fpsMap = appSettings.cameraTargetFps
        fpsMap[cameraKey] = mode.fps
        appSettings.cameraTargetFps = fpsMap
        cameraManager.setTargetFps(camIndex, mode.fps)
        // Live only when the camera is ALREADY the impact camera (a mode chip):
        // from the VIEW combo the instance still has its old role, and the
        // role change reconnects it with this crop anyway.
        if (liveInstance && liveInstance.perspective === CameraInstance.Impact)
            liveInstance.setCropRoi(Qt.rect(x, y, w, h))
        roiMap[roiKey] = { x: x, y: y, w: w, h: h }
        appSettings.cameraRoi = roiMap
    }

    function setImpactExposure(cameraKey, us, liveInstance) {
        var map = appSettings.cameraExposureUs
        map[cameraKey] = us
        appSettings.cameraExposureUs = map
        // A streaming camera takes it now (impact_camera_design.md §10.3);
        // otherwise the next connect primes it.
        if (liveInstance && liveInstance.isRecording)
            liveInstance.applyLiveTuning(us, -1)
    }

    // ── Impact camera tuning (impact_camera_design.md §10.3) ─────────────────
    // gainDb goes to the camera (live when it streams, else at the next
    // connect); viewGain is a display stretch on the tile and the replay;
    // strobe drives Line1 at the next connect; note is stamped into the clip.
    // Defaults are CameraInstance's (flir_camera_settings.md §5: 18 dB, the
    // Chameleon3's maximum, measured 2026-10-08). No gamma: a Chameleon3 has
    // none in raw Bayer, and the control was dropped.
    readonly property double impactDefaultGainDb:   18
    readonly property double impactDefaultViewGain: 1

    function impactTuning(cameraKey, member, fallback) {
        var t = appSettings.cameraTuning[cameraKey]
        return (t && t[member] !== undefined) ? t[member] : fallback
    }

    function setImpactTuning(cameraKey, member, value, liveInstance) {
        var map = appSettings.cameraTuning
        var t = map[cameraKey] ? Object.assign({}, map[cameraKey]) : {}
        t[member] = value
        map[cameraKey] = t
        appSettings.cameraTuning = map
        if (liveInstance && liveInstance.isRecording) {
            if (member === "gainDb") liveInstance.applyLiveTuning(0, value)
        }
    }

    // ── Exposure and gain on every other role (flir_image_clarity.md §6.4) ──
    // Kept under "<key>#wide" so the impact camera's ~100 µs never reaches a
    // down-the-line camera (camera_roi.h tuningKey). Absent = camera auto. A
    // lock reaches a streaming camera at once; going back to auto, or any
    // change while the camera is connected but idle, reconnects it — only the
    // connect writer returns the nodes to auto.
    function wideTuningKey(cameraKey) { return cameraKey + "#wide" }

    function wideExposureUs(cameraKey) {
        var v = appSettings.cameraExposureUs[wideTuningKey(cameraKey)]
        return (v !== undefined && v > 0) ? v : 0
    }

    function wideGainDb(cameraKey) {
        var t = appSettings.cameraTuning[wideTuningKey(cameraKey)]
        return (t && t.gainDb !== undefined && t.gainDb >= 0) ? t.gainDb : -1
    }

    function applyWideTuning(cameraKey, exposureUs, gainDb, liveInstance) {
        if (!liveInstance)
            return
        var locking = exposureUs > 0 || gainDb >= 0
        if (liveInstance.isRecording && locking)
            liveInstance.applyLiveTuning(exposureUs, gainDb)
        else
            cameraManager.reconnectCamera(cameraKey)
    }

    function setWideExposure(cameraKey, us, liveInstance) {
        var k = wideTuningKey(cameraKey)
        var map = appSettings.cameraExposureUs
        if (us > 0) map[k] = us
        else        delete map[k]
        appSettings.cameraExposureUs = map
        applyWideTuning(cameraKey, us, -1, liveInstance)
    }

    function setWideGain(cameraKey, db, liveInstance) {
        var k = wideTuningKey(cameraKey)
        var map = appSettings.cameraTuning
        var t = map[k] ? Object.assign({}, map[k]) : {}
        if (db >= 0) t.gainDb = db
        else         delete t.gainDb
        if (Object.keys(t).length > 0) map[k] = t
        else                           delete map[k]
        appSettings.cameraTuning = map
        applyWideTuning(cameraKey, 0, db, liveInstance)
    }

    // A crop origin/size number: the compact field in the data face. It keeps the bare TextInput's
    // behaviour it was built on: no edit menu, not in the Tab chain, Qt's own editingFinished only,
    // the left button only, and only the text's band (not the box's padding) takes a press.
    component CropField: PpTextField {
        size:         "compact"
        monospace:    true
        animateFocus: true
        implicitWidth: Theme.sp(70)
        editMenu:         false
        activeFocusOnTab: false
        commitOnBlur:     false
        padHit:           false
        // The left button only: a TextField takes every button (acceptedMouseButtons is not settable
        // from QML), so the others are held here before the field can focus or move its cursor.
        TapHandler { acceptedButtons: Qt.RightButton | Qt.MiddleButton; gesturePolicy: TapHandler.WithinBounds }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Inline component — camera device row
    // ─────────────────────────────────────────────────────────────────────────
    component CameraDeviceRow: PpDeviceCard {
        id: camRow

        // One entry from cameraManager.cameraList.
        // ⚠ ALWAYS `camRow.camData`, never a bare `camData`, inside the row.  A
        // bare name is looked up through the row's context, and while the row
        // is being destroyed that lookup fails (undefined, or a ReferenceError)
        // even though `camRow.…` still resolves.  Any binding that re-runs in
        // that window — e.g. one on `camRow.instance` as the preview instance
        // stops — then logs "Cannot read property 'maxWidth' of undefined".
        property var camData: ({})

        // The selected CameraInstance for this camera (null when not selected).
        readonly property var realInstance: {
            var instances = cameraManager.instances
            for (var i = 0; i < instances.length; ++i) {
                if (instances[i].cameraKey === camRow.camData.cameraKey)
                    return instances[i]
            }
            return null
        }

        // The preview-only instance behind BOTH the row tile and the crop
        // editor.  ⚠ One instance, not two: they name the same PPCP Source, and
        // two would collide on the deterministic capture Stream id and stop
        // each other (VideoInputPpcp::reclaimStream).  syncPreview() moves the
        // sink between the two VideoOutputs instead.
        property var localPreviewInstance: null

        // Is the row tile actually showing something?
        readonly property bool rowPreviewLive:
            localPreviewInstance !== null
            && localPreviewInstance.lastPreviewError === ""
            && localPreviewInstance.cameraFps > 0

        // Effective instance. While the crop editor is open the camera is
        // guaranteed disconnected (the open flow stops capture and deselects
        // it), so the editor always binds to the full-sensor preview-only
        // instance — a camera reconnected elsewhere can't hijack the editor.
        readonly property var instance: roiOpen ? localPreviewInstance
                                                : (realInstance !== null ? realInstance
                                                                         : localPreviewInstance)

        readonly property bool roiOpen: root.openRoiIndex === camRow.camData.index
        readonly property bool tuningOpen: root.openTuningIndex === camRow.camData.index
        readonly property bool ballOpen: root.openBallIndex === camRow.camData.index

        // The club/ball impact camera: its row swaps the frame-rate chips for
        // the modes the camera itself reported at enumeration (crop × rate)
        // and adds a locked-exposure chip.
        readonly property bool isImpact:    camRow.camData.perspective === CameraInstance.Impact
        readonly property var  impactModes: camRow.camData.impactModes || []

        // Effective fps for storage calculations:
        // priority: user-set target → live measured configuredFps → capability maxFps → 30
        readonly property double currentFps: {
            var stored = appSettings.cameraTargetFps[camRow.camData.cameraKey]
            if (stored !== undefined && stored > 0) return stored
            if (camRow.instance && camRow.instance.configuredFps > 0)
                return camRow.instance.configuredFps
            return camRow.camData.maxFps > 0 ? camRow.camData.maxFps : 30
        }

        // From where the header ENDS (its top margin included): summing heights
        // alone left the card one margin short, which clipped the last line of
        // whatever section came last.
        implicitHeight: camRow.roiOpen
                            ? camRow.headerRow.y + camRow.headerRow.height + bodyRow.height + wideTuneRow.height + impactRow.height + roiPanel.height
                            : camRow.headerRow.y + camRow.headerRow.height + (camRow.camData.enabled ? bodyRow.height : excludedNote.height)
                              + wideTuneRow.height + impactRow.height
                              + (camRow.ballOpen ? ballPanel.height : 0)

        Behavior on implicitHeight { NumberAnimation { duration: Theme.durationFast } }

        dimmed: !camRow.camData.enabled
        borderColor: (camRow.roiOpen || camRow.ballOpen)
                        ? Theme.colorAccent
                        : (camRow.camData.enabled ? Theme.colorBorderStrong : Theme.colorBorderMid)
        statusColor: camRow.realInstance && camRow.realInstance.isRecording
                        ? Theme.colorGood
                        : (camRow.camData.enabled ? Theme.colorWarn : Theme.colorText3)

        // ── Header row (after the card's status dot) ─────────────────────────
        headerItems: [
            // ── The live tile, for a phone, from the moment it connects ──────
            //
            // CORE 5.11.2 calls setup and framing preview's MAIN use, and a
            // preview that only appears once someone opens the crop editor is
            // useless for it — by then the framing decision has been made
            // blind.  We ask the phone for preview at `declare` and
            // PpcpHostService holds a consumer per Source from then on, so by
            // the time this row exists the pictures are already arriving.
            //
            // ⚠ PPCP ONLY (`camData.isPpcp`).  An industrial camera is not
            // started to fill a thumbnail — see camera_manager.cpp beside the
            // flag.
            Rectangle {
                id: rowPreviewTile
                visible: camRow.camData.isPpcp
                Layout.preferredWidth:  Theme.sp(64)
                Layout.preferredHeight: Theme.sp(36)
                Layout.alignment: Qt.AlignVCenter
                color:        Theme.colorBg
                radius:       Theme.sp(3)
                border.width: 1
                border.color: Theme.colorBorder
                clip:         true

                VideoOutput {
                    id: rowVideoOutput
                    anchors.fill:    parent
                    anchors.margins: 1
                    fillMode:        VideoOutput.PreserveAspectFit
                }

                // ⚠ Says WHY there is no picture rather than showing black.
                // "no peer attached" is the ordinary state for this backend
                // when the phone has gone, not a fault.
                Text {
                    anchors.centerIn: parent
                    width:            parent.width - Theme.sp(4)
                    visible:          !camRow.rowPreviewLive
                    text:             camRow.localPreviewInstance
                                      && camRow.localPreviewInstance.lastPreviewError !== ""
                                          ? qsTr("no signal") : qsTr("waiting…")
                    font.family:      Theme.fontData
                    font.pixelSize:   Theme.fontSzMicro
                    color:            Theme.colorText3
                    horizontalAlignment: Text.AlignHCenter
                    elide:            Text.ElideRight
                }
            },

            // Alias (editable) + meta
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(2)

                PpTextField {
                    Layout.fillWidth: true
                    placeholderText:  qsTr("Device alias…")
                    text:             camRow.camData.alias
                    onEditingFinished: cameraManager.setCameraAlias(camRow.camData.cameraKey, text)
                }

                Row {
                    spacing: Theme.gap(10)
                    Text {
                        text:           camRow.camData.description || ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                    Text {
                        text:           camRow.camData.interface || ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                    Text {
                        text:           camRow.camData.serialNumber || ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }
            },

            // Enable toggle
            Row {
                spacing: Theme.gap(6)
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text:           qsTr("Enable")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                    anchors.verticalCenter: parent.verticalCenter
                }

                PpSwitch {
                    checked: camRow.camData.enabled
                    onToggled: (v) => cameraManager.setExcluded(camRow.camData.index, !v)
                }
            }
        ]

        // ── Excluded note (shown when disabled) ──────────────────────────────
        Item {
            id: excludedNote
            anchors.top:   camRow.headerRow.bottom
            anchors.left:  parent.left
            anchors.right: parent.right
            height: camRow.camData.enabled ? 0 : implicitContentHeight + Theme.gap(14)
            visible: !camRow.camData.enabled
            clip: true

            readonly property real implicitContentHeight: excText.implicitHeight + Theme.gap(14)

            Text {
                id: excText
                anchors {
                    left:        parent.left
                    right:       parent.right
                    top:         parent.top
                    leftMargin:  Theme.gap(14)
                    rightMargin: Theme.gap(14)
                    topMargin:   Theme.gap(4)
                }
                text:           qsTr("Excluded — will not appear in view assignment or consume capture resources.")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.italic:    true
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
            }
        }

        // ── Body row (visible when enabled) ──────────────────────────────────
        // The camera's controls (View, Image, Frame rate, Trigger, Fixed in
        // place) wrap in a Flow, and the action buttons (Set crop, Ball
        // detection) sit at the right of it. On a card too narrow for even
        // the widest column beside the buttons (the first-launch 1120 px window
        // at a large font scale and Spacious), the buttons stack on a row of
        // their own under the controls, still right-aligned (`stacked`). Every
        // column stays whole and on the card; nothing is dropped or hidden for
        // width. `stacked` compares the columns' and buttons' implicit widths
        // with the row's own (anchored) width, so it never feeds back into
        // itself.
        GridLayout {
            id: bodyRow
            anchors.top:    camRow.headerRow.bottom
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.leftMargin:   Theme.gap(14)
            anchors.rightMargin:  Theme.gap(14)
            // The bottom padding: under the one row it is part of the row (the
            // buttons centre in it, as they always did); when stacked it is the
            // buttons' bottom margin, so the two rows sit at their own heights.
            height: camRow.camData.enabled ? implicitHeight + (stacked ? 0 : Theme.gap(28)) : 0
            visible: camRow.camData.enabled
            columns: stacked ? 1 : 2
            columnSpacing: Theme.gap(16)
            rowSpacing:    Theme.gap(16)
            clip: true

            // The widest shown column, and the whole controls group on one line.
            readonly property real widestColumn: {
                var w = 0
                for (var i = 0; i < controlsFlow.children.length; ++i) {
                    var c = controlsFlow.children[i]
                    if (c.visible) w = Math.max(w, c.implicitWidth)
                }
                return w
            }
            readonly property real controlsOneLine: root.oneLineWidth(controlsFlow)
            readonly property bool stacked: width > 0
                && width - columnSpacing - actionButtons.implicitWidth < widestColumn

            Flow {
                id: controlsFlow
                spacing: Theme.gap(16)
                Layout.fillWidth: true
                Layout.preferredWidth: bodyRow.controlsOneLine
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft

                // View selector ──────────────────────────────────────────────────
                ColumnLayout {
                    spacing: Theme.gap(4)

                    PpMicro {
                        kind: "caps"
                        text: qsTr("View")
                    }

                    PpComboBox {
                        id: viewCombo
                        implicitWidth: Theme.sp(168)

                        // Any number of cameras may share a perspective (e.g. two
                        // face-on cameras in one session) — except Club/Ball
                        // Impact, which exactly one camera holds (assignPerspective
                        // clears it elsewhere) and which is only offered to a
                        // camera that reaches 420 fps at some crop
                        // (impact_camera_design.md §2). A phone over PPCP never
                        // does: ≤ 240 fps and a rolling shutter.
                        readonly property var viewOptions: {
                            var opts = [
                                { label: qsTr("— Unassigned —"), perspective: CameraInstance.None },
                                { label: qsTr("Face-on"),         perspective: CameraInstance.FaceOn },
                                { label: qsTr("Down-the-line"),   perspective: CameraInstance.DownTheLine }
                            ]
                            if (camRow.camData.impactCapable || camRow.camData.perspective === CameraInstance.Impact)
                                opts.push({ label: qsTr("Club/Ball Impact"), perspective: CameraInstance.Impact })
                            opts.push({ label: qsTr("Other"), perspective: CameraInstance.Other })
                            return opts
                        }

                        model: viewOptions.map(function(o) { return o.label })

                        Component.onCompleted: {
                            var p = appSettings.cameraPerspective[camRow.camData.cameraKey] || 0
                            for (var i = 0; i < viewOptions.length; i++) {
                                if (viewOptions[i].perspective === p) { currentIndex = i; break }
                            }
                        }

                        Connections {
                            target: appSettings
                            function onCameraPerspectiveChanged() {
                                var p = appSettings.cameraPerspective[camRow.camData.cameraKey] || 0
                                for (var i = 0; i < viewCombo.viewOptions.length; i++) {
                                    if (viewCombo.viewOptions[i].perspective === p) {
                                        viewCombo.currentIndex = i; break
                                    }
                                }
                            }
                        }

                        onActivated: (idx) => {
                            var p = viewOptions[idx].perspective
                            // Both assignPerspective and applyImpactMode rebuild
                            // the rows synchronously (cameraListChanged) — read
                            // everything into locals FIRST, write after.
                            var panelRoot = root
                            var mgr       = cameraManager
                            var key       = camRow.camData.cameraKey
                            var camIndex  = camRow.camData.index
                            var maxW      = camRow.camData.maxWidth
                            var maxH      = camRow.camData.maxHeight
                            var modes     = camRow.impactModes
                            var inst      = camRow.realInstance
                            var storedFps = appSettings.cameraTargetFps[key]
                            var expUnset  = appSettings.cameraExposureUs[key] === undefined
                            // No impact strip of its own yet (camera_roi.h) — including a camera
                            // whose strip used to live in the shared crop: seed the mode.
                            var stripUnset = appSettings.cameraRoi[key + "#impact"] === undefined
                            if (p === CameraInstance.Impact) {
                                // First assignment seeds the recommended mode and
                                // exposure, so the camera is usable with no
                                // further click (impact_camera_design.md §10.2).
                                if (expUnset)
                                    panelRoot.setImpactExposure(key, panelRoot.impactDefaultExposureUs)
                                if ((!(storedFps >= 420) || stripUnset) && modes.length > 0)
                                    panelRoot.applyImpactMode(key, camIndex, maxW, maxH, modes[0], inst)
                            }
                            mgr.assignPerspective(key, p)
                        }
                    }
                }

                // Mirrored toggle ─────────────────────────────────────────────────
                ColumnLayout {
                    spacing: Theme.gap(4)

                    PpMicro {
                        kind: "caps"
                        text: qsTr("Image")
                    }

                    PpChoiceChip {
                        id: mirrorChip
                        readonly property bool isMirrored: (appSettings.cameraIsMirrored[camRow.camData.cameraKey] === true)

                        style:    "solid"
                        label:    qsTr("Mirrored")
                        selected: isMirrored
                        onClicked: {
                            var map = appSettings.cameraIsMirrored
                            if (mirrorChip.isMirrored)
                                delete map[camRow.camData.cameraKey]
                            else
                                map[camRow.camData.cameraKey] = true
                            appSettings.cameraIsMirrored = map
                            if (camRow.realInstance)
                                cameraManager.setIsMirrored(camRow.realInstance, !mirrorChip.isMirrored)
                        }
                    }
                }

                // Frame rate chips (every camera but the impact camera, whose rate
                // is part of its mode below) ─────────────────────────────────────
                ColumnLayout {
                    visible: !camRow.isImpact
                    spacing: Theme.gap(4)

                    PpMicro {
                        kind: "caps"
                        text: qsTr("Frame rate")
                    }

                    Row {
                        spacing: Theme.gap(4)

                        readonly property var fpsOptions: {
                            var opts = []
                            var max = camRow.camData.maxFps > 0 ? camRow.camData.maxFps : 60
                            if (max >= 30) opts.push(30)
                            if (max >= 60 && max > 30) opts.push(60)
                            if (max >= 120 && max > 60) opts.push(120)
                            if (max > 0 && opts.indexOf(max) < 0) opts.push(max)
                            return opts
                        }

                        readonly property double selectedFps: {
                            var v = appSettings.cameraTargetFps[camRow.camData.cameraKey]
                            return (v !== undefined && v > 0) ? v : (camRow.camData.maxFps > 0 ? camRow.camData.maxFps : 60)
                        }

                        Repeater {
                            model: parent.fpsOptions

                            delegate: PpChoiceChip {
                                style: "mono"; moves: false
                                required property var modelData
                                label:    (Math.round(modelData * 10) / 10) + qsTr(" fps")
                                selected: Math.abs(parent.selectedFps - modelData) < 0.5
                                onClicked: {
                                    var map = appSettings.cameraTargetFps
                                    map[camRow.camData.cameraKey] = modelData
                                    appSettings.cameraTargetFps = map
                                    cameraManager.setTargetFps(camRow.camData.index, modelData)
                                }
                            }
                        }
                    }
                }

                // Trigger chips ───────────────────────────────────────────────────
                ColumnLayout {
                    spacing: Theme.gap(4)

                    PpMicro {
                        kind: "caps"
                        text: qsTr("Trigger")
                    }

                    Row {
                        spacing: Theme.gap(4)

                        readonly property string selectedMode: {
                            var v = appSettings.cameraTriggerMode[camRow.camData.cameraKey]
                            return v !== undefined ? v : "freerun"
                        }

                        Repeater {
                            model: camRow.camData.hwTrigger
                                    ? [{ label: qsTr("Free-run"), mode: "freerun" },
                                       { label: qsTr("HW sync"),  mode: "hwsync"  }]
                                    : [{ label: qsTr("Free-run"), mode: "freerun" }]

                            delegate: PpChoiceChip {
                                style: "mono"; moves: false
                                required property var modelData
                                label:    modelData.label
                                selected: parent.selectedMode === modelData.mode
                                onClicked: {
                                    var map = appSettings.cameraTriggerMode
                                    map[camRow.camData.cameraKey] = modelData.mode
                                    appSettings.cameraTriggerMode = map
                                    cameraManager.setTriggerMode(camRow.camData.index, modelData.mode)
                                }
                            }
                        }
                    }
                }

                // ── Fixed in place toggle ────────────────────────────────────────
                ColumnLayout {
                    spacing: Theme.gap(4)

                    PpMicro {
                        kind: "caps"
                        text: qsTr("Fixed in place")
                    }

                    RowLayout {
                        spacing: Theme.gap(6)

                        PpSwitch {
                            readonly property bool fixedVal: {
                                var v = appSettings.cameraFixedInPlace[camRow.camData.cameraKey]
                                return v !== undefined ? !!v : false
                            }
                            checked: fixedVal
                            onToggled: (v) => {
                                var map = appSettings.cameraFixedInPlace
                                map[camRow.camData.cameraKey] = v
                                appSettings.cameraFixedInPlace = map
                            }
                        }

                        Text {
                            text: {
                                var v = appSettings.cameraFixedInPlace[camRow.camData.cameraKey]
                                return (v !== undefined && !!v) ? qsTr("Yes") : qsTr("No")
                            }
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText2
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    Text {
                        text:           qsTr("Camera is attached to the wall or immovable.")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        font.italic:    true
                        color:          Theme.colorText3
                        wrapMode:       Text.WordWrap
                        Layout.preferredWidth: Theme.sp(160)
                    }
                }
            }

            // Action buttons ─────────────────────────────────────────────────
            Row {
                id: actionButtons
                spacing:  Theme.gap(6)
                Layout.alignment: bodyRow.stacked ? (Qt.AlignTop | Qt.AlignRight) : (Qt.AlignVCenter | Qt.AlignRight)
                Layout.bottomMargin: bodyRow.stacked ? Theme.gap(28) : 0

                // Set crop — always available (setRoi() is a software crop on every camera)
                PpButton {
                    id: cropBtn
                    visible: true
                    style: "wash"; size: "compact"
                    implicitHeight: Theme.sp(26)
                    horizontalPadding: Theme.sp(24)
                    active: camRow.roiOpen
                    label: qsTr("Set crop")

                    onClicked: {
                        if (camRow.roiOpen) {
                            root.openRoiIndex = -1
                            return
                        }
                        // The crop editor must preview the FULL sensor,
                        // and the crop only applies at connect — so stop
                        // any capture and disconnect this camera before
                        // opening. NOTE: setSelected() rebuilds the
                        // Repeater delegates SYNCHRONOUSLY, destroying
                        // this delegate's QML context mid-handler — after
                        // that, every unqualified name lookup throws a
                        // ReferenceError, even for objects that outlive
                        // the row. Snapshot everything into JS locals
                        // FIRST; locals survive context destruction.
                        var mgr         = cameraManager
                        var toast       = cropToast
                        var panelRoot   = root
                        var idx         = camRow.camData.index
                        var wasSelected = camRow.camData.selected
                        var wasActive   = mgr.isRecording || wasSelected
                        var notice      = qsTr("Capture stopped — crop editing previews the full sensor")
                        if (mgr.isRecording)
                            mgr.stopAll()
                        if (wasSelected)
                            mgr.setSelected(idx, false)
                        if (wasActive)
                            toast.show(notice)
                        panelRoot.closeBallPanel()
                        panelRoot.openRoiIndex = idx
                    }
                }

                // Ball detection — hitting area + in-situ calibration
                // (design §8.1). Face-on cameras only; the profile only
                // persists for a fixed-in-place camera, which the panel
                // explains inline.
                PpButton {
                    id: ballBtn
                    visible: camRow.camData.perspective === CameraInstance.FaceOn
                    style: "wash"; size: "compact"
                    implicitHeight: Theme.sp(26)
                    horizontalPadding: Theme.sp(24)
                    active: camRow.ballOpen
                    label: qsTr("Ball detection")

                    onClicked: {
                        if (camRow.ballOpen) {
                            root.closeBallPanel()
                            return
                        }
                        // Calibration needs the LIVE pipeline (the ball
                        // detector only exists on buffer-backed
                        // instances), so opening connects the camera via
                        // the normal path — the wizard's Connect ditto.
                        // setSelected rebuilds the Repeater delegates
                        // SYNCHRONOUSLY (see the crop button above):
                        // snapshot locals first.
                        var mgr         = cameraManager
                        var panelRoot   = root
                        var idx         = camRow.camData.index
                        var wasSelected = camRow.camData.selected
                        panelRoot.closeBallPanel()   // restore any other row first
                        panelRoot.openRoiIndex = -1
                        panelRoot.ballAutoConnected = !wasSelected
                        if (!wasSelected) {
                            mgr.setSelected(idx, true)
                            if (!mgr.isRecording && mgr.anySelected)
                                mgr.startAll()
                        }
                        panelRoot.openBallIndex = idx
                    }
                }

            }
        }

        // ── Exposure and gain, every role but Impact (flir_image_clarity.md §6.4)
        // Collapsed to one line: the toggle and what the camera runs at now
        // (the words wrap beside the toggle rather than run off the card).
        // Opened, its two groups wrap like the body row: each group sits whole
        // on a line, the captions are no wider than the card, and the gain
        // chips (to the sensor's maximum: ten of them on a 48 dB Blackfly)
        // wrap inside the card (root.oneLineWidth).
        ColumnLayout {
            id: wideTuneRow
            readonly property bool shown: camRow.camData.enabled && !camRow.isImpact && camRow.camData.tuningLockable === true
            readonly property double exposureUs: root.wideExposureUs(camRow.camData.cameraKey)
            readonly property double gainDb:     root.wideGainDb(camRow.camData.cameraKey)
            readonly property double framePeriodUs: camRow.currentFps > 0 ? 1e6 / camRow.currentFps : 0
            anchors.top:    bodyRow.bottom
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.leftMargin:  Theme.gap(14)
            anchors.rightMargin: Theme.gap(14)
            height: shown ? implicitHeight + Theme.gap(20) : 0
            visible: shown
            spacing: Theme.gap(24)   // between rows: wider than a row's own wrapped lines (gap 16), so a wrapped group reads as part of its row
            clip: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(12)
                PpButton {
                    id: wideToggle
                    style: "wash"; size: "micro"; mono: true
                    implicitHeight: Theme.sp(24)
                    moves: false
                    active: camRow.tuningOpen
                    glyph: camRow.tuningOpen ? "▾" : "▸"
                    label: qsTr("Exposure and gain")
                    onClicked: root.openTuningIndex = camRow.tuningOpen ? -1 : camRow.camData.index
                }
                // What it runs at, and — while it streams — what the raw frame
                // measures: the number to hold when an exposure is locked.
                Text {
                    readonly property var inst: camRow.realInstance
                    text: (wideTuneRow.exposureUs > 0 ? qsTr("%1 µs").arg(wideTuneRow.exposureUs.toFixed(0)) : qsTr("Auto exposure"))
                          + " · "
                          + (wideTuneRow.gainDb >= 0 ? qsTr("%1 dB").arg(wideTuneRow.gainDb.toFixed(0)) : qsTr("auto gain"))
                          + ((inst && inst.isRecording)
                             ? qsTr("   ·   levels: median %1, peak %2, clipped %3%")
                                   .arg(inst.levelBackground.toFixed(0))
                                   .arg(inst.levelPeak.toFixed(0))
                                   .arg((inst.levelClipped * 100).toFixed(1))
                             : "")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                    Layout.fillWidth: true
                    wrapMode:       Text.WordWrap
                }
            }

            Flow {
                visible: camRow.tuningOpen
                Layout.fillWidth: true
                spacing: Theme.gap(16)   // between groups, and between a row's wrapped lines

                // Exposure: auto, or locked to cut motion blur.
                ColumnLayout {
                    spacing: Theme.gap(4)
                    PpMicro { kind: "caps"; text: Theme.caps(qsTr("Exposure")) }
                    Row {
                        spacing: Theme.gap(4)
                        PpChoiceChip {
                            style: "mono"; moves: false
                            label:    qsTr("Auto")
                            selected: wideTuneRow.exposureUs <= 0
                            onClicked: root.setWideExposure(camRow.camData.cameraKey, 0, camRow.realInstance)
                        }
                        // The frame period, as auto uses it today: the lock that keeps the
                        // blur the shaft tracker reads when more light would let auto
                        // shorten the exposure (flir_image_clarity.md §6.5).
                        PpChoiceChip {
                            style: "mono"; moves: false
                            readonly property double us: Math.floor(wideTuneRow.framePeriodUs * 0.99)
                            visible:  us > 0
                            label:    qsTr("Full frame")
                            selected: wideTuneRow.exposureUs > 0 && Math.abs(wideTuneRow.exposureUs - us) < 0.5
                            onClicked: root.setWideExposure(camRow.camData.cameraKey, us, camRow.realInstance)
                        }
                        PpTextField {
                            width:  Theme.sp(72)
                            height: Theme.sp(24)
                            implicitHeight: Theme.sp(24)
                            leftPadding: Theme.gap(8); rightPadding: Theme.gap(8)
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            placeholderText: qsTr("µs")
                            text: wideTuneRow.exposureUs > 0 ? wideTuneRow.exposureUs.toFixed(0) : ""
                            validator: DoubleValidator { bottom: 5; top: 50000; decimals: 1 }
                            onEditingFinished: {
                                var v = parseFloat(text)
                                if (!(v > 0) || Math.abs(v - wideTuneRow.exposureUs) < 0.05) return
                                root.setWideExposure(camRow.camData.cameraKey, v, camRow.realInstance)
                            }
                        }
                    }
                    Text {
                        text: (wideTuneRow.framePeriodUs > 0
                               ? qsTr("On Auto each frame is exposed for up to %1 µs, the whole time between frames. ").arg(wideTuneRow.framePeriodUs.toFixed(0))
                               : "")
                              + qsTr("Keep it that long: the club's blur is how the shaft tracker times the club. If you add light, choose Full frame so the exposure stays long and the gain drops instead. Changes apply at once while the camera is streaming.")
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                        color: Theme.colorText3; wrapMode: Text.WordWrap
                        Layout.preferredWidth: Math.min(Theme.sp(420), wideTuneRow.width)
                    }
                }

                // Gain: auto, or locked to the sensor's own range.
                ColumnLayout {
                    spacing: Theme.gap(4)
                    PpMicro { kind: "caps"; text: Theme.caps(qsTr("Gain")) }
                    Flow {
                        id: wideGainRow
                        spacing: Theme.gap(4)
                        Layout.preferredWidth: Math.min(root.oneLineWidth(wideGainRow), wideTuneRow.width)
                        readonly property double maxDb: camRow.camData.gainMaxDb > 0 ? camRow.camData.gainMaxDb : 24
                        PpChoiceChip {
                            style: "mono"; moves: false
                            label:    qsTr("Auto")
                            selected: wideTuneRow.gainDb < 0
                            onClicked: root.setWideGain(camRow.camData.cameraKey, -1, camRow.realInstance)
                        }
                        Repeater {
                            model: [0, 6, 12, 18, 24, 30, 36, 42, 48].filter(function(v) { return v <= wideGainRow.maxDb + 0.5 })
                            delegate: PpChoiceChip {
                                style: "mono"; moves: false
                                required property var modelData
                                label:    modelData + qsTr(" dB")
                                selected: wideTuneRow.gainDb >= 0 && Math.abs(wideTuneRow.gainDb - modelData) < 0.5
                                onClicked: root.setWideGain(camRow.camData.cameraKey, modelData, camRow.realInstance)
                            }
                        }
                    }
                    Text {
                        text: {
                            var inst = camRow.realInstance
                            var held = (inst && inst.appliedGainDb >= 0) ? qsTr("Camera holds %1 dB. ").arg(inst.appliedGainDb.toFixed(1)) : ""
                            return held + qsTr("Auto gain fills in for a locked exposure, up to the sensor's maximum. Every 6 dB doubles the brightness, and the noise with it.")
                        }
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                        color: Theme.colorText3; wrapMode: Text.WordWrap
                        Layout.preferredWidth: Math.min(Theme.sp(260), wideTuneRow.width)
                    }
                }

            }
        }

        ColumnLayout {
            id: impactRow
            anchors.top:    wideTuneRow.bottom
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.leftMargin:  Theme.gap(14)
            anchors.rightMargin: Theme.gap(14)
            height: (camRow.camData.enabled && camRow.isImpact) ? implicitHeight + Theme.gap(24) : 0
            visible: camRow.camData.enabled && camRow.isImpact
            spacing: Theme.gap(24)   // between rows: wider than a row's own wrapped lines (gap 16), so a wrapped group reads as part of its row
            clip: true

          // ── Impact row — the impact camera's mode and its tuning ───────────
          // Its own block beneath the body row. It moved there when the body
          // row was one unwrapped line, where its chip groups and notes pushed
          // the action buttons (Set crop, which is how the strip is positioned
          // over the ball) off the clipped right edge.
          // Row 1: the mode — what makes the camera an impact camera at all —
          // and the TUNING disclosure. Then the tuning that makes the club
          // visible in it. Every row is a Flow, as the body row is: its groups
          // wrap onto more lines when the card is narrow, each caption and the
          // mode and gain chips are sized to at most the row's width, and
          // nothing is dropped or hidden for width.
          Flow {
            Layout.fillWidth: true
            spacing: Theme.gap(16)   // between groups, and between a row's wrapped lines

            // Impact mode chips — crop size × rate, as the camera reported them
            // at enumeration (impact_camera_design.md §3.1, §10.2) ───────────
            ColumnLayout {
                visible: camRow.isImpact
                spacing: Theme.gap(4)

                PpMicro {
                    kind: "caps"
                    text: qsTr("Impact mode")
                }

                Flow {
                    id: modeFlow
                    spacing: Theme.gap(4)
                    Layout.preferredWidth: Math.min(Theme.sp(520), impactRow.width)

                    // The impact strip, stored apart from the camera's other-role crop (camera_roi.h).
                    readonly property var    storedRoi: appSettings.cameraRoi[camRow.camData.cameraKey + "#impact"]
                    readonly property double storedFps: {
                        var v = appSettings.cameraTargetFps[camRow.camData.cameraKey]
                        return (v !== undefined && v > 0) ? v : 0
                    }

                    Repeater {
                        model: camRow.impactModes

                        delegate: PpChoiceChip {
                            id: modeChip
                            style: "mono"; moves: false
                            required property var modelData
                            required property int index

                            label: modelData.w + "×" + modelData.h + "  "
                                   + Math.round(modelData.fps) + qsTr(" fps")
                                   + (modeChip.index === 0 ? "  ★" : "")
                            // Selected when the stored crop is this size (to the
                            // node increment) and the stored rate is this rate.
                            selected: {
                                var r = modeFlow.storedRoi
                                if (!r) return false
                                return Math.abs(r.w * (camRow.camData.maxWidth  || 0) - modelData.w) < 8
                                    && Math.abs(r.h * (camRow.camData.maxHeight || 0) - modelData.h) < 8
                                    && Math.abs(modeFlow.storedFps - modelData.fps) < 1
                            }
                            onClicked: {
                                // applyImpactMode rebuilds the rows — locals first.
                                var panelRoot = root
                                var key       = camRow.camData.cameraKey
                                var camIndex  = camRow.camData.index
                                var maxW      = camRow.camData.maxWidth
                                var maxH      = camRow.camData.maxHeight
                                var mode      = modeChip.modelData
                                var inst      = camRow.realInstance
                                panelRoot.applyImpactMode(key, camIndex, maxW, maxH, mode, inst)
                            }
                        }
                    }
                }

                Text {
                    text: qsTr("Rates are the camera's advertised maximum at that crop; delivered runs a few percent lower. ★ is the recommended mode. Use Set crop to place the box over the ball, about 60% of the way across. Applies on the next connect.")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.italic:    true
                    color:          Theme.colorText3
                    wrapMode:       Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(520), impactRow.width)
                }
            }

            // Everything else about the impact camera lives behind this
            // disclosure: exposure, gain, view, strobe, levels and the
            // note are for the operator tuning a room, not for choosing a
            // camera, and they overwhelm the row when always shown. The crop,
            // rate and resolution (the mode chips above, and Set crop) stay.
            ColumnLayout {
                spacing: Theme.gap(4)
                PpMicro { kind: "caps"; text: Theme.caps(qsTr("Tuning")) }
                PpButton {
                    id: tuningToggle
                    style: "wash"; size: "micro"; mono: true
                    implicitHeight: Theme.sp(24)
                    moves: false
                    active: camRow.tuningOpen
                    glyph: camRow.tuningOpen ? "▾" : "▸"
                    label: camRow.tuningOpen ? qsTr("Hide exposure, gain and light")
                                             : qsTr("Exposure, gain and light")
                    onClicked: root.openTuningIndex = camRow.tuningOpen ? -1 : camRow.camData.index
                }
                Text {
                    text: qsTr("Defaults suit the studio; open this to tune to a room.")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(200), impactRow.width)
                }
            }

          }

          // ── The tuning section (collapsed by default) ─────────────────────
          ColumnLayout {
            id: tuningSection
            visible: camRow.tuningOpen
            Layout.fillWidth: true
            spacing: Theme.gap(24)   // between rows: wider than a row's own wrapped lines (gap 16), so a wrapped group reads as part of its row

          Flow {
            Layout.fillWidth: true
            spacing: Theme.gap(16)   // between groups, and between a row's wrapped lines

            // Exposure chips — the impact camera's locked exposure ────────────
            ColumnLayout {
                visible: camRow.isImpact
                spacing: Theme.gap(4)

                PpMicro {
                    kind: "caps"
                    text: qsTr("Exposure")
                }

                Row {
                    id: exposureRow
                    spacing: Theme.gap(4)

                    readonly property double selectedUs: {
                        var v = appSettings.cameraExposureUs[camRow.camData.cameraKey]
                        return (v !== undefined && v > 0) ? v : root.impactDefaultExposureUs
                    }

                    Repeater {
                        model: [30, 50, 70, 100]

                        delegate: PpChoiceChip {
                            id: expChip
                            style: "mono"; moves: false
                            required property var modelData
                            label:    modelData + qsTr(" µs")
                            selected: Math.abs(exposureRow.selectedUs - modelData) < 0.5
                            onClicked: root.setImpactExposure(camRow.camData.cameraKey, expChip.modelData, camRow.realInstance)
                        }
                    }

                    // Any value: once gain is in play the right exposure is
                    // whatever the blur budget allows for the club in hand.
                    PpTextField {
                        id: expField
                        width:  Theme.sp(72)
                        height: Theme.sp(24)
                        implicitHeight: Theme.sp(24)
                        leftPadding: Theme.gap(8); rightPadding: Theme.gap(8)
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        placeholderText: qsTr("µs")
                        text: exposureRow.selectedUs.toFixed(0)
                        validator: DoubleValidator { bottom: 5; top: 50000; decimals: 1 }
                        onEditingFinished: {
                            var v = parseFloat(text)
                            if (!(v > 0) || Math.abs(v - exposureRow.selectedUs) < 0.05) return
                            root.setImpactExposure(camRow.camData.cameraKey, v, camRow.realInstance)
                        }
                    }
                }

                Text {
                    text: qsTr("Locked, auto-exposure off. 70 µs or less keeps blur under 2 px at 1 mm per pixel — the light has to follow. Takes effect now on a streaming camera.")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.italic:    true
                    color:          Theme.colorText3
                    wrapMode:       Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(260), impactRow.width)
                }
            }


          }

          // ── Row 2: gain, view gain, strobe — tuning to the room ──────────
          // (impact_camera_design.md §10.3). Gain writes to the
          // camera the moment they are clicked when it streams; the tile's
          // level readout (bg / peak / clip) is what to watch.
          Flow {
            Layout.fillWidth: true
            spacing: Theme.gap(16)   // between groups, and between a row's wrapped lines

            // Sensor gain — before the ADC, so it lifts the club above the
            // 8-bit floor. Chips to the camera's own maximum.
            ColumnLayout {
                spacing: Theme.gap(4)
                PpMicro { kind: "caps"; text: Theme.caps(qsTr("Gain")) }
                Flow {
                    id: gainRow
                    Layout.preferredWidth: Math.min(root.oneLineWidth(gainRow), impactRow.width)
                    spacing: Theme.gap(4)
                    readonly property double selected:
                        root.impactTuning(camRow.camData.cameraKey, "gainDb", root.impactDefaultGainDb)
                    readonly property double maxDb: camRow.camData.gainMaxDb > 0 ? camRow.camData.gainMaxDb : 24
                    Repeater {
                        model: [0, 6, 12, 18, 24, 30, 36, 42, 48].filter(function(v) { return v <= gainRow.maxDb + 0.5 })
                        delegate: PpChoiceChip {
                            style: "mono"; moves: false
                            required property var modelData
                            label:    modelData + qsTr(" dB")
                            selected: Math.abs(gainRow.selected - modelData) < 0.5
                            onClicked: root.setImpactTuning(camRow.camData.cameraKey, "gainDb", modelData, camRow.realInstance)
                        }
                    }
                }
                Text {
                    text: {
                        var inst = camRow.realInstance
                        var held = (inst && inst.appliedGainDb >= 0) ? qsTr("Camera holds %1 dB. ").arg(inst.appliedGainDb.toFixed(1)) : ""
                        return held + qsTr("Default 18 dB: at 70 µs the camera is short of light, and gain is the one lever it has. Every 6 dB doubles the ball and the club. Auto-gain off; the black level is lifted so the shadows do not clip.")
                    }
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(300), impactRow.width)
                }
            }

            // View gain — the tile and the replay only. The recorded pixels
            // are never touched; the level readout ignores it.
            ColumnLayout {
                spacing: Theme.gap(4)
                PpMicro { kind: "caps"; text: Theme.caps(qsTr("View")) }
                Row {
                    id: viewRow
                    spacing: Theme.gap(4)
                    readonly property double selected:
                        root.impactTuning(camRow.camData.cameraKey, "viewGain", root.impactDefaultViewGain)
                    Repeater {
                        model: [1, 2, 4, 8, 16]
                        delegate: PpChoiceChip {
                            style: "mono"; moves: false
                            required property var modelData
                            label:    "×" + modelData
                            selected: Math.abs(viewRow.selected - modelData) < 0.5
                            onClicked: root.setImpactTuning(camRow.camData.cameraKey, "viewGain", modelData, null)
                        }
                    }
                }
                Text {
                    text: qsTr("Display stretch only — for aiming a dark tile. Stamped into the clip and used on replay.")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(200), impactRow.width)
                }
            }

            // Strobe — Line1 carries ExposureActive for an LED strobe driver.
            ColumnLayout {
                spacing: Theme.gap(4)
                PpMicro { kind: "caps"; text: Theme.caps(qsTr("Strobe")) }
                PpSwitch {
                    checked: root.impactTuning(camRow.camData.cameraKey, "strobe", false)
                    onToggled: (v) => root.setImpactTuning(camRow.camData.cameraKey, "strobe", v, null)
                }
                Text {
                    text: qsTr("Line1 = exposure active. Next connect.")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(150), impactRow.width)
                }
            }

          }

          // ── Row 3: the live levels and the note ──────────────────────────
          Flow {
            Layout.fillWidth: true
            spacing: Theme.gap(16)   // between groups, and between a row's wrapped lines

            // What the raw frame measures right now, if the camera streams:
            // the number that turns tuning from guessing into a procedure.
            ColumnLayout {
                spacing: Theme.gap(4)
                PpMicro { kind: "caps"; text: Theme.caps(qsTr("Levels")) }
                Text {
                    readonly property var inst: camRow.realInstance
                    text: (inst && inst.isRecording)
                          ? qsTr("mat %1   peak %2   clipped %3%")
                                .arg(inst.levelBackground.toFixed(0))
                                .arg(inst.levelPeak.toFixed(0))
                                .arg((inst.levelClipped * 100).toFixed(1))
                          : qsTr("— (camera not streaming)")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.colorText
                }
                Text {
                    text: qsTr("Aim for the mat under 40 and the peak near 250 with almost nothing clipped; the club body then sits around 100.")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                    Layout.preferredWidth: Math.min(Theme.sp(300), impactRow.width)
                }
            }

            // Free text stamped into every clip: what the camera cannot read.
            ColumnLayout {
                spacing: Theme.gap(4)
                width: Math.min(Theme.sp(520), impactRow.width)   // in a Flow: sized from the row, the field fills it
                PpMicro { kind: "caps"; text: Theme.caps(qsTr("Note")) }
                PpTextField {
                    id: noteField
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.sp(520)
                    height: Theme.sp(28)
                    implicitHeight: Theme.sp(28)
                    font.pixelSize: Theme.fontSzMicro
                    placeholderText: qsTr("Lens, aperture, light — e.g. 8 mm f/1.4, two 100 W floods at 0.5 m")
                    text: root.impactTuning(camRow.camData.cameraKey, "note", "")
                    onEditingFinished: {
                        if (text === root.impactTuning(camRow.camData.cameraKey, "note", "")) return
                        root.setImpactTuning(camRow.camData.cameraKey, "note", text, null)
                    }
                }
                Text {
                    text: qsTr("Recorded on every clip, so a September clip and a July one can be told apart by more than the date.")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.italic: true
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
          }
          }
        }

        // ── ROI panel ────────────────────────────────────────────────────────
        Item {
            id: roiPanel
            anchors.top:   impactRow.bottom
            anchors.left:  parent.left
            anchors.right: parent.right
            height: camRow.roiOpen ? roiPanelContent.implicitHeight : 0
            clip: true
            visible: camRow.roiOpen

            Behavior on height { NumberAnimation { duration: Theme.durationFast } }

            ColumnLayout {
                id: roiPanelContent
                anchors.left:  parent.left
                anchors.right: parent.right
                spacing: 0

                // Top border separator
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color:  Theme.colorBorderMid
                    opacity: Theme.borderOpacityNormal
                }

                // ── Camera capabilities strip ────────────────────────────────
                PpFactStrip {
                    Layout.fillWidth: true
                    cells: [
                        { key: qsTr("Sensor"),         val: (camRow.camData.vendorName || "") + " " + (camRow.camData.modelName || "") },
                        { key: qsTr("Max resolution"),  val: (camRow.camData.maxWidth || 0) + " × " + (camRow.camData.maxHeight || 0) + " px" },
                        { key: qsTr("Pixel format"),    val: camRow.camData.pixelFormat || "—" },
                        { key: qsTr("Bit depth"),       val: (camRow.camData.bitsPerPixel || 0) + " bit" },
                        { key: qsTr("Bytes / px"),      val: ((camRow.camData.bitsPerPixel || 0) / 8.0).toFixed(2) }
                    ]
                }

                // ── Preview + controls ───────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin:  Theme.gap(16)
                    Layout.rightMargin: Theme.gap(16)
                    Layout.topMargin:   Theme.gap(16)
                    Layout.bottomMargin: Theme.gap(28)
                    spacing: Theme.gap(20)

                    // Video preview + draggable ROI
                    ColumnLayout {
                        spacing: 0
                        Layout.alignment: Qt.AlignTop

                        // Preview area
                        Rectangle {
                            id: previewRect
                            // The full sensor's aspect, inside the old 510×288 box: the crop
                            // box below is drawn as fractions of THIS rectangle, so the
                            // picture has to fill it exactly — a 1280×1024 Chameleon3 in a
                            // 16:9 box was pillarboxed and the box sat off the picture.
                            readonly property real sensorAspect:
                                (camRow.camData.maxWidth > 0 && camRow.camData.maxHeight > 0)
                                    ? camRow.camData.maxWidth / camRow.camData.maxHeight : 16.0 / 9.0
                            readonly property real boxW: Theme.sp(510)
                            readonly property real boxH: Theme.sp(288)
                            width:  sensorAspect >= boxW / boxH ? boxW : boxH * sensorAspect
                            height: sensorAspect >= boxW / boxH ? boxW / sensorAspect : boxH
                            color:  "#080a0c"
                            border.width: 1
                            border.color: Theme.colorBorderMid
                            radius: 2
                            clip: true

                            // ── ROI box pixel positions (bound to instance.cropRoi) ──
                            readonly property real rX: camRow.instance ? camRow.instance.cropRoi.x      * previewRect.width  : 0
                            readonly property real rY: camRow.instance ? camRow.instance.cropRoi.y      * previewRect.height : 0
                            readonly property real rW: camRow.instance && camRow.instance.cropRoi.width  > 0
                                                           ? camRow.instance.cropRoi.width  * previewRect.width  : previewRect.width
                            readonly property real rH: camRow.instance && camRow.instance.cropRoi.height > 0
                                                           ? camRow.instance.cropRoi.height * previewRect.height : previewRect.height

                            // ── Unified drag state ────────────────────────────
                            property string roiDragMode: "none" // none|move|tl|tr|bl|br
                            property real   roiDragStartX: 0
                            property real   roiDragStartY: 0
                            property real   roiOrigX: 0
                            property real   roiOrigY: 0
                            property real   roiOrigW: 0
                            property real   roiOrigH: 0

                            // ── Live video feed ───────────────────────────────
                            VideoOutput {
                                id: settingsVideoOutput
                                anchors.fill: parent
                                visible: !(camRow.localPreviewInstance && camRow.localPreviewInstance.needsDebayer)
                            }
                            // A raw-Bayer camera (the Chameleon3s) is demosaiced on the GPU
                            // here exactly as on the camera tiles. The settings sink above
                            // only ever carried the mosaic as greyscale.
                            BayerVideoItem {
                                id: settingsBayerView
                                anchors.fill: parent
                                visible: camRow.localPreviewInstance !== null
                                         && camRow.localPreviewInstance.needsDebayer
                            }

                            // A failed start is otherwise silent — the tile just stays
                            // black forever.  Most common cause today: a PPCP camera
                            // whose phone isn't connected right now ("PPCP camera: no
                            // peer attached"), which is an ordinary state for that
                            // backend, not a fault — same visual language as
                            // ImusPanel.qml's "Not connected" placeholder.
                            Rectangle {
                                anchors.fill: parent
                                color:        Theme.colorBg
                                visible:      camRow.localPreviewInstance !== null
                                              && camRow.localPreviewInstance.lastPreviewError !== ""

                                Text {
                                    anchors.centerIn: parent
                                    anchors.margins:  Theme.gap(16)
                                    width:             parent.width - Theme.gap(32)
                                    text:              camRow.localPreviewInstance
                                                           ? camRow.localPreviewInstance.lastPreviewError
                                                           : ""
                                    font.family:       Theme.fontData
                                    font.pixelSize:    Theme.fontSzMicro
                                    color:              Theme.colorText3
                                    wrapMode:           Text.WordWrap
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            // Persist ROI changes directly to AppSettings whenever the
                            // instance's cropRoi changes (drag, numeric field, preset).
                            //
                            // ⚠ GUARDED AGAINST A DESTROYED CONTEXT, NOT JUST A NULL
                            // TARGET. `cropRoiChanged` can fire off the C++ side as
                            // `destroyPreviewInstance()` (below) tears the instance
                            // down — the same "setSelected() rebuilds every Repeater
                            // delegate synchronously" hazard the crop button's own
                            // onClicked works around by snapshotting locals first
                            // (see its comment). This handler has no "before" to
                            // snapshot from — it only runs REACTIVELY.
                            //
                            // ⚠ `typeof appSettings === "undefined"` is NOT a safe
                            // guard here, and was tried first — it only protects
                            // against an identifier that was never DECLARED. Once
                            // the enclosing QML context itself is mid-destruction
                            // (this row's Repeater delegate torn down while its
                            // preview instance's last cropRoiChanged is still in
                            // flight — see reclaimStream()'s note in
                            // VideoInputPpcp.cpp for how that race arises), even a
                            // `typeof` lookup throws, because the scope-chain
                            // resolution itself is what's broken, not the identifier.
                            // try/catch is the one thing that catches an exception
                            // regardless of why the lookup failed — confirmed
                            // necessary 25 Aug 2026: the typeof guard alone still
                            // logged "ReferenceError: appSettings is not defined"
                            // here on a real PPCP camera's crop editor.
                            function persistRoi() {
                                try {
                                    if (!camRow.camData.cameraKey) return
                                    var roi = camRow.instance.cropRoi
                                    var map = appSettings.cameraRoi
                                    // The crop of the role the camera is in (camera_roi.h).
                                    var roiKey = camRow.camData.cameraKey
                                               + (camRow.camData.perspective === CameraInstance.Impact ? "#impact" : "")
                                    if (roi.width > 0 && roi.height > 0)
                                        map[roiKey] = { x: roi.x, y: roi.y, w: roi.width, h: roi.height }
                                    else
                                        delete map[roiKey]
                                    appSettings.cameraRoi = map
                                } catch (e) {
                                    // The context this handler needed is gone —
                                    // the row is being torn down and there is
                                    // nothing left here to persist ROI for.
                                }
                            }

                            Connections {
                                target: camRow.instance
                                function onCropRoiChanged() {
                                    // ⛔ NOT WHILE A DRAG IS IN FLIGHT, AND THIS IS WHY
                                    // RESIZING THE CROP BOX WAS ALMOST IMPOSSIBLE.
                                    //
                                    // Writing `appSettings.cameraRoi` emits
                                    // `cameraRoiChanged`, and CameraManager wires that
                                    // straight to `cameraListChanged` (camera_manager.cpp,
                                    // "a crop edit must refresh the list") because
                                    // `cameraList()` derives initialWidth/initialHeight
                                    // from the persisted crop.  That rebuilds every
                                    // delegate in this Repeater — including THIS
                                    // `previewRect` and the MouseArea holding the mouse
                                    // grab — so the drag died on the first pixel and the
                                    // operator had to re-press for each one.
                                    //
                                    // ⚠ AND IT IS WHY MOVING WORKED WHILE RESIZING DID
                                    // NOT.  Qt leaves a Repeater's delegates standing when
                                    // a row's CONTENTS have not changed: a move alters x/y
                                    // only, which no list field carries, so the rebuild was
                                    // a no-op.  A resize changes the crop-derived
                                    // initialWidth/initialHeight, the row genuinely
                                    // differs, and the delegates go.
                                    //
                                    // ⚠ AND IT TOOK THE PREVIEW WITH IT.  The teardown ran
                                    // `Component.onDestruction` → `destroyPreviewInstance()`
                                    // → `stopCapture()`, which is where the four
                                    // "the device closed the preview stream (not_needed)"
                                    // lines in Mark's 31 Aug log came from.
                                    //
                                    // The gesture is persisted once, on release.  A
                                    // numeric field, a preset or a clear still lands here
                                    // and is written immediately.
                                    if (previewRect.roiDragMode !== "none") return
                                    previewRect.persistRoi()
                                }
                            }

                            // Wire/clear the settings sink and start/stop the full-sensor
                            // preview-only instance (no event buffer, no pose pipeline, no
                            // crop) with the panel. A plain function — not just Connections —
                            // because opening the panel deselects the camera, which rebuilds
                            // every Repeater delegate: the rebuilt row is created with
                            // roiOpen already true, so onRoiOpenChanged never fires and
                            // Component.onCompleted must run the same wiring.
                            // ⚠ ONE INSTANCE, TWO PLACES TO DRAW IT.  A phone's
                            // row keeps a preview alive whenever the panel is
                            // open, so the tile in the header is never blank
                            // waiting for a round trip; opening the crop editor
                            // moves the SAME instance's sink to the big
                            // VideoOutput rather than making a second one,
                            // which would collide on the Stream id and stop the
                            // first.  An industrial camera keeps the old
                            // behaviour exactly: nothing runs until the editor
                            // opens.
                            function syncPreview() {
                                var want = camRow.roiOpen || camRow.camData.isPpcp
                                if (want) {
                                    if (!camRow.localPreviewInstance)
                                        camRow.localPreviewInstance = cameraManager.createPreviewInstance(camRow.camData.index)
                                    var inst = camRow.localPreviewInstance
                                    if (!inst) return
                                    if (camRow.roiOpen) {
                                        // Seed a default crop the first time the editor opens
                                        var r = inst.cropRoi
                                        if (r.width <= 0 || r.height <= 0)
                                            inst.setCropRoi(Qt.rect(0.3, 0.0, 0.4, 1.0))
                                        inst.setSettingsSink(settingsVideoOutput.videoSink)
                                        inst.addBayerItem(settingsBayerView)
                                    } else {
                                        inst.removeBayerItem(settingsBayerView)
                                        inst.setSettingsSink(rowVideoOutput.videoSink)
                                    }
                                    inst.startPreview()
                                } else if (camRow.localPreviewInstance) {
                                    camRow.localPreviewInstance.removeBayerItem(settingsBayerView)
                                    camRow.localPreviewInstance.setSettingsSink(null)
                                    camRow.localPreviewInstance.stopPreview()
                                    cameraManager.destroyPreviewInstance(camRow.localPreviewInstance)
                                    camRow.localPreviewInstance = null
                                }
                            }

                            Component.onCompleted: syncPreview()
                            // ⚠ NO `localPreviewInstance = null` HERE.  The row is
                            // going away, and nulling it changes `camRow.instance`,
                            // which re-runs every crop/fps binding below inside a
                            // context that is already half torn down — the
                            // "Cannot read property 'maxWidth' of undefined" burst
                            // each time a phone connecting rebuilt the list.
                            Component.onDestruction: {
                                var inst = camRow.localPreviewInstance
                                if (inst) {
                                    inst.removeBayerItem(settingsBayerView)
                                    inst.setSettingsSink(null)
                                    inst.stopPreview()
                                    cameraManager.destroyPreviewInstance(inst)
                                }
                            }

                            Connections {
                                target: camRow
                                function onRoiOpenChanged() { previewRect.syncPreview() }
                            }

                            // ── Overlay shades around the ROI ─────────────────
                            readonly property bool hasRoi: camRow.instance
                                                        && camRow.instance.cropRoi.width > 0

                            Rectangle {   // left
                                x: 0; y: 0
                                width:  previewRect.rX
                                height: previewRect.height
                                color:  "black"; opacity: 0.52
                                visible: previewRect.hasRoi
                            }
                            Rectangle {   // right
                                x: previewRect.rX + previewRect.rW; y: 0
                                width:  previewRect.width - (previewRect.rX + previewRect.rW)
                                height: previewRect.height
                                color:  "black"; opacity: 0.52
                                visible: previewRect.hasRoi
                            }
                            Rectangle {   // top
                                x: previewRect.rX; y: 0
                                width:  previewRect.rW
                                height: previewRect.rY
                                color:  "black"; opacity: 0.52
                                visible: previewRect.hasRoi
                            }
                            Rectangle {   // bottom
                                x: previewRect.rX
                                y: previewRect.rY + previewRect.rH
                                width:  previewRect.rW
                                height: previewRect.height - (previewRect.rY + previewRect.rH)
                                color:  "black"; opacity: 0.52
                                visible: previewRect.hasRoi
                            }

                            // ── ROI outline + corner handles (visual only) ────
                            Item {
                                id: roiVisual
                                x:      previewRect.rX
                                y:      previewRect.rY
                                width:  previewRect.rW
                                height: previewRect.rH
                                visible: previewRect.hasRoi
                                z: 5

                                Rectangle {
                                    anchors.fill: parent
                                    color:        Theme.colorAccent
                                    opacity:      0.05
                                }
                                Rectangle {
                                    anchors.fill:  parent
                                    color:        "transparent"
                                    border.width:  1.5
                                    border.color:  Theme.colorAccent
                                }

                                // Corner handle squares (visual only — drag handled by the MouseArea below)
                                readonly property int hs: Theme.sp(10)
                                Rectangle { width: parent.hs; height: parent.hs; x: -parent.hs/2;              y: -parent.hs/2;              color: Theme.colorAccent; border.width:1; border.color:"black" }
                                Rectangle { width: parent.hs; height: parent.hs; x: parent.width-parent.hs/2;  y: -parent.hs/2;              color: Theme.colorAccent; border.width:1; border.color:"black" }
                                Rectangle { width: parent.hs; height: parent.hs; x: -parent.hs/2;              y: parent.height-parent.hs/2; color: Theme.colorAccent; border.width:1; border.color:"black" }
                                Rectangle { width: parent.hs; height: parent.hs; x: parent.width-parent.hs/2;  y: parent.height-parent.hs/2; color: Theme.colorAccent; border.width:1; border.color:"black" }
                            }

                            // ── Single unified drag MouseArea ─────────────────
                            // Covers the entire preview so the mouse is never "outside"
                            // its active area during a fast drag.
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                preventStealing: true
                                z: 20

                                // Handle hit radius (larger than the 10px visual square)
                                readonly property real hr: Theme.sp(16)

                                function hitZone(mx, my) {
                                    if (!previewRect.hasRoi) return "none"
                                    var rx = previewRect.rX, ry = previewRect.rY
                                    var rw = previewRect.rW, rh = previewRect.rH
                                    var h  = hr
                                    if (Math.abs(mx - rx)      < h && Math.abs(my - ry)      < h) return "tl"
                                    if (Math.abs(mx - (rx+rw)) < h && Math.abs(my - ry)      < h) return "tr"
                                    if (Math.abs(mx - rx)      < h && Math.abs(my - (ry+rh)) < h) return "bl"
                                    if (Math.abs(mx - (rx+rw)) < h && Math.abs(my - (ry+rh)) < h) return "br"
                                    if (mx > rx && mx < rx+rw && my > ry && my < ry+rh)           return "move"
                                    return "none"
                                }

                                cursorShape: {
                                    switch (hitZone(mouseX, mouseY)) {
                                    case "tl": case "br": return Qt.SizeFDiagCursor
                                    case "tr": case "bl": return Qt.SizeBDiagCursor
                                    case "move":          return Qt.SizeAllCursor
                                    default:              return Qt.CrossCursor
                                    }
                                }

                                onPressed: (mouse) => {
                                    previewRect.roiDragMode   = hitZone(mouse.x, mouse.y)
                                    previewRect.roiDragStartX = mouse.x
                                    previewRect.roiDragStartY = mouse.y
                                    if (camRow.instance && previewRect.hasRoi) {
                                        var r = camRow.instance.cropRoi
                                        previewRect.roiOrigX = r.x
                                        previewRect.roiOrigY = r.y
                                        previewRect.roiOrigW = r.width
                                        previewRect.roiOrigH = r.height
                                    }
                                }

                                onPositionChanged: (mouse) => {
                                    if (previewRect.roiDragMode === "none") return
                                    if (!camRow.instance) return
                                    // All deltas are computed from press-start in normalised coords
                                    var dx = (mouse.x - previewRect.roiDragStartX) / previewRect.width
                                    var dy = (mouse.y - previewRect.roiDragStartY) / previewRect.height
                                    var ox = previewRect.roiOrigX, oy = previewRect.roiOrigY
                                    var ow = previewRect.roiOrigW, oh = previewRect.roiOrigH
                                    var nx, ny, nw, nh
                                    switch (previewRect.roiDragMode) {
                                    case "move":
                                        nx = Math.max(0, Math.min(1.0 - ow, ox + dx))
                                        ny = Math.max(0, Math.min(1.0 - oh, oy + dy))
                                        camRow.instance.setCropRoi(Qt.rect(nx, ny, ow, oh))
                                        break
                                    case "tl":
                                        nx = Math.max(0,   Math.min(ox + ow - 0.02, ox + dx))
                                        ny = Math.max(0,   Math.min(oy + oh - 0.02, oy + dy))
                                        nw = Math.max(0.02, ow - (nx - ox))
                                        nh = Math.max(0.02, oh - (ny - oy))
                                        camRow.instance.setCropRoi(Qt.rect(nx, ny, nw, nh))
                                        break
                                    case "tr":
                                        ny = Math.max(0,   Math.min(oy + oh - 0.02, oy + dy))
                                        nw = Math.max(0.02, Math.min(1.0 - ox, ow + dx))
                                        nh = Math.max(0.02, oh - (ny - oy))
                                        camRow.instance.setCropRoi(Qt.rect(ox, ny, nw, nh))
                                        break
                                    case "bl":
                                        nx = Math.max(0,   Math.min(ox + ow - 0.02, ox + dx))
                                        nw = Math.max(0.02, ow - (nx - ox))
                                        nh = Math.max(0.02, Math.min(1.0 - oy, oh + dy))
                                        camRow.instance.setCropRoi(Qt.rect(nx, oy, nw, nh))
                                        break
                                    case "br":
                                        nw = Math.max(0.02, Math.min(1.0 - ox, ow + dx))
                                        nh = Math.max(0.02, Math.min(1.0 - oy, oh + dy))
                                        camRow.instance.setCropRoi(Qt.rect(ox, oy, nw, nh))
                                        break
                                    }
                                }

                                onReleased: {
                                    // The one write for the whole gesture — see the
                                    // cropRoiChanged handler above for why it is not
                                    // per-pixel.  ⚠ AFTER clearing the mode, so the
                                    // handler that fires on the way through does not
                                    // skip it.
                                    const wasDragging = previewRect.roiDragMode !== "none"
                                    previewRect.roiDragMode = "none"
                                    if (wasDragging) previewRect.persistRoi()
                                }
                            }

                            // Live badge
                            Rectangle {
                                anchors.top:        parent.top
                                anchors.left:       parent.left
                                anchors.topMargin:  Theme.gap(8)
                                anchors.leftMargin: Theme.gap(8)
                                implicitWidth:  liveBadgeRow.implicitWidth + Theme.sp(16)
                                implicitHeight: Theme.sp(18)
                                color:  Qt.rgba(26/255, 74/255, 46/255, 0.9)
                                border.width: 1
                                border.color: Theme.colorGood
                                radius: 2
                                z: 25

                                Row {
                                    id: liveBadgeRow
                                    anchors.centerIn: parent
                                    spacing: Theme.gap(4)

                                    Rectangle {
                                        width:  Theme.sp(5); height: Theme.sp(5); radius: Theme.sp(3)
                                        color:  Theme.colorGood
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text:           qsTr("Live")
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color:          Theme.colorGood
                                    }
                                }
                            }

                            // FPS badge — hidden when fps is unknown (0)
                            Text {
                                anchors.top:         parent.top
                                anchors.right:       parent.right
                                anchors.topMargin:   Theme.gap(8)
                                anchors.rightMargin: Theme.gap(8)
                                readonly property double displayFps: {
                                    if (camRow.instance && camRow.instance.configuredFps > 0)
                                        return camRow.instance.configuredFps
                                    return camRow.camData.maxFps > 0 ? camRow.camData.maxFps : 0
                                }
                                visible: displayFps > 0
                                text:    (Math.round(displayFps * 10) / 10).toFixed(1) + qsTr(" fps")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Qt.rgba(1, 1, 1, 0.3)
                                z: 25
                            }
                        }

                        // Footer below preview
                        Rectangle {
                            width:  Theme.sp(340)
                            height: Theme.sp(26)
                            color:  Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorderMid
                            radius: 0

                            RowLayout {
                                anchors {
                                    fill:        parent
                                    leftMargin:  Theme.gap(10)
                                    rightMargin: Theme.gap(10)
                                }

                                Text {
                                    text: {
                                        var w = camRow.camData.maxWidth  || 0
                                        var h = camRow.camData.maxHeight || 0
                                        return w + " × " + h + " px (full)"
                                    }
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorText3
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: {
                                        if (!camRow.instance || camRow.instance.cropRoi.width <= 0)
                                            return ""
                                        var r  = camRow.instance.cropRoi
                                        var rw = Math.round(r.width  * (camRow.camData.maxWidth  || 0))
                                        var rh = Math.round(r.height * (camRow.camData.maxHeight || 0))
                                        return rw + " × " + rh + " px (ROI)"
                                    }
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorAccent
                                }
                            }
                        }
                    }

                    // ── Numeric inputs + storage ─────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap(10)
                        Layout.alignment: Qt.AlignTop

                        // Origin inputs
                        ColumnLayout {
                            spacing: Theme.gap(4)

                            PpMicro {
                                kind: "caps"
                                text: qsTr("Origin (px)")
                            }

                            RowLayout {
                                spacing: Theme.gap(6)

                                Text {
                                    text:  "X"
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: Theme.colorText3
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                CropField {
                                    id: xField
                                    text: camRow.instance ? Math.round(camRow.instance.cropRoi.x * (camRow.camData.maxWidth || 0)).toString() : "0"
                                    onEditingFinished: {
                                        if (!camRow.instance) return
                                        var val = parseInt(text) || 0
                                        var r   = camRow.instance.cropRoi
                                        var nx  = Math.max(0, Math.min(1.0 - r.width, val / (camRow.camData.maxWidth || 1)))
                                        camRow.instance.setCropRoi(Qt.rect(nx, r.y, r.width, r.height))
                                    }
                                }

                                Text {
                                    text:  "Y"
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: Theme.colorText3
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                CropField {
                                    id: yField
                                    text: camRow.instance ? Math.round(camRow.instance.cropRoi.y * (camRow.camData.maxHeight || 0)).toString() : "0"
                                    onEditingFinished: {
                                        if (!camRow.instance) return
                                        var val = parseInt(text) || 0
                                        var r   = camRow.instance.cropRoi
                                        var ny  = Math.max(0, Math.min(1.0 - r.height, val / (camRow.camData.maxHeight || 1)))
                                        camRow.instance.setCropRoi(Qt.rect(r.x, ny, r.width, r.height))
                                    }
                                }
                            }
                        }

                        // Size inputs
                        ColumnLayout {
                            spacing: Theme.gap(4)

                            PpMicro {
                                kind: "caps"
                                text: qsTr("Size (px)")
                            }

                            RowLayout {
                                spacing: Theme.gap(6)

                                Text {
                                    text:  "W"
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: Theme.colorText3
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                CropField {
                                    id: wField
                                    text: camRow.instance ? Math.round(camRow.instance.cropRoi.width  * (camRow.camData.maxWidth  || 0)).toString() : (camRow.camData.maxWidth  || 0).toString()
                                    onEditingFinished: {
                                        if (!camRow.instance) return
                                        var val = parseInt(text) || (camRow.camData.maxWidth || 0)
                                        var r   = camRow.instance.cropRoi
                                        var nw  = Math.max(0.01, Math.min(1.0 - r.x, val / (camRow.camData.maxWidth || 1)))
                                        camRow.instance.setCropRoi(Qt.rect(r.x, r.y, nw, r.height))
                                    }
                                }

                                Text {
                                    text:  "H"
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: Theme.colorText3
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                CropField {
                                    id: hField
                                    text: camRow.instance ? Math.round(camRow.instance.cropRoi.height * (camRow.camData.maxHeight || 0)).toString() : (camRow.camData.maxHeight || 0).toString()
                                    onEditingFinished: {
                                        if (!camRow.instance) return
                                        var val = parseInt(text) || (camRow.camData.maxHeight || 0)
                                        var r   = camRow.instance.cropRoi
                                        var nh  = Math.max(0.01, Math.min(1.0 - r.y, val / (camRow.camData.maxHeight || 1)))
                                        camRow.instance.setCropRoi(Qt.rect(r.x, r.y, r.width, nh))
                                    }
                                }
                            }
                        }

                        // Preset buttons
                        ColumnLayout {
                            spacing: Theme.gap(4)

                            PpMicro {
                                kind: "caps"
                                text: qsTr("Presets")
                            }

                            Row {
                                spacing: Theme.gap(4)

                                Repeater {
                                    model: [
                                        { label: qsTr("Full frame"),    action: "full"    },
                                        { label: qsTr("Default crop"),  action: "default" },
                                        { label: qsTr("16:9"),          action: "16:9"    }
                                    ]

                                    delegate: PpButton {
                                        id: presetChip
                                        required property var modelData

                                        style: "wash"; size: "compact"
                                        implicitHeight: Theme.sp(24)
                                        moves: false
                                        label: modelData.label

                                        onClicked: {
                                            if (!camRow.instance) return
                                            switch (modelData.action) {
                                            case "full":
                                                camRow.instance.setCropRoi(Qt.rect(0, 0, 1, 1))
                                                break
                                            case "default":
                                                camRow.instance.setCropRoi(Qt.rect(0.3, 0, 0.4, 1.0))
                                                break
                                            case "16:9": {
                                                var mw = camRow.camData.maxWidth  || 1
                                                var mh = camRow.camData.maxHeight || 1
                                                var h  = mw * 9.0 / 16.0 / mh
                                                var y  = (1.0 - h) / 2.0
                                                camRow.instance.setCropRoi(Qt.rect(0, y, 1.0, h))
                                                break
                                            }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Storage block
                        Rectangle {
                            id: storageRect
                            Layout.fillWidth: true
                            implicitHeight:   storageCol.implicitHeight + Theme.gap(24)
                            color:  Theme.colorBg2
                            border.width: 1
                            border.color: Theme.colorBorderStrong
                            radius: Theme.radius

                            // Slot sizing matches CameraInstance's ring buffer allocation exactly.
                            // slotBytesPerPixel = worst-case BPP (min 2), slotWidth/Height = max resolution.
                            readonly property int  slotBpp:   camRow.camData.slotBytesPerPixel || 2
                            readonly property int  slotW:     camRow.camData.slotWidth  || camRow.camData.maxWidth  || 0
                            readonly property int  slotH:     camRow.camData.slotHeight || camRow.camData.maxHeight || 0
                            readonly property real slotBytes: slotW * slotH * slotBpp

                            // Full-frame display values use the default (actual) format for the caption.
                            readonly property real bpp:       (camRow.camData.bitsPerPixel > 0 ? camRow.camData.bitsPerPixel : 8) / 8.0
                            readonly property real fullBytes: slotBytes  // slot = full frame at worst-case BPP

                            // ROI crop dimensions (normalised → pixels using default resolution for display).
                            readonly property real roiCropW:  camRow.instance && camRow.instance.cropRoi.width  > 0
                                                                ? camRow.instance.cropRoi.width  * (camRow.camData.maxWidth  || 0)
                                                                : (camRow.camData.maxWidth  || 0)
                            readonly property real roiCropH:  camRow.instance && camRow.instance.cropRoi.height > 0
                                                                ? camRow.instance.cropRoi.height * (camRow.camData.maxHeight || 0)
                                                                : (camRow.camData.maxHeight || 0)
                            // ROI bytes uses the slot BPP so the saving is relative to the allocated slot.
                            readonly property real roiBytes:  roiCropW * roiCropH * slotBpp
                            readonly property int  framePct:  fullBytes > 0 ? Math.round(roiBytes / fullBytes * 100) : 100

                            // Slot count mirrors SourceRing: nextPow2(ceil(fps × 5)).
                            readonly property double slotFps: (camRow.camData.slotFps > 0 ? camRow.camData.slotFps : camRow.currentFps) || 60
                            readonly property int    slotCount: {
                                var n = Math.ceil(slotFps * 5.0)
                                if (n <= 0) return 1
                                --n
                                n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16
                                return n + 1
                            }
                            readonly property real   ringBytes:     roiBytes  * slotCount
                            readonly property real   ringFullBytes: fullBytes * slotCount
                            readonly property int    ringPct:       ringFullBytes > 0 ? Math.round(ringBytes / ringFullBytes * 100) : 100

                            ColumnLayout {
                                id: storageCol
                                anchors {
                                    fill:        parent
                                    margins:     Theme.gap(12)
                                }
                                spacing: Theme.gap(10)

                                // Frame storage bar
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.gap(4)

                                    RowLayout {
                                        Layout.fillWidth: true
                                        PpMicro {
                                            kind:             "caps"
                                            text:             qsTr("Frame storage (ROI vs full frame)")
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text:           storageRect.framePct + "%"
                                            font.family:    Theme.fontData
                                            font.pixelSize: Theme.fontSzMicro
                                            color:          Theme.colorAccent
                                        }
                                    }

                                    Text {
                                        text: {
                                            var roiMb  = (storageRect.roiBytes  / 1048576).toFixed(2)
                                            var fullMb = (storageRect.fullBytes / 1048576).toFixed(2)
                                            return qsTr("ROI: ") + roiMb + qsTr(" MB/frame  (full frame: ") + fullMb + qsTr(" MB)")
                                        }
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color:          Theme.colorText2
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: Theme.sp(4)
                                        radius: Theme.sp(2)
                                        color:  Theme.colorBg3

                                        Rectangle {
                                            width:  parent.width * Math.min(1, storageRect.framePct / 100.0)
                                            height: parent.height
                                            radius: parent.radius
                                            color:  Theme.colorAccent
                                            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                                        }
                                    }
                                }

                                // Ring buffer bar
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.gap(4)

                                    RowLayout {
                                        Layout.fillWidth: true
                                        PpMicro {
                                            kind:             "caps"
                                            text:             qsTr("Ring buffer  ")
                                                              + storageRect.slotCount
                                                              + qsTr(" slots  (")
                                                              + Math.round(storageRect.slotFps)
                                                              + qsTr(" fps × 5 s → nextPow2)")
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text:           storageRect.ringPct + "%"
                                            font.family:    Theme.fontData
                                            font.pixelSize: Theme.fontSzMicro
                                            color:          Theme.colorGood
                                        }
                                    }

                                    Text {
                                        text: {
                                            var frames   = storageRect.slotCount
                                            var roiMb    = (storageRect.roiBytes     / 1048576).toFixed(2)
                                            var ringMb   = (storageRect.ringBytes    / 1048576).toFixed(0)
                                            var ringFull = (storageRect.ringFullBytes / 1048576).toFixed(0)
                                            return frames + qsTr(" slots × ") + roiMb + qsTr(" MB = ") + ringMb + qsTr(" MB  (full: ") + ringFull + qsTr(" MB)")
                                        }
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color:          Theme.colorText2
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: Theme.sp(4)
                                        radius: Theme.sp(2)
                                        color:  Theme.colorBg3

                                        Rectangle {
                                            width:  parent.width * Math.min(1, storageRect.ringPct / 100.0)
                                            height: parent.height
                                            radius: parent.radius
                                            color:  Theme.colorGood
                                            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                                        }
                                    }
                                }
                            }
                        }

                        // How the crop is applied
                        Text {
                            text: qsTr("Crop is applied when the camera connects — in hardware where the sensor supports it, otherwise on arriving frames. The preview always shows the full sensor; changes take effect on the next connect.")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            font.italic:    true
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                            Layout.fillWidth: true
                        }

                        // Reset / done buttons
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap(6)

                            PpButton {
                                id: resetBtn
                                style: "wash"; size: "compact"
                                implicitHeight: Theme.sp(26)
                                label: qsTr("Reset to full frame")

                                onClicked: {
                                    if (camRow.instance)
                                        camRow.instance.setCropRoi(Qt.rect(0, 0, 1, 1))
                                }
                            }

                            Item { Layout.fillWidth: true }

                            PpButton {
                                id: roiDoneBtn
                                style: "tonal"; tone: "accent"; size: "compact"
                                implicitHeight: Theme.sp(26)
                                horizontalPadding: Theme.sp(24)
                                label: qsTr("Done")

                                onClicked:    root.openRoiIndex = -1
                            }
                        }
                    }
                }
            }
        }

        // ── Ball detection panel — hitting area + calibration (design §8.1) ──
        Item {
            id: ballPanel
            anchors.top:   roiPanel.bottom
            anchors.left:  parent.left
            anchors.right: parent.right
            height: camRow.ballOpen ? ballPanelContent.implicitHeight : 0
            clip: true
            visible: camRow.ballOpen

            Behavior on height { NumberAnimation { duration: Theme.durationFast } }

            readonly property bool fixedInPlace:
                appSettings.cameraFixedInPlace[camRow.camData.cameraKey] === true

            ColumnLayout {
                id: ballPanelContent
                anchors.left:  parent.left
                anchors.right: parent.right
                spacing: Theme.gap(12)

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color:  Theme.colorBorderMid
                    opacity: Theme.borderOpacityNormal
                }

                // Camera not connected — everything here needs live frames.
                Text {
                    visible: camRow.realInstance === null
                    Layout.fillWidth: true
                    Layout.margins: Theme.gap(16)
                    text: qsTr("Connecting the camera\u2026 If this persists, check the camera is not in use elsewhere.")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText2
                }

                RowLayout {
                    visible: camRow.realInstance !== null
                    Layout.fillWidth: true
                    Layout.margins: Theme.gap(16)
                    Layout.topMargin: Theme.gap(4)
                    Layout.bottomMargin: Theme.gap(28)
                    spacing: Theme.gap(16)

                    // Live view with the hitting-area overlay; drag to draw.
                    // Loader-gated so a collapsed panel never subscribes to
                    // the video stream.
                    Loader {
                        id: ballFrameLoader
                        Layout.preferredWidth: Theme.sp(450)
                        Layout.preferredHeight: Theme.sp(285)
                        Layout.alignment: Qt.AlignTop
                        active: camRow.ballOpen && camRow.realInstance !== null
                        // Seed a default hitting area the first time the
                        // editor opens, so a manipulable rect is there
                        // immediately (crop-editor pattern). Never touches a
                        // calibrated camera (those always have an area).
                        onLoaded: {
                            var inst = camRow.realInstance
                            if (inst && inst.roi.width <= 0)
                                cameraManager.setBallRoi(inst, Qt.rect(0.40, 0.55, 0.20, 0.30))
                        }
                        sourceComponent: PpCameraFrame {
                            instance: camRow.realInstance
                            displayName: camRow.camData.alias || camRow.camData.description
                            showPoseOverlay:      false
                            showHittingArea:      true
                            roiEditable:          true
                            showPerspectiveBadge: false
                            showStatsOverlay:     false
                            showReplayOverlay:    false
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.gap(8)

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Place the hitting area over where the ball sits at address: drag the rectangle to move it, drag a corner to resize, or drag outside it to draw a new one. Then empty the mat and press Learn.")
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText2
                        }

                        Text {
                            visible: !ballPanel.fixedInPlace
                            Layout.fillWidth: true
                            text: qsTr("This camera isn't marked fixed in place — the hitting area will not be restored next session.")
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            font.italic: true
                            color: Theme.colorWarn
                        }

                        // v2 temporal detector (Option A): learn the empty-mat
                        // baseline, then a placed ball is detected live in the
                        // hitting-area overlay above. Empty the mat, click, then
                        // place a ball. (The start-session wizard has the guided
                        // version of this flow.)
                        Text {
                            visible: camRow.realInstance !== null
                            text: qsTr("→ Learn hitting area (empty the mat, then place a ball)")
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorAccent
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: cameraManager.relearnBallBaseline(camRow.realInstance)
                            }
                        }
                    }
                }
            }
        }
    }

    // Informational notice shown when opening the crop editor stops an
    // active capture / disconnects the camera. Lives on the panel root so it
    // survives the Repeater delegate rebuild triggered by setSelected() —
    // parented to the panel itself, over the scroll view, not laid out in
    // its content column.
    PpToast {
        id: cropToast
        parent: root
        showUndo: false
        glyph: "⚠"
        z: 1000
        margin: Theme.gap(24)
    }

    // ── Page header ────────────────────────────────────────────────
    PpPageHeader {
        Layout.fillWidth: true
        spacing:     root.contentSpacing
        eyebrow:     Theme.caps(qsTr("Hardware"))
        title:       qsTr("Cameras")
        description: qsTr("All detected cameras are listed below. Enable each device, assign it to a view, configure capture parameters, and optionally define a crop region to reduce frame storage and ring buffer size.")
    }

    // ── Detected cameras section ───────────────────────────────────
    RowLayout {
        Layout.fillWidth: true

        PpMicro {
            kind: "body"
            text: Theme.caps(qsTr("Detected cameras"))
            Layout.fillWidth: true
        }

        PpButton {
            label: qsTr("Refresh")
            onClicked: cameraManager.enumerate()
        }
    }

    // Camera rows
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.gap(8)

        Repeater {
            model: cameraManager.cameraList

            delegate: CameraDeviceRow {
                required property var modelData
                camData: modelData
                Layout.fillWidth: true
            }
        }

        // Empty state
        PpCardNote {
            kind:             "aside"
            wrapMode:         Text.NoWrap
            visible:          cameraManager.cameraList.length === 0
            text:             qsTr("No cameras detected. Connect a camera and click Refresh.")
            Layout.fillWidth: true
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Global capture section ─────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Global capture")) }

    // Pre-roll buffer row
    SettingsRow {
        objectName: "setting_preroll"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle {
                title:    qsTr("Pre-roll buffer")
                subtitle: qsTr("Seconds of frames held before swing trigger — directly sets ring buffer size above")
            }

            // Chip group for preroll values
            Row {
                spacing: Theme.gap(4)
                Layout.alignment: Qt.AlignVCenter

                readonly property var prerollOptions: [
                    { label: qsTr("0.5 s"), value: 0.5 },
                    { label: qsTr("1.0 s"), value: 1.0 },
                    { label: qsTr("2.0 s"), value: 2.0 }
                ]

                Repeater {
                    model: parent.prerollOptions

                    delegate: PpChoiceChip {
                        id: prerollChip
                        required property var modelData
                        style:     "mono"
                        label:     modelData.label
                        selected:  Math.abs(appSettings.cameraPreroll - modelData.value) < 0.01
                        onClicked: appSettings.cameraPreroll = modelData.value
                    }
                }
            }
        }
    }

    // Synchronise cameras row
    SettingsRow {
        objectName: "setting_camSync"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)

            SettingsRowTitle {
                title:    qsTr("Synchronise cameras")
                subtitle: qsTr("Lock frame timing across all enabled cameras")
            }

            PpSwitch {
                checked: appSettings.cameraSyncEnabled
                onToggled: (v) => appSettings.cameraSyncEnabled = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
