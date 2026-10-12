import QtQuick
import QtQuick.Controls
import QtTest
import PinPointStudio

// Probe: the stage's draggable separators (PpModeStage.qml, ViewLayout.setSizes). Run it through the
// typeset harness's slot + guard, never on Mark's own settings:
//   --probe-qml <this> --probe-session <dir> --probe-step render|drag|restore --probe-out <dir> --probe-tag <t>
// Steps (Analyse, panels charts + table + markup, which draw offscreen):
//   render   grabs the window in split and in stage. No new API is touched, so it also runs on a
//            build from before the separators (the "before" renders).
//   drag     the render step, then real mouse drags through QtTest: hover cursor, a split drag
//            (live widths move, nothing persisted until release, one write on release, the cards
//            and their panels not rebuilt), a clamp at the minimum, the stage's main and side
//            separators likewise, and grabs at rest after.
//   restore  a relaunch on the same slot: the stored sizes are laid out again (widths printed for
//            comparison with the drag run), then a double-click on each arrangement resets it.
// Prints "PROBE: PASS|FAIL <what>" and "PROBE: RESULT PASS|FAIL".
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string step: arg("--probe-step", "render")
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string tag: arg("--probe-tag", "sep")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: 8
    readonly property var allKeys: ["sessionDiagnostics", "camera", "swing3d", "launchMonitor", "wristMotion", "charts", "table", "markup"]
    readonly property var onKeys: ["charts", "table", "markup"]
    readonly property int mode: SessionMode.analyse
    property int fails: 0
    property int writes: 0

    TestCase { id: tc; name: "stageSeparators"; when: false; running: false }

    Connections {
        target: appSettings
        function onViewLayoutByModeChanged() { probe.writes++ }
    }

    function log(s) { console.warn("PROBE: " + s) }
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails++ }
    function walkPred(item, pred, out) {
        if (!item) return
        try { if (pred(item)) out.push(item) } catch (e) {}
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walkPred(kids[i], pred, out)
    }
    function root() { return probe.Window.window.contentItem }
    function cardFor(key) {
        const cs = []
        walkPred(root(), it => it.def !== undefined && it.def !== null && it.def.key === key && it.visible && it.width > 0, cs)
        return cs.length ? cs[0] : null
    }
    // the panel a card's Loader holds
    function panelOf(card) {
        const ls = []
        walkPred(card, it => it.sourceComponent !== undefined && it.item !== undefined && it.status !== undefined, ls)
        return ls.length ? ls[0].item : null
    }
    function sep(name) {
        const ss = []
        walkPred(root(), it => it.objectName === name && it.visible, ss)
        return ss.length ? ss[0] : null
    }
    function near(a, b) { return Math.abs(a - b) <= 1 }
    function stageItem() {
        const st = []
        walkPred(root(), it => it._defs !== undefined && it.tabIndex !== undefined && it.active !== undefined && it.visible, st)
        return st.length ? st[0] : null
    }
    function minW() { return stageItem()._minW }
    function minH() { return stageItem()._minH }
    function w(key) { const c = cardFor(key); return c ? Math.round(c.width) : -1 }
    function h(key) { const c = cardFor(key); return c ? Math.round(c.height) : -1 }
    function storedSizes() {
        const e = appSettings.viewLayoutByMode[String(probe.mode)]
        return (e && e.sizes) ? JSON.stringify(e.sizes) : "{}"
    }
    function geom(label) {
        log(label + " widths charts/table/markup = " + w("charts") + "/" + w("table") + "/" + w("markup")
            + "  heights = " + h("charts") + "/" + h("table") + "/" + h("markup") + "  stored " + storedSizes())
    }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }
    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    // A chain of steps, each run after the previous one's delay.
    function chain(steps) {
        if (!steps.length) return
        const s = steps.shift()
        after(s[0], () => { s[1](); chain(steps) })
    }
    property var frame: null
    function grab(path, then) {
        if (!probe.frame)
            probe.frame = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { x: -100000; color: Theme.colorBg; property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true; hideSource: false } }', probe)
        const item = Overlay.overlay.parent
        probe.frame.width = item.width; probe.frame.height = item.height
        probe.frame.src.sourceRect = Qt.rect(0, 0, 0, 0)
        probe.frame.src.sourceItem = item
        probe.frame.src.scheduleUpdate()
        after(300, () => probe.frame.grabToImage(function (img) { img.saveToFile(path); log("saved " + path); then() }))
    }
    function arrange(name) {
        for (const k of probe.allKeys) ViewLayout.setPanel(probe.mode, k, probe.onKeys.indexOf(k) >= 0)
        ViewLayout.setPanel(probe.mode, "timeline", true)
        ViewLayout.setPanel(probe.mode, "carousel", true)
        ViewLayout.setArrangement(probe.mode, name)
    }

    // ── steps ────────────────────────────────────────────────────────────────
    function renders(suffix, then) {
        arrange("split")
        after(2500, () => {
            geom("split " + suffix)
            grab(outDir + "/" + tag + "_split_" + suffix + ".png", () => {
                arrange("stage")
                after(2500, () => {
                    geom("stage " + suffix)
                    grab(outDir + "/" + tag + "_stage_" + suffix + ".png", then)
                })
            })
        })
    }

    function dragStep() {
        arrange("split")
        // clear any sizes a previous run left, so the drag starts from the default
        ViewLayout.setSizes(probe.mode, "split", null)
        ViewLayout.setSizes(probe.mode, "stage", null)
        after(2500, () => {
            const s0 = sep("stageSep_split_0"), s1 = sep("stageSep_split_1")
            check(!!s0 && !!s1, "split has two separators")
            if (!s0) { finish(); return }
            check(s0.width >= 8 && s0.width <= 12, "split separator hit zone " + s0.width + " px")
            const c0 = cardFor("charts"), c1 = cardFor("table"), c2 = cardFor("markup")
            const gapL = c0.x + c0.width, gapR = c1.x
            check(s0.x >= gapL && s0.x + s0.width <= gapR, "hit zone wholly within the gap [" + gapL + "," + gapR + "] → [" + s0.x + "," + (s0.x + s0.width) + "]")
            check(s0.cursorShape === Qt.SplitHCursor, "split cursor is SplitHCursor")
            tc.mouseMove(s0, s0.width / 2, s0.height / 2)
            after(100, () => {
                check(s0.containsMouse, "hover reaches the separator")
                const p0 = panelOf(c0), p1 = panelOf(c1), p2 = panelOf(c2)
                const w0 = w("charts"), w1 = w("table"), w2 = w("markup")
                geom("split before drag")
                const before = storedSizes()
                probe.writes = 0
                const x00 = s0.x
                // move the first edge 150 px right
                tc.mousePress(s0, s0.width / 2, s0.height / 2)
                let k = 1
                function mv() {
                    // the separator moves under the pointer: give positions in its CURRENT frame
                    tc.mouseMove(s0, s0.width / 2 + 30 * k - (s0.x - x00), s0.height / 2)
                    if (++k <= 5) after(30, mv)
                    else after(150, () => {
                        geom("split mid-drag")
                        check(near(w("charts"), w0 + 150) && near(w("table"), w1 - 150) && w("markup") === w2,
                              "mid-drag: charts +150, table -150, markup unchanged (" + w("charts") + "," + w("table") + "," + w("markup") + ")")
                        check(probe.writes === 0 && storedSizes() === before, "mid-drag: nothing persisted (writes " + probe.writes + ")")
                        tc.mouseRelease(s0, s0.width / 2, s0.height / 2)
                        after(400, () => {
                            geom("split after release")
                            check(probe.writes === 1, "release: exactly one write (" + probe.writes + ")")
                            check(storedSizes() !== before && storedSizes().indexOf("split") >= 0, "release: split weights stored")
                            check(near(w("charts"), w0 + 150) && near(w("table"), w1 - 150), "release: widths held")
                            check(cardFor("charts") === c0 && cardFor("table") === c1 && cardFor("markup") === c2, "cards not rebuilt")
                            check(panelOf(c0) === p0 && panelOf(c1) === p1 && panelOf(c2) === p2, "panels not reloaded")
                            clampSplit()
                        })
                    })
                }
                after(30, mv)
            })
        })
    }

    function clampSplit() {
        // drag the second edge 3000 px left: table clamps at the minimum, charts keeps its width
        const s1 = sep("stageSep_split_1")
        const wc = w("charts"), sum = w("table") + w("markup")
        const x10 = s1.x
        tc.mousePress(s1, s1.width / 2, s1.height / 2)
        tc.mouseMove(s1, s1.width / 2 - 1000, s1.height / 2)
        after(50, () => {
            tc.mouseMove(s1, s1.width / 2 - 3000 - (s1.x - x10), s1.height / 2)
            after(150, () => {
                tc.mouseRelease(s1, s1.width / 2, s1.height / 2)
                after(400, () => {
                    geom("split after clamp")
                    check(w("table") === minW() && w("charts") === wc && near(w("table") + w("markup"), sum),
                          "clamp: table held at " + minW() + " px, charts unchanged, pair width kept")
                    stageDrag()
                })
            })
        })
    }

    function stageDrag() {
        arrange("stage")
        after(2500, () => {
            const sm = sep("stageSep_main"), ss = sep("stageSep_side_0")
            check(!!sm && !!ss, "stage has a main and a side separator")
            if (!sm || !ss) { finish(); return }
            check(sm.cursorShape === Qt.SplitHCursor && ss.cursorShape === Qt.SplitVCursor, "stage cursors SplitH / SplitV")
            const c0 = cardFor("charts"), c1 = cardFor("table"), c2 = cardFor("markup")
            const p0 = panelOf(c0), p1 = panelOf(c1)
            geom("stage before drag")
            const w0 = w("charts"), wt = w("table"), h1 = h("table"), h2 = h("markup")
            probe.writes = 0
            const xm0 = sm.x
            tc.mousePress(sm, sm.width / 2, sm.height / 2)
            tc.mouseMove(sm, sm.width / 2 - 100, sm.height / 2)
            after(50, () => {
                tc.mouseMove(sm, sm.width / 2 - 200 - (sm.x - xm0), sm.height / 2)
                after(150, () => {
                    check(near(w("charts"), w0 - 200) && near(w("table"), wt + 200) && probe.writes === 0,
                          "stage main mid-drag: dominant -200, side +200, nothing persisted (" + w("charts") + "," + w("table") + ", writes " + probe.writes + ")")
                    tc.mouseRelease(sm, sm.width / 2, sm.height / 2)
                    after(400, () => {
                        geom("stage after main release")
                        check(probe.writes === 1 && storedSizes().indexOf("main") >= 0, "stage main: one write, main stored")
                        // side: move the edge 120 px down
                        probe.writes = 0
                        const ys0 = ss.y
                        tc.mousePress(ss, ss.width / 2, ss.height / 2)
                        tc.mouseMove(ss, ss.width / 2, ss.height / 2 + 60)
                        after(50, () => {
                            tc.mouseMove(ss, ss.width / 2, ss.height / 2 + 120 - (ss.y - ys0))
                            after(150, () => {
                                check(near(h("table"), h1 + 120) && near(h("markup"), h2 - 120) && probe.writes === 0,
                                      "stage side mid-drag: table +120, markup -120, nothing persisted")
                                tc.mouseRelease(ss, ss.width / 2, ss.height / 2)
                                after(400, () => {
                                    geom("stage after side release")
                                    check(probe.writes === 1 && storedSizes().indexOf("side") >= 0 && storedSizes().indexOf("main") >= 0,
                                          "stage side: one write, side stored, main kept")
                                    check(cardFor("charts") === c0 && panelOf(c0) === p0 && cardFor("table") === c1 && panelOf(c1) === p1,
                                          "stage cards/panels not rebuilt")
                                    // clamp the side edge far down: markup held at 140
                                    tc.mousePress(ss, ss.width / 2, ss.height / 2)
                                    tc.mouseMove(ss, ss.width / 2, ss.height / 2 + 3000)
                                    after(150, () => {
                                        tc.mouseRelease(ss, ss.width / 2, ss.height / 2)
                                        after(400, () => {
                                            check(h("markup") === minH(), "clamp: markup held at " + minH() + " px tall (" + h("markup") + ")")
                                            // and the main edge far right: side held at 220
                                            tc.mousePress(sm, sm.width / 2, sm.height / 2)
                                            tc.mouseMove(sm, sm.width / 2 + 5000, sm.height / 2)
                                            after(150, () => {
                                                tc.mouseRelease(sm, sm.width / 2, sm.height / 2)
                                                after(400, () => {
                                                    check(w("table") === minW(), "clamp: side column held at " + minW() + " px (" + w("table") + ")")
                                                    // back to a moderate size for the restore run
                                                    tc.mousePress(sm, sm.width / 2, sm.height / 2)
                                                    tc.mouseMove(sm, sm.width / 2 - 300, sm.height / 2)
                                                    after(150, () => {
                                                        tc.mouseRelease(sm, sm.width / 2, sm.height / 2)
                                                        after(400, () => {
                                                            geom("stage final")
                                                            // leave split selected too, so restore sees both
                                                            renders("dragged", finish)
                                                        })
                                                    })
                                                })
                                            })
                                        })
                                    })
                                })
                            })
                        })
                    })
                })
            })
        })
    }

    function restoreStep() {
        log("restore: stored on launch " + storedSizes())
        renders("restored", () => {
            // stage is showing: double-click the main separator → stage resets
            const sm = sep("stageSep_main")
            tc.mouseDoubleClickSequence(sm, sm.width / 2, sm.height / 2)
            after(500, () => {
                geom("stage after double-click")
                check(storedSizes().indexOf("stage") < 0, "double-click resets stage sizes")
                arrange("split")
                after(2500, () => {
                    const s0 = sep("stageSep_split_0")
                    tc.mouseDoubleClickSequence(s0, s0.width / 2, s0.height / 2)
                    after(500, () => {
                        geom("split after double-click")
                        check(storedSizes() === "{}", "double-click resets split sizes")
                        check(Math.abs(w("charts") - w("table")) <= 1 && Math.abs(w("table") - w("markup")) <= 1, "split even again")
                        renders("reset", finish)
                    })
                })
            })
        })
    }

    function finish() {
        log("RESULT " + (fails === 0 ? "PASS" : "FAIL") + " (" + fails + " failed)")
        after(300, () => Qt.exit(fails === 0 ? 0 : 1))
    }

    Timer {
        interval: 2500; running: true
        onTriggered: {
            let muted = 0
            const seen = new Set()
            function mute(o) {
                if (!o || seen.has(o)) return
                seen.add(o)
                if (String(o).indexOf("TingPlayer") === 0) { o.volume = 0; o.frequency = 1; muted++ }
                const ds = o.data || []
                for (let i = 0; i < ds.length; ++i) mute(ds[i])
            }
            mute(probe.root())
            probe.log("muted " + muted + " ting players")   // silence: no probe may play sound
            Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', probe.root())
            // nothing heavy on while the session loads
            for (const k of probe.allKeys) ViewLayout.setPanel(probe.mode, k, k === "charts")
            ViewLayout.setPanel(SessionMode.replay, "sessionDiagnostics", false)
            navController.navigate(2)
            SessionMode.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            probe.after(4000, () => {
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
                }
                probe.after(3000, () => {
                    SessionMode.enterAnalyse()
                    probe.after(3000, () => {
                        if (probe.step === "render") renders("rest", finish)
                        else if (probe.step === "drag") renders("rest", dragStep)
                        else restoreStep()
                    })
                })
            })
        }
    }
}
