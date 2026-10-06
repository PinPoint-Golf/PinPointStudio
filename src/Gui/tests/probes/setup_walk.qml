// END-TO-END: SESSION SETUP, WALKED IN THE REAL APP (session-setup refactor, Stage 5b).
//
// The setup tests (tests/setup/) run the shell over fakes; they cannot see the real managers, the
// real module as the app loads it, or Main's own wiring. This drives the real app:
//
//   find Home and the setup shell → open setup for the Wrist preset EXACTLY as Main does (Home's
//   startSessionRequested(Wrist): Main sets the club, calls sessionSetup.open() and navigates) →
//   walk every step to Ready with Continue, or Skip where a gate needs hardware → at each step log
//   ONE "PROBE: step" line: the eyebrow, the indicator model, the footer (primary, hint, Skip,
//   Back) → on Ready log every issue, every "What is set up" row and every "What this session will
//   record" row verbatim → Cancel (the flow releases devices) → exit.
//
// At every step it asserts: exactly one page object alive under the shell (R2, L1) and at most one
// View3D under the shell (L2).
//
// ⚠ NEVER PRESSES Connect (Cameras or Sensors) and never presses Learn, Scan, Recalibrate or a
// mount: it connects nothing, writes no setting, and plays no sound. To guarantee no calibration
// starts, by default it disables every sensor FOR THIS SESSION before walking (session-only
// enablement, manager-owned and re-seeded from settings on the next open — not saved), so no arm
// step applies. `--probe-sensors on` keeps them: if a sensor of yours is found and mounted, the
// Calibrate page is entered and its routine BEGINS (the guide's intro plays), but with nothing
// connected it can make no device call and cannot complete, so no ting.
//
// Headless-safe: no window interaction; it polls on a 200 ms timer.
//
// Run it against a COPY of the settings file (build/run-me/setup-walk-probe.sh does this):
//   XDG_CONFIG_HOME=<copy dir> PINPOINT_LOG_STDERR=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi \
//     <PinPointStudio> --probe-qml <abs path to this file> [--probe-sensors on] [--probe-wait-ms 6000]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property int  waitMs:      parseInt(arg("--probe-wait-ms", "6000"))
    readonly property bool keepSensors: arg("--probe-sensors", "off") === "on"

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        if (!ok) { log("FAIL " + what); failures += 1 }
        return ok
    }
    function finish() {
        tick.running = false
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function findAll(item, pred, out) {
        out = out || []
        if (!item) return out
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
        return out
    }
    function find(item, pred) { const a = findAll(item, pred); return a.length > 0 ? a[0] : null }
    function rootItem() { let t = probe; while (t.parent) t = t.parent; return t }
    function typeName(o) { return String(o).split("(")[0].split("_QML")[0] }
    function json(v) { return JSON.stringify(v) }

    property double startedMs: Date.now()
    property int    stage: 0
    property var    home: null
    property var    shell: null
    property string lastKey: ""
    property int    visits: 0
    property double waitSince: 0
    property bool   framingLogged: false

    Timer {
        id: tick
        interval: 200; running: true; repeat: true
        onTriggered: probe.step()
    }

    // The footer as shown.
    function footer() {
        const prim = find(shell, function(o) { return o.objectName === "setupPrimary" })
        const lbl  = prim ? find(prim, function(o) { return typeName(o) === "QQuickText" }) : null
        const hint = find(shell, function(o) { return o.objectName === "setupHint" })
        const skip = find(shell, function(o) { return o.objectName === "setupSkip" })
        const back = find(shell, function(o) { return o.objectName === "setupBack" })
        return { primary: lbl ? lbl.text : "", dimmed: prim ? prim.opacity < 0.5 : false,
                 hint: hint ? hint.text : "", skip: skip !== null && skip.visible,
                 back: back !== null && back.visible }
    }
    function indicatorModel() {
        return shell._indicatorModel.map(function(s) {
            return s.number + ":" + s.key + "=" + s.state + (s.current ? "*" : "") + (s.attention ? "!" : "")
        }).join(" ")
    }
    function livePages() {
        return findAll(shell, function(o) { return o.stepKey !== undefined && o.enter !== undefined
                                                  && o.leave !== undefined && o.canContinue !== undefined
                                                  && o.objectName !== "flowPip" }).length
    }
    function view3Ds() {
        return findAll(shell, function(o) { const n = typeName(o); return n === "View3D" || n === "QQuick3DViewport" }).length
    }

    function step() {
        const elapsed = Date.now() - startedMs
        if (stage === 0) {
            home  = find(rootItem(), function(o) { return o.startSessionRequested !== undefined && o.selectedClub !== undefined })
            shell = find(rootItem(), function(o) { return o.presets !== undefined && o.canHeaderForward !== undefined
                                                         && o.flow !== undefined && o.headerBack !== undefined })
            if ((home === null || shell === null) && elapsed < waitMs) return
            if (!check(home !== null, "Home (ScreenHome) found") || !check(shell !== null, "the setup shell (ScreenSessionSetup) found")) {
                finish(); return
            }
            // Let the enumerator list remembered sensors before the open() seeds enablement.
            if (elapsed < waitMs) return
            log("open: Home.startSessionRequested(Wrist) — Main's handler sets the club, opens setup, navigates")
            home.startSessionRequested(SessionController.Wrist)
            stage = 1
            return
        }
        if (stage === 1) {
            if (shell.flow.busy || !shell.flow.isOpen || shell.flow.current === "") return
            check(shell.visible, "the shell is showing after open")
            if (!keepSensors) {
                const devs = shell.ctx.devices
                for (let i = 0; i < devs.length; ++i)
                    if (devs[i].sessionEnabled) shell.ctx.setSensorEnabled(devs[i].id, false)
                log("sensors disabled for this session (" + devs.length + " listed) — no arm step can apply; "
                    + "--probe-sensors on to keep them")
            }
            stage = 2
            return
        }
        if (stage === 2) {
            if (shell.flow.busy || shell.flow.page === null) return
            const key = shell.flow.current
            if (key !== lastKey) {
                lastKey = key
                visits += 1
                waitSince = Date.now()
                const f = footer()
                log("step " + visits + " " + key + " | eyebrow '" + shell.flow.stepLabelFor(key) + "' | indicator "
                    + indicatorModel() + " | primary '" + f.primary + "'" + (f.dimmed ? " (dimmed)" : "")
                    + " hint '" + f.hint + "' skip " + f.skip + " back " + f.back
                    + " | pages " + livePages() + " View3D " + view3Ds())
                check(livePages() === 1, key + ": exactly one page alive (" + livePages() + ")")
                check(view3Ds() <= 1, key + ": at most one View3D under the shell (" + view3Ds() + ")")
            }
            if (key === "framing" && !framingLogged) framingReport()
            if (key === "ready") { readyReport(); return }
            if (visits > 20) { check(false, "the walk did not reach Ready in 20 steps"); finish(); return }
            // A page that is still connecting something it was asked to before the probe (it never
            // asks) — give it a moment, then go on with Skip.
            const f2 = footer()
            if (f2.primary === "Continue →" && shell.flow.canContinue) { shell.flow.next("done"); return }
            if (f2.skip) { shell.flow.next("skipped"); return }
            if (Date.now() - waitSince < 5000) return
            check(false, key + ": neither Continue nor Skip is offered (primary '" + f2.primary + "')")
            finish()
            return
        }
        if (stage === 4) {
            if (shell.flow.busy) return
            check(!shell.flow.isOpen, "Cancel closed the flow")
            check(livePages() === 0 && view3Ds() === 0, "nothing left alive under the shell after Cancel")
            finish()
        }
    }

    // The Framing step (pages/FramingPage.qml): one verdict row per connected camera that sees the
    // golfer. Headless, nobody holds the top, so a live camera reads "Hold the top…".
    function framingReport() {
        framingLogged = true
        const rows = findAll(shell.flow.page, function(o) { return o.objectName.indexOf("framingCamera_") === 0 })
        log("framing cameras " + rows.length + " | hint '" + footer().hint + "'")
        for (let i = 0; i < rows.length; ++i)
            log("  framing '" + rows[i].cameraKey + "' verdict " + rows[i].verdict + " '" + rows[i].verdictText
                + "' pose " + (rows[i].instance ? rows[i].instance.poseEnabled : "no instance"))
        check(rows.length === shell.ctx.framingCameras.length,
              "one framing row per connected camera (" + rows.length + " of " + shell.ctx.framingCameras.length + ")")
    }
    // Whether Framing was in the walk, and why: it applies only with a connected camera that sees
    // the golfer (SetupSteps.qml).
    function framingPresence() {
        const n = shell.ctx.framingCameras.length
        const inPlan = shell.flow.plan.indexOf("framing") >= 0
        log("framing step " + (inPlan ? "applies" : "does not apply") + " — " + n
            + " connected camera(s) that see the golfer" + (framingLogged ? ", visited" : ""))
        check(inPlan === (n > 0), "framing applies iff a camera is connected (plan " + inPlan + ", cameras " + n + ")")
        check(inPlan === framingLogged, "framing visited iff it applies")
    }

    function readyReport() {
        stage = 3
        framingPresence()
        const page = shell.flow.page
        const f = footer()
        const heading = find(page, function(o) { return o.objectName === "readyHeading" })
        const notice  = find(page, function(o) { return o.objectName === "readyNotice" })
        log("ready heading '" + (heading ? heading.text : "?") + "' | start '" + f.primary + "'")
        const iss = shell.flow.issues
        log("ready issues " + iss.length)
        for (let i = 0; i < iss.length; ++i) log("  issue '" + iss[i].text + "' panel " + iss[i].panel)
        const rows = findAll(page, function(o) { return o.rowLabel !== undefined && o.rowValue !== undefined && o.visible })
        for (let r = 0; r < rows.length; ++r)
            log("  set up '" + rows[r].rowLabel + "' = '" + rows[r].rowValue + "' " + (rows[r].good ? "good" : "warn"))
        const cap = findAll(page, function(o) { return o.objectName === "capabilityRow" && o.visible })
        for (let c = 0; c < cap.length; ++c)
            log("  records '" + cap[c].family + " — " + cap[c].stateText
                + (cap[c].reason !== "" ? " · " + cap[c].reason : "") + "' (" + cap[c].tone + ")")
        log("ready notice '" + (notice ? notice.text : "?") + "'")
        check(cap.length === 6, "six capability rows on Ready (" + cap.length + ")")
        check(rows.length >= 4, "the set-up table is shown (" + rows.length + " rows)")
        // Leave as Cancel does: the flow releases what it connected (nothing, here).
        shell.flow.exit("cancel")
        stage = 4
    }
}
