// Stage 0 (part B2) characterisation tests — calibration, HackMotion routine: catalogue
// group CH and L5 (docs/design/session_wizard_refactor_design.md §7.3), written at Stage 0
// against the then-current wizard; since Stage 5c against session setup + ImuCalibrationFlow
// (+ PpImuPanel for CH9/L5) over the fakes. Conventions as in tst_setup_calib_witmotion.qml: one function per catalogue ID,
// _data() rows per stage, the Expected column asserted, expectFail() immediately before the
// one assertion that encodes a predicted finding, per-row windows from design §2.4.
//
// Since Stage 2 the routines have a `pace`: CH1 asserts the real durations at the real pace
// with the real 3-D guide; every other case runs the same wizard and guide with the clock
// scaled by `scaled` (×0.05). Routine-level cases (CHr*) drive a HackMotionArmRoutine alone.
//
// ⚠ The pacing assertions (CH1) and leave windows (CH8) assume QML Timers run
// on wall clock — see test_0_harnessClock in tst_setup_calib_witmotion.qml: under the offscreen
// QPA the default threaded render loop's animation driver does not, QSG_RENDER_LOOP=basic or QSG_USE_SIMPLE_ANIMATION_DRIVER=1 (the ctest env) does.
//
// The wizard-level cases run against session setup (ScreenSessionSetup) since Stage 5b; the retired
// wizard and its expected-failure markers were deleted at Stage 5c.
//
// FakeHmDevice is in autoScript mode unless a case says otherwise. CH3/CH4/CH5/CH11 drive it
// by hand ("manual") and assert today's exact strings, quoted from ImuCalibrationFlow.qml
// with their line numbers.
import QtQuick
import QtTest
import PinPointStudio
import "support"

