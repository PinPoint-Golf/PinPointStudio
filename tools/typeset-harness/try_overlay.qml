import QtQuick
import QtQuick.Controls
import PinPointStudio

// The typeset harness's LIVE TRY-OUT (tools/typeset-harness/README.md; run it through try.sh).
// The real app, windowed, with a candidate panel laid over EVERY instance of the target panel. A mode
// switch, a layout change or a theme change can rebuild the stage's panels, so the overlay re-attaches
// once a second. The real body stays underneath (transparent) and does the work: the mock reads it and
// calls it, so a click in the mock runs the real paths (Session diagnostics: a shot moves the real
// carousel, a condition opens the real detail). Quit the app to end. Nothing here changes the app's code.
//
//   --probe-variant <abs path.qml>   --probe-session <dir>   --probe-ordinal 8
//   --probe-target <objectName | key:<stageKey>>   default sdBody
//   --probe-panel-key <stageKey>     switched on in Replay and Analyse (default: from the target)
//   --probe-feed p1,p2               body properties BOUND onto the mock (sdBody: readout,cards,source,header,detail)
//   --probe-view-prop <prop>         body property the mock's `view` is BOUND to (sdBody: view, which is
//                                    "detail" while a condition is open, else the tab)
//   --probe-tab-prop <prop>          body property the self-test writes to change view (sdBody: tab)
//   --probe-views v1,v2              the views the self-test drives (non-"detail" ones are tabs)
//   --probe-detail <id>              the detail the self-test opens via body._openDetail(id), when the body has it
//   --probe-selftest                 drive it like a golfer, print PASS/FAIL per step and RESULT, then quit
//
// ⚠ PANEL-SPECIFIC BINDING. A try-out must bind the VIEW as well as the data, or only the first tab
// ever shows (Mark's first try-out, 11 Oct). Which body property carries the view is per panel: it is
// the profile's VIEW_PROP (panels/<name>.env). A panel without one has a single view.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    function list(s) { return String(s).split(",").filter(x => x !== "") }
    readonly property string panelPath: arg("--probe-variant", "")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))
    readonly property int navIndex: parseInt(arg("--probe-nav", "2"))
    readonly property string target: arg("--probe-target", "sdBody")
    readonly property string panelKey: arg("--probe-panel-key",
        target.indexOf("key:") === 0 ? target.slice(4) : (target === "sdBody" ? "sessionDiagnostics" : ""))
    readonly property var feedProps: list(arg("--probe-feed", target === "sdBody" ? "readout,cards,source,header,detail" : ""))
    readonly property string viewProp: arg("--probe-view-prop", target === "sdBody" ? "view" : "")
    readonly property string tabProp: arg("--probe-tab-prop", target === "sdBody" ? "tab" : "")
    readonly property var views: list(arg("--probe-views", target === "sdBody" ? "shot,session,watching,detail" : ""))
    readonly property string detailId: arg("--probe-detail", "over_the_top")
    property var comp: null
    property var attached: []          // [{ body, overlay }]
    function log(s) { console.warn("TRY: " + s) }
    function walkPred(item, pred, out) {
        if (!item) return
        try { if (pred(item)) out.push(item) } catch (e) {}
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walkPred(kids[i], pred, out)
    }
    function panelIn(card) {
        let level = [card], best = null
        while (level.length) {
            const nextLevel = []
            for (const it of level) {
                const kids = it.children || []
                for (let i = 0; i < kids.length; ++i) {
                    const k = kids[i]
                    if (k.sourceComponent !== undefined && k.item !== undefined && k.item) {
                        if (!best || k.item.width * k.item.height > best.width * best.height) best = k.item
                        continue
                    }
                    nextLevel.push(k)
                }
            }
            level = nextLevel
        }
        return best
    }
    function findTargets() {
        const root = probe.Window.window.contentItem, found = []
        if (probe.target.indexOf("key:") === 0) {
            const k = probe.target.slice(4), cards = []
            walkPred(root, it => it.def !== undefined && it.def !== null && it.def.key === k, cards)
            for (const c of cards) { const p = panelIn(c); if (p) found.push(p) }
        } else walkPred(root, it => it.objectName === probe.target, found)
        return found
    }
    // In the tabs arrangement only the selected tab's panel exists: select the target's tab, so the
    // overlay has something to sit on (Mark's own settings may lead with the camera).
    function selectTab() {
        if (probe.panelKey === "") return
        const st = []
        walkPred(probe.Window.window.contentItem, it => it._defs !== undefined && it.tabIndex !== undefined && it.active !== undefined && it.visible, st)
        for (const s of st) {
            const idx = s.active.findIndex(d => d.key === probe.panelKey)
            if (idx >= 0 && s.tabIndex !== idx && ViewLayout.arrangementFor(SessionMode.mode) === "tabs") s.tabIndex = idx
        }
    }
    function has(o, p) { try { return !!o && (p in o) } catch (e) { return false } }
    function feed(o, b) {
        if (has(o, "body")) o.body = b
        for (const p of probe.feedProps) if (has(o, p)) o[p] = Qt.binding(() => b[p])
        // The view follows the body: for Session diagnostics its tab, or "detail" while a condition is open.
        if (probe.viewProp !== "" && has(o, "view")) o.view = Qt.binding(() => String(b[probe.viewProp]))
    }
    function sweep() {
        const bs = findTargets()
        // drop overlays whose body is gone (the stage may already have destroyed the overlay with it)
        probe.attached = probe.attached.filter(a => {
            const alive = bs.indexOf(a.body) >= 0
            if (!alive && a.overlay) { try { a.overlay.destroy() } catch (e) {} }
            return alive
        })
        for (const b of bs) {
            if (probe.attached.some(a => a.body === b)) continue
            const o = probe.comp.createObject(b.parent, { z: 100 })
            if (!o) { log("could not create the panel: " + probe.comp.errorString()); continue }
            o.x = Qt.binding(() => b.x); o.y = Qt.binding(() => b.y)
            o.width = Qt.binding(() => b.width); o.height = Qt.binding(() => b.height)
            o.visible = Qt.binding(() => b.visible)
            feed(o, b)
            // A pass-through overlay (root declares `property bool passThrough: true`, e.g. a no-op used to try
            // the REAL panel) leaves the panel showing; a mock replaces it, so the panel underneath goes clear.
            if (!(("passThrough" in o) && o.passThrough === true)) b.opacity = 0
            probe.attached.push({ body: b, overlay: o })
            log("attached over a " + Math.round(b.width) + "×" + Math.round(b.height) + " panel")
        }
    }
    Timer { interval: 1000; repeat: true; running: probe.comp !== null; onTriggered: probe.sweep() }
    // Open the fixture shot in Replay, so the panel is on screen from the start.
    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }
    Timer {
        id: openShot; interval: 4000
        onTriggered: {
            for (let i = 0; i < shots.count; ++i) {
                const o = shots.objectAt(i)
                if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
            }
            probe.after(2000, () => probe.selectTab())
        }
    }
    // ── --probe-selftest: drive it the way a golfer would and check the mock follows every step ──
    // Replay: attached; every tab followed. Analyse (a mode switch rebuilds the stage): re-attached;
    // every tab again; the detail opens and Back returns. A mock that declares `register` must read
    // "Replay" / "Analyse" in the matching mode (the one-file-per-register convention).
    // Optional overlay hooks (a pass-through over the REAL panel uses them to check it, e.g.
    // screens/charts_real.qml): selfCheck(tab) → "" when the panel shows that tab, else why; and, for a
    // body with no _openDetail, selfOpen() / selfCheck("open") / selfClose() / selfCheck("closed") as
    // its drill-in.
    readonly property bool selftest: Qt.application.arguments.indexOf("--probe-selftest") >= 0
    property int fails: 0
    function ok(c, what) { log((c ? "PASS " : "FAIL ") + what); if (!c) fails++ }
    function live() { return probe.attached.find(a => a.body && a.body.visible) || (probe.attached.length ? probe.attached[0] : null) }
    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    function setTab(b, v) { b[probe.tabProp] = (typeof b[probe.tabProp] === "number") ? Number(v) : v }
    function driveTabs(modeName, then) {
        const tabs = probe.tabProp !== "" ? probe.views.filter(v => v !== "detail") : []
        function step(i) {
            if (i >= tabs.length) { then(); return }
            const a = probe.live()
            if (a) setTab(a.body, tabs[i])
            probe.after(2500, () => {
                const b = probe.live()
                const v = b && b.overlay && has(b.overlay, "view") ? String(b.overlay.view) : "(no view)"
                if (probe.viewProp !== "") probe.ok(v === String(tabs[i]), modeName + ": tab " + tabs[i] + " followed (" + v + ")")
                // an overlay that can say what the panel under it shows (a pass-through over the real panel)
                if (b && b.overlay && typeof b.overlay.selfCheck === "function") {
                    const why = String(b.overlay.selfCheck(String(tabs[i])))
                    probe.ok(why === "", modeName + ": the panel shows " + tabs[i] + (why === "" ? "" : " — " + why))
                }
                step(i + 1)
            })
        }
        step(0)
    }
    function checkAttached(modeName, regName) {
        const a = probe.live()
        probe.ok(!!a, "attached in " + modeName)
        if (a && has(a.overlay, "register")) probe.ok(a.overlay.register === regName, modeName + " register (" + a.overlay.register + ")")
    }
    function finish() { probe.log(probe.fails ? "RESULT FAIL " + probe.fails : "RESULT PASS"); Qt.exit(probe.fails ? 1 : 0) }
    function runSelftest() {
        // Silence: a self-test is a probe, and no probe may play sound.
        const seen = new Set()
        function mute(o) {
            if (!o || seen.has(o)) return
            seen.add(o)
            if (String(o).indexOf("TingPlayer") === 0) { o.volume = 0; o.frequency = 1 }
            const ds = o.data || []
            for (let i = 0; i < ds.length; ++i) mute(ds[i])
        }
        mute(probe.Window.window.contentItem)
        probe.after(10000, () => {
            probe.checkAttached("Replay", "Replay")
            probe.driveTabs("Replay", () => {
                SessionMode.enterAnalyse()
                probe.after(2500, () => probe.selectTab())
                probe.after(6000, () => {
                    probe.checkAttached("Analyse", "Analyse")
                    probe.driveTabs("Analyse", () => {
                        const a = probe.live()
                        // a panel without _openDetail may have its own drill-in, driven by its overlay
                        // (selfOpen / selfCheck("open") / selfClose / selfCheck("closed"); Charts: an Apart row)
                        if (a && a.overlay && typeof a.overlay.selfOpen === "function" && typeof a.body._openDetail !== "function") {
                            const what = String(a.overlay.selfOpen())
                            probe.after(4000, () => {
                                const b = probe.live()
                                const why = b ? String(b.overlay.selfCheck("open")) : "detached"
                                probe.ok(why === "", what + " opens in place" + (why === "" ? "" : " — " + why))
                                if (b) b.overlay.selfClose()
                                probe.after(2500, () => {
                                    const c = probe.live()
                                    const why2 = c ? String(c.overlay.selfCheck("closed")) : "detached"
                                    probe.ok(why2 === "", what + " closes again" + (why2 === "" ? "" : " — " + why2))
                                    probe.finish()
                                })
                            })
                            return
                        }
                        if (!a || typeof a.body._openDetail !== "function" || probe.views.indexOf("detail") < 0) { probe.finish(); return }
                        const back = String(a.body[probe.viewProp])
                        a.body._openDetail(probe.detailId)
                        probe.after(2500, () => {
                            const b = probe.live()
                            probe.ok(b && String(b.overlay.view) === "detail", "a condition opens the detail (" + (b ? b.overlay.view : "") + ")")
                            if (b) b.body._closeDetail()
                            probe.after(2500, () => {
                                const c = probe.live()
                                probe.ok(c && String(c.overlay.view) === back, "Back returns to " + back + " (" + (c ? c.overlay.view : "") + ")")
                                probe.finish()
                            })
                        })
                    })
                })
            })
        })
    }
    Component.onCompleted: {
        probe.comp = Qt.createComponent("file://" + probe.panelPath)
        if (probe.comp.status !== Component.Ready) { log("overlay error: " + probe.comp.errorString()); probe.comp = null; if (probe.selftest) Qt.exit(2); return }
        log("ready — open a session, pick a shot, switch Replay / Analyse")
        // The try-out runs on a COPY of the settings: make sure the panel is on in Replay and Analyse.
        if (probe.panelKey !== "") {
            ViewLayout.setPanel(SessionMode.replay, probe.panelKey, true)
            ViewLayout.setPanel(SessionMode.analyse, probe.panelKey, true)
        }
        if (probe.sessionDir !== "") {
            navController.navigate(probe.navIndex)
            SessionMode.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            openShot.start()
        }
        if (probe.selftest) runSelftest()
    }
}
