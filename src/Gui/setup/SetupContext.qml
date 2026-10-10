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

// The hardware, as session setup sees it (session_wizard_refactor_design.md §4.8, R6). The ONE
// object in the flow that touches the managers: descriptors, the flow and the pages read the
// derivations below and call the actions below, and never name cameraManager or imuManager
// (lint W4 for pages). This fixes F12: the slot → device → unit lookup was re-derived about ten
// times in the retired ScreenSessionWizard.qml, each copy with its own hand-placed dependency reads, and one
// missing read made a binding go stale.
//
// ⚠ REACTIVE DEPENDENCIES ARE WRITTEN ONCE, HERE. The role resolvers (deviceIdForRole, roleForDevice,
// instanceForRole …) are Q_INVOKABLEs and not reactive on their own, so every binding that calls
// them first ASSIGNS from the properties that change their answer: imuDeviceList (enumeration and
// session enablement), instances (connection — the real manager re-emits instancesChanged on every
// imuConnected change, imu_manager.cpp createInstance) and appSettings.imuRoles (placement).
// They are assigned to a local, never written as bare statements: a bare `a.b` line is dropped
// by the QML compiler and the dependency silently goes with it (memory note
// qml-dead-statement-bindings).
//
// Inputs default to the app's context properties and are injectable, so the tests build this over
// fakes without a child context if they want to.
//
// Line references "wizard l.N" are to the retired ScreenSessionWizard.qml (deleted at Stage 5c; in
// git history) as of Stage 3; each derivation
// mirrors the one quoted, except where a comment says why not (§4.12: nothing is "required").
import QtQuick
import PinPointStudio
import "setup_capability_rows.js" as CapRows