Item {
    id: root
    width: 1400
    height: 900

    CalibDriver { id: drv; anchors.fill: parent; tc: tcase }

    TestCase {
        id: tcase
        name: "SetupCalibHackMotion"
        when: windowShown

        // ── Pacing of today's chain (ImuCalibrationFlow.qml l.412–434, 1145–1234) ──
        readonly property var pace: ({
            hmStart: 1500,          // l.1147 — read window before beginCalibration
            settle: 2000,           // _hmSettleMs — pose-0 settle before confirmHorizontal
            raiseAnim: 3000,        // _hmRaiseAnimMs
            raiseSettle: 500,       // _hmRaiseSettleMs
            returnAnim: 1500,       // _hmReturnAnimMs
            refSettle: 1500,        // _hmRefSettleMs
            presenceWait: 6000,     // _hmPresenceWaitMs
            lib: 30                 // FakeHmDevice.libraryLatencyMs per queued library step
        })
        readonly property int margin: 1000
        readonly property real frameMs: 1000 / 60

        // The scaled clock (every case but CH1).
        readonly property real scaled: 0.05
        readonly property var sp: ({
            hmStart: pace.hmStart * scaled, settle: pace.settle * scaled,
            raiseAnim: pace.raiseAnim * scaled, raiseSettle: pace.raiseSettle * scaled,
            returnAnim: pace.returnAnim * scaled, refSettle: pace.refSettle * scaled,
            presenceWait: pace.presenceWait * scaled, lib: pace.lib
        })
        readonly property int smargin: 400
        function open(setup, opts) {
            opts = opts || {}
            opts.scale = scaled
            return drv.openAtCalibrate(setup, opts)
        }
        function tol(nominal) { return 0.15 * nominal + frameMs }
        function near(actual, nominal, what) {
            verify(Math.abs(actual - nominal) <= tol(nominal),
                   what + ": " + Math.round(actual) + " ms, expected " + nominal + " ±"
                   + Math.round(tol(nominal)) + " ms")
        }

        // Library enums (FakeHmDevice / ImuCalibrationFlow.qml l.347–375).
        readonly property int pAwaitHorizontal: 1
        readonly property int pObservingRaise: 3
        readonly property int pApplying: 5
        readonly property int pVerifying: 6
        readonly property int pComplete: 7
        readonly property int pAborted: 8

        // Today's strings, verbatim.
        // l.734–737 (_hmInvalidated)
        readonly property string msgInvalidated: "The sensor's calibration is gone — a dropped link destroys it "
            + "(measured 0.70° → 18.80° at the same pose with the strap untouched). It cannot be resumed "
            + "or restored. Re-run the routine once the sensor is back."

        function init() {
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            appSettings.imuRoles = ({})
        }
        function cleanup() {
            drv.teardown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        function phaseAt(p, nth) {
            var k = 0, log = drv.hm.phaseLog
            for (var i = 0; i < log.length; ++i)
                if (log[i].phase === p && k++ === (nth || 0)) return drv.toElapsed(log[i].t)
            return -1
        }
        function callAt(name) {
            var c = drv.firstCall("HM-1", name)
            return c ? drv.toElapsed(c.t) : -1
        }
        function names(list) { return list.map(function(c) { return c.name }) }

        // Manual mode: walk the device straight to VERIFYING (hmStep 4) as the library would
        // report it, without waiting on the guide between markers.
        function manualToVerifying() {
            var dev = drv.hm
            tryVerify(function() { return dev.countCalls("beginCalibration") === 1 }, sp.hmStart + 2000)
            dev.calibrationActive = true
            dev.calibrationPhase = pAwaitHorizontal
            dev.calibrationPhase = pObservingRaise
            dev.calibrationPhase = pApplying
            dev.calibrationPhase = pVerifying
            compare(drv.stage(), "hm4")
            tryVerify(function() { return dev.countCalls("confirmReferencePose") === 1 },
                      sp.returnAnim + sp.refSettle + 1500, "confirmReferencePose never sent")
        }

        // ── CH1 ─ happy path ─────────────────────────────────────────────────
        function test_CH01_happyPath() {
            drv.openAtCalibrate("hackmotion")
            var dev = drv.hm
            var m = drv.mark()
            var tOpen = drv.openedAt
            var sawDoneBeforeCalibrated = false
            drv.d().calibrationDoneChanged.connect(function() {
                if (drv.done() && dev.calibrationState !== dev.sCalibrated) sawDoneBeforeCalibrated = true
            })
            tryVerify(function() { return drv.done() }, 30000,
                      "never done (stage " + drv.stage() + ", calls " + JSON.stringify(names(dev.calls)) + ")")
            wait(1000)
            var ev = drv.eventsSince(m, "wiz")
            console.info("[CH1] calls " + JSON.stringify(dev.calls.map(function(c) {
                             return c.name + "@+" + Math.round(drv.toElapsed(c.t) - tOpen) }))
                         + " events " + drv.fmtEvents(ev, tOpen))

            compare(JSON.stringify(names(dev.calls)),
                    JSON.stringify(["beginCalibration", "confirmHorizontal", "confirmRaise", "confirmReferencePose"]))
            compare(dev.refusals, 0)
            compare(drv.completedSince(m), 1, "completed() not exactly once")
            compare(drv.tingsSince(m), 1, "not exactly one ting for one completed calibration")
            compare(drv.tingCount(), 1, "the flow's TingPlayer played other than once")
            verify(!sawDoneBeforeCalibrated, "done before calibrationState === CALIBRATED")
            compare(dev.calibrationState, dev.sCalibrated)
            compare(drv.stage(), "hm5")

            var fins = ev.filter(function(e) { return e.kind === "guide" && e.name === "finished" })
            compare(fins.length, 2, "guide finished " + drv.fmtEvents(fins, tOpen))
            compare(fins[0].value, "hmRaise"); compare(fins[1].value, "hmReturn")
            near(callAt("beginCalibration") - tOpen, pace.hmStart, "beginCalibration after entering")
            near(callAt("confirmHorizontal") - phaseAt(pAwaitHorizontal), pace.settle, "confirmHorizontal after AWAIT_HORIZONTAL")
            near(fins[0].t - phaseAt(pObservingRaise), pace.raiseAnim, "the raise guide")
            near(callAt("confirmRaise") - fins[0].t, pace.raiseSettle, "confirmRaise after the raise guide")
            near(fins[1].t - phaseAt(pVerifying), pace.returnAnim, "the return guide")
            near(callAt("confirmReferencePose") - fins[1].t, pace.refSettle, "confirmReferencePose after the return")
        }

        // ── CH2 ─ forearm travel 8° ──────────────────────────────────────────
        function test_CH02_shortRaiseAborts() {
            // Our abort is answered by the library one queue step later with ABORTED/CALLER;
            // that answer must not reword our stop (it used to become "Calibration cancelled.",
            // found at Stage 2). So the message is asserted only AFTER the answer has arrived
            // and been handled — deterministic on the scaled clock.
            open("hackmotion", { prepare: function(d) { d.hm.raiseTravelDeg = 8 } })
            var dev = drv.hm
            tryVerify(function() { return drv.stage() === "hmStopped" }, 20000, "never stopped (stage " + drv.stage() + ")")
            compare(dev.countCalls("abortCalibration"), 1)
            compare(dev.countCalls("confirmRaise"), 0)
            tryVerify(function() { return dev.calibrationPhase === pAborted
                                          && drv.fx.appLog.matching("aborted reason=caller after our own stop").length === 1 },
                      2000, "the device's ABORTED/CALLER never arrived (phase " + dev.calibrationPhase + ")")
            compare(dev.calibrationAbortReason, dev.aCaller)
            var msg = drv.message()
            console.info("[CH2] " + msg)
            // l.1186–1192
            compare(msg, "Your forearm only moved 8° — the sensor needs about 30° to work out which way "
                    + "your wrist bends, and it cannot tell on its own that the movement was missing. "
                    + "Nothing was applied. Follow the guide and tap Recalibrate.")
            compare(drv.messageKind(), "warn")
            verify(drv.textShown(msg), "the message is not on screen")
            verify(!drv.done())
        }

        // ── CH3 ─ each refusal ───────────────────────────────────────────────
        function test_CH03_refusals_data() {
            var call = "wr_calibration_begin"   // the name HmSessionWorker carries (hm_session_worker.cpp l.388)
            return [
                // l.746–748
                { tag: "NO_STREAM", status: -14, kind: "error", msg: "The sensor stopped streaming, so it "
                  + "cannot watch the raise (" + call + " was refused). Tap Recalibrate once data is flowing." },
                // l.750–751
                { tag: "BUSY", status: -18, kind: "warn", msg: "The sensor is busy retrieving swing data ("
                  + call + " was refused) — try again in a moment." },
                // l.753
                { tag: "LINK_DOWN", status: -12, kind: "error", msg: "The sensor's link went down ("
                  + call + " was refused)." },
                // l.755–756
                { tag: "INVALID_STATE", status: -2, kind: "error", msg: "The sensor was not in a state to "
                  + "accept " + call + ". Tap Recalibrate to start the routine from the beginning." },
                // l.758
                { tag: "other", status: -7, kind: "error", msg: "The sensor refused " + call + " (status -7)." }
            ]
        }
        function test_CH03_refusals(row) {
            open("hackmotion", { hmMode: "manual" })
            var dev = drv.hm
            tryVerify(function() { return dev.countCalls("beginCalibration") === 1 }, sp.hmStart + 2000)
            compare(drv.stage(), "hm1")
            dev.calibrationCallRefused(row.status, "wr_calibration_begin")
            compare(drv.stage(), "hmStopped")
            compare(drv.d().hmStep, 9)
            compare(drv.message(), row.msg)
            compare(drv.messageKind(), row.kind)
            verify(drv.failed())
            verify(drv.textShown(row.msg), "the message is not on screen")
            // A late ABORTED/CALLER (our stop's echo) must not reword the refusal.
            dev.calibrationAbortReason = dev.aCaller
            dev.calibrationPhase = pAborted
            compare(drv.message(), row.msg)
            compare(drv.messageKind(), row.kind)
        }

        // ── CH4 ─ each abort reason ──────────────────────────────────────────
        function test_CH04_abortReasons_data() {
            return [
                // l.765–766
                { tag: "RAISE_TOO_SLOW", reason: 2, kind: "warn", msg: "That took too long between the two "
                  + "positions — the sensor needs one continuous raise. Tap Recalibrate and follow the guide." },
                // l.768–769
                { tag: "STREAM_LOST", reason: 3, kind: "error", msg: "The sensor's data stream stopped "
                  + "part-way through, so the raise could not be watched. Tap Recalibrate once data is flowing." },
                // l.771–772
                { tag: "LINK_LOST", reason: 4, kind: "error", msg: "The sensor's link dropped part-way "
                  + "through. Reconnect it, then run the routine again — a dropped link destroys any calibration." },
                // l.774–775
                { tag: "NO_RESULT", reason: 5, kind: "error", msg: "The sensor did not answer in time, so no "
                  + "calibration was applied. Tap Recalibrate." },
                // l.777
                { tag: "CALLER", reason: 1, kind: "error", msg: "Calibration cancelled." },
                // l.778
                { tag: "NONE", reason: 0, kind: "error", msg: "The calibration routine stopped before it "
                  + "finished. Tap Recalibrate." }
            ]
        }
        function test_CH04_abortReasons(row) {
            open("hackmotion", { hmMode: "manual" })
            var dev = drv.hm
            tryVerify(function() { return dev.countCalls("beginCalibration") === 1 }, sp.hmStart + 2000)
            dev.calibrationActive = true
            dev.calibrationPhase = pAwaitHorizontal
            dev.calibrationPhase = pObservingRaise
            compare(drv.stage(), "hm2")
            dev.calibrationActive = false
            dev.calibrationAbortReason = row.reason
            dev.calibrationPhase = pAborted
            compare(drv.stage(), "hmStopped")
            compare(drv.message(), row.msg)
            compare(drv.messageKind(), row.kind)
            verify(drv.textShown(row.msg), "the message is not on screen")
        }

        // ── CH5 ─ the presence measurement never lands ───────────────────────
        function test_CH05_presenceMissing_data() {
            return [
                // l.710–713
                { tag: "notMeasured", msg: "Too few readings arrived at the reference pose to check the "
                  + "calibration, so it is NOT confirmed — the sensor applied its transform, but nothing "
                  + "verified it. Hold the first position still and tap Recalibrate." },
                // l.1230–1232
                { tag: "silent", msg: "The sensor never reported a check at the reference pose, so the "
                  + "calibration is NOT confirmed. Hold the first position still and tap Recalibrate." }
            ]
        }
        function test_CH05_presenceMissing(row) {
            open("hackmotion", { hmMode: "manual" })
            var dev = drv.hm
            manualToVerifying()
            var tRef = callAt("confirmReferencePose")
            if (row.tag === "notMeasured") {
                dev.presenceSamplesUsed = 3
                dev.presenceNotMeasured = true
                dev.calibrationState = dev.sUnknown
                dev.calibrationActive = false
                dev.calibrationPhase = pComplete
                compare(drv.stage(), "hmStopped")
            } else {
                wait(sp.presenceWait - tol(sp.presenceWait) - 20)
                compare(drv.stage(), "hm4", "stopped before the presence wait expired")
                tryVerify(function() { return drv.stage() === "hmStopped" }, 2 * tol(sp.presenceWait) + 400)
                near(drv.now() - tRef, sp.presenceWait, "the presence timeout")
            }
            compare(drv.message(), row.msg)
            compare(drv.messageKind(), "error")
            verify(!drv.done())
        }

        // ── CH6 ─ link drop at each step and after done ──────────────────────
        function test_CH06_linkDrop_data() {
            return [ { tag: "hm1", step: 1 }, { tag: "hm2", step: 2 }, { tag: "hm3", step: 3 },
                     { tag: "hm4", step: 4 }, { tag: "done", step: 5 } ]
        }
        function test_CH06_linkDrop(row) {
            open("hackmotion")
            var dev = drv.hm
            if (row.step === 3) {
                // APPLYING lasts one library step; widen it so the drop lands inside it.
                tryVerify(function() { return drv.d().hmStep === 2 }, 10000)
                dev.libraryLatencyMs = 1500
            }
            tryVerify(function() { return drv.d().hmStep === row.step }, 30000,
                      "never reached hmStep " + row.step + " (stage " + drv.stage() + ")")
            verify(drv.d().hmStep === row.step)
            drv.fx.imuManager.disconnectDevice("HM-1")
            compare(drv.stage(), "hmStopped")
            verify(drv.d().hmInvalidated, "not marked invalidated")
            verify(!drv.done())
            compare(drv.message(), msgInvalidated)
            compare(drv.messageKind(), "error")
            verify(drv.textShown(msgInvalidated), "the message is not on screen")
            // A late ABORTED/CALLER after the drop must not reword the invalidation.
            dev.calibrationAbortReason = dev.aCaller
            dev.calibrationPhase = pAborted
            compare(drv.message(), msgInvalidated)
            compare(drv.stage(), "hmStopped")
            // (The catalogue's "outcome cleared from the draft" has no subject before the
            // refactor: there is no SetupDraft. Recorded as not applicable.)
        }

        // ── CH7 ─ the previous attempt's COMPLETE lingers at begin ───────────
        function test_CH07_staleCompleteIgnored() {
            open("hackmotion", { prepare: function(d) {
                var dev = d.hm
                dev._libPhase = dev.pComplete
                dev.calibrationPhase = dev.pComplete
                dev.calibrationState = dev.sUncalibrated
                dev.calibrationAbortReason = dev.aCaller
                dev.presenceSamplesUsed = 64
                dev.presenceAngleDeg = 9.0
                dev.libraryLatencyMs = 600      // hold the stale fields through the begin
            } })
            var dev = drv.hm
            tryVerify(function() { return drv.d().hmStep === 1 }, sp.hmStart + 2000)
            compare(dev.calibrationPhase, pComplete, "the stale COMPLETE is already gone — the case proves nothing")
            dev.calibrationStateChanged()       // a state event while the new run is being begun
            compare(drv.stage(), "hm1")
            verify(!drv.failed(), "a stale verdict was read: " + drv.message())
            dev.libraryLatencyMs = 30
            tryVerify(function() { return drv.d().hmStep >= 3 }, 15000, "stage " + drv.stage())
            verify(drv.stage() !== "hmStopped", drv.message())
        }

        // ── CH8 ─ Back or Skip at each step ──────────────────────────────────
        // Window = the rest of the chain from that step to done (§2.4), plus the margin.
        function windowFrom(step) {
            if (step === 1) return sp.settle + sp.raiseAnim + sp.raiseSettle + sp.returnAnim
                                   + sp.refSettle + 8 * sp.lib + smargin
            if (step === 2) return sp.raiseAnim + sp.raiseSettle + sp.returnAnim + sp.refSettle
                                   + 6 * sp.lib + smargin
            if (step === 4) return sp.returnAnim + sp.refSettle + 2 * sp.lib + smargin
            return 0
        }
        function test_CH08_leaveAtEachStep_data() {
            var out = []
            var hows = ["back", "skip"], steps = [1, 2, 4]
            for (var i = 0; i < hows.length; ++i)
                for (var j = 0; j < steps.length; ++j)
                    out.push({ tag: hows[i] + "_hm" + steps[j], how: hows[i], step: steps[j] })
            return out
        }
        function test_CH08_leaveAtEachStep(row) {
            open("hackmotion")
            var dev = drv.hm
            // Leave as the step begins: hm1 once the library is in AWAIT_HORIZONTAL, hm2 / hm4
            // as their guide animation starts.
            if (row.step === 1)
                tryVerify(function() { return dev.calibrationPhase === pAwaitHorizontal }, 10000)
            else if (row.step === 2)
                tryVerify(function() { return drv.animStage() === "hmRaise" }, 15000)
            else
                tryVerify(function() { return drv.animStage() === "hmReturn" }, 20000)
            compare(drv.stage(), "hm" + row.step)
            var wasActive = dev.calibrationActive
            var m = drv.mark()
            if (row.how === "back") drv.leaveBack(); else drv.leaveSkip()
            var win = windowFrom(row.step)
            wait(win)
            var calls = drv.callsSince(m)
            var aborts = calls.filter(function(c) { return c.name === "abortCalibration" }).length
            var others = calls.filter(function(c) { return c.name !== "abortCalibration" })
            var msg = drv.message()
            var tings = drv.tingsSince(m)
            var detail = row.how + " at hm" + row.step + " (active " + wasActive + "), watched " + win
                         + " ms: calls " + drv.fmtCalls(calls) + "; states "
                         + drv.fmtEvents(drv.eventsSince(m, "wiz", "state"), m.t) + "; timers "
                         + drv.fmtEvents(drv.eventsSince(m, "wiz", "timer"), m.t) + "; completed "
                         + drv.completedSince(m) + "; stage now " + drv.stage() + "; device phase "
                         + dev.calibrationPhase + "; tings after leave " + tings + "; message '" + msg + "'"
            console.info("[CH8] " + detail)
            var clean = aborts === (wasActive ? 1 : 0) && others.length === 0 && drv.completedSince(m) === 0
                        && tings === 0
                        && (row.step !== 4 || msg.indexOf("declined") >= 0)
            verify(clean, detail)
            compare(aborts, 1, "exactly one abortCalibration on leave: " + detail)
        }

        // ── CH9 ─ two hosts alive ────────────────────────────────────────────
        // Models "the toolbar sensor popup has been opened earlier in this run of the app".
        // ⚠ It is NOT the app's normal state: Qt Quick Controls DEFERS a Popup's contentItem
        // until the popup is first opened (QQuickPopup's
        // Q_CLASSINFO("DeferredPropertyNames", "background,contentItem")), so imuPopup's
        // PpImuPanel (PpSessionToolbar.qml l.705–718) does not exist until then. Once opened
        // it stays built: closed, in list mode, invisible, with its gate closed — which is
        // what the driver hosts. A host that is not active holds no routine, so the panel's
        // flow must send nothing.
        function test_CH09_twoHostsMarkersOnce() {
            open("hackmotion", { hmMode: "strict", withPanel: true })
            var dev = drv.hm
            compare(drv.panel.mode, "list")
            verify(!drv.panelFlow.visible)
            verify(!drv.gateOpen(drv.panelFlow))
            var m = drv.mark()
            tryVerify(function() { return drv.done() || drv.stage() === "hmStopped" }, 30000,
                      "wizard routine neither done nor stopped (stage " + drv.stage() + ")")
            wait(sp.raiseAnim + sp.returnAnim + sp.refSettle + smargin)   // let any stray marker land
            var counts = {}
            var markers = ["beginCalibration", "confirmHorizontal", "confirmRaise", "confirmReferencePose", "abortCalibration"]
            for (var i = 0; i < markers.length; ++i) counts[markers[i]] = dev.countCalls(markers[i])
            var detail = "counts " + JSON.stringify(counts) + "; refusals " + dev.refusals + " "
                         + JSON.stringify(dev.refusalLog.map(function(r) { return r.call + ":" + r.status }))
                         + "; wizard stage " + drv.stage() + " ('" + drv.message() + "'), panel stage "
                         + drv.stage(drv.panelFlow) + "; panel timers "
                         + drv.fmtEvents(drv.eventsSince(m, "panel", "timer"), m.t) + "; panel states "
                         + drv.fmtEvents(drv.eventsSince(m, "panel", "state"), m.t)
            console.info("[CH9/F3] " + detail)
            var once = counts.beginCalibration === 1 && counts.confirmHorizontal === 1
                       && counts.confirmRaise === 1 && counts.confirmReferencePose === 1
                       && dev.refusals === 0
            verify(once, detail)
        }

        // ── CH10 ─ complete → Check → Back ───────────────────────────────────
        function test_CH10_backFromCheckShowsComplete() {
            open("hackmotion")
            var dev = drv.hm
            tryVerify(function() { return drv.done() }, 30000, "never done (stage " + drv.stage() + ")")
            verify(drv.continueToCheck(), "Continue did not reach Check")
            var m = drv.mark()
            drv.backFromCheck()
            compare(drv.wiz.currentStep, drv.wiz.stepCalibrate)
            var doneRightAfter = drv.done()
            wait(sp.hmStart + smargin)
            var detail = "after Back from Check: calls " + drv.fmtCalls(drv.callsSince(m))
                         + "; done right after " + doneRightAfter + ", now " + drv.done()
                         + "; stage " + drv.stage() + "; device state " + dev.calibrationState
                         + "; unit.calibrated " + dev.unitLowerArm.calibrated
            console.info("[CH10/F1] " + detail)
            compare(drv.callsSince(m, "beginCalibration").length, 0, detail)
            verify(doneRightAfter && drv.done(), "does not show complete: " + detail)
            // Showing complete is not completing: no second ting.
            compare(drv.tingsSince(m), 0, "a ting on Back from Check: " + detail)
        }

        // ── CH11 ─ not connected / not streaming at begin ────────────────────
        function test_CH11_notReadyAtBegin_data() {
            return [
                // l.549–550
                { tag: "notConnected", msg: "The wrist sensor is not connected. Connect it on the IMUs "
                  + "step, then tap Recalibrate." },
                // l.554–556
                { tag: "notStreaming", msg: "The wrist sensor is connected but not streaming, and the "
                  + "sensor has to WATCH the raise — it cannot calibrate from two still poses. Wait for "
                  + "the stream, then tap Recalibrate." }
            ]
        }
        function test_CH11_notReadyAtBegin(row) {
            open("hackmotion", { hmMode: "manual", prepare: function(d) {
                if (row.tag === "notConnected") d.fx.imuManager.disconnectDevice("HM-1")
                else d.hm.streaming = false
            } })
            var dev = drv.hm
            verify(drv.isHackMotion())
            tryVerify(function() { return drv.stage() === "hmStopped" }, sp.hmStart + 2000,
                      "never stopped (stage " + drv.stage() + ")")
            compare(drv.message(), row.msg)
            compare(drv.messageKind(), "error")
            compare(dev.countCalls("beginCalibration"), 0)
            verify(drv.textShown(row.msg), "the message is not on screen")
        }

        // ── L5 ─ after Start, the wizard's routine stays silent ──────────────
        // The wizard's Calibrate is skipped (as a real visit can), the session is started, and
        // the toolbar panel runs its own calibration. Strict device, so a duplicate shows.
        function test_L05_wizardSilentAfterStart() {
            open("hackmotion", { hmMode: "strict", withPanel: true })
            var dev = drv.hm
            drv.leaveSkip()                     // before hmStart fires: the wizard never began
            compare(dev.countCalls("beginCalibration"), 0)
            drv.startSession()
            compare(drv.fx.sessionController.running, true)
            verify(!drv.wiz.visible)
            var m = drv.mark()
            drv.enterToolbarCalibrate()
            tryVerify(function() { return drv.done(drv.panelFlow) || drv.stage(drv.panelFlow) === "hmStopped" },
                      40000, "panel routine neither done nor stopped (panel stage " + drv.stage(drv.panelFlow) + ")")
            wait(sp.raiseAnim + sp.returnAnim + sp.refSettle + smargin)
            var wizEvents = drv.eventsSince(m, "wiz")
            var counts = {}
            var markers = ["beginCalibration", "confirmHorizontal", "confirmRaise", "confirmReferencePose", "abortCalibration"]
            for (var i = 0; i < markers.length; ++i) counts[markers[i]] = drv.callsSince(m, markers[i]).length
            var detail = "panel stage " + drv.stage(drv.panelFlow) + " ('" + drv.message(drv.panelFlow)
                         + "'); counts " + JSON.stringify(counts) + "; refusals " + dev.refusals + " "
                         + JSON.stringify(dev.refusalLog.map(function(r) { return r.call + ":" + r.status }))
                         + "; wizard flow events " + drv.fmtEvents(wizEvents, m.t) + "; wizard stage "
                         + drv.stage() + " ('" + drv.message() + "')"
            console.info("[L5/F3] " + detail)
            var silent = wizEvents.length === 0 && dev.refusals === 0
                         && counts.confirmHorizontal === 1 && counts.confirmRaise === 1
                         && counts.confirmReferencePose === 1
            verify(silent, detail)
        }

        // ── CH12 ─ one ting per completed calibration, whatever happens after ─
        // Completes once, waits 10 s, then drives what could flip calibrationDone: a Settings
        // round trip, a calibrationStateChanged() re-emitted with the same CALIBRATED state, a
        // late guide leadArmAnimFinished(), and Back from Check — which shows complete and must
        // not ting (the retired wizard re-ran the device routine there, F1, and tinged twice).
        function test_CH12_noRepeatedTing() {
            open("hackmotion")
            var dev = drv.hm
            var m = drv.mark()
            tryVerify(function() { return drv.done() }, 30000, "never done (stage " + drv.stage() + ")")
            compare(drv.tingsSince(m), 1)
            wait(10000)
            compare(drv.tingsSince(m), 1, "a ting in the 10 s after completion")
            drv.suspend(); wait(300); drv.resume(); wait(300)
            compare(drv.tingsSince(m), 1, "a ting on the Settings round trip")
            compare(dev.calibrationState, dev.sCalibrated)
            dev.calibrationStateChanged()                  // same CALIBRATED state, re-emitted
            wait(100)
            compare(drv.tingsSince(m), 1, "a ting on a re-emitted CALIBRATED state")
            drv.flow._guide.view.leadArmAnimFinished()            // a late guide completion
            wait(300)
            compare(drv.tingsSince(m), 1, "a ting on a late leadArmAnimFinished")
            verify(drv.continueToCheck())
            drv.backFromCheck()
            compare(drv.tingsSince(m), 1, "a ting on Back from Check")
            // Back from Check shows the calibration complete (R8) — no re-run, so no second
            // completion and no second ting. (The retired wizard re-ran it: F1, two tings.)
            tryVerify(function() { return drv.done() }, 30000, "the re-run never completed (stage " + drv.stage() + ")")
            wait(1000)
            console.info("[CH12] tings " + drv.tingsSince(m) + ", completions " + drv.completedSince(m)
                         + ", beginCalibration " + dev.countCalls("beginCalibration"))
            compare(drv.tingsSince(m), drv.completedSince(m), "tings ≠ completions")
            compare(drv.tingsSince(m), 1)
        }

        // ══ Routine-level cases (Stage 2): a HackMotionArmRoutine alone, over a FakeGuide,
        // on the scaled clock — its stop() and `active` contract against the device.

        function rReachStep(r, step) {
            if (step === 1)
                tryVerify(function() { return drv.hm.calibrationPhase === pAwaitHorizontal }, 5000)
            else if (step === 2)
                tryVerify(function() { return r._animStage === "hmRaise" }, 5000)
            else
                tryVerify(function() { return r._animStage === "hmReturn" }, 5000)
            compare(r.stage, "hm" + step)
        }

        // CHr1 — a full run at the routine: the four markers once each, done once, the
        // guide asked for the raise and the return, each marker one app-log line.
        function test_CHr1_fullRun() {
            var r = drv.hostRoutine("hackmotion", "hackmotion")
            var dev = drv.hm, g = drv.fakeGuide
            var completions = 0
            r.completed.connect(function() { completions++ })
            r.begin()
            tryVerify(function() { return r.done }, 10000, "never done (stage " + r.stage + ", "
                      + JSON.stringify(names(dev.calls)) + ")")
            wait(200)
            compare(JSON.stringify(names(dev.calls)),
                    JSON.stringify(["beginCalibration", "confirmHorizontal", "confirmRaise", "confirmReferencePose"]))
            compare(completions, 1)
            compare(r.stage, "hm5")
            var anims = g.log.filter(function(e) { return e.name === "animate" })
            compare(anims.length, 2)
            compare(anims[0].ms, sp.raiseAnim); compare(anims[1].ms, sp.returnAnim)
            var markers = ["beginCalibration", "confirmHorizontal", "confirmRaise", "confirmReferencePose"]
            for (var i = 0; i < markers.length; ++i)
                compare(drv.fx.appLog.matching("hackmotion " + markers[i], "Calib").length, 1,
                        markers[i] + " — " + JSON.stringify(drv.fx.appLog.lines))
        }

        // CHr2 — stop(reason) / `active` false at each step: abortCalibration exactly once,
        // today's wording (the VERIFYING case says "declined"), then nothing at all.
        function test_CHr2_stopAtEachStep_data() {
            var out = [], steps = [1, 2, 4]
            for (var i = 0; i < steps.length; ++i) {
                out.push({ tag: "stop_hm" + steps[i], step: steps[i], how: "stop" })
                out.push({ tag: "inactive_hm" + steps[i], step: steps[i], how: "inactive" })
            }
            return out
        }
        function test_CHr2_stopAtEachStep(row) {
            var r = drv.hostRoutine("hackmotion", "hackmotion")
            var dev = drv.hm, g = drv.fakeGuide
            r.begin()
            rReachStep(r, row.step)
            verify(dev.calibrationActive)
            var m = drv.mark()
            var cancels = g.cancels
            if (row.how === "stop") r.stop("back")
            else r.active = false
            compare(r.stage, "idle")
            verify(g.cancels > cancels, "the guide was not cancelled")
            wait(windowFrom(row.step))
            var calls = drv.callsSince(m)
            compare(drv.fmtCalls(calls), JSON.stringify(["HM-1.abortCalibration"]))
            compare(drv.eventsSince(m, "routine", "timer").length, 0)
            compare(r.stage, "idle")
            verify(!r.done)
            if (row.step === 4) verify(r.message.indexOf("declined") >= 0, r.message)
            else compare(r.message, "Calibration cancelled before the sensor applied anything.")
            // A second stop (the host leaving after a page's own stop) sends nothing more.
            r.stop("again")
            r.active = false
            compare(drv.callsSince(m, "abortCalibration").length, 1)
        }

        // CHr3 — the CH2 twin at the routine: the travel gate's own stop survives the device's
        // ABORTED/CALLER answer; a device abort while still RUNNING keeps its own text.
        function test_CHr3_ownStopSurvivesAbortEcho() {
            var r = drv.hostRoutine("hackmotion", "hackmotion",
                                    { prepare: function(d) { d.hm.raiseTravelDeg = 8 } })
            var dev = drv.hm
            r.begin()
            tryVerify(function() { return r.stage === "hmStopped" }, 5000, "never stopped (" + r.stage + ")")
            var msg = r.message
            verify(msg.indexOf("only moved 8°") >= 0 && msg.indexOf("about 30°") >= 0, msg)
            tryVerify(function() { return dev.calibrationPhase === pAborted }, 2000, "no ABORTED answer")
            compare(drv.fx.appLog.matching("aborted reason=caller after our own stop").length, 1)
            compare(r.message, msg)
            compare(r.messageKind, "warn")
            // Contrast: an abort the DEVICE raises mid-run (still running, not stopped by us)
            // reads as before, even with reason CALLER.
            r.begin()
            tryVerify(function() { return r.stage === "hm2" }, 5000, "no fresh raise (" + r.stage + ")")
            dev.mode = "manual"
            dev.calibrationAbortReason = dev.aCaller
            dev.calibrationPhase = pAborted
            compare(r.stage, "hmStopped")
            compare(r.message, "Calibration cancelled.")
        }

        // CH13 — the toolbar popup (both behaviours on a wG3): closing it mid-run is leaving
        // (exactly one abort, today's wording, then nothing; no ting); reopening it in calibrate
        // mode runs FRESH; closing after completion and reopening re-runs nothing and does not
        // ting. The panel lives in a real Popup, built on its first open.
        function test_CH13_toolbarPopupCloseIsLeave() {
            drv.hostToolbar("hackmotion", { scale: scaled })
            var dev = drv.hm
            drv.openToolbar()
            drv.panel.mode = "calibrate"
            verify(drv.flow.active, "the panel flow is not active in calibrate mode")
            tryVerify(function() { return drv.animStage() === "hmRaise" }, 5000, "no raise (" + drv.stage() + ")")
            verify(dev.calibrationActive)
            var m = drv.mark()
            drv.closeToolbar()
            verify(!drv.panel.visible, "closing the popup did not hide its contentItem")
            verify(!drv.flow.active, "the flow is still active with the popup closed")
            compare(drv.panel.mode, "calibrate")
            wait(windowFrom(2))
            var detail = "calls " + drv.fmtCalls(drv.callsSince(m)) + " timers "
                         + drv.fmtEvents(drv.eventsSince(m, "panel", "timer"), m.t)
            compare(drv.fmtCalls(drv.callsSince(m)), JSON.stringify(["HM-1.abortCalibration"]), detail)
            compare(drv.eventsSince(m, "panel", "timer").length, 0, detail)
            compare(drv.tingsSince(m, "panel"), 0)
            compare(drv.message(), "Calibration cancelled before the sensor applied anything.")
            verify(!drv.done())

            var m2 = drv.mark()
            drv.openToolbar()
            verify(drv.flow.active)
            tryVerify(function() { return drv.done() }, 10000, "the fresh run never completed (" + drv.stage() + ")")
            wait(200)
            compare(drv.callsSince(m2, "beginCalibration").length, 1, drv.fmtCalls(drv.callsSince(m2)))
            compare(drv.callsSince(m2, "abortCalibration").length, 0)
            compare(drv.tingsSince(m2, "panel"), 1)
            compare(drv.panel.mode, "list", "completion did not return the panel to its list")

            var m3 = drv.mark()
            drv.closeToolbar()
            drv.openToolbar()
            wait(sp.hmStart + smargin)
            compare(drv.callsSince(m3).length, 0, drv.fmtCalls(drv.callsSince(m3)))
            compare(drv.tingsSince(m3, "panel"), 0)
            verify(drv.done(), "the completed calibration did not survive the round trip")
        }
    }
}
