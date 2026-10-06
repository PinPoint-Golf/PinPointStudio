// SetupDriver — the ADAPTER between the session-setup tests and the setup shell,
// ScreenSessionSetup.qml (docs/design/session_wizard_refactor_design.md §7.3). Written at Stage 5a
// as "SetupDriver2", beside a driver for the retired ScreenSessionWizard.qml with the same API;
// that driver and that wizard were deleted at Stage 5c and this one took the name.
//
// How it works:
//   - the shell is built over the fakes with harness.createWithContext("ScreenSessionSetup", …);
//   - navigation is QUEUED (R5): every action that may navigate settles on `flow.busy` — several
//     event-loop turns, never wait(0) — before it returns, so a case reads the result at once;
//   - footer and header controls are found by objectName;
//   - Main.qml's reactions are replicated as Stage 5b wrote them: Cancel no longer calls
//     releaseDevices() (the flow released them before cancelled()), ‹ on the first step arrives as
//     exitRequested("back") → navController.back(), Start reads presets[type].railIndex.
//
// Since Stage 5b every page is real: the sensor rows, their mounts, Scan, the calibration flow on
// the Calibrate page, the Check page's Recalibrate and the Ready page are read off the live page.
// A function that looks for something the current page does not show throws, so a case cannot
// pass on nothing.
//
// Step keys: "goals","cameras","framing","triangulate","ball","imus","calibrateArm","checkArm","ready".
import QtQuick
import PinPointStudio
import "../fakes"

