// RENDER SOAK: SESSION SETUP'S 3-D VIEWS, CREATED AND DESTROYED FOR SIX MINUTES (design §7.4 RS3,
// session-setup refactor Stage 5b; memory note view3d-disappearance-watch).
//
// Since Stage 5b the Calibrate page's guide (BodyVizView) and the Check page's arm view
// (ArmVizView) exist only while their page is current, so every Calibrate ↔ Check move destroys
// one View3D and builds the other. This probe makes that move every `--probe-period-s` (30 s) for
// `--probe-soak-s` (360 s), WINDOWED, and each time, just before it moves:
//   - grabs the current 3-D view and a deliberately blank control item (grabToImage);
//   - logs the luminance standard deviation of each (the grab is drawn into a small Canvas and
//     read back with getImageData — no file needed); a rendered view must beat the blank control;
//   - with `--probe-out <dir>`, also saves both grabs as PNGs there (cycle-numbered), so a run whose
//     in-JS measure fails can still be judged by eye;
//   - counts the View3Ds under the shell (must be ≤ 1).
// One "PROBE: cycle" line per cycle; "RESULT PASS" / "RESULT FAIL (n)" at the end; exit 0 / 1.
//
// ── THE NO-SENSOR MODE, AND WHAT IT CAN AND CANNOT EXERCISE ──────────────────────────────────────
// Without hardware there is no arm group, so the registry offers no Calibrate or Check step. The
// probe therefore registers two steps of its OWN through SetupSteps.extensions (the extension seam
// test N17 proves): "soakCalibrate" on the REAL pages/CalibrateArmPage.qml and "soakCheck" on the
// REAL pages/CheckArmPage.qml, applying always, after the Sensors step. Every sensor is disabled for
// this session first (session-only, not saved), so your own arm steps cannot apply.
//   Exercised: the real shell's Loader-per-page lifecycle for both 3-D pages, every 30 s for six
//   minutes — BodyVizView's 19-GLB load, the guide's fullyLoaded gate and the Witmotion routine's
//   intro animation (the routine begins on entry and plays the intro; with no sensor its phase-1
//   hold never runs, so it makes no device call and never completes — no ting); ArmVizView built
//   and torn down; liveWrist switched on and off by the Check page; the View3D count; and whether a
//   view renders blank after N rebuilds.
//   NOT exercised: any sensor (no Connect is pressed), the capture chain past the intro, the
//   HackMotion routine and its guide camera, the live arm tracking on Check (the arm stays at rest;
//   readouts read "—"), the mount question, and anything the real arm descriptors gate on.
//
// Run WINDOWED, on a visible Space (memory note mac-accessed-over-vnc: a hidden window may not
// render), against a COPY of the settings (build/run-me/setup-soak-probe.sh does this):
//   XDG_CONFIG_HOME=<copy dir> PINPOINT_LOG_STDERR=1 <PinPointStudio> --probe-qml <abs path to this file> \
//     [--probe-soak-s 360] [--probe-period-s 30] [--probe-out <dir for PNGs>]

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
    readonly property int    soakMs:   parseInt(arg("--probe-soak-s", "360")) * 1000
    readonly property int    periodMs: parseInt(arg("--probe-period-s", "30")) * 1000
    readonly property string outDir:   arg("--probe-out", "")
    readonly property int    waitMs:   parseInt(arg("--probe-wait-ms", "6000"))
    // A rendered avatar against the background shows real contrast; the blank control has none.
    readonly property real   minStd:   2.0

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
    function view3Ds() {
        return findAll(shell, function(o) { const n = typeName(o); return n === "View3D" || n === "QQuick3DViewport" })
    }

    // The blank control: a flat rectangle the size of a small grab, drawn under everything.
    Rectangle {
        id: blankControl
        z: -100
        width: 160; height: 100
        color: Theme.colorBg
    }
    // The meter: a grab is drawn into it (scaled down) and read back.
    Canvas {
        id: meter
        z: -101
        width: 96; height: 60
        opacity: 0.01
        renderStrategy: Canvas.Immediate
        property var pending: ({})       // url → callback
        onImageLoaded: {
            for (const url in pending) {
                if (!isImageLoaded(url)) continue
                const cb = pending[url]
                delete pending[url]
                cb(probe.stdOf(url))
            }
        }
    }
    // Luminance standard deviation of `url` drawn into the meter; NaN when it cannot be read.
    function stdOf(url) {
        try {
            const ctx = meter.getContext("2d")
            ctx.clearRect(0, 0, meter.width, meter.height)
            ctx.drawImage(url, 0, 0, meter.width, meter.height)
            const d = ctx.getImageData(0, 0, meter.width, meter.height).data
            let n = 0, sum = 0, sum2 = 0
            for (let i = 0; i + 3 < d.length; i += 4) {
                const y = 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2]
                sum += y; sum2 += y * y; ++n
            }
            if (n === 0) return NaN
            const m = sum / n
            return Math.sqrt(Math.max(0, sum2 / n - m * m))
        } catch (e) {
            log("meter: " + e)
            return NaN
        }
    }
    function measure(url, cb) {
        if (meter.isImageLoaded(url)) { cb(stdOf(url)); return }
        const p = meter.pending; p[url] = cb; meter.pending = p
        meter.loadImage(url)
    }

    property double startedMs: Date.now()
    property double soakStartMs: 0
    property double lastMoveMs: 0
    property int    stage: 0
    property int    cycle: 0
    property var    home: null
    property var    shell: null
    property var    extSteps: []
    property bool   grabbing: false

    Timer {
        id: tick
        interval: 250; running: true; repeat: true
        onTriggered: probe.step()
    }

    function makeStep(key, title, page, after) {
        return Qt.createQmlObject('import PinPointStudio; StepDescriptor { key: "' + key + '"; title: "' + title
                                  + '"; eyebrow: "' + title.toUpperCase() + '"; group: "sensors"; page: "' + page
                                  + '"; after: "' + after + '" }', probe, "soakStep_" + key)
    }

    function step() {
        const elapsed = Date.now() - startedMs
        if (stage === 0) {
            home  = find(rootItem(), function(o) { return o.startSessionRequested !== undefined && o.selectedClub !== undefined })
            shell = find(rootItem(), function(o) { return o.presets !== undefined && o.canHeaderForward !== undefined
                                                         && o.flow !== undefined && o.headerBack !== undefined })
            if ((home === null || shell === null || elapsed < waitMs)) {
                if (elapsed < waitMs + 10000) return
                check(home !== null && shell !== null, "Home and the setup shell found"); finish(); return
            }
            extSteps = [makeStep("soakCalibrate", "Soak calibrate", "pages/CalibrateArmPage.qml", "imus"),
                        makeStep("soakCheck",     "Soak check",     "pages/CheckArmPage.qml",     "soakCalibrate")]
            shell.steps.extensions = extSteps
            log("soak steps registered: " + JSON.stringify(shell.steps.keys))
            home.startSessionRequested(SessionController.Wrist)     // exactly as Main opens setup
            stage = 1
            return
        }
        if (stage === 1) {
            if (shell.flow.busy || !shell.flow.isOpen) return
            const devs = shell.ctx.devices
            for (let i = 0; i < devs.length; ++i)
                if (devs[i].sessionEnabled) shell.ctx.setSensorEnabled(devs[i].id, false)
            log("plan " + JSON.stringify(shell.flow.plan) + "; " + devs.length + " sensor(s) disabled for this session")
            stage = 2
            return
        }
        if (stage === 2) {
            // Walk to the soak's Calibrate with Continue / Skip; never Connect.
            if (shell.flow.busy || shell.flow.page === null) return
            if (shell.flow.current === "soakCalibrate") {
                soakStartMs = Date.now(); lastMoveMs = soakStartMs
                log("on soakCalibrate; soaking " + soakMs / 1000 + " s, a move every " + periodMs / 1000 + " s")
                stage = 3
                return
            }
            const skip = find(shell, function(o) { return o.objectName === "setupSkip" })
            if (shell.flow.canContinue) shell.flow.next("done")
            else if (skip && skip.visible) shell.flow.next("skipped")
            else if (elapsed > waitMs + 60000) { check(false, "stuck on " + shell.flow.current); finish() }
            return
        }
        if (stage === 3) {
            if (grabbing || shell.flow.busy) return
            if (Date.now() - lastMoveMs < periodMs) return
            grabbing = true
            grabCycle()
            return
        }
    }

    function grabCycle() {
        cycle += 1
        const key = shell.flow.current
        const views = view3Ds()
        const n = views.length
        check(n <= 1, "cycle " + cycle + ": at most one View3D under the shell (" + n + ")")
        if (n === 0) {
            check(false, "cycle " + cycle + ": no 3-D view on " + key)
            afterGrab(key, NaN, NaN)
            return
        }
        const view = views[0]
        view.grabToImage(function(vr) {
            if (outDir !== "") vr.saveToFile(outDir + "/soak_" + ("0" + cycle).slice(-2) + "_" + key + "_view.png")
            blankControl.grabToImage(function(cr) {
                if (outDir !== "") cr.saveToFile(outDir + "/soak_" + ("0" + cycle).slice(-2) + "_control.png")
                // ⚠ With --probe-out the PNGs are the measurement (judged afterwards, outside the
                // app). The in-app Canvas readback never called back for a grab url in the real
                // app (5 Oct 2026: the first soak sat on cycle 1 for seven minutes), so it is
                // used only when there is nowhere to save.
                if (outDir !== "") { afterGrab(key, NaN, NaN, n); return }
                measure(vr.url, function(sv) {
                    measure(cr.url, function(sc) { afterGrab(key, sv, sc, n) })
                })
            })
        })
    }

    function afterGrab(key, sv, sc, n) {
        log("cycle " + cycle + " " + key + " | View3D " + n + " | view std " + (isNaN(sv) ? "n/a" : sv.toFixed(2))
            + " | control std " + (isNaN(sc) ? "n/a" : sc.toFixed(2)) + " | t+" + Math.round((Date.now() - soakStartMs) / 1000) + " s")
        if (!isNaN(sv) && !isNaN(sc))
            check(sv > sc + minStd, "cycle " + cycle + ": the " + key + " view is not rendered (std " + sv.toFixed(2)
                  + " vs control " + sc.toFixed(2) + ")")
        else if (outDir === "")
            check(false, "cycle " + cycle + ": the grab could not be measured and no --probe-out was given")
        if (Date.now() - soakStartMs >= soakMs) {
            shell.flow.exit("cancel")
            shell.steps.extensions = []
            finish()
            return
        }
        // Move: Calibrate → Check by Skip (nothing to calibrate), Check → Calibrate by Back.
        if (key === "soakCalibrate") shell.flow.next("skipped")
        else                         shell.flow.back()
        lastMoveMs = Date.now()
        grabbing = false
    }
}
