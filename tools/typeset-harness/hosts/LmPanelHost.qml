import QtQuick
import QtTest
import PinPointStudio

// A render host for the REAL launch monitor panel (tools/typeset-harness/README.md). render.sh needs an
// overlay for print and cast; this one is not a mock — it instantiates PpLaunchMonitorPanel itself, which
// reads the same session the stage panel reads, and forwards the harness contract to it. With a
// disk-loaded app clone (memory: qml-disk-mock-harness) it renders the panel's working-tree QML.
//
//   render.sh --panel launchMonitor --tab-prop tab hosts/LmPanelHost.qml <tag> print-portrait
//   render.sh --panel launchMonitor --views main --states "session+pin14@disp,shot" hosts/LmPanelHost.qml <tag> full
//
// demoState tokens, joined by "+": shot | session (the tab) · club<LABEL> | clubreset (relabel the focused shot) · pin<ordinal>@<disp|map|pf|ls|strip> (pin the
// hover card on that shot in that figure, through the body's own pinCard) · chooser (open the pinned card's
// club chooser). Interaction states need the model's graphics.shots (the figures' marks).
Item {
    id: host
    property string view: "main"
    property string medium: "screen"
    property string printOrientation: ""
    property int printPage: 0
    readonly property int printPageCount: panel.printPageCount
    property string demoState: ""
    property string surface: "panel"
    readonly property string register: panel.graphicsBody ? panel.graphicsBody.register : ""

    PpLaunchMonitorPanel {
        id: panel
        anchors.fill: parent
        medium: host.medium
        printOrientation: host.printOrientation
        printPage: host.printPage
        surface: host.surface
        interactive: host.medium === "screen" && host.surface !== "cast"
    }

    function walk(item, pred, out) {
        if (!item) return
        try { if (pred(item)) out.push(item) } catch (e) {}
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out)
    }
    function applyDemo() {
        const b = panel.graphicsBody
        if (!b) return
        b.unpinCard()
        const toks = String(host.demoState).split("+").filter(t => t)
        for (const t of toks) if (t === "shot" || t === "session") panel.tab = t
        // club<LABEL> relabels the focused shot through the model (as the pinned card's Club does), e.g.
        // club3_WOOD, club4_HYBRID, clubSAND_WEDGE; clubreset restores its default. It is SAVED with the slot's copy.
        for (const t of toks) {
            const m = t.match(/^club(.+)$/)
            if (m) b.clubRequested(SessionMode.focusedShotId, m[1] === "reset" ? "" : m[1].replace(/_/g, " "))
        }
        Qt.callLater(() => pinStep(toks, 0))
    }
    function pinStep(toks, tries) {
        const b = panel.graphicsBody
        for (const t of toks) {
            const m = t.match(/^pin(\d+)@(\w+)$/)
            if (!m) continue
            let id = -1
            for (const s of panel.shotList) if (s.ordinal === +m[1]) id = s.shotId
            const pickers = []
            walk(b, it => it.src === m[2] && it.pts !== undefined && it.visible && it.width > 0, pickers)
            let done = false
            for (const pk of pickers) for (const p of pk.pts) if (p.id === id && !done) {
                b.pinCard(id, m[2], pk.mapToItem(b.cardLayerItem, p.x, p.y)); done = true
            }
            if (!done) { if (tries < 8) { retry.toks = toks; retry.tries = tries + 1; retry.start() } else console.warn("PROBE: no mark for " + t); return }
            if (toks.indexOf("chooser") >= 0) b.chooserFor = id
            // leave<ms>: after <ms>, a REAL click (QtTest TestEvent) on the pinned card's "Leave out" chip; the probe logs
            // whether the body asked the model, and where the chip and the click were. leave0 clicks at once (as a test
            // without a settle would). The model's answer is undone straight after (the slot's copy is restored).
            const lv = toks.map(x => x.match(/^leave(\d+)$/)).find(x => !!x)
            if (lv) host.clickLeave(id, +lv[1])
        }
    }
    TestEvent { id: te }
    property int excludeAsks: 0
    Connections { target: panel.graphicsBody; function onExcludeRequested(id, ex) { host.excludeAsks++; console.warn("PROBE: LEAVE excludeRequested " + id + " " + ex) } }
    function chipNamed(t) { const cs = []; walk(panel.graphicsBody, it => it.objectName === "lmChip" && it.visible && String(it.text).indexOf(t) === 0, cs); return cs[0] || null }
    function clickLeave(id, ms) {
        const go = () => {
            const c = chipNamed("Leave out")
            if (!c) { console.warn("PROBE: LEAVE no chip"); return }
            const p = c.mapToItem(null, c.width / 2, c.height / 2), before = host.excludeAsks
            console.warn("PROBE: LEAVE chip at " + Math.round(p.x) + "," + Math.round(p.y) + " size " + Math.round(c.width) + "x" + Math.round(c.height) + " after " + ms + " ms")
            te.mouseClick(c, c.width / 2, c.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            console.warn("PROBE: LEAVE result " + (host.excludeAsks > before ? "ASKED" : "NOT ASKED") + " pinned " + panel.graphicsBody.pinnedId)
            if (host.excludeAsks > before) panel.graphicsBody.excludeRequested(id, false)      // restore the slot's copy
        }
        if (ms > 0) leaveTimer.go = go, leaveTimer.interval = ms, leaveTimer.start(); else go()
    }
    Timer { id: leaveTimer; property var go: null; onTriggered: if (go) go() }
    Timer { id: retry; property var toks: []; property int tries: 0; interval: 300; onTriggered: host.pinStep(toks, tries) }
    onDemoStateChanged: applyDemo()

    // what the model published (for the report): the graphics map's keys and one shot, once
    Timer {
        interval: 2500; running: true
        onTriggered: {
            const g = panel.graphicsBody ? panel.graphicsBody.g : null
            if (!g) return
            console.warn("LMKEYS " + Object.keys(g).sort().join(","))
            if (g.shots && g.shots.length) console.warn("LMSHOT0 " + JSON.stringify(g.shots[0]))
            if (g.clubs && g.clubs.length) console.warn("LMCLUB0 " + JSON.stringify(g.clubs[0]).slice(0, 600))
            console.warn("LMMISMATCH " + JSON.stringify(g.mismatch) + " DEVICE " + JSON.stringify(g.device))
        }
    }
    // the This shot layout the body chose, for the layout sheet (assemble with the render's log)
    Timer {
        interval: 700; repeat: true; running: host.medium === "screen"
        onTriggered: {
            const L = panel.graphicsBody ? panel.graphicsBody.lastShotLayout : null
            if (L) console.warn("LMLAYOUT W " + L.W + " H " + L.H + " em " + L.em.toFixed(1) + " → " + L.arrangement + " cells " + L.cw + "×" + L.ch
                                + " Impact " + (isFinite(L.impactPxMm) ? L.impactPxMm.toFixed(2) + " px/mm" : "- (no art)")
                                + " flight " + L.fl.part + (L.fl.beside ? " beside" : " below") + " k " + L.fl.k.toFixed(2)
                                + (L.fl.inView ? "" : " (below the fold)") + " total " + Math.round(L.totalH))
        }
    }
}
