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

// Session setup — Framing. One live frame per connected camera with the pose skeleton over it, and
// a verdict per camera on the headroom above the hands at the top of the backswing.
//
// Why: on 5 Oct 2026 the down-the-line frame put the hands at the top only 44–113 px below the top
// edge of a 988 px frame (4–11 %); the club left the picture from just after P3 to about P5 on all
// 31 swings and the 3-D club was unmeasured there. The rule (two_camera_capture_protocol.md §2,
// "headroom check"): at the top the wrists sit at least one club length below the top edge —
// roughly the lower two-thirds of the frame — and the clubhead is in view at the top and the
// finish, in every camera. MoveNet sees wrists, not the clubhead, so the page checks the wrists
// and the intro asks the golfer to look for the clubhead.
//
// Never gates Continue: a camera that cannot be moved still records, and the golfer decides.
// Pose is turned on per instance for the visit and put back on leave (the Capture view's Motion
// overlay owns it otherwise). Hardware through `ctx` only (lint W4).
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: framingCol.implicitHeight + Theme.sp(32)

    // ── The rule ─────────────────────────────────────────────────────────────────────────────────
    // A wrist or shoulder counts at MoveNet score ≥ 0.3 (the overlay draws from 0.25).
    readonly property real minScore:    0.3
    // The highest wrist must sit at least this far down the frame at the top: the lower
    // two-thirds. 5 Oct's DTL frame was at 0.04–0.11.
    readonly property real minHeadroom: 0.33
    // The top is held for a moment, not a frame: the verdict is the smallest wrist height of the
    // top samples in the last ~2 s, so one high frame of a held top decides it.
    readonly property int  windowMs:    2000

    // cameraKey → { state: "hold" | "ok" | "tight", topY: the window's smallest wrist y (0..1) }.
    // Reassigned (never mutated) so the rows and the hint re-evaluate.
    property var verdicts: ({})
    // cameraKey → [{ t, y }] — top samples inside the window. Read only by _ingest; not bound.
    property var _rings: ({})
    // cameraKey → { inst, was } — pose state found on enter, put back on leave.
    property var _savedPose: ({})

    function verdictOf(key) { var v = page.verdicts[key]; return v ? v.state : "hold" }
    function verdictText(key) {
        var s = page.verdictOf(key)
        if (s === "ok")    return qsTr("Headroom OK")
        if (s === "tight") return qsTr("Too tight — tilt the camera up or move it back")
        return qsTr("Hold the top so the camera can check")
    }
    function cameraLabel(c) {
        var name = c.alias || c.description
        if (c.perspective === CameraInstance.FaceOn)      return qsTr("Face-on camera — %1").arg(name)
        if (c.perspective === CameraInstance.DownTheLine) return qsTr("Down-the-line camera — %1").arg(name)
        return qsTr("Camera — %1").arg(name)
    }

    // One pose result for one camera. MoveNet's 17 COCO keypoints as { x, y, score }, x and y
    // normalised 0..1 to the frame the overlay draws on (camera_instance.cpp onPoseEstimated);
    // y grows downward. 5/6 shoulders, 9/10 wrists.
    function _ingest(key, kps, nowMs) {
        if (!kps || kps.length < 17) return
        var lw = kps[9], rw = kps[10]
        if (lw.score < page.minScore || rw.score < page.minScore) return
        var sh = []
        if (kps[5].score >= page.minScore) sh.push(kps[5].y)
        if (kps[6].score >= page.minScore) sh.push(kps[6].y)
        if (sh.length === 0) return
        // A top: both wrists above the higher shoulder. Address, impact and most of the finish
        // have the hands below it and say nothing about headroom.
        if (Math.max(lw.y, rw.y) >= Math.min.apply(null, sh)) return

        var ring = (page._rings[key] || []).filter(function(s) { return nowMs - s.t <= page.windowMs })
        ring.push({ t: nowMs, y: Math.min(lw.y, rw.y) })
        // Written back whole: a `var` read is not promised to be the stored object.
        var rings = page._rings
        rings[key] = ring
        page._rings = rings
        var topY = 1
        for (var i = 0; i < ring.length; ++i) topY = Math.min(topY, ring[i].y)
        var state = topY >= page.minHeadroom ? "ok" : "tight"

        var old = page.verdicts[key]
        var pct = Math.round(topY * 100)
        if (old && old.state === state && Math.round(old.topY * 100) === pct) return
        var v = {}
        for (var k in page.verdicts) v[k] = page.verdicts[k]
        v[key] = { state: state, topY: topY }
        page.verdicts = v
        if (!old || old.state !== state)
            appLog.info("Setup", "framing camera=" + key + " verdict=" + state + " topWristY=" + topY.toFixed(3))
    }

    // Pose on for every camera on the page that is not already noted; a camera that connects
    // while the page is up is picked up through framingCamerasChanged.
    function _poseOn() {
        var cams = page.ctx.framingCameras
        var saved = page._savedPose
        for (var i = 0; i < cams.length; ++i) {
            var inst = cams[i].instance
            if (!inst || saved[cams[i].cameraKey] !== undefined) continue
            saved[cams[i].cameraKey] = { inst: inst, was: page.ctx.cameraPoseEnabled(inst) }
            page.ctx.setCameraPoseEnabled(inst, true)
        }
        page._savedPose = saved
    }
    function _poseRestore() {
        var saved = page._savedPose
        for (var k in saved) {
            // An instance torn down while the page was up (the camera disconnected) has nothing
            // to put back.
            try { page.ctx.setCameraPoseEnabled(saved[k].inst, saved[k].was) } catch (e) {}
        }
        page._savedPose = ({})
    }

    function enter(direction) { page._poseOn() }
    // Every exit, the suspend for Settings included: pose is put back while the page is not shown,
    // and turned on again by enter("resume").
    function leave(reason) { page._poseRestore() }

    canContinue: true
    readonly property var _states: {
        var cams = page.ctx.framingCameras, out = []
        for (var i = 0; i < cams.length; ++i) out.push(page.verdictOf(cams[i].cameraKey))
        return out
    }
    readonly property int _tight: _states.filter(function(s) { return s === "tight" }).length
    readonly property int _ok:    _states.filter(function(s) { return s === "ok" }).length
    hint: _states.length === 0 ? qsTr("Waiting for the cameras")
        : _tight > 0           ? (_states.length === 1 ? qsTr("Too tight — tilt the camera up or move it back")
                                                       : qsTr("Too tight on %1 of %2 cameras — tilt them up or move them back")
                                                             .arg(_tight).arg(_states.length))
        : _ok === _states.length ? (_states.length === 1 ? qsTr("Headroom OK")
                                                         : qsTr("Headroom OK on every camera"))
        : qsTr("Hold the top of your backswing so the cameras can check")
    hintTone: _tight > 0 ? "warn" : (_states.length > 0 && _ok === _states.length ? "good" : "neutral")

    Connections {
        target:  page.ctx
        enabled: page.active                            // R3
        function onFramingCamerasChanged() { page._poseOn() }
    }

    Column {
        id: framingCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
        spacing: Theme.sp(16)

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("Frame the whole swing")
            body:    qsTr("Hold your club at the top of your backswing, then at your finish. Every camera must see your hands below the top third of its frame and the clubhead in the picture.")
        }

        Repeater {
            model: page.ctx.framingCameras

            delegate: RowLayout {
                id: camRow
                required property var modelData
                readonly property string cameraKey: modelData.cameraKey
                readonly property QtObject instance: modelData.instance
                readonly property string verdict: page.verdictOf(cameraKey)
                readonly property string verdictText: page.verdictText(cameraKey)
                objectName: "framingCamera_" + cameraKey

                width: framingCol.width
                spacing: Theme.sp(20)

                Connections {
                    target:  camRow.instance
                    enabled: page.active && camRow.instance !== null      // R3
                    function onPoseKeypointsChanged() {
                        page._ingest(camRow.cameraKey, camRow.instance.poseKeypoints, Date.now())
                    }
                }

                Loader {
                    Layout.preferredWidth:  Theme.sp(420)
                    Layout.preferredHeight: Theme.sp(270)
                    Layout.alignment:       Qt.AlignTop
                    // Only live while this page is active (R3), as the Ball frame.
                    active: page.active && camRow.instance !== null
                    sourceComponent: PpCameraFrame {
                        instance:             camRow.instance
                        showPoseOverlay:      true
                        showHittingArea:      false
                        showPerspectiveBadge: false
                        showStatsOverlay:     false
                        showReplayOverlay:    false
                    }
                }

                SetupCheckRow {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    label:    page.cameraLabel(camRow.modelData)
                    ok:       camRow.verdict === "ok"
                    warn:     camRow.verdict === "tight"
                    // Not yet seen at the top is not a failure: a muted row, not a red one.
                    optional: true
                    readonly property string _pct: page.verdicts[camRow.cameraKey]
                        ? qsTr(" · hands %1% down the frame at the top")
                              .arg(Math.round(page.verdicts[camRow.cameraKey].topY * 100))
                        : ""
                    subOk:   camRow.verdictText + _pct
                    subWarn: camRow.verdictText + _pct
                    subFail: camRow.instance === null ? qsTr("Waiting for the camera's picture")
                                                      : camRow.verdictText
                }
            }
        }
    }
}
