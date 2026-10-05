// CalibDriver — the ADAPTER between the calibration test cases (tst_setup_calib_*.qml,
// catalogue groups CW, CH and L5 in docs/design/session_wizard_refactor_design.md §7.3)
// and session setup / ImuCalibrationFlow / PpImuPanel.
//
// ⚠ THIS IS THE ONE FILE THE REFACTOR REWRITES. The test cases speak only this API
// (openAtCalibrate, stage, done, leaveBack, mark/callsSince, …); everything that knows
// how the code is built lives below and nowhere else. Since Stage 2 that is: the flow's
// loaded routine (`flow._routine`, a WitmotionArmRoutine or HackMotionArmRoutine, which
// exists only while the flow is active and is re-created on every activation), the
// routines' public outputs (`stage`, `done`, `message`, …) and the internal names they
// carried over from the old flow's `d` object (calibPhase, hmStep, _animStage, …), the
// declaration order of each routine's Timers, the guide's view (`flow._guide.view`), the
// flow's `pace` (the scaled clock) and `_lastMessage`, and the Recalibrate link's
// place in the Check panel.
//
// It reaches internals that have no id outside their file by walking the object tree
// (`data` / `children`) and matching on the members they carry. No objectName was added
// to production for this.
//
// The driver needs the TestCase for waiting: set `tc` before calling anything that waits.
//
// ── SESSION SETUP (since Stage 5c the only shell) ───────────────────────────────────────────────
// The flow lives on the Calibrate PAGE, which exists only while Calibrate is current (R2) — every
// entry is a NEW flow. `wiz` is an adapter with the retired wizard's names (currentStep,
// stepCalibrate, stepStates, goNext, goBack, visible), keyed by step key, so the cases read as
// they were written; and `flow` follows the Calibrate page: the live flow while it is up (each one
// instrumented as it appears, src "wiz", so events and tings accumulate across visits), else
// `ghostFlow` — what an idle flow answers after the step is left (not visible, not active, no
// routine, its last message, and done when the draft's arm outcome still stands).
import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio
import "../fakes"