Item {
    id: drv

    // ── Wiring supplied by the test file ─────────────────────────────────────
    property Item host: null
    property var  testCase: null
    property var  testLog: null

    // ── Fakes (created by setUp(), destroyed by tearDown()) ──────────────────
    property var imu:       null
    property var cams:      null
    property var liveWrist: null
    property var athlete:   null
    property var session:   null
    property var nav:       null
    property var lm:        null
    property var appLogFake: null
    property var journal:   null

    // The shell (named `wizard` so the cases read the same against both shells).
    property var wizard: null
    readonly property var flow:  wizard ? wizard.flow  : null
    readonly property var ctx:   wizard ? wizard.ctx   : null
    readonly property var draft: wizard ? wizard.draft : null

    property var shellCalls: []
    property var emitted: []

    // Main.qml screen indices (Main.qml:541–551).
    readonly property int screenHome:     0
    readonly property int screenSettings: 9
    readonly property int screenWizard:   10

    readonly property var stepKeys: ["goals", "cameras", "framing", "triangulate", "ball", "imus",
                                     "calibrateArm", "checkArm", "ready"]

    Component { id: imuMgrComp;    FakeImuManager {} }
    Component { id: camMgrComp;    FakeCameraManager {} }
    Component { id: athleteComp;   FakeAthlete {} }
    Component { id: liveWristComp; FakeLiveWrist {} }
    Component { id: sessionComp;   FakeSessionController {} }
    Component { id: navComp;       FakeNav {} }
    Component { id: lmComp;        FakeLaunchMonitor {} }
    Component { id: appLogComp;    FakeAppLog {} }
    // N17's test page (support/pages/ExtraPage.qml) reports to `pageJournal`.
    Component { id: journalComp;   PageJournal {} }
    // PpCameraFrame (the camera rows' thumbnails, the Ball frame) reads `shotReplay`…
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
    property var _shotReplay: null
    // …and `shotProcessor` (the live-capture replay source).
    Component {
        id: shotProcessorComp
        QtObject {
            property bool isReplaying: false
            property var  replayAnalysisDetail: ({})
            property real replayPositionUs: 0
            signal replayPositionChanged()
        }
    }
    property var _shotProcessor: null

    function _rec(name, args) { shellCalls.push({ name: name, args: args, t: Date.now() }) }

    // ── Lifecycle ────────────────────────────────────────────────────────────
    function setUp() {
        // The REAL AppSettings (scratch QSettings) is shared by every test in the file.
        appSettings.imuRoles           = ({})
        appSettings.imuAlias           = ({})
                appSettings.sessionGoalsByType = ({})
        appSettings.cameraExcluded     = []
        appSettings.imuExcluded        = []
        appSettings.cameraFixedInPlace = ({})
        imu        = imuMgrComp.createObject(drv)
        cams       = camMgrComp.createObject(drv)
        athlete    = athleteComp.createObject(drv)
        liveWrist  = liveWristComp.createObject(drv)
        session    = sessionComp.createObject(drv)
        nav        = navComp.createObject(drv)
        lm         = lmComp.createObject(drv)
        appLogFake = appLogComp.createObject(drv)
        journal    = journalComp.createObject(drv)
        _shotReplay = shotReplayComp.createObject(drv)
        _shotProcessor = shotProcessorComp.createObject(drv)
        shellCalls = []
        emitted = []
        wizard = null
    }

    function tearDown() {
        if (wizard) harness.destroyNow(wizard)
        wizard = null
        var objs = [imu, cams, athlete, liveWrist, session, nav, lm, appLogFake, journal, _shotReplay, _shotProcessor]
        for (var i = 0; i < objs.length; ++i) if (objs[i]) objs[i].destroy()
        imu = cams = athlete = liveWrist = session = nav = lm = appLogFake = journal = _shotReplay = _shotProcessor = null
    }

    function _create() {
        // Hidden at creation, as in Main's StackLayout before Home navigates to it.
        var w = harness.createWithContext("ScreenSessionSetup", {
            imuManager:        imu,
            cameraManager:     cams,
            athleteController: athlete,
            liveWrist:         liveWrist,
            sessionController: session,
            navController:     nav,
            launchMonitor:     lm,
            appLog:            appLogFake,
            shotReplay:        _shotReplay,
            shotProcessor:     _shotProcessor,
            pageJournal:       journal
        }, host, { width: host.width, height: host.height, visible: false })
        if (!w) throw new Error("SetupDriver: ScreenSessionSetup failed to instantiate")
        w.cancelled.connect(_onCancelled)
        w.sessionStartRequested.connect(_onSessionStartRequested)
        w.exitRequested.connect(_onExitRequested)
        w.navigateToSettings.connect(_onNavigateToSettings)
        wizard = w
        return w
    }

    // Main shows exactly one StackLayout child: the shell is visible iff it is current.
    function _syncShell() { if (wizard) wizard.visible = (nav.currentIndex === screenWizard) }

    // Queued navigation: wait until nothing is queued and no page is loading.
    function settle() {
        if (!wizard) return
        testCase.tryVerify(function() { return !wizard.flow.busy }, 5000, "the setup flow never settled")
    }

    // ── Main.qml replicas, as Stage 5b wrote them ────────────────────────────
    // ScreenHome.onStartSessionRequested (Main.qml:674–680), with reset → open:
    //     sessionController.activeClub = screenHome.selectedClub
    //     sessionSetup.open(sessionTypeIndex)
    //     navController.navigate(root.screenWizard)
    function open(type) {
        var w = wizard || _create()
        if (nav.currentIndex === screenWizard) {    // coming from Home: the shell was hidden
            nav.currentIndex = screenHome
            _syncShell()
        }
        w.open(type)
        nav.navigate(screenWizard)
        nav.canGoBack = true
        _syncShell()
        settle()
        return w
    }

    // Main.qml (ScreenSessionSetup.onCancelled) — the devices were released by the flow before cancelled():
    //     onCancelled: {
    //         sessionController.activeClub = ""
    //         navController.navigate(root.screenHome)
    //     }
    function _onCancelled() {
        emitted.push({ name: "cancelled", args: [], t: Date.now() })
        session.activeClub = ""
        nav.navigate(screenHome)
        _syncShell()
    }

    // Main.qml (onSessionStartRequested) — as before, the rail index read from `presets`:
    //     navController.navigateRail(sessionSetup.presets[type].railIndex)
    function _onSessionStartRequested(type, goals) {
        var g = []
        for (var i = 0; i < goals.length; ++i) g.push(goals[i])
        emitted.push({ name: "sessionStartRequested", args: [type, g], t: Date.now() })
        var map = appSettings.sessionGoalsByType
        map[type.toString()] = g
        appSettings.sessionGoalsByType = map
        appSettings.lastSessionType    = type
        var rail = wizard.presets[type].railIndex
        nav.navigateRail(rail)
        nav.currentIndex = rail
        _rec("shotProcessor.beginSessionFolder", [type, false])
        _rec("shotModel.loadSessionDir", [""])
        session.start(type)
        cams.startCapture()
        _syncShell()
    }

    // ‹ on the first step — the flow released the devices; Main goes back.
    //     onExitRequested: navController.back()
    function _onExitRequested(reason) {
        emitted.push({ name: "exitRequested", args: [reason], t: Date.now() })
        nav.back()
        nav.currentIndex = screenHome               // FakeNav.back() only records
        _syncShell()
    }

    // Main.qml:771–774 (unchanged):
    //     onNavigateToSettings: { settingsScreen.activeNavIndex = panelIndex; navController.navigate(root.screenSettings) }
    function _onNavigateToSettings(panelIndex) {
        emitted.push({ name: "navigateToSettings", args: [panelIndex], t: Date.now() })
        nav.navigate(screenSettings)
        _syncShell()
    }

    // The app header (Main.qml PpHeader):
    //     backEnabled:    onWizard ? sessionSetup.canHeaderBack    : navController.canGoBack
    //     forwardEnabled: onWizard ? sessionSetup.canHeaderForward : navController.canGoForward
    //     onBackRequested:    onWizard ? sessionSetup.headerBack()    : navController.back()
    //     onForwardRequested: onWizard ? sessionSetup.headerForward() : navController.forward()
    function headerBackEnabled() {
        return nav.currentIndex === screenWizard ? wizard.canHeaderBack : nav.canGoBack
    }
    function headerForwardEnabled() {
        return nav.currentIndex === screenWizard ? wizard.canHeaderForward : nav.canGoForward
    }
    function headerBack() {
        if (!headerBackEnabled()) return false
        if (nav.currentIndex === screenWizard) wizard.headerBack()
        else                                   nav.back()
        settle()
        return true
    }
    function headerForward() {
        if (!headerForwardEnabled()) return false
        if (nav.currentIndex === screenWizard) wizard.headerForward()
        else                                   nav.forward()
        settle()
        return true
    }

    // A trip to Settings and back: the shell is hidden (suspend), not destroyed.
    function suspend() {
        nav.navigate(screenSettings)
        _syncShell()
        settle()
    }
    function resume() {
        nav.back()
        nav.currentIndex = screenWizard
        _syncShell()
        settle()
    }

    // ── Hardware fixtures (as SetupDriver) ───────────────────────────────────
    function addCamera(key, perspective, selected, alias) {
        cams.addCamera(key, perspective, alias || key)
        if (selected) cams.setCameraSelected(key, true)
    }
    function addWitmotion(id, slot, connected) {
        imu.addWitmotion(id, "")
        if (slot) imu.assign(id, slot)
        return connected ? imu.connectDevice(id) : null
    }
    function addWg3(id, connected) {
        imu.addHackMotion(id, "")
        imu.assign(id, "A")
        return connected ? imu.connectDevice(id) : null
    }
    function addCameraInstance(key, props) { return cams.addInstance(key, props) }
    function cameraEnabled(key) {
        var l = cams.cameraList
        for (var i = 0; i < l.length; ++i) if (l[i].cameraKey === key) return l[i].sessionEnabled
        return undefined
    }
    function imuEnabled(id) {
        var l = imu.imuDeviceList
        for (var i = 0; i < l.length; ++i) if (l[i].id === id) return l[i].sessionEnabled
        return undefined
    }

    // ── Plan / state ─────────────────────────────────────────────────────────
    // The flow's plan, which PINS the current step until it is left (§4.5) — the one place the
    // shell's answer differs by design from the retired wizard's (see N14).
    function plan() { return flow.plan.slice() }
    function current() { return flow.current }
    function state(key) { return draft.state(key) }
    function states() {
        var p = plan(), out = {}
        for (var i = 0; i < p.length; ++i) out[p[i]] = state(p[i])
        return out
    }
    // The selected chips in chip (preset) order, as SetupDriver reads them, while Goals is
    // current; otherwise the draft's picks in that same order (the chips show the draft).
    function selectedGoals() {
        var chips = _goalChips(), out = []
        if (chips.length === 0) {
            var defs = draft.goalDefs
            for (var d = 0; d < defs.length; ++d) if (draft.goals.indexOf(defs[d].key) >= 0) out.push(defs[d].key)
            return out
        }
        for (var i = 0; i < chips.length; ++i) if (chips[i].sel) out.push(chips[i].modelData.key)
        return out
    }
    function sessionType() { return draft.preset }

    // ── Tree walking ─────────────────────────────────────────────────────────
    function findAll(item, pred, out) {
        out = out || []
        if (!item) return out
        if (pred(item)) out.push(item)
        var kids = item.children
        if (kids) for (var i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
        return out
    }
    function find(item, pred) { var a = findAll(item, pred); return a.length > 0 ? a[0] : null }
    function typeName(o) { return String(o).split("(")[0].split("_QML")[0] }
    function _isText(o) { return typeName(o) === "QQuickText" }
    function _isButton(o) { return o.label !== undefined && o.primary !== undefined && o.clicked !== undefined
                                   && o.destructive !== undefined }
    function _named(name) { return find(wizard, function(o) { return o.objectName === name }) }
    function _goalChips() {
        return findAll(wizard, function(o) { return o.sel !== undefined && o.showLast !== undefined
                                                    && o.modelData !== undefined })
    }

    function _primary() {
        var p = _named("setupPrimary")
        if (!p) throw new Error("SetupDriver: footer primary button not found")
        return p
    }
    function _click(item, what) {
        if (!item) throw new Error("SetupDriver: " + what + " not found")
        if (!item.visible) throw new Error("SetupDriver: " + what + " is not visible")
        for (var p = item; p && p !== host; p = p.parent) testCase.waitForItemPolished(p)
        testCase.mouseClick(item)
    }

    // ── Footer ───────────────────────────────────────────────────────────────
    function primaryLabel() {
        var t = find(_primary(), _isText)
        return t ? t.text : ""
    }
    function primaryDimmed() {
        var p = _primary()
        testCase.tryVerify(function() { var o = p.opacity
                                        return o === 1 || Math.abs(o - 0.4) < 1e-3 || Math.abs(o - 0.8) < 1e-3 },
                           2000, "primary opacity never settled")
        return p.opacity < 0.5
    }
    function primaryTone() {
        var c = _primary()._primaryBase
        function is(base) { return Qt.colorEqual(c, base) }
        if (is(Theme.colorGood)) return "good"
        if (is(Theme.colorWarn)) return "warn"
        if (is(Theme.colorAccent)) return "accent"
        return String(c)
    }
    function clickPrimary() { _click(_primary(), "footer primary button"); settle() }
    function hint() { var t = _named("setupHint"); return t ? t.text : "" }
    function canSkip() { var b = _named("setupSkip"); return b !== null && b.visible }
    function canBack() { var b = _named("setupBack"); return b !== null && b.visible }

    function next() {
        var l = primaryLabel()
        if (l !== "Continue →") throw new Error("SetupDriver.next(): the primary reads '" + l + "', not Continue")
        clickPrimary()
    }
    function skip()   { _click(_named("setupSkip"), "footer Skip button"); settle() }
    function back()   { _click(_named("setupBack"), "footer Back button"); settle() }
    function cancel() { _click(_named("setupCancel"), "Cancel button"); settle() }
    function start() {
        var l = primaryLabel()
        if (l.indexOf("Start") < 0) throw new Error("SetupDriver.start(): the primary reads '" + l + "'")
        clickPrimary()
    }
    function walkTo(key) {
        var seq = [], guard = 0
        while (current() !== key && guard++ < 12) {
            var before = current()
            if (primaryLabel() === "Continue →") {
                clickPrimary()
                if (current() !== before) { seq.push([before, "continue"]); continue }
            }
            if (canSkip()) { skip(); seq.push([before, "skip"]); continue }
            throw new Error("SetupDriver.walkTo(" + key + "): stuck on " + before + " (primary '"
                            + primaryLabel() + "', no Skip)")
        }
        return seq
    }

    // ── Page content ─────────────────────────────────────────────────────────
    function stepLabel() {
        var ts = findAll(wizard, function(o) { return _isText(o) && o.visible && /^STEP /.test(o.text) })
        if (ts.length !== 1)
            throw new Error("SetupDriver.stepLabel(): " + ts.length + " visible eyebrows: "
                            + JSON.stringify(ts.map(function(t) { return t.text })))
        return ts[0].text
    }
    // Every step's eyebrow. Only the current page is alive (R2), so the others are the label the
    // flow will bind their eyebrow to (WizardPage.stepLabel ← flow.stepLabelFor(key)); the live
    // page's own eyebrow must read the same, and is checked to.
    function allStepLabels() {
        var p = plan(), out = []
        for (var i = 0; i < p.length; ++i) out.push(flow.stepLabelFor(p[i]))
        var live = findAll(wizard, function(o) { return _isText(o) && /^STEP /.test(o.text) })
        for (var j = 0; j < live.length; ++j)
            if (out.indexOf(live[j].text) < 0)
                throw new Error("SetupDriver.allStepLabels(): the live eyebrow '" + live[j].text
                                + "' is not the flow's " + JSON.stringify(out))
        return out
    }
    // Indicator pip for `key`: {state: "current"|"done"|"skipped"|"future", glyph, shown}.
    // A step not in the plan (or leaving it) reads shown: false.
    function _pipItem(key) {
        var ind = _named("setupIndicator")
        return find(ind, function(o) { return o.objectName === "flowPip" && o.stepKey === key && o.present })
    }
    function pip(key) {
        var c = _pipItem(key)
        if (!c) return { state: "absent", glyph: "", shown: false }
        var s = c.stateName === "pending" ? "future" : c.stateName
        return { state: s, glyph: c.glyph, shown: c.shown, attention: c.attention, tip: c.tipText,
                 clickable: c.clickable }
    }
    // New-shell only (N16, P3, P5): click a pip, hover one.
    function clickPip(key) {
        var c = _pipItem(key)
        if (!c) throw new Error("SetupDriver.clickPip(): no pip for " + key)
        var area = find(c, function(o) { return typeName(o) === "QQuickMouseArea" })
        for (var p = area; p && p !== host; p = p.parent) testCase.waitForItemPolished(p)
        testCase.mouseClick(area)
        settle()
    }
    function hoverPip(key) {
        var c = _pipItem(key)
        if (!c) throw new Error("SetupDriver.hoverPip(): no pip for " + key)
        var area = find(c, function(o) { return typeName(o) === "QQuickMouseArea" })
        testCase.mouseMove(area, area.width / 2, area.height / 2)
        return find(c, function(o) { return o.objectName === "flowPipTip" })
    }
    function readinessIssues() {
        var src = flow.issues, out = []
        for (var i = 0; i < src.length; ++i) out.push({ text: src[i].text, panel: src[i].panel })
        return out
    }
    // Ready's "What is set up" table as SHOWN (the flow's summary rows and the launch monitor).
    function summaryRows() {
        if (current() !== "ready") throw new Error("SetupDriver.summaryRows(): not on Ready")
        return findAll(flow.page, function(o) { return o.rowLabel !== undefined && o.rowValue !== undefined
                                                       && o.good !== undefined && o.visible })
                   .map(function(r) {
                       var o = { label: r.rowLabel, value: r.rowValue, good: r.good }
                       if (r.tone !== "") o.tone = r.tone      // only "neutral" rows carry one
                       return o })
    }
    // Ready's "What this session will record" as SHOWN: one { family, state, reason, tone, text }
    // per row, `text` the row as one line ("Wrist — Not recorded · no wrist sensor").
    function capabilityRows() {
        if (current() !== "ready") throw new Error("SetupDriver.capabilityRows(): not on Ready")
        return findAll(flow.page, function(o) { return o.objectName === "capabilityRow" && o.visible })
                   .map(function(r) { return { family: r.family, state: r.stateText, reason: r.reason, tone: r.tone,
                                               text: r.family + " — " + r.stateText
                                                     + (r.reason !== "" ? " · " + r.reason : "") } })
    }
    function _readyText(name) {
        if (current() !== "ready") throw new Error("SetupDriver: not on Ready")
        var t = find(flow.page, function(o) { return o.objectName === name && o.visible })
        return t ? t.text : ""
    }
    function readyHeading() { return _readyText("readyHeading") }
    function readyNotice()  { return _readyText("readyNotice") }

    function toggleGoal(key) {
        _click(find(wizard, function(o) { return o.sel !== undefined && o.showLast !== undefined
                                                 && o.modelData !== undefined && o.modelData.key === key }),
               "goal chip " + key)
        settle()
    }

    // Device rows: {label, sub, ok, warn, disabled, chip, toggleOn}
    function _rows(prefixRe) {
        return findAll(wizard, function(o) { return o.subOk !== undefined && o.subFail !== undefined
                                                    && o.chipText !== undefined && o.toggled !== undefined
                                                    && prefixRe.test(o.label) })
    }
    function _rowInfo(r) {
        return { label: r.label,
                 sub: r.disabled ? r.subDisabled : (r.ok ? r.subOk : (r.warn ? r.subWarn : r.subFail)),
                 ok: r.ok, warn: r.warn, disabled: r.disabled, chip: r.chipText,
                 toggleShown: r.showToggle, toggleOn: r.toggleChecked,
                 deviceId: r.deviceId !== undefined ? r.deviceId : "",
                 absent: r.objectName === "absentMountRow" }
    }
    // The Sensors page's rows, one per enumerated sensor, then one per mount whose sensor is not
    // found. Throws off the Sensors page.
    function _sensorRows() {
        if (current() !== "imus") throw new Error("SetupDriver: not on the Sensors page (on " + current() + ")")
        return findAll(flow.page, function(o) {
            return (o.objectName === "sensorRow" || o.objectName === "absentMountRow") && o.visible })
    }
    function imuRows() { return _sensorRows().map(_rowInfo) }
    function cameraRows() { return _rows(/camera/i).map(_rowInfo) }
    function _toggleIn(row) {
        return find(row, function(o) { return o !== row && o.checked !== undefined && o.toggled !== undefined })
    }
    function toggleCameraRow(text) {
        var r = _rows(/camera/i).filter(function(o) { return o.label.indexOf(text) >= 0 })[0]
        if (!r) throw new Error("SetupDriver: no camera row containing '" + text + "'")
        _click(_toggleIn(r), "enable switch on '" + r.label + "'")
        settle()
    }
    // The old slot letters, for the cases written against them: the sensor holding that slot's
    // role (A leadForearm, B leadHand, C leadUpperArm).
    readonly property var _slotRoles: ({ A: "leadForearm", B: "leadHand", C: "leadUpperArm" })
    function _sensorRow(deviceId) {
        var r = _sensorRows().filter(function(o) { return o.objectName === "sensorRow" && o.deviceId === deviceId })[0]
        if (!r) throw new Error("SetupDriver: no sensor row for " + deviceId)
        return r
    }
    function toggleImuRow(slot) {
        var id = ctx.roles[_slotRoles[slot]].deviceId
        var r = _sensorRow(id)
        _click(_toggleIn(r), "enable switch on '" + r.label + "'")
        settle()
    }
    // ── The mount question (Sensors page) ────────────────────────────────
    // The column under a sensor's row that holds its mount line.
    function _mountColumn(deviceId) {
        var row = _sensorRow(deviceId)
        var col = row.parent              // the delegate Column: the row, then the mount block
        return col
    }
    function mountQuestionShown(deviceId) {
        var q = find(_mountColumn(deviceId), function(o) { return o.objectName === "mountQuestion" })
        return q !== null && q.visible
    }
    // The choices shown: [{ role, text, taken }].
    function mountOptions(deviceId) {
        return findAll(_mountColumn(deviceId), function(o) { return o.objectName === "mountOption" && o.visible })
                   .map(function(o) { return { role: o.role, text: o.optionText, taken: o.taken } })
    }
    function chooseMount(deviceId, role) {
        var o = find(_mountColumn(deviceId), function(x) { return x.objectName === "mountOption" && x.role === role
                                                                 && x.visible })
        if (!o) throw new Error("SetupDriver.chooseMount(): no option " + role + " for " + deviceId)
        _click(o, "mount option " + role)
        settle()
    }
    function clickChangeMount(deviceId) {
        _click(find(_mountColumn(deviceId), function(o) { return o.objectName === "mountChange" }),
               "change-mount link for " + deviceId)
        settle()
    }
    function mountRefusal(deviceId) {
        var t = find(_mountColumn(deviceId), function(o) { return o.objectName === "mountRefusal" })
        return t !== null && t.visible ? t.text : ""
    }
    function _scanButton() {
        if (current() !== "imus") throw new Error("SetupDriver: Scan is on the Sensors page (on " + current() + ")")
        var b = find(flow.page, function(o) { return o.objectName === "sensorScan" })
        if (!b) throw new Error("SetupDriver: Scan button not found")
        return b
    }
    function scanLabel() {
        var t = findAll(_scanButton(), function(o) { return _isText(o) && o.visible })
        return t.length ? t[0].text : ""
    }
    function clickScan() { _click(_scanButton(), "Scan button"); settle() }
    // "Recalibrate" text link on Check (not the Triangulate page's PpButton of that name).
    function clickRecalibrate() {
        if (current() !== "checkArm") throw new Error("SetupDriver.clickRecalibrate(): not on Check")
        _click(find(flow.page, function(o) { return _isText(o) && o.text === "Recalibrate" && o.visible
                                                    && o.children.length > 0 }), "Recalibrate link")
        settle()
    }

    // ── Progress / internals the cases observe (read-only) ───────────────────
    // The arm group's recorded outcome, checked against the devices' live validity (R8).
    function calibrationDone()    { return draft.outcome("arm").done }
    // The Calibrate page's calibration flow, or null when Calibrate is not the current page (it
    // exists only then — R2).
    function _calibFlow() {
        if (!flow || flow.page === null) return null
        return find(flow.page, function(o) { return o.calibrationDone !== undefined && o.isHackMotion !== undefined
                                                    && o.begin !== undefined && o._guide !== undefined })
    }
    function guideReady() { var f = _calibFlow(); return f !== null && f._guide.view !== null && f._guide.view.fullyLoaded }
    function calibrationRunning() { var f = _calibFlow(); return f !== null && f._runRequested }
    // The paced sensor connect lives in ImuManager now (Stage 1, F10).
    function imuQueueActive() { return imu.pacedConnectActive === true }
    function connectingFrameRunning() {
        var f = find(wizard, function(o) { return typeName(o) === "PpConnectingFrame" })
        return f ? f.running : false
    }
    // The Ball page's "Learning…" flag; false when the Ball page is not alive (it cannot be
    // learning then — the page and its timer are gone).
    function ballLearning() {
        var p = flow.page
        return p !== null && p.learning !== undefined ? p.learning : false
    }
    function clickLearn() {
        _click(find(wizard, function(o) { return _isButton(o) && o.label === "Learn hitting area" }),
               "Learn hitting area button")
    }
    // The old wizard's _forceImuRecalibrate has no counterpart: Recalibrate is
    // flow.goTo(key, true), applied at once. Nothing can be left set.
    function recalibrateFlagSet() { return false }

    // Live page objects under the shell: WizardPage instances (the Loader's item, and nothing else).
    function pageObjects() {
        return findAll(wizard, function(o) { return o.stepKey !== undefined && o.enter !== undefined
                                                    && o.leave !== undefined && o.canContinue !== undefined
                                                    && o.objectName !== "flowPip" }).length
    }
    function view3dCount() {
        return findAll(wizard, function(o) { var n = typeName(o)
                                             return n === "View3D" || n === "QQuick3DViewport" }).length
    }
    function armVizCount() {
        return findAll(wizard, function(o) { return typeName(o) === "ArmVizView" }).length
    }
}