QtObject {
    id: ctx

    // ── Inputs ───────────────────────────────────────────────────────────────────────────────────
    property var cameraMgr: cameraManager
    property var imuMgr:    imuManager
    property var settings:  appSettings
    property var athlete:   athleteController
    property var launchMon: launchMonitor
    // The visit's draft, for setupFacts (calibration outcomes). The host wires it.
    property var draft: null
    // The live wrist-angle computer (LiveWristAngles) the Check page switches on and reads. The
    // SHELL wires it to the app's `liveWrist` context property: written here, the name would
    // resolve to this very property (scope first) and never reach the context one.
    // DECISION(stage5b): wired by the host, not defaulted here, for that reason; null in a host
    // that has no Check page (the engine tests' FlowShell).
    property var liveWrist: null

    // ScreenSettings sub-panel indices for the issues' "→ Open … settings" links (wizard l.37–38).
    readonly property int settingsPanelCameras: 3
    readonly property int settingsPanelImus:    4

    // ── Presets: what the session type still picks (§4.12) ──────────────────────────────────────
    // A preset picks the goals list, the rail screen Start lands on and the type handed to
    // sessionController.start — and nothing else. No step's applies or gate reads it (lint W8).
    // Copied from the wizard's sessionTypes + goalDefsByType (l.247–256, 294–333), strings
    // unchanged, indexed by SessionController.Type (0 Swing, 1 Wrist, 2 Grf, 3 Coach).
    //
    // Dropped on purpose:
    //   - sessionTypes[].requiredCameras (Swing 2, Wrist 1, Grf 2, Coach 2) and requiredImus
    //     (3, 2, 3, 3): nothing is "required" any more (§4.12). requiredCameras only ever changed
    //     anything for the three comingSoon types, which Home never opens (ScreenHome.qml:30),
    //     so for every session that can actually start the derivations below are today's.
    //   - imuRequirements (wizard l.264–289) — the per-type slot table. What it said, for the
    //     record (Stage 6 re-baselines Ready's golden tables R1/R3 on purpose against it):
    //       Swing:  A Thorax (req), B Lumbar spine (req), C T12 junction (req)
    //       Wrist:  A Forearm (req), B Hand (req), C Upper arm (optional)
    //       Grf:    A Lead thigh (req), B Trail thigh (req), C Lumbar spine (req)
    //       Coach:  as Swing.
    //     Its job is now done by roles (ImuMounts) and instrument groups (`groups` below): the
    //     Wrist row is the arm group's minimum (leadForearm + leadHand) plus the optional
    //     leadUpperArm; the others named sensors no analysis reads and are not carried over.
    readonly property var presets: [
        { icon: "◑", name: qsTr("Swing Analysis"), railIndex: 1, comingSoon: true,
          goals: [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: Theme.caps(qsTr("Check where i'm at · identify areas to improve")) },
            { key: "kinematicSequence", name: qsTr("Kinematic sequence"),  sub: Theme.caps(qsTr("Segment velocity order"))    },
            { key: "xFactor",           name: qsTr("X-factor"),            sub: qsTr("HIP–SHOULDER SEPARATION")   },
            { key: "swingTempo",        name: qsTr("Swing tempo"),          sub: qsTr("BACK : DOWN RATIO")         },
            { key: "earlyExtension",    name: qsTr("Early extension"),      sub: Theme.caps(qsTr("Hip sway detection"))        },
            { key: "clubPath",          name: qsTr("Club path"),            sub: Theme.caps(qsTr("In-out trend"))              },
            { key: "wristAngles",       name: qsTr("Wrist angles"),         sub: Theme.caps(qsTr("Flexion at impact"))         }
          ] },
        { icon: "⌖", name: qsTr("Wrist Motion"), railIndex: 2, comingSoon: false,
          goals: [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: Theme.caps(qsTr("Check where i'm at · identify areas to improve")) },
            { key: "wristAngleTop",       name: qsTr("Wrist angle at the top"),    sub: Theme.caps(qsTr("Flat / bowed / cupped at top"))     },
            { key: "impactConditions",    name: qsTr("Impact conditions"),         sub: Theme.caps(qsTr("Flexion / extension at contact"))   },
            { key: "wristAngleSequence",  name: qsTr("Wrist angle sequence"),      sub: Theme.caps(qsTr("Transition & arc profile"))         },
            { key: "trailWristExtension", name: qsTr("Trail wrist extension"),     sub: Theme.caps(qsTr("Scoop / flip detection"))           }
          ] },
        { icon: "⇅", name: qsTr("Ground Forces"), railIndex: 3, comingSoon: true,
          goals: [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: Theme.caps(qsTr("Check where i'm at · identify areas to improve")) },
            { key: "kinematicSequence", name: qsTr("Kinematic sequence"),  sub: Theme.caps(qsTr("Segment velocity order"))    },
            { key: "xFactor",           name: qsTr("X-factor"),            sub: qsTr("HIP–SHOULDER SEPARATION")   },
            { key: "swingTempo",        name: qsTr("Swing tempo"),          sub: qsTr("BACK : DOWN RATIO")         },
            { key: "earlyExtension",    name: qsTr("Early extension"),      sub: Theme.caps(qsTr("Hip sway detection"))        },
            { key: "clubPath",          name: qsTr("Club path"),            sub: Theme.caps(qsTr("In-out trend"))              },
            { key: "wristAngles",       name: qsTr("Wrist angles"),         sub: Theme.caps(qsTr("Flexion at impact"))         }
          ] },
        { icon: "✦", name: qsTr("AI Coach"), railIndex: 4, comingSoon: true,
          goals: [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: Theme.caps(qsTr("Check where i'm at · identify areas to improve")) },
            { key: "kinematicSequence", name: qsTr("Kinematic sequence"),  sub: Theme.caps(qsTr("Segment velocity order"))    },
            { key: "xFactor",           name: qsTr("X-factor"),            sub: qsTr("HIP–SHOULDER SEPARATION")   },
            { key: "swingTempo",        name: qsTr("Swing tempo"),          sub: qsTr("BACK : DOWN RATIO")         },
            { key: "earlyExtension",    name: qsTr("Early extension"),      sub: Theme.caps(qsTr("Hip sway detection"))        },
            { key: "clubPath",          name: qsTr("Club path"),            sub: Theme.caps(qsTr("In-out trend"))              },
            { key: "wristAngles",       name: qsTr("Wrist angles"),         sub: Theme.caps(qsTr("Flexion at impact"))         }
          ] }
    ]

    // ── Athlete ──────────────────────────────────────────────────────────────────────────────────
    // The shell header reads the name (wizard l.648); routines read handedness.
    readonly property string athleteName: athlete.currentName
    readonly property bool   leftHanded:  athlete.currentHandedness === "Left"

    // ── Cameras ──────────────────────────────────────────────────────────────────────────────────
    // cameraList fires cameraListChanged on selection AND session-enablement changes, so every
    // camera derivation depends on it alone (plus `instances` for the live objects).
    readonly property var cameraList: cameraMgr.cameraList

    // Every cameraList entry per perspective — any number may share one (wizard l.376–388).
    readonly property var faceOn: {
        var out = [], list = ctx.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].perspective === CameraInstance.FaceOn) out.push(list[i])
        return out
    }
    readonly property var dtl: {
        var out = [], list = ctx.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].perspective === CameraInstance.DownTheLine) out.push(list[i])
        return out
    }
    // Unassigned, Other and Impact cameras: they record, analysis does not read them (l.968–972).
    readonly property var others: {
        var out = [], list = ctx.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].perspective !== CameraInstance.FaceOn
                    && list[i].perspective !== CameraInstance.DownTheLine)
                out.push(list[i])
        return out
    }
    // Every available camera takes part in the session (l.395).
    readonly property var sessionCameras: cameraList

    // A face-on AND a down-the-line camera are SELECTED — the pair Triangulate is for (l.88–97).
    readonly property bool hasFaceOnAndDtlSelected: {
        var fo = false, dl = false, list = ctx.cameraList
        for (var i = 0; i < list.length; ++i) {
            if (!list[i].selected) continue
            if (list[i].perspective === CameraInstance.FaceOn)           fo = true
            else if (list[i].perspective === CameraInstance.DownTheLine) dl = true
        }
        return fo && dl
    }

    // Every ENABLED camera is connected and at least one face-on is (l.420–432). Today's extra
    // clause, "≥1 down-the-line when the type requires two cameras", is dropped (§4.12); it only
    // ever applied to the comingSoon types.
    readonly property bool camsAllConnected: {
        var cams = ctx.cameraList, fo = 0
        for (var i = 0; i < cams.length; ++i) {
            if (!cams[i].sessionEnabled) continue
            if (!cams[i].selected) return false
            if (cams[i].perspective === CameraInstance.FaceOn) ++fo
        }
        return fo > 0
    }
    // Something for Connect to do: an enabled camera not yet selected (camsCol.canConnect, l.932).
    readonly property bool anyCameraToConnect: {
        var cams = ctx.cameraList
        for (var i = 0; i < cams.length; ++i)
            if (cams[i].sessionEnabled && !cams[i].selected) return true
        return false
    }
    readonly property bool anyCameraEnabled: {
        var cams = ctx.cameraList
        for (var i = 0; i < cams.length; ++i) if (cams[i].sessionEnabled) return true
        return false
    }
    // Enabled and connected, the count Ready's camera row reports (l.1964–1966).
    readonly property int connectedCameraCount: {
        var cams = ctx.cameraList, n = 0
        for (var i = 0; i < cams.length; ++i) if (cams[i].sessionEnabled && cams[i].selected) ++n
        return n
    }
    readonly property bool camerasConnecting: cameraMgr.anyConnecting   // l.2232

    // A face-on / DTL camera enabled and connected: what the session will record (setupFacts).
    readonly property bool faceOnConnected: {
        var fo = ctx.faceOn
        for (var i = 0; i < fo.length; ++i) if (fo[i].sessionEnabled && fo[i].selected) return true
        return false
    }
    readonly property bool dtlConnected: {
        var d = ctx.dtl
        for (var i = 0; i < d.length; ++i) if (d[i].sessionEnabled && d[i].selected) return true
        return false
    }

    // One of the triangulation pair's cameras is fixed in place: stereo calibration and
    // triangulation become optional (l.408–414).
    readonly property bool anyFixedCamera: {
        var fixed = settings.cameraFixedInPlace
        var cams  = ctx.faceOn.concat(ctx.dtl)
        for (var i = 0; i < cams.length; ++i)
            if (fixed[cams[i].cameraKey]) return true
        return false
    }
    // ⚠ STUBS, kept as today (D7): nothing on the camera manager reports these yet, so every
    // non-fixed two-camera setup reads "not confirmed" (F11; wizard l.342–343).
    readonly property bool stereoCalibrationValid: false
    readonly property bool triangulationValid:     false

    // The first CONNECTED face-on camera's live instance, whose ballPresent gates Ball (l.103–114).
    readonly property QtObject ballInstance: {
        var insts = cameraMgr.instances
        var fo = ctx.faceOn
        for (var i = 0; i < fo.length; ++i)
            for (var j = 0; j < insts.length; ++j)
                if (insts[j].cameraKey === fo[i].cameraKey)
                    return insts[j]
        return null
    }
    readonly property bool ballPresent: ballInstance !== null && ballInstance.ballPresent

    // The live instance for a cameraList entry's key (row thumbnails, l.1023–1030). Not reactive
    // on its own: a binding calling it must read `cameraInstances` first.
    readonly property var cameraInstances: cameraMgr.instances
    function cameraInstanceFor(cameraKey) {
        var insts = cameraMgr.instances
        for (var i = 0; i < insts.length; ++i)
            if (insts[i].cameraKey === cameraKey) return insts[i]
        return null
    }

    // The cameras the Framing step checks: every ENABLED, CONNECTED camera that sees the golfer —
    // not the impact camera, which films a 240-row strip of club and ball with no body in it and
    // never runs pose (camera_manager.cpp createController). Each entry is the cameraList entry's
    // fields plus `instance`, the live CameraInstance (null until the pipeline has made one).
    readonly property var framingCameras: {
        var cams = ctx.cameraList, insts = cameraMgr.instances, out = []
        for (var i = 0; i < cams.length; ++i) {
            var c = cams[i]
            if (!c.sessionEnabled || !c.selected || c.perspective === CameraInstance.Impact) continue
            var inst = null
            for (var j = 0; j < insts.length; ++j)
                if (insts[j].cameraKey === c.cameraKey) { inst = insts[j]; break }
            out.push({ cameraKey: c.cameraKey, alias: c.alias || "", description: c.description || "",
                       perspective: c.perspective, instance: inst })
        }
        return out
    }

    // Live pose on one camera's pipeline, for the Framing step. The app's own switch is the
    // Capture view's Motion overlay (Main.qml binds cameraManager.livePoseEnabled to it, and a
    // new instance takes that value); the page turns pose on per instance for its visit and puts
    // back what it found, so the Capture switch is never written.
    function cameraPoseEnabled(inst)     { return inst !== null && inst !== undefined && inst.poseEnabled === true }
    function setCameraPoseEnabled(inst, on) { if (inst) inst.poseEnabled = on }

    // ── Camera actions ───────────────────────────────────────────────────────────────────────────
    // Connect every enabled, unselected camera, then start the pipelines (camsCol.startConnect,
    // l.944–951). Cameras connect synchronously; no pacing.
    function connectCameras() {
        var cams = cameraList
        for (var i = 0; i < cams.length; ++i)
            if (cams[i].sessionEnabled && !cams[i].selected)
                cameraMgr.setSelected(cams[i].index, true)
        if (!cameraMgr.isRecording && cameraMgr.anySelected)
            cameraMgr.startAll()
    }
    // Session-local, manager-owned (shared with the toolbar); disabling disconnects (l.1043).
    function setCameraEnabled(cameraKey, on) { cameraMgr.setSessionCameraEnabled(cameraKey, on) }
    // The Ball step's default hitting area when none is set (l.1203–1207).
    function ensureBallRoi() {
        var inst = ballInstance
        if (inst && inst.roi.width <= 0)
            cameraMgr.setBallRoi(inst, Qt.rect(0.40, 0.55, 0.20, 0.30))
    }
    function relearnBallBaseline() { cameraMgr.relearnBallBaseline(ballInstance) }   // l.1245

    // ── Sensors ──────────────────────────────────────────────────────────────────────────────────
    readonly property var groupOfRole: ({ leadForearm: "arm", leadHand: "arm", leadUpperArm: "arm",
                                          pelvis: "trunk", thorax: "trunk" })

    // One entry per ENUMERATED sensor. A wG3 is ONE entry (one peripheral) holding two roles; its
    // `role` is the one roleForDevice reports for it ("leadForearm" — the palm's leadHand comes by
    // the cable, imu_role_map.cpp).
    readonly property var devices: {
        var list  = imuMgr.imuDeviceList     // enumeration + session enablement
        var insts = imuMgr.instances         // connection state
        var roles = settings.imuRoles        // roleForDevice() is Q_INVOKABLE, not reactive
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var d    = list[i]
            var inst = imuMgr.instanceFor(d.id)
            var conn = inst !== null && inst.imuConnected === true
            var role = imuMgr.roleForDevice(d.id)
            var hm   = d.vendor === "hackmotion"
            // displayNameForDevice() reads the alias and roles maps and is not reactive on its own;
            // the dependency is USED below (an unused local is dropped with its dependency).
            var named = settings.imuAlias !== undefined && settings.imuRoles !== undefined
            var label = inst !== null ? inst.stateLabel : ""
            // A connect that gave up (wizard l.1577: "Error" / "Not found") is not still connecting:
            // the page offers Connect again rather than "Connecting…" for ever. (Stage 5b.)
            var failed = inst !== null && !conn && (label === "Error" || label === "Not found")
            out.push({ id: d.id, index: d.index, vendor: d.vendor, alias: d.alias || "",
                       description: d.description || "", transport: d.transport || "",
                       // Never a raw id (Stage 5d): the manager's remembered-name rule.
                       name: named ? imuMgr.displayNameForDevice(d.id) : "",
                       present: d.present === true, sessionEnabled: d.sessionEnabled === true,
                       connected: conn, connecting: inst !== null && !conn && !failed, failed: failed,
                       stateLabel: label,
                       isHackMotion: hm, role: role,
                       roleLabel: ImuMounts.deviceMountLabel(hm, role),
                       group: role !== "" && ctx.groupOfRole[role] !== undefined ? ctx.groupOfRole[role] : "",
                       // A wG3's unit for its role ("Lower arm"); "" for a Witmotion, whose device
                       // IS the segment (wizard l.1531–1532).
                       unitLabel: hm && role !== "" ? imuMgr.unitLabelForRole(role) : "",
                       instance: inst })
        }
        return out
    }

    // role → who holds it, through the manager's owner ladder (present+enabled > present+disabled
    // > any claimant). `unit` is what a viz or routine binds to (a wG3's HmUnit); `deviceObject`
    // the peripheral, which carries the connect state (imu_manager.h ⚠ — a unit has no
    // imuConnected of its own). `calibratedValid` is the device's LIVE validity — Witmotion:
    // fullyCalibrated on the unit; HackMotion: connected and calibrationState CALIBRATED (2).
    readonly property var roles: {
        var devs  = ctx.devices
        var _list = imuMgr.imuDeviceList
        var _inst = imuMgr.instances
        var _map  = settings.imuRoles
        var byId = {}
        for (var i = 0; i < devs.length; ++i) byId[devs[i].id] = devs[i]
        var out = {}
        var all = ImuMounts.allRoles
        for (var r = 0; r < all.length; ++r) {
            var role = all[r]
            var id   = imuMgr.deviceIdForRole(role)
            var dev  = id !== "" && byId[id] !== undefined ? byId[id] : null
            var unit = id !== "" ? imuMgr.instanceForRole(role) : null
            var obj  = id !== "" ? imuMgr.deviceForRole(role) : null
            var valid = false
            if (dev !== null && dev.isHackMotion)
                valid = obj !== null && obj.imuConnected === true && obj.calibrationState === 2
            else if (unit !== null)
                valid = unit.fullyCalibrated === true
            var present = dev !== null && dev.present
            var enabled = dev !== null && dev.sessionEnabled
            out[role] = { role: role, label: ImuMounts.label(role), deviceId: id,
                          assigned: id !== "", device: dev, unit: unit, deviceObject: obj,
                          unitLabel: id !== "" ? imuMgr.unitLabelForRole(role) : "",
                          present: present, enabled: enabled, inSession: present && enabled,
                          connected: dev !== null && dev.connected, calibratedValid: valid }
        }
        return out
    }

    // Instrument groups (§4.12). A group is IN THE SESSION when a present, session-enabled device
    // holds at least one of its roles; COMPLETE when every role of its minimum set is so held.
    // Arm minimum: leadForearm + leadHand (the old Wrist table's required A and B); leadUpperArm is
    // optional (roll). DECISION(stage4): trunk minimum is pelvis AND thorax — the pair route and
    // the body-rotation lanes need both (trunk_imu_design.md "With both sensors bound").
    readonly property var groupDefs: ({
        arm:   { roles: ImuMounts.armRoles,   minimum: ["leadForearm", "leadHand"] },
        trunk: { roles: ImuMounts.trunkRoles, minimum: ["pelvis", "thorax"] }
    })
    readonly property var groups: {
        var r = ctx.roles, defs = ctx.groupDefs, out = {}
        for (var g in defs) {
            var def = defs[g]
            var held = [], devIds = [], missing = []
            for (var i = 0; i < def.roles.length; ++i) {
                var e = r[def.roles[i]]
                if (!e.inSession) continue
                held.push(def.roles[i])
                if (devIds.indexOf(e.deviceId) < 0) devIds.push(e.deviceId)
            }
            for (var j = 0; j < def.minimum.length; ++j)
                if (!r[def.minimum[j]].inSession) missing.push(def.minimum[j])
            var inSession = held.length > 0
            // The routine is the lead forearm holder's vendor, as ImuCalibrationFlow picks it
            // (l.107–109 there): a wG3 → "hackmotion", anything else → "witmotion". Trunk sensors
            // are Witmotions (a wG3 can take only leadForearm).
            var routine = ""
            if (inSession)
                routine = (g === "arm" && r.leadForearm.device !== null && r.leadForearm.device.isHackMotion)
                          ? "hackmotion" : "witmotion"
            out[g] = { roles: held, minimum: def.minimum, inSession: inSession,
                       complete: missing.length === 0, missingRoles: missing,
                       routine: routine, devices: devIds }
        }
        return out
    }

    // Found, enabled, and no mount: "found is not wanted" — it sits out the session (§4.12).
    readonly property var unassignedDevices: {
        var devs = ctx.devices, out = []
        for (var i = 0; i < devs.length; ++i)
            if (devs[i].present && devs[i].sessionEnabled && devs[i].role === "") out.push(devs[i])
        return out
    }
    // Present, enabled, holding a role: the sensors this session uses. One entry per PERIPHERAL —
    // a wG3 fills two roles and is one device, and queuing it twice would connect it twice (the
    // dedup assignedDevices() did by hand, wizard l.1304–1330, is structural here).
    readonly property var inSessionDevices: {
        var devs = ctx.devices, out = []
        for (var i = 0; i < devs.length; ++i)
            if (devs[i].present && devs[i].sessionEnabled && devs[i].role !== "") out.push(devs[i])
        return out
    }
    readonly property var devicesToConnect: {
        var devs = ctx.inSessionDevices, out = []
        for (var i = 0; i < devs.length; ++i) if (!devs[i].connected) out.push(devs[i])
        return out
    }
    // DECISION(stage4): false when nothing is in the session, as today's imusAllConnected is false
    // when no slot is required (l.480) — the sensors step then offers Skip, not a silent pass.
    readonly property bool allInSessionConnected: inSessionDevices.length > 0 && devicesToConnect.length === 0
    readonly property bool sensorsConnecting: imuMgr.pacedConnectActive || imuMgr.anyConnecting
    readonly property bool sensorScanActive:  imuMgr.imuScanActive
    readonly property int  sensorsFound:      imuMgr.imuDeviceList.length
    // The Sensors page's "Connecting…": the paced queue still has a sensor to connect, or a sensor
    // of THIS session has been asked to connect and has not yet (D3: the primary must not read
    // Continue while either holds). A sensor outside the session does not count.
    readonly property bool sensorsConnectingInSession: {
        if (imuMgr.pacedConnectActive) return true
        var devs = ctx.inSessionDevices
        for (var i = 0; i < devs.length; ++i) if (devs[i].connecting) return true
        return false
    }

    // A mount whose holder the scan cannot see (D6): one entry per absent DEVICE, so a wG3's pair is
    // one entry named as the pair. `name` is the alias the coach gave it, else its id — an absent
    // sensor is not in the device list, so its description is not known.
    readonly property var absentMounts: {
        var r = ctx.roles, all = ImuMounts.allRoles, byId = {}, order = []
        var named = settings.imuAlias !== undefined   // used below: displayNameForDevice reads it
        for (var i = 0; i < all.length; ++i) {
            var e = r[all[i]]
            if (!e.assigned || e.present) continue
            if (byId[e.deviceId] === undefined) { byId[e.deviceId] = []; order.push(e.deviceId) }
            byId[e.deviceId].push(all[i])
        }
        var out = []
        for (var j = 0; j < order.length; ++j) {
            var id = order[j], roles = byId[id]
            var pair = roles.indexOf("leadForearm") >= 0 && roles.indexOf("leadHand") >= 0
            var labels = []
            if (pair) labels.push(ImuMounts.hackMotionLabel)
            for (var k = 0; k < roles.length; ++k)
                if (!(pair && (roles[k] === "leadForearm" || roles[k] === "leadHand")))
                    labels.push(ImuMounts.label(roles[k]))
            // The remembered name — a switched-off sensor is not in the device list, and its id
            // is a "{…}" UUID the golfer must never see (Stage 5d).
            out.push({ deviceId: id, roles: roles, mountLabel: labels.join(", "),
                       name: named ? imuMgr.displayNameForDevice(id) : "" })
        }
        return out
    }

    // ── Sensor actions ───────────────────────────────────────────────────────────────────────────
    // The paced BLE connect lives in ImuManager now (Stage 1, F10): one call, 2 s apart as today
    // (wizard l.1352–1389).
    function connectSensors() {
        var devs = devicesToConnect, ids = []
        for (var i = 0; i < devs.length; ++i) ids.push(devs[i].id)
        if (ids.length > 0) imuMgr.connectPaced(ids, 2000)
    }
    function cancelPacedConnect()          { imuMgr.cancelPacedConnect() }
    function setSensorEnabled(deviceId, on) { imuMgr.setSessionImuEnabled(deviceId, on) }   // l.1565
    function rescanSensors()               { imuMgr.rescanImu() }                          // l.1459

    // ── Mounts (§4.12, §4.13) ────────────────────────────────────────────────────────────────────
    // The Sensors page asks where a found sensor is worn, and confirms a remembered mount. The
    // rules are ImuManager's (one claim per role; a wG3 takes leadForearm and the manager writes
    // its pair) and the labels ImuMounts' — the same two the Settings ▸ IMUs combo uses, so the
    // two pickers cannot disagree.
    //
    // Writes the mount; returns the manager's answer. false = refused (the holder arrives on
    // mountRefused) or a caller error. A mount is a SETTING: it outlives the session.
    function setMount(deviceId, role) { return imuMgr.setRoleForDevice(deviceId, role) }

    // The present, session-enabled sensor OTHER than `exceptDeviceId` whose claim on `role` would
    // make the manager refuse a new one (imu_role_map.cpp claimRole; ImusPanel.holderOf) — by
    // device id, "" when none. Placement KEYS are read, so a wG3's unit keys count.
    function mountHolder(role, exceptDeviceId) {
        if (!role) return ""
        var roles = settings.imuRoles, list = imuMgr.imuDeviceList
        for (var key in roles) {
            if (roles[key] !== role) continue
            var owner = key.indexOf("#") >= 0 ? key.substring(0, key.indexOf("#")) : key
            if (owner === exceptDeviceId) continue
            for (var i = 0; i < list.length; ++i)
                if (list[i].id === owner && list[i].present === true && list[i].sessionEnabled !== false)
                    return owner
        }
        return ""
    }
    // The holder as the golfer may read it — never a raw id (ImuManager::displayNameForDevice,
    // the rule Settings ▸ IMUs uses too).
    function sensorName(deviceId) { return imuMgr.displayNameForDevice(deviceId) }
    function mountHolderName(role, exceptDeviceId) {
        var h = mountHolder(role, exceptDeviceId)
        return h === "" ? "" : sensorName(h)
    }
    // The mounts offered for `device` (a ctx.devices entry): a wG3 only its pair ("Lead forearm +
    // hand", role leadForearm), anything else every OFFERED mount (ImuMounts.offeredRoles — the
    // trunk waits for its calibrate step). Each { role, label, holder, holderName, taken }; a mount
    // is taken when another present, enabled sensor holds it (for the wG3, either half of the pair).
    // Not reactive on its own: a binding calling it reads ctx.devices first.
    function offeredMounts(device) {
        var offered = device.isHackMotion ? ["leadForearm"] : ImuMounts.offeredRoles
        var out = []
        for (var i = 0; i < offered.length; ++i) {
            var role = offered[i]
            var holder = device.isHackMotion
                         ? (mountHolder("leadForearm", device.id) || mountHolder("leadHand", device.id))
                         : mountHolder(role, device.id)
            out.push({ role: role, label: ImuMounts.deviceMountLabel(device.isHackMotion, role),
                       holder: holder, holderName: holder !== "" ? sensorName(holder) : "",
                       taken: holder !== "" })
        }
        return out
    }

    // The manager's refusal of a mount (ImuManager::roleRefused), relayed so a page can name the
    // holder without reaching for the manager (lint W4). Wired by hand rather than with a
    // Connections: this object has no `active` to gate one on (lint W1), and it is wired for as
    // long as it lives, to whichever manager it is given.
    signal mountRefused(string deviceId, string role, string holderId)
    function _relayRefusal(deviceId, role, holderId) { ctx.mountRefused(deviceId, role, holderId) }
    property var _refusalSource: null
    function _wireRefusals() {
        if (_refusalSource !== null && _refusalSource.roleRefused !== undefined)
            _refusalSource.roleRefused.disconnect(_relayRefusal)
        _refusalSource = imuMgr
        if (imuMgr && imuMgr.roleRefused !== undefined) imuMgr.roleRefused.connect(_relayRefusal)
    }
    onImuMgrChanged: _wireRefusals()
    Component.onCompleted: _wireRefusals()
    Component.onDestruction: {
        if (_refusalSource !== null && _refusalSource.roleRefused !== undefined)
            _refusalSource.roleRefused.disconnect(_relayRefusal)
    }

    // ── Launch monitor ───────────────────────────────────────────────────────────────────────────
    readonly property bool   launchMonitorConfigured: launchMon.configured === true
    readonly property string launchMonitorState:      launchMon.state !== undefined ? launchMon.state : ""
    // The short device name ("GC Quad"); "" when nothing is configured or the connector names none.
    readonly property string launchMonitorName:       launchMon.deviceName !== undefined ? launchMon.deviceName : ""

    // ── What the session will record (§4.14) ─────────────────────────────────────────────────────
    // The map MetricCatalog.setupSummary() takes. imuRoles are the roles in the session whose
    // group's calibration OUTCOME holds per the draft (an assigned-but-uncalibrated sensor produces
    // nothing — setup_capability.h). DECISION(stage4): `hackMotion` is true only when the wG3 holds
    // the arm AND its outcome holds, because contextForSetup adds LeadForearm + LeadHand for it on
    // its own; and faceOn/dtl mean enabled AND connected, not merely assigned.
    readonly property var setupFacts: {
        var r = ctx.roles, g = ctx.groups, defs = ctx.groupDefs
        var out = []
        var hm = false
        for (var grp in defs) {
            var oc = ctx.draft ? ctx.draft.outcome(grp) : null
            if (!oc || !oc.done) continue
            var rl = defs[grp].roles
            for (var i = 0; i < rl.length; ++i) if (r[rl[i]].inSession) out.push(rl[i])
            if (grp === "arm" && g.arm.routine === "hackmotion") hm = true
        }
        // DECISION(stage5c): a skipped Cameras step records no video (its issue says so), so
        // its cameras do not count, connected or not.
        var camsSkipped = ctx.draft !== null && ctx.draft.state("cameras") === "skipped"
        return { faceOn: ctx.faceOnConnected && !camsSkipped, dtl: ctx.dtlConnected && !camsSkipped,
                 imuRoles: out, hackMotion: hm, launchMonitor: ctx.launchMonitorConfigured }
    }

    // The closing page's "What this session will record" (§4.14): the catalogue's per-group answer
    // for setupFacts (MetricCatalog.setupSummary — the resolver, nothing re-implemented), rolled
    // up into the six families and worded by setup_capability_rows.js, the ONE place that wording
    // lives. One catalogue per context. Each row { key, label, state, stateText, tone, reason,
    // measured, estimated, unavailable, total, text } — CapRows.rows has the rules.
    property MetricCatalog _catalog: MetricCatalog {}
    readonly property var capabilitySummary: _catalog.setupSummary(setupFacts)
    // The same setup without the launch monitor — only when one is configured — so the rows can
    // tell a family the launch monitor alone measures (CapRows "Whose word it is").
    readonly property var capabilitySummaryWithoutLm: {
        var f = ctx.setupFacts
        if (!f.launchMonitor) return null
        return _catalog.setupSummary({ faceOn: f.faceOn, dtl: f.dtl, imuRoles: f.imuRoles,
                                       hackMotion: f.hackMotion, launchMonitor: false })
    }
    readonly property var capability: {
        var g = ctx.groups, r = ctx.roles, f = ctx.setupFacts
        var oc = ctx.draft ? ctx.draft.outcome("arm") : null
        // A camera's state for the reasons (Stage 5c): "connected" when the session will record
        // it (setupFacts), "unconnected" when one is assigned but is not, "absent" when none is.
        var cam = function(recorded, assigned) {
            return recorded ? "connected" : (assigned.length > 0 ? "unconnected" : "absent")
        }
        return CapRows.rows(ctx.capabilitySummary, {
            armInSession:  g.arm.inSession,
            armCalibrated: oc !== null && oc.done,
            upperArmHeld:  r.leadUpperArm.inSession,
            cameraCount:   (f.faceOn ? 1 : 0) + (f.dtl ? 1 : 0),
            faceOn:        cam(f.faceOn, ctx.faceOn),
            dtl:           cam(f.dtl, ctx.dtl),
            lmName:        ctx.launchMonitorName
        }, ctx.capabilitySummaryWithoutLm)
    }

    // ── Session lifecycle ────────────────────────────────────────────────────────────────────────
    // A new session inherits the GLOBAL enablement afresh (wizard l.576–586). Called once per visit,
    // from SetupDraft.reset() — never on a visibility edge (F4).
    function seedSessionEnablement() {
        var camList = cameraMgr.cameraList
        for (var i = 0; i < camList.length; ++i)
            cameraMgr.setSessionCameraEnabled(camList[i].cameraKey,
                settings.cameraExcluded.indexOf(camList[i].cameraKey) < 0)
        var devList = imuMgr.imuDeviceList
        for (var j = 0; j < devList.length; ++j)
            imuMgr.setSessionImuEnabled(devList[j].id, settings.imuExcluded.indexOf(devList[j].id) < 0)
    }

    // Release every device setup may have connected (wizard l.194–201): the paced queue first, so
    // nothing reconnects behind the teardown; then cameras (stopCapture also drops capture intent,
    // which releases the microphone); then sensors (BLE battery).
    function releaseDevices() {
        imuMgr.cancelPacedConnect()
        cameraMgr.stopCapture()
        cameraMgr.disconnectAll()
        imuMgr.disconnectAll()
    }
}