Item {
    id: drv

    property var tc: null

    // ── Fakes ────────────────────────────────────────────────────────────────
    Component { id: imuMgrComp;    FakeImuManager {} }
    Component { id: camMgrComp;    FakeCameraManager {} }
    Component { id: athleteComp;   FakeAthlete {} }
    Component { id: liveWristComp; FakeLiveWrist {} }
    Component { id: sessionComp;   FakeSessionController {} }
    Component { id: navComp;       FakeNav {} }
    Component { id: lmComp;        FakeLaunchMonitor {} }
    Component { id: appLogComp;    FakeAppLog {} }
    Component { id: guideComp;     FakeGuide {} }
    // PpCameraFrame (session setup's camera rows and Ball frame) reads these two.
    Component {
        id: shotReplayComp
        QtObject {
            property bool   active: false
            property var    streams: []
            property real   positionUs: 0
            property real   impactPositionUs: 0
            property string swingDir: ""
            property var    analysisDetail: ({})
            signal positionChanged()
            function setVideoSink(i, s) {}
        }
    }
    Component {
        id: shotProcessorComp
        QtObject {
            property bool isReplaying: false
            property var  replayAnalysisDetail: ({})
            property real replayPositionUs: 0
            signal replayPositionChanged()
        }
    }

    // ── The wizard adapter, and the flow that stands in when no Calibrate page is up ──
    property var setup: null              // ScreenSessionSetup
    property real _guideReadyAt: 0        // when the current guide view last became fully loaded
    QtObject {
        id: newWiz
        readonly property string stepImus:      "imus"
        readonly property string stepCalibrate: "calibrateArm"
        readonly property string stepConfirm:   "checkArm"
        readonly property string stepReady:     "ready"
        readonly property string currentStep: drv.setup ? drv.setup.flow.current : ""
        readonly property var    stepStates:  drv.setup ? drv.setup.draft.states : ({})
        readonly property bool   visible:     drv.setup ? drv.setup.visible : false
        function goNext(mark) { drv.setup.flow.next(mark); drv._settle() }
        function goBack()     { drv.setup.flow.back();     drv._settle() }
    }
    QtObject {
        id: ghostFlow
        readonly property bool   visible: false
        readonly property bool   active:  false
        readonly property bool   calibrationDone: drv.setup ? drv.setup.draft.outcome("arm").done : false
        readonly property bool   calibrationFailed: false
        readonly property bool   mountFailed: false
        readonly property bool   isHackMotion: drv.setup ? drv.setup.ctx.groups.arm.routine === "hackmotion" : false
        readonly property int    phase: 0
        readonly property var    _routine: null
        readonly property var    _guide: null
        readonly property bool   _runRequested: false
        property string _lastMessage: ""
        property string _lastMessageKind: ""
    }
    function _settle() {
        tc.tryVerify(function() { return !drv.setup.flow.busy }, 5000, "the setup flow never settled")
    }
    // The page changed: a new Calibrate page brings a new calibration flow — paced, instrumented
    // and made `flow` before it is activated (SetupFlow sets `page` before `active`); any other
    // page (or none) leaves the ghost.
    function _onSetupPageChanged() {
        var p = setup ? setup.flow.page : null
        if (p !== null && p.stepKey === "calibrateArm") {
            var f = findItem(p, _isFlow)
            if (!f) tc.fail("CalibDriver: no ImuCalibrationFlow on the Calibrate page")
            f.pace = paceObject(scale)
            f._lastMessageChanged.connect(function() {
                ghostFlow._lastMessage = f._lastMessage; ghostFlow._lastMessageKind = f._lastMessageKind })
            f._lastMessageKindChanged.connect(function() { ghostFlow._lastMessageKind = f._lastMessageKind })
            instrument(f, "wiz")
            flow = f
        } else {
            flow = ghostFlow
        }
    }

    // ── Hosts ────────────────────────────────────────────────────────────────
    Item { id: wizHost;  anchors.fill: parent }
    Item { id: flowHost; anchors.fill: parent; visible: drv.standalone !== null }
    // Stands in for the toolbar's imuPopup (PpSessionToolbar.qml l.705–718). ⚠ A Popup does
    // NOT build its contentItem with the toolbar: Qt Quick Controls DEFERS `contentItem`
    // (and `background`) until the popup is first opened (QQuickPopup's
    // Q_CLASSINFO("DeferredPropertyNames", "background,contentItem")). Once opened it stays
    // built, and alive while closed — closed, the content is not visible. So a panel hosted
    // here, alive and invisible, models "the toolbar sensor popup has been opened earlier in
    // this run of the app", not the app's normal state (in which no panel exists until the
    // popup is first opened). enterToolbarCalibrate() "opens" it.
    Item { id: panelHost; visible: false; width: 380; height: 760 }
    // The real toolbar host: a Popup whose contentItem is the panel (PpSessionToolbar.qml
    // l.705–718). The panel is built on the FIRST open, as Qt defers a declared contentItem
    // until then (hostToolbar / openToolbar). Closing hides the popup item, and with it the
    // panel's effective visibility.
    Popup {
        id: imuPopup
        parent: drv
        width: 380; height: 760
        padding: 0
        closePolicy: Popup.NoAutoClose
    }
    property bool panelInPopup: false

    // ── State ────────────────────────────────────────────────────────────────
    property var    fx:         null     // {imuManager, cameraManager, …} context fakes
    property string setupName:  ""
    readonly property var wiz:  newWiz   // the adapter over session setup (above)
    property var    flow:       null     // the flow under test: the wizard's, or standalone
    property var    panel:      null     // PpImuPanel (toolbar host)
    property var    panelFlow:  null
    property var    standalone: null
    property var    routine:    null     // hostRoutine(): a routine on its own
    property var    fakeGuide:  null
    property var    imus:       ({})     // Witmotion fakes by slot {A, B, C}
    property var    hm:         null     // FakeHmDevice in slot A (or the spare in CW12)

    // Every recorded event, oldest first: {t, src: "wiz"|"panel"|"flow", kind, name, value}
    //   kind "timer"   — a flow Timer's triggered()
    //   kind "state"   — calibPhase / hmStep / _animStage / done / failed changed
    //   kind "guide"   — the BodyVizView's leadArmAnimFinished(), or a target change
    //   kind "completed"
    //   kind "ting"    — the flow's TingPlayer was asked to play
    property var events: []
    property var completedBy: ({})
    property var _hooked: []              // BodyVizViews already instrumented
    property var _hookedRoutines: []      // routines already instrumented
    property var _lastAnim: ({})          // src → the last guide stage started
    property string stallStage: ""        // see stall()
    property real   openedAt: 0           // when openAtCalibrate() saw the flow visible
    readonly property real stallDurationMs: 1.0e9

    // The scaled clock. 1 = the routines' real pace; openAtCalibrate / hostStandalone /
    // hostRoutine take opts.scale. `tick` is overridden so the hold timers' sampling
    // interval stays above a frame (QML timers advance on the animation clock).
    property real scale: 1
    readonly property var scaledPaceExtra: ({ tick: 400 })    // × 0.05 = 20 ms
    // The stillness HOLD on the scaled clock, before scaling (0 = the routine's own, 2000 ms →
    // 100 ms at × 0.05). A case that must catch a hold MID-accumulation sets it long (Stage 5d:
    // at 100 ms the "≥ a quarter, not yet done" window was ~75 ms, and a 50 ms poll sometimes
    // stepped over it — the routine then completed and the reach timed out). Everything else
    // stays short. Applies to every scaled host the driver builds.
    property int holdPace: 0
    function paceObject(sc) {
        if (sc === undefined || sc === 1) return null
        var o = { scale: sc }
        for (var k in scaledPaceExtra) o[k] = scaledPaceExtra[k]
        if (holdPace > 0) o.hold = holdPace
        return o
    }
    // The driver's mark taken the moment a calibration flow went INACTIVE — i.e. right after the
    // routine's stop() returned (the flow stops its routine in its own activeChanged handler,
    // which runs before this one, connected later). Navigation is queued (R5), so a mark taken
    // when the leave is REQUESTED can see a hold tick that lands before the leave is applied.
    property var leaveMark: null

    // Each routine's Timers (WitmotionArmRoutine.qml / HackMotionArmRoutine.qml, the
    // properties `_<name>Timer`), with the pace key of each interval — checked on
    // instrumentation so a renamed or re-paced timer fails loudly here.
    readonly property var wtTimers:  [["stabilityHold", "tick"], ["introStart", "introStart"],
                                      ["introReady", "introReady"], ["phase1MinHold", "minHold"],
                                      ["phase1Hold", "tick"], ["captureTransition", "captureTransition"],
                                      ["raiseReady", "raiseReady"]]
    readonly property var hmTimers:  [["hmStart", "hmStart"], ["hmHorizontalSettle", "settle"],
                                      ["hmRaiseConfirm", "raiseSettle"], ["hmRefSettle", "refSettle"],
                                      ["hmPresenceWait", "presenceWait"]]

    function now() { return testLog.elapsedMs() }

    // ── Tree helpers ─────────────────────────────────────────────────────────
    function findItem(item, pred) {
        if (!item) return null
        if (pred(item)) return item
        var kids = item.children
        if (kids) for (var i = 0; i < kids.length; ++i) {
            var r = findItem(kids[i], pred)
            if (r) return r
        }
        return null
    }
    function findAll(item, pred, out) {
        out = out || []
        if (!item) return out
        if (pred(item)) out.push(item)
        var kids = item.children
        if (kids) for (var i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
        return out
    }
    function _isFlow(o) {
        return o.calibrationDone !== undefined && o.isHackMotion !== undefined
               && o.begin !== undefined && o._guide !== undefined
    }
    // The flow's loaded routine (null while the flow is not active).
    function dOf(f) { return f ? f._routine : null }
    function _isHmRoutine(r) { return r !== null && r !== undefined && r.hmStep !== undefined }
    // The routine's Timers, in the table's order: each is the property `_<name>Timer`.
    function timersOf(r) {
        var out = []
        if (!r) return out
        var tab = timerTable(r)
        for (var i = 0; i < tab.length; ++i) out.push(r["_" + tab[i][0] + "Timer"])
        return out
    }
    function timerTable(r) { return _isHmRoutine(r) ? hmTimers : wtTimers }
    // The flow's completion TingPlayer. In this binary it is the silent stand-in
    // (tests/setup/silent_ting_player.h), which counts play() in `playCount`.
    function tingOf(f) {
        var data = f.data
        for (var i = 0; i < data.length; ++i) {
            var o = data[i]
            if (o && o.playCount !== undefined && o.play !== undefined) return o
        }
        return null
    }
    function tingCount(f) { return tingOf(f || flow).playCount }
    function tingsSince(m, src) { return eventsSince(m, src || "wiz", "ting").length }
    function timer(name, f) {
        var r = dOf(f || flow)
        var ts = timersOf(r), tab = timerTable(r)
        for (var i = 0; i < tab.length; ++i) if (tab[i][0] === name) return ts[i]
        return null
    }
    function runningTimers(f) {
        var r = dOf(f || flow)
        var ts = timersOf(r), tab = timerTable(r), out = []
        for (var i = 0; i < ts.length; ++i) if (ts[i].running) out.push(tab[i][0])
        return out
    }

    // ── Instrumentation ──────────────────────────────────────────────────────
    // ⚠ A quaternion read off a property is a value-type REFERENCE that re-reads the property
    // on access, so a logged one would show the property's later value. Detach it.
    function _q(q) { return Qt.quaternion(q.scalar, q.x, q.y, q.z) }
    function _ev(src, kind, name, value) {
        events.push({ t: now(), src: src, kind: kind, name: name, value: value })
        // Also into the fake app log, tag "Drv": the routine's own lines ("stop reason=…") and
        // these then sit in ONE ordered list, so "did the tick land before or after stop()?"
        // is answered by order, not by comparing two clocks to the millisecond.
        if (fx && fx.appLog)
            fx.appLog.info("Drv", src + ":" + kind + ":" + name + (kind === "state" ? "=" + value : ""))
    }
    function instrument(f, src) {
        var ting = tingOf(f)
        if (!ting) tc.fail("CalibDriver: the flow's TingPlayer was not found")
        ting.playCountChanged.connect(function() { drv._ev(src, "ting", "play", ting.playCount) })
        f.completed.connect(function() {
            var c = drv.completedBy
            c[src] = (c[src] || 0) + 1
            drv._ev(src, "completed", "completed")
        })
        var hookBvv = function() {
            var b = f._guide.view
            if (!b || drv._hooked.indexOf(b) >= 0) return
            drv._hooked.push(b)
            if (b.fullyLoaded) drv._guideReadyAt = drv.now()
            b.fullyLoadedChanged.connect(function() { if (b.fullyLoaded) drv._guideReadyAt = drv.now() })
            b.leadArmAnimFinished.connect(function() { drv._ev(src, "guide", "finished", drv._lastAnim[src] || "") })
            b.leadArmOverrideRotationChanged.connect(function() {
                drv._ev(src, "guide", "armTarget", drv._q(b.leadArmOverrideRotation))
            })
            b.leadForeArmOverrideRotationChanged.connect(function() {
                drv._ev(src, "guide", "foreArmTarget", drv._q(b.leadForeArmOverrideRotation))
            })
            b.leadArmAnimDurationChanged.connect(function() { drv._applyStall(f) })
        }
        hookBvv()
        f.activeChanged.connect(function() { if (!f.active) drv.leaveMark = drv.mark() })
        f._guide.viewChanged.connect(function() { drv._ev(src, "guide", "bvvChanged", f._guide.view); hookBvv() })
        // A routine exists only while the flow is active, and a new one is made on every
        // activation: instrument each as it appears.
        instrumentRoutine(f._routine, src, f)
        // (Not an event: the flow drops a stopped routine on the turn after it stops it, which
        // is bookkeeping, not behaviour — the "nothing happens after leave" cases count events.)
        f._routineChanged.connect(function() { drv.instrumentRoutine(f._routine, src, f) })
    }
    // _lastAnim[src] — the stage a finishing animation belonged to (the routine clears
    // _animStage in its own finished handler, which runs before ours).
    function instrumentRoutine(r, src, holder) {
        if (!r || _hookedRoutines.indexOf(r) >= 0) return
        _hookedRoutines.push(r)
        var ts = timersOf(r), tab = timerTable(r)
        for (var i = 0; i < ts.length; ++i) {
            if (!ts[i]) tc.fail("CalibDriver: the routine has no _" + tab[i][0] + "Timer")
            var want = Math.round(r._p[tab[i][1]] * r._p.scale)
            if (ts[i].interval !== want)
                tc.fail("CalibDriver: timer #" + i + " (" + tab[i][0] + ") has interval "
                        + ts[i].interval + ", expected " + want + " — the routine changed shape")
            ;(function(name) {
                ts[i].triggered.connect(function() { drv._ev(src, "timer", name) })
            })(tab[i][0])
        }
        if (r.calibPhase !== undefined)
            r.calibPhaseChanged.connect(function() { drv._ev(src, "state", "calibPhase", r.calibPhase) })
        if (r.hmStep !== undefined)
            r.hmStepChanged.connect(function() { drv._ev(src, "state", "hmStep", r.hmStep) })
        r._animStageChanged.connect(function() {
            if (r._animStage !== "") drv._lastAnim[src] = r._animStage
            drv._ev(src, "state", "animStage", r._animStage)
            if (holder) drv._applyStall(holder)
        })
        r.calibrationDoneChanged.connect(function() { drv._ev(src, "state", "done", r.calibrationDone) })
        r.calibrationFailedChanged.connect(function() { drv._ev(src, "state", "failed", r.calibrationFailed) })
        r.mountFailedChanged.connect(function() { drv._ev(src, "state", "mountFailed", r.mountFailed) })
    }

    // CW5: hold the guide animation named `stage` ("introUp", "introDown", "raise", "hmRaise",
    // "hmReturn") so it never finishes. The guide is the real BodyVizView, whose
    // FrameAnimation advances by frameTime·1000/leadArmAnimDuration — so a duration of 1e9 ms
    // freezes it with no production change. Applied whenever the routine enters that stage or
    // the guide sets a duration while in it.
    function stall(stage) { stallStage = stage }
    function _applyStall(f) {
        if (stallStage === "") return
        var d = dOf(f), b = f._guide.view
        if (!b || !d || d._animStage !== stallStage) return
        if (b.leadArmAnimDuration !== stallDurationMs) b.leadArmAnimDuration = stallDurationMs
    }

    // ── Fakes and devices ────────────────────────────────────────────────────
    function _makeFakes() {
        fx = {
            imuManager:        imuMgrComp.createObject(drv),
            cameraManager:     camMgrComp.createObject(drv),
            athleteController: athleteComp.createObject(drv),
            liveWrist:         liveWristComp.createObject(drv),
            sessionController: sessionComp.createObject(drv),
            navController:     navComp.createObject(drv),
            launchMonitor:     lmComp.createObject(drv),
            appLog:            appLogComp.createObject(drv),
            shotReplay:        shotReplayComp.createObject(drv),
            shotProcessor:     shotProcessorComp.createObject(drv)
        }
    }
    // setup: "witmotion2" | "witmotion3" | "hackmotion" | "none"
    // opts.hmMode: "autoScript" (default) | "strict" | "manual"
    // opts.spareHm: also a CONNECTED but unassigned wG3 "HM-1" (CW12)
    // opts.spareWitmotion: also a connected but unassigned Witmotion "WT-X" (CW12 reverse)
    function _addDevices(setup, opts) {
        var im = fx.imuManager
        var slots = setup === "witmotion3" ? ["A", "B", "C"]
                  : setup === "witmotion2" ? ["A", "B"] : []
        var quats = { A: [0.98, 0.10, 0.12, 0.10], B: [0.95, -0.20, 0.15, 0.17],
                      C: [0.90, 0.30, -0.20, 0.25] }
        var m = {}
        for (var i = 0; i < slots.length; ++i) {
            var id = "WT-" + slots[i]
            im.addWitmotion(id, "Witmotion " + slots[i])
            im.assign(id, slots[i])
            var inst = im.connectDevice(id)
            var q = quats[slots[i]], n = Math.sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3])
            inst.setQuat(q[0]/n, q[1]/n, q[2]/n, q[3]/n)
            m[slots[i]] = inst
        }
        imus = m
        hm = null
        if (setup === "hackmotion" || opts.spareHm) {
            im.addHackMotion("HM-1", "wG3")
            if (setup === "hackmotion") im.assign("HM-1", "A")
            hm = im.connectDevice("HM-1")
            hm.mode = opts.hmMode || "autoScript"
            // The fake's raise travel lands this long after OBSERVING_RAISE; it has to land
            // inside the (scaled) raise guide, as it does at the real pace.
            hm.raiseTravelAfterMs = Math.round(hm.raiseTravelAfterMs * drv.scale)
        }
        if (opts.spareWitmotion) {
            im.addWitmotion("WT-X", "Witmotion spare")
            var x = im.connectDevice("WT-X")
            x.setQuat(0.97, 0.12, 0.1, 0.17)
            m.X = x
            imus = m
        }
    }
    function devices() {
        var out = []
        var ds = fx ? fx.imuManager._devices : []
        for (var i = 0; i < ds.length; ++i) if (ds[i].inst) out.push(ds[i].inst)
        return out
    }

    // ── Hosting ──────────────────────────────────────────────────────────────
    function _waitGuideLoaded(f) {
        tc.tryVerify(function() { return f._guide.view !== null && f._guide.view.fullyLoaded }, 30000,
                     "BodyVizView never fully loaded (" + (f._guide.view ? f._guide.view.loadedCount : "no view") + ")")
    }

    function releaseGuide() { flow._guide.view.loadedCount = flow._guide.view.totalSegments }

    // Builds the fakes and session setup as Main hosts it (shown, then open(Wrist) from Home), and
    // walks it to Calibrate through the flow's own operations. The guide view exists only from the
    // Calibrate page on, so it loads after the entry; `openedAt` is when it became watchable — the
    // moment the routine's first timer can start.
    //   opts.withPanel — also host a PpImuPanel in list mode, as the closed toolbar popup
    //   opts.holdGuide — the guide is held "not loaded" from its first load until releaseGuide()
    //                    (the intro delay is 3 s at the real pace; the hold lands within a poll of
    //                    the load). "Not loaded" is the guide's own public tally: zeroing
    //                    loadedCount holds fullyLoaded false until releaseGuide() restores it.
    //   opts.prepare   — function(drv), run on the fakes before the shell is built
    //   opts.scale     — the routines' pace (default 1 = the real pace)
    function openAtCalibrate(setupKind, opts) {
        opts = opts || {}
        setupName = setupKind
        scale = opts.scale !== undefined ? opts.scale : 1
        appSettings.imuRoles = ({})
        _makeFakes()
        _addDevices(setupKind, opts)
        if (opts.prepare) opts.prepare(drv)
        setup = harness.createWithContext("ScreenSessionSetup", fx, wizHost,
                                          { width: drv.width, height: drv.height, visible: false })
        tc.verify(setup !== null, "ScreenSessionSetup failed to instantiate")
        setup.flow.pageChanged.connect(drv._onSetupPageChanged)
        flow = ghostFlow
        setup.open(SessionController.Wrist)
        setup.visible = true
        _settle()
        if (opts.withPanel) _createPanel()
        var guard = 0
        while (newWiz.currentStep !== "calibrateArm" && guard++ < 20) {
            var before = newWiz.currentStep
            newWiz.goNext("done")
            if (newWiz.currentStep === before) newWiz.goNext("skipped")
        }
        tc.compare(newWiz.currentStep, "calibrateArm", "session setup did not reach Calibrate")
        tc.verify(flow !== ghostFlow, "the Calibrate page's flow was not picked up")
        tc.tryVerify(function() { return flow.visible }, 5000, "calibration flow not visible on Calibrate")
        _waitGuideLoaded(flow)
        if (opts.holdGuide) flow._guide.view.loadedCount = 0
        openedAt = _guideReadyAt
        return newWiz
    }

    function _createPanel(inPopup) {
        panelInPopup = inPopup === true
        panel = harness.createWithContext("PpImuPanel", fx, panelInPopup ? null : panelHost,
                                          { width: panelHost.width, height: panelHost.height })
        if (panelInPopup) imuPopup.contentItem = panel
        tc.verify(panel !== null, "PpImuPanel failed to instantiate")
        panelFlow = findItem(panel, _isFlow)
        tc.verify(panelFlow !== null, "ImuCalibrationFlow not found in PpImuPanel")
        panelFlow.pace = paceObject(scale)
        instrument(panelFlow, "panel")
    }

    // A standalone flow (CW11), in `layoutMode`, over fresh fakes.
    function hostStandalone(setup, layoutMode, opts) {
        opts = opts || {}
        setupName = setup
        scale = opts.scale !== undefined ? opts.scale : 1
        _makeFakes()
        _addDevices(setup, opts)
        // The flow has no activity of its own since Stage 5b: its host binds `active`, and here
        // the host is the case.
        var init = { width: drv.width, height: drv.height, layoutMode: layoutMode, active: true }
        if (paceObject(scale) !== null) init.pace = paceObject(scale)
        standalone = harness.createWithContext("ImuCalibrationFlow", fx, flowHost, init)
        tc.verify(standalone !== null, "ImuCalibrationFlow failed to instantiate")
        flow = standalone
        instrument(flow, "flow")
        _waitGuideLoaded(flow)
        return flow
    }
    // A routine on its own (routine-level cases): the fakes for `setup`, a FakeGuide, the
    // segments resolved by role as the flow resolves them, pace × opts.scale (default
    // 0.05), active. Its events are recorded with src "routine".
    function hostRoutine(vendor, setup, opts) {
        opts = opts || {}
        setupName = setup
        scale = opts.scale !== undefined ? opts.scale : 0.05
        _makeFakes()
        _addDevices(setup, opts)
        if (opts.prepare) opts.prepare(drv)
        fakeGuide = guideComp.createObject(drv)
        var im = fx.imuManager
        var segs = vendor === "hackmotion"
            ? { device:   im.deviceForRole("leadForearm"),
                lowerArm: im.instanceForRole("leadForearm"),
                palm:     im.instanceForRole("leadHand") }
            : { a: im.instanceForRole("leadForearm"),
                b: im.instanceForRole("leadHand"),
                c: im.instanceForRole("leadUpperArm") }
        var init = { guide: fakeGuide, segments: segs, active: true }
        if (paceObject(scale) !== null) init.pace = paceObject(scale)
        routine = harness.createWithContext(vendor === "hackmotion" ? "HackMotionArmRoutine"
                                                                    : "WitmotionArmRoutine",
                                            fx, null, init)
        tc.verify(routine !== null, "the routine failed to instantiate")
        instrumentRoutine(routine, "routine", null)
        return routine
    }

    // The BodyVizView instances currently in a flow's tree.
    function guideViews(f) {
        return findAll(f || flow, function(o) {
            return o.leadArmAnimFinished !== undefined && o.resetArmAnimation !== undefined
        })
    }

    function teardown() {
        if (panelInPopup) { imuPopup.close(); imuPopup.contentItem = null; panelInPopup = false }
        if (setup)      harness.destroyNow(setup)
        setup = null
        ghostFlow._lastMessage = ""; ghostFlow._lastMessageKind = ""
        _guideReadyAt = 0
        if (panel)      harness.destroyNow(panel)
        if (standalone) harness.destroyNow(standalone)
        if (routine)    harness.destroyNow(routine)
        if (fakeGuide)  fakeGuide.destroy()
        panel = null; standalone = null; flow = null; panelFlow = null
        routine = null; fakeGuide = null; scale = 1
        if (fx) for (var k in fx) if (fx[k]) fx[k].destroy()
        fx = null
        imus = ({}); hm = null
        events = []; completedBy = ({}); stallStage = ""; _hooked = []; leaveMark = null; holdPace = 0
        _hookedRoutines = []; _lastAnim = ({})
    }

    // ── Normalised state ─────────────────────────────────────────────────────
    // "idle" | "intro" | "phase1" | "captured" | "raise" | "phase2" | "done" | "failed" |
    // "mountFailed"; HackMotion: "idle" | "hm0".."hm5" | "hmStopped". The routine says it
    // (its `stage`); with no routine loaded the flow is idle, or done if it holds a done.
    function stage(f) {
        f = f || flow
        var r = dOf(f)
        if (r) return r.stage
        if (f.calibrationDone) return f.isHackMotion ? "hm5" : "done"
        return "idle"
    }
    function animStage(f) { var r = dOf(f || flow); return r ? r._animStage : "" }
    function hmStep(f)    { var r = dOf(f || flow); return r && r.hmStep !== undefined ? r.hmStep : 0 }
    function phase(f)     { return (f || flow).phase }
    function phase1AccumMs(f) { var r = dOf(f || flow); return r && r.phase1AccumMs !== undefined ? r.phase1AccumMs : 0 }
    function stableAccumMs(f) { var r = dOf(f || flow); return r && r.stableAccumMs !== undefined ? r.stableAccumMs : 0 }
    function minHoldDone(f)   { var r = dOf(f || flow); return r ? r._phase1MinHoldDone === true : false }
    function isHackMotion(f)  { return (f || flow).isHackMotion }
    function d(f)             { return dOf(f || flow) }
    function done(f)   { return (f || flow).calibrationDone }
    function failed(f) { return (f || flow).calibrationFailed }
    function gateOpen(f) { return (f || flow)._runRequested }

    // The user-visible status message: the stop/failure text when there is one (the
    // HackMotion HmFailText, the Witmotion MountFailText), else "". After a leave the
    // routine is gone; its last word is what the flow kept.
    function message(f) {
        f = f || flow
        var r = dOf(f)
        return r ? r.message : f._lastMessage
    }
    function messageKind(f) {
        f = f || flow
        var r = dOf(f)
        return r ? r.messageKind : f._lastMessageKind
    }
    // Is `text` actually on screen in the flow (a visible Text carrying it)?
    function textShown(text, f) {
        return findItem(f || flow, function(o) {
            return o.text !== undefined && o.text === text && o.visible === true
                   && o.font !== undefined
        }) !== null
    }
    // The shared StatusBadge's label (ArmCalibrationStatus.qml).
    function badgeText(f) {
        var t = findItem(f || flow, function(o) {
            return o.font !== undefined && o.visible === true
                   && ["Complete", "Calibrating", "Failed", "Pending"].indexOf(o.text) >= 0
        })
        return t ? t.text : ""
    }

    function guideAnimating(f) {
        var g = (f || flow)._guide
        var b = g ? g.view : null
        return b !== null && b !== undefined && b._leadArmP < 1.0
    }
    function completedCount(src) { return completedBy[src || "wiz"] || 0 }

    // ── Navigation (the wizard's public functions) ───────────────────────────
    function leaveBack()       { wiz.goBack() }
    function leaveSkip()       { wiz.goNext("skipped") }
    function continueToCheck() { wiz.goNext("done"); return wiz.currentStep === wiz.stepConfirm }
    function backFromCheck()   { wiz.goBack() }
    // The deepest visible item under `item`'s centre, walking down from the window's content
    // item with childAt() — so a click lands where the user's would, or the test says why not.
    function _hitAt(item) {
        var top = drv.Window.contentItem
        var p = top, hit = null
        while (p) {
            var c = item.mapToItem(p, item.width / 2, item.height / 2)
            var k = p.childAt(c.x, c.y)
            if (!k) break
            hit = k; p = k
        }
        return hit
    }
    function _isWithin(o, anc) { for (var p = o; p; p = p.parent) if (p === anc) return true; return false }
    // Clicks `item` with the real pointer, then checks that its handler ran (`ran()`). If the
    // pointer did not get there — covered at its centre, or (observed 5 Oct for the flow's
    // Recalibrate PpButton, offscreen) a press that no MouseArea in the wizard ever sees,
    // not even as hover — the handler is invoked directly and the substitution logged, so
    // the case still tests the behaviour behind the button. Returns true for a real click.
    function _click(item, what, ran, invoke) {
        var hit = _hitAt(item)
        if (hit && _isWithin(hit, item)) {
            tc.mouseClick(item)
            if (ran()) return true
            console.info("[CalibDriver] the pointer click on " + what + " did not reach its handler "
                         + "(topmost at its centre: " + hit + "); invoking it directly")
        } else {
            console.info("[CalibDriver] " + what + " is covered at its centre by " + hit
                         + "; invoking its handler directly")
        }
        invoke()
        return false
    }

    // The "Recalibrate" link on the Check page (pages/CheckArmPage.qml), clicked.
    function recalibrateFromCheck() {
        var link = findItem(setup, function(o) {
            if (o.text !== "Recalibrate" || o.font === undefined || !o.visible) return false
            for (var i = 0; i < o.children.length; ++i)
                if (o.children[i].pressAndHoldInterval !== undefined) return true   // a MouseArea
            return false
        })
        tc.verify(link !== null, "the Recalibrate link was not found on the Check page")
        var ma = null
        for (var i = 0; i < link.children.length; ++i)
            if (link.children[i].pressAndHoldInterval !== undefined) ma = link.children[i]
        var before = wiz.currentStep
        return _click(link, "the Check page's Recalibrate link",
                      function() { return wiz.currentStep !== before }, function() { ma.clicked(null) })
    }
    // The flow's own "↺  Recalibrate" button (both layouts), clicked.
    function clickFlowRecalibrate(f) {
        var btn = findItem(f || flow, function(o) {
            return o.label !== undefined && o.label === "↺  Recalibrate" && o.visible
        })
        tc.verify(btn !== null, "the flow's Recalibrate button is not visible")
        var n = 0
        var count = function() { n++ }
        btn.clicked.connect(count)
        var real = _click(btn, "the flow's Recalibrate button", function() { return n > 0 },
                          function() { btn.clicked() })
        btn.clicked.disconnect(count)
        return real
    }
    // A Settings round trip: Main hides the wizard (StackLayout), and shows it again. The new
    // shell's suspend and resume are queued (R5): settle on them.
    function suspend() { setup.visible = false; _settle() }
    function resume()  { setup.visible = true;  _settle() }
    // Ready → Start, as Main.qml's onSessionStartRequested does it (l.754–768): the wizard
    // screen is left (hidden, not destroyed) and the session starts.
    function startSession() {
        var g = 0
        while (newWiz.currentStep !== "ready" && g++ < 20) {
            var b = newWiz.currentStep
            newWiz.goNext("done")
            if (newWiz.currentStep === b) newWiz.goNext("skipped")
        }
        tc.compare(newWiz.currentStep, "ready")
        setup.flow.exit("start")                     // Start: devices kept
        _settle()
        fx.navController.navigateRail(setup.presets[SessionController.Wrist].railIndex)
        setup.visible = false
        fx.sessionController.start(SessionController.Wrist)
    }
    // The toolbar IMU popup opened and its Calibrate action pressed (PpImuPanel l.211).
    function enterToolbarCalibrate() {
        if (!panel) _createPanel()
        panelHost.visible = true
        panel.mode = "calibrate"
        tc.tryVerify(function() { return panelFlow.visible }, 5000, "panel flow not visible")
    }

    // ── The toolbar popup ────────────────────────────────────────────────────
    // Fakes and devices for `setup`, no wizard; the panel is built on the first
    // openToolbar(), inside the real Popup. Its flow is `flow` (events src "panel").
    function hostToolbar(setup, opts) {
        opts = opts || {}
        setupName = setup
        scale = opts.scale !== undefined ? opts.scale : 1
        _makeFakes()
        _addDevices(setup, opts)
    }
    function openToolbar() {
        imuPopup.open()
        if (!panel) { _createPanel(true); flow = panelFlow }
        tc.tryVerify(function() { return imuPopup.opened && panel.visible }, 5000, "the popup did not open")
    }
    function closeToolbar() {
        imuPopup.close()
        tc.tryVerify(function() { return !imuPopup.visible }, 5000, "the popup did not close")
    }
    function popupOpen() { return imuPopup.opened }

    // ── Call logs ────────────────────────────────────────────────────────────
    // Count of `name` (or of every call) on every fake device.
    function deviceCalls(name, devId) {
        var n = 0, ds = devices()
        for (var i = 0; i < ds.length; ++i) {
            if (devId && ds[i].deviceId !== devId) continue
            var c = ds[i].calls
            for (var j = 0; j < c.length; ++j) if (!name || c[j].name === name) ++n
        }
        return n
    }
    function mark() {
        var m = { t: now(), ev: events.length, calls: {}, completed: {} }
        var ds = devices()
        for (var i = 0; i < ds.length; ++i) m.calls[ds[i].deviceId] = ds[i].calls.length
        for (var k in completedBy) m.completed[k] = completedBy[k]
        return m
    }
    // Device calls since `m`, oldest first, as {device, name, t}. `name` filters.
    function callsSince(m, name) {
        var out = [], ds = devices()
        for (var i = 0; i < ds.length; ++i) {
            var c = ds[i].calls, from = m.calls[ds[i].deviceId] || 0
            for (var j = from; j < c.length; ++j)
                if (!name || c[j].name === name)
                    out.push({ device: ds[i].deviceId, name: c[j].name, t: c[j].t })
        }
        out.sort(function(a, b) { return a.t - b.t })
        return out
    }
    function eventsSince(m, src, kind) {
        var out = []
        for (var i = m.ev; i < events.length; ++i) {
            var e = events[i]
            if (src && e.src !== src) continue
            if (kind && e.kind !== kind) continue
            out.push(e)
        }
        return out
    }
    // A fake's call time (Date.now()) on the driver's clock (testLog.elapsedMs()).
    function toElapsed(dateMs) { return dateMs + (now() - Date.now()) }
    function firstCall(devId, name) {
        var ds = devices()
        for (var i = 0; i < ds.length; ++i) {
            if (ds[i].deviceId !== devId) continue
            for (var j = 0; j < ds[i].calls.length; ++j)
                if (ds[i].calls[j].name === name) return ds[i].calls[j]
        }
        return null
    }
    function completedSince(m, src) { src = src || "wiz"; return (completedBy[src] || 0) - (m.completed[src] || 0) }
    function fmtCalls(list) {
        return JSON.stringify(list.map(function(c) { return c.device + "." + c.name })) }
    function fmtEvents(list, t0) {
        return JSON.stringify(list.map(function(e) {
            return e.src + ":" + e.kind + ":" + e.name
                   + (e.kind === "state" ? "=" + e.value : "")
                   + "@+" + Math.round(e.t - (t0 || 0))
        }))
    }
}
