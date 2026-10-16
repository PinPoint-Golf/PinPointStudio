import QtQuick
import QtQuick.Controls
import PinPointStudio

// The typeset harness's stage-audit probe (tools/typeset-harness/README.md; run it through
// render_stage.sh). Draws nothing of its own: for each theme × layout × panel it sets ViewLayout so
// that panel shows in the requested arrangement, with the timeline and carousel on, waits, then grabs
// the window and the panel's stage card (title strip included). Use it for step 1's "before" renders.
//   --probe-session <dir> --probe-ordinal 8 --probe-mode analyse|replay --probe-nav 2
//   --probe-themes 12,7 --probe-layouts full,half,third --probe-panels camera,charts,...
//   --probe-settle 3000 --probe-out <dir> --probe-tag audit
// Writes <out>/<tag>_<mode>_<layout>_<aesthetic>_<light|dark>_<panel>_{window,panel}.png
// (the mode joins the tag, so assemble_review.py reads it as tag <tag>_<mode>, view <panel>).
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var themes: arg("--probe-themes", "12,7").split(",").map(Number)
    readonly property var layouts: arg("--probe-layouts", "full,half,third").split(",")
    readonly property var panels: arg("--probe-panels", "camera,swing3d,launchMonitor,charts,table,markup").split(",")
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string tag: arg("--probe-tag", "audit")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))
    readonly property string modeName: arg("--probe-mode", "analyse")
    readonly property int settle: parseInt(arg("--probe-settle", "3000"))
    readonly property int navIndex: parseInt(arg("--probe-nav", "2"))
    readonly property var allKeys: ["sessionDiagnostics", "camera", "swing3d", "launchMonitor", "charts", "table", "markup"]
    property var jobs: []
    property int startTheme: 0
    function log(s) { console.warn("PROBE: " + s) }
    function walkPred(item, pred, out) {
        if (!item) return
        try { if (pred(item)) out.push(item) } catch (e) {}
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walkPred(kids[i], pred, out)
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
            probe.frame.grabToImage(function (img) { img.saveToFile(path); then() })
        })
    }
    function mode() { return probe.modeName === "replay" ? SessionMode.replay : SessionMode.analyse }
    // half: the panel + one neighbour; third: + two. Neighbours are cheap canvases.
    function neighbours(key, n) {
        const pool = ["charts", "table", "camera"].filter(k => k !== key)
        return pool.slice(0, n)
    }
    function applyLayout(layout, key) {
        const m = probe.mode()
        let on
        if (layout === "full") on = probe.allKeys.filter(k => k !== "sessionDiagnostics" || k === key)   // diagnostics is heavy: on only when audited
        else on = [key].concat(probe.neighbours(key, layout === "half" ? 1 : 2))
        for (const k of probe.allKeys) ViewLayout.setPanel(m, k, on.indexOf(k) >= 0)
        ViewLayout.setPanel(m, "timeline", true)
        ViewLayout.setPanel(m, "carousel", true)
        ViewLayout.setArrangement(m, layout === "full" ? "tabs" : "split")
        if (layout === "full") {
            const st = []
            probe.walkPred(probe.Window.window.contentItem, it => it._defs !== undefined && it.tabIndex !== undefined && it.active !== undefined && it.visible, st)
            if (st.length) {
                const idx = st[0].active.findIndex(d => d.key === key)
                st[0].tabIndex = Math.max(0, idx)
            } else probe.log("no stage found")
        }
        probe.log("layout " + layout + " " + key + " → " + ViewLayout.arrangementFor(m) + " " + ViewLayout.enabledKeysFor(m).join(","))
    }
    function cardFor(key) {
        const cs = []
        probe.walkPred(probe.Window.window.contentItem, it => it.def !== undefined && it.def !== null && it.def.key === key && it.visible && it.width > 0, cs)
        return cs.length ? cs[0] : null
    }
    function next() {
        if (jobs.length === 0) { Theme.themeIndex = startTheme; after(300, () => Qt.exit(0)); return }
        const j = jobs.shift()
        const switching = Theme.themeIndex !== j.theme
        Theme.themeIndex = j.theme
        after(switching ? 1500 : 10, () => {
            probe.applyLayout(j.layout, j.panel)
            after(probe.settle, () => {
                const stem = outDir + "/" + tag + "_" + probe.modeName + "_" + j.layout + "_" + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + "_" + j.panel
                const card = probe.cardFor(j.panel)
                grab(Overlay.overlay.parent, stem + "_window.png", () => {
                    if (!card) { probe.log("no card for " + j.panel); next(); return }
                    probe.log("card " + j.panel + " " + Math.round(card.width) + "x" + Math.round(card.height))
                    grab(card, stem + "_panel.png", () => { probe.log("saved " + stem); next() })
                })
            })
        })
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
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
            probe.log("muted " + muted + " ting players")   // silence: no probe may play sound
            Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', probe.Window.window.contentItem)
            for (const t of probe.themes) for (const l of probe.layouts) for (const p of probe.panels) probe.jobs.push({ theme: t, layout: l, panel: p })
            // start with no heavy panel on while the session loads
            const m = probe.mode()
            for (const k of probe.allKeys) ViewLayout.setPanel(m, k, k === "charts")
            ViewLayout.setPanel(SessionMode.replay, "sessionDiagnostics", false)
            ViewLayout.setPanel(SessionMode.analyse, "sessionDiagnostics", false)
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
                    probe.after(3000, () => probe.next())
                })
            })
        }
    }
}
