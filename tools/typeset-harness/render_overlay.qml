import QtQuick
import QtQuick.Controls
import PinPointStudio

// The typeset harness's render probe (tools/typeset-harness/README.md; run it through render.sh).
// Loads the fixture session in the real app, sets the real stage arrangement, finds the target panel,
// lays a mock QML file exactly over it (or, with no overlay, leaves the real panel showing), feeds the
// mock the panel's live data, and steps themes × views × demo states, grabbing the window and the panel.
//
//   --probe-target <objectName | key:<stageKey>>   default sdBody (Session diagnostics). key:charts finds
//                                                   the panel loaded in the stage card for "charts".
//   --probe-panel-key <stageKey>    the ViewLayout key the layout switches on (default: from the target)
//   --probe-variant <abs path.qml | none>          the mock; "none" (or empty) renders the real panel
//   --probe-feed p1,p2              body properties copied onto the mock (default for sdBody:
//                                   readout,cards,source,header,detail)
//   --probe-tab-prop <prop>         body property written to select a view (sdBody: tab); a number
//                                   is written when the property holds one
//   --probe-views v1,v2             "detail" opens --probe-detail via body._openDetail(id) when the body has it
//   --probe-detail <id>             default over_the_top
//   --probe-layout full|stage|stagecam|half|third|cast     --probe-mode analyse|replay
//   --probe-print portrait|landscape   A4 pages, grabbed at 2× (needs a dpr-2 screen)
//   --probe-states s1,s2            sets the mock's demoState before each grab
//   --probe-themes 7,12   --probe-session <dir>   --probe-ordinal 8   --probe-nav 2
//   --probe-out <dir>   --probe-tag <prefix>
//   --probe-dump 1                  print each fed property (and each detail id) as "DUMP <name> <json>", then quit
// Writes <out>/<tag>_<layout>_<aesthetic>_<light|dark>_<view>[_<state>]_{window,panel}.png
//     or <out>/<tag>_print-<orient>_<aesthetic>_<light|dark>_p<N>.png
//
// The mock's root declares what it reads and only reads it: `view`, the fed properties, optionally
// `body` (the live panel, so clicks run the real paths), `medium`, `printOrientation`, `printPage`,
// `printPageCount`, `demoState`, `surface`. The probe sets only what the root declares.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    function list(s) { return String(s).split(",").filter(x => x !== "") }
    readonly property string target: arg("--probe-target", "sdBody")
    readonly property string panelKey: arg("--probe-panel-key",
        target.indexOf("key:") === 0 ? target.slice(4) : (target === "sdBody" ? "sessionDiagnostics" : ""))
    readonly property var feedProps: list(arg("--probe-feed", target === "sdBody" ? "readout,cards,source,header,detail" : ""))
    readonly property string tabProp: arg("--probe-tab-prop", target === "sdBody" ? "tab" : "")
    readonly property var themes: list(arg("--probe-themes", "7,12")).map(Number)
    readonly property var views: list(arg("--probe-views", target === "sdBody" ? "shot,session,watching,detail" : "main"))
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string tag: arg("--probe-tag", "variant")
    readonly property string variantPath: arg("--probe-variant", "none")
    readonly property bool overlay: variantPath !== "" && variantPath !== "none"
    readonly property string detailId: arg("--probe-detail", "over_the_top")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))
    readonly property int navIndex: parseInt(arg("--probe-nav", "2"))
    readonly property string layout: arg("--probe-layout", "full")
    readonly property string modeName: arg("--probe-mode", "analyse")
    readonly property var states: list(arg("--probe-states", ""))
    readonly property string printMode: arg("--probe-print", "")
    readonly property bool dump: arg("--probe-dump", "") !== ""
    readonly property var allKeys: ["camera", "sessionDiagnostics", "swing3d", "launchMonitor", "wristMotion", "charts", "table", "markup"]
    property var host: null
    property var page: null
    property var jobs: []
    property int startTheme: 0
    property var body: null
    property var variant: null
    property var comp: null
    function log(s) { console.warn("PROBE: " + s) }
    function walkPred(item, pred, out) {
        if (!item) return
        try { if (pred(item)) out.push(item) } catch (e) {}
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walkPred(kids[i], pred, out)
    }
    // The card's panel Loader: of the Loaders under a stage card that have loaded something (not
    // looking inside what they loaded), the one whose item is largest. The shallowest is the card's
    // heading Loader when the card has a heading, so depth alone picks a 25 px title row.
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
    // Every live instance of the target, visible ones first (two screens can each hold a stage).
    function findTargets() {
        const root = probe.Window.window.contentItem, found = []
        if (probe.target.indexOf("key:") === 0) {
            const k = probe.target.slice(4), cards = []
            walkPred(root, it => it.def !== undefined && it.def !== null && it.def.key === k, cards)
            for (const c of cards) { const p = panelIn(c); if (p) found.push(p) }
        } else walkPred(root, it => it.objectName === probe.target, found)
        return found.filter(b => b.visible && b.width > 0).concat(found.filter(b => !(b.visible && b.width > 0)))
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
    property var bg: null
    property var frame: null
    function grab(item, path, then) {
        if (!probe.frame)
            probe.frame = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { x: -100000; color: Theme.colorBg; property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true; hideSource: false } }', probe)
        if (item === Overlay.overlay.parent) {
            probe.frame.width = item.width; probe.frame.height = item.height
            probe.frame.src.sourceRect = Qt.rect(0, 0, 0, 0)
        } else {
            const p = item.mapToItem(Overlay.overlay.parent, 0, 0)
            const pad = 16
            probe.frame.width = item.width + 2 * pad; probe.frame.height = item.height + 2 * pad
            probe.frame.src.sourceRect = Qt.rect(p.x - pad, p.y - pad, item.width + 2 * pad, item.height + 2 * pad)
        }
        probe.frame.src.sourceItem = Overlay.overlay.parent
        probe.frame.src.scheduleUpdate()
        after(300, () => {
            probe.frame.grabToImage(function (img) { img.saveToFile(path); probe.log("saved " + path); then() })
        })
    }
    function mode() { return probe.modeName === "replay" ? SessionMode.replay : SessionMode.analyse }
    // full / cast: the stage's tabs, on the target's tab. half: the target + one neighbour; third and
    // stage: + two; stagecam: the target in the side column beside a dominant camera. The timeline and
    // carousel are on, as Mark uses the stage (so height is what it really is).
    function applyLayout() {
        const m = probe.mode(), key = probe.panelKey
        if (key === "") { probe.log("no panel key for target " + probe.target + " — pass --probe-panel-key"); return }
        ViewLayout.setPanel(m, "timeline", true)
        ViewLayout.setPanel(m, "carousel", true)
        if (probe.layout === "full" || probe.layout === "cast") {
            ViewLayout.setPanel(m, key, true)
            ViewLayout.setArrangement(m, "tabs")
        } else {
            let on
            if (probe.layout === "stagecam") on = ["camera", key]
            else if (probe.layout === "stage") on = [key].concat(["charts", "table", "markup"].filter(k => k !== key).slice(0, 2))
            else on = [key].concat(["charts", "table", "camera"].filter(k => k !== key).slice(0, probe.layout === "half" ? 1 : 2))
            for (const k of probe.allKeys) ViewLayout.setPanel(m, k, on.indexOf(k) >= 0)
            ViewLayout.setArrangement(m, (probe.layout === "stage" || probe.layout === "stagecam") ? "stage" : "split")
        }
        probe.log("layout " + probe.layout + " → " + ViewLayout.arrangementFor(m) + " " + ViewLayout.enabledKeysFor(m).join(","))
    }
    function selectTab() {
        if (probe.layout !== "full" && probe.layout !== "cast") return
        const st = []
        walkPred(probe.Window.window.contentItem, it => it._defs !== undefined && it.tabIndex !== undefined && it.active !== undefined && it.visible, st)
        if (!st.length) { probe.log("no stage found to select the tab"); return }
        const idx = st[0].active.findIndex(d => d.key === probe.panelKey)
        if (idx >= 0 && st[0].tabIndex !== idx) st[0].tabIndex = idx
    }
    // Does the mock's root declare p? (An unset `property var` reads undefined, so test with `in`.)
    function has(o, p) { try { return !!o && (p in o) } catch (e) { return false } }
    function feed() {
        const b = probe.body, v = probe.variant
        if (!probe.overlay || !v) return
        if (has(v, "body")) v.body = b
        for (const p of probe.feedProps) if (has(v, p)) v[p] = b[p]
    }
    function setTab(view) {
        const b = probe.body
        if (probe.tabProp === "" || b[probe.tabProp] === undefined) return
        b[probe.tabProp] = (typeof b[probe.tabProp] === "number") ? Number(view) : view
    }
    function showView(view, then) {
        const b = probe.body
        const hasDetail = typeof b._openDetail === "function"
        if (view === "detail") {
            if (hasDetail) b._openDetail(probe.detailId); else probe.log("target has no _openDetail(id): the detail view shows its current state")
        } else {
            if (hasDetail && b.detailOpen) b._closeDetail()
            setTab(view)
        }
        after(1500, () => {
            feed()
            if (probe.overlay && has(probe.variant, "view")) probe.variant.view = view
            after(1200, then)
        })
    }
    // ── print: an A4 sheet at 96 dpi logical (794×1123 portrait), on a dpr-2 screen, grabbed at 2× ──
    property var printJobs: []
    function startPrint(comp) {
        const portrait = probe.printMode === "portrait"
        const pw = portrait ? 794 : 1123, ph = portrait ? 1123 : 794
        probe.page = Qt.createQmlObject('import QtQuick; Rectangle { z: 1000000; color: "#FFFFFF" }', probe.Window.window.contentItem)
        probe.page.width = pw; probe.page.height = ph
        probe.variant = comp.createObject(probe.page, { width: pw, height: ph })
        if (!probe.variant) { probe.log("print variant create failed: " + comp.errorString()); Qt.exit(2); return }
        if (has(probe.variant, "medium")) probe.variant.medium = "print"
        if (has(probe.variant, "printOrientation")) probe.variant.printOrientation = probe.printMode
        for (const t of probe.themes) probe.printJobs.push(t)
        probe.nextPrintTheme()
    }
    function nextPrintTheme() {
        if (probe.printJobs.length === 0) { Theme.themeIndex = probe.startTheme; after(300, () => Qt.exit(0)); return }
        Theme.themeIndex = probe.printJobs.shift()
        after(1500, () => {
            const bs = probe.findTargets()
            if (bs.length) probe.body = bs[0]
            if (typeof probe.body._openDetail === "function") probe.body._openDetail(probe.detailId)
            after(1500, () => { probe.feed(); probe.variant.printPage = 0; after(1200, () => probe.printPageAt(0)) })
        })
    }
    function printPageAt(p) {
        const n = probe.variant.printPageCount || 1
        if (p >= n) { probe.nextPrintTheme(); return }
        probe.variant.printPage = p
        after(900, () => {
            const path = probe.outDir + "/" + probe.tag + "_print-" + probe.printMode + "_" + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + "_p" + (p + 1) + ".png"
            probe.page.grabToImage(function (img) { img.saveToFile(path); probe.log("saved " + path); probe.printPageAt(p + 1) }, Qt.size(probe.page.width * 2, probe.page.height * 2))
        })
    }
    // A theme switch can rebuild the stage's panels (split / stage arrangements), destroying the body
    // the mock was laid over. Re-find the target after every switch and re-attach if it changed.
    function attach(b) {
        probe.body = b
        if (!probe.overlay || probe.layout === "cast") return true
        const v = probe.comp.createObject(b.parent, { z: 100 })
        if (!v) { probe.log("variant create failed: " + probe.comp.errorString()); Qt.exit(2); return false }
        // Bound, not copied: the body can still be settling to its final size.
        v.x = Qt.binding(() => b.x); v.y = Qt.binding(() => b.y)
        v.width = Qt.binding(() => b.width); v.height = Qt.binding(() => b.height)
        probe.variant = v
        b.opacity = 0
        return true
    }
    function ensureBody() {
        const bs = probe.findTargets()
        if (bs.length < 1) { probe.log("no " + probe.target + " after the switch"); Qt.exit(1); return false }
        if (bs[0] === probe.body && (probe.variant || !probe.overlay)) return true
        if (probe.layout === "cast") { probe.body = bs[0]; probe.body.opacity = 1; return true }
        if (probe.variant) probe.variant.destroy()
        probe.variant = null
        if (!attach(bs[0])) return false
        probe.log("re-attached to a rebuilt panel " + probe.body.width + "x" + probe.body.height)
        return true
    }
    function next() {
        if (jobs.length === 0) { Theme.themeIndex = startTheme; after(300, () => Qt.exit(0)); return }
        const j = jobs.shift()
        Theme.themeIndex = j.theme
        after(1500, () => { probe.selectTab(); after(300, () => { if (!probe.ensureBody()) return; showView(j.view, () => {
            const stem = outDir + "/" + tag + "_" + probe.layout + "_" + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + "_" + j.view + (j.state ? "_" + j.state : "")
            if (probe.overlay && has(probe.variant, "demoState")) probe.variant.demoState = j.state
            const panel = probe.layout === "cast" ? probe.host : (probe.overlay ? probe.variant : probe.body)
            after(j.state ? 900 : 0, () => grab(Overlay.overlay.parent, stem + "_window.png",
                () => grab(panel, stem + "_panel.png", next)))
        }) }) })
    }
    function dumpAll() {
        const b = probe.body
        function j(o) { try { return JSON.stringify(o) } catch (e) { return JSON.stringify("ERR " + e) } }
        for (const p of probe.feedProps) console.warn("DUMP " + p + " " + j(b[p]))
        if (probe.tabProp !== "") console.warn("DUMP " + probe.tabProp + " " + j(b[probe.tabProp]))
        const ids = typeof b._openDetail === "function" ? list(probe.detailId) : []
        function step(i) {
            if (i >= ids.length) { probe.log("dump done"); Qt.exit(0); return }
            b._openDetail(ids[i])
            probe.after(1500, () => { console.warn("DUMP detail_" + ids[i] + " " + j(b.detail)); step(i + 1) })
        }
        step(0)
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            // Silence: no probe may play sound (the app's tings are muted at their source).
            let muted = 0
            const seen = new Set()
            function mute(o) {
                if (!o || seen.has(o)) return
                seen.add(o)
                if (String(o).indexOf("TingPlayer") === 0) { o.volume = 0; o.frequency = 1; muted++ }
                const ds = o.data || []
                for (let i = 0; i < ds.length; ++i) mute(ds[i])
            }
            mute(probe.Window.window.contentItem)
            probe.log("muted " + muted + " ting players")
            if (!probe.overlay && (probe.layout === "cast" || probe.printMode !== "")) {
                probe.log("cast and print need an overlay: the real panel has no offscreen cast or page here"); Qt.exit(2); return
            }
            probe.bg = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', probe.Window.window.contentItem)
            for (const t of probe.themes) for (const vw of probe.views) for (const st of (probe.states.length ? probe.states : [""])) probe.jobs.push({ theme: t, view: vw, state: st })
            probe.applyLayout()
            navController.navigate(probe.navIndex)
            SessionMode.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            probe.after(4000, () => {
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
                }
                probe.after(3000, () => {
                    if (probe.modeName !== "replay") SessionMode.enterAnalyse()
                    probe.after(4000, () => {
                        probe.selectTab()
                        probe.after(500, () => {
                            const bs = probe.findTargets()
                            if (bs.length < 1) { probe.log("no " + probe.target + " on the stage"); Qt.exit(1); return }
                            probe.body = bs[0]
                            probe.log("target " + probe.target + " at " + probe.body.x + "," + probe.body.y + " " + probe.body.width + "x" + probe.body.height)
                            if (probe.dump) { probe.dumpAll(); return }
                            if (!probe.overlay) { probe.next(); return }
                            const comp = Qt.createComponent("file://" + probe.variantPath)
                            probe.comp = comp
                            if (comp.status !== Component.Ready) { probe.log("variant error: " + comp.errorString()); Qt.exit(2); return }
                            if (probe.printMode !== "") { probe.startPrint(comp); return }
                            if (probe.layout === "cast") {
                                // The second-screen cast: a window of its own (~55 % × 72 % of a 1920×1080 display),
                                // hosted here as a 1040×760 sheet over the app so the mock reads the live body.
                                probe.host = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: 1000000; x: 60; y: 60; width: 1040; height: 760; color: Theme.colorBg; border.width: 1; border.color: Theme.colorBorderMid }', probe.Window.window.contentItem)
                                probe.variant = comp.createObject(probe.host, { x: 24, y: 24, width: 1040 - 48, height: 760 - 48 })
                                if (!probe.variant) { probe.log("variant create failed: " + comp.errorString()); Qt.exit(2); return }
                                if (has(probe.variant, "surface")) probe.variant.surface = "cast"
                            } else if (!probe.attach(probe.body)) return
                            probe.next()
                        })
                    })
                })
            })
        }
    }
}
