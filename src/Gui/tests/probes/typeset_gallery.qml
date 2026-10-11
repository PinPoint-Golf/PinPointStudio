/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

import QtQuick
import QtQuick.Controls
import PinPointStudio

// TYPESET GALLERY — every block of the typeset library once, for the given view, each under a caption
// naming it, wired the way a register wires them (clicks → the real body), laid over the REAL Session
// diagnostics panel in the real stage arrangement and fed the real data. Ported from the round-4 mock
// library's Gallery.qml and the render_variant3 harness, onto the built types (PpTypeset / PpDiagRoles /
// PpBrush and the Pp blocks). It renders, it does not assert: judge the PNGs; QML errors appear in the log.
//
//   XDG_CONFIG_HOME=<scratch cfg> QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM="offscreen:configfile=<screen.json>" \
//     PinPointStudio --probe-qml <this file> --probe-session <SCRATCH copy of a session> --probe-ordinal 8 \
//     --probe-layout full|stage|half|third|cast --probe-mode analyse|replay --probe-themes 7,12 \
//     --probe-views shot,session,watching,detail --probe-detail over_the_top --probe-states "" \
//     [--probe-print portrait|landscape] --probe-out <dir> --probe-tag gallery
//   screen.json: {"screens":[{"name":"s","x":0,"y":0,"width":1680,"height":1050,"logicalDpi":96,"logicalBaseDpi":96,"dpr":1}]}
// Loading a session regrades and WRITES into it: always point --probe-session at a scratch copy.
// PNGs: <out>/<tag>_<layout>_<aesthetic>_<light|dark>_<view>[_<state>]_{window,panel}.png, or
//       <out>/<tag>_print-<orient>_<aesthetic>_<light|dark>_p<N>.png
// demoState (--probe-states): hoverShot12 · hoverCondition · question-clean · question-together · pulse ·
//       openPath · page2 · page3. Silent: the app's ting players are muted before the session loads.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var themes: arg("--probe-themes", "7,12").split(",").map(Number)
    readonly property var views: arg("--probe-views", "shot,session,watching,detail").split(",")
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string tag: arg("--probe-tag", "gallery")
    readonly property string detailId: arg("--probe-detail", "over_the_top")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))
    readonly property string layout: arg("--probe-layout", "full")          // full | stage | half | third | cast
    readonly property string modeName: arg("--probe-mode", "analyse")       // analyse | replay
    readonly property var states: arg("--probe-states", "").split(",").filter(s => s !== "")
    readonly property string printMode: arg("--probe-print", "")            // "" | portrait | landscape
    property var host: null
    property var page: null
    property var jobs: []
    property int startTheme: 0
    property var body: null
    property var variant: null
    function log(s) { console.warn("PROBE: " + s) }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
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
            probe.frame.grabToImage(function (img) { img.saveToFile(path); probe.log("saved " + path); then() })
        })
    }
    function applyLayout() {
        const m = probe.modeName === "replay" ? SessionMode.replay : SessionMode.analyse
        const others = ["camera", "swing3d", "launchMonitor", "wristMotion", "charts", "table", "markup"]
        if (probe.layout === "full" || probe.layout === "cast") {
            ViewLayout.setPanel(m, "sessionDiagnostics", true)
            ViewLayout.setArrangement(m, "tabs")
        } else {
            const on = probe.layout === "half" ? ["charts"] : ["charts", "table"]
            ViewLayout.setPanel(m, "sessionDiagnostics", true)
            for (const k of others) ViewLayout.setPanel(m, k, on.indexOf(k) >= 0)
            ViewLayout.setArrangement(m, probe.layout === "stage" ? "stage" : "split")
        }
        probe.log("layout " + probe.layout + " → " + ViewLayout.arrangementFor(m) + " " + ViewLayout.enabledKeysFor(m).join(","))
    }
    function feed() {
        const b = probe.body, v = probe.variant
        if (v.body !== undefined) v.body = b
        v.readout = b.readout; v.cards = b.cards; v.source = b.source; v.header = b.header
        v.detail = b.detail
    }
    function showView(view, then) {
        const b = probe.body
        if (view === "detail") { b._openDetail(probe.detailId) } else if (b.detailOpen) b._closeDetail()
        after(1500, () => {
            feed()
            probe.variant.view = view
            after(1200, then)
        })
    }
    // ── print: an A4 sheet at 96 dpi logical (794×1123 portrait), grabbed at 2× ──
    property var printJobs: []
    function startPrint(comp) {
        const portrait = probe.printMode === "portrait"
        const pw = portrait ? 794 : 1123, ph = portrait ? 1123 : 794
        probe.page = Qt.createQmlObject('import QtQuick; Rectangle { z: 1000000; color: "#FFFFFF" }', probe.Window.window.contentItem)
        probe.page.width = pw; probe.page.height = ph
        probe.variant = comp.createObject(probe.page, { width: pw, height: ph, medium: "print" })
        if (!probe.variant) { probe.log("print gallery create failed: " + comp.errorString()); Qt.exit(2); return }
        probe.variant.printOrientation = probe.printMode
        for (const t of probe.themes) probe.printJobs.push(t)
        probe.nextPrintTheme()
    }
    function nextPrintTheme() {
        if (probe.printJobs.length === 0) { Theme.themeIndex = probe.startTheme; after(300, () => Qt.exit(0)); return }
        Theme.themeIndex = probe.printJobs.shift()
        probe.body._openDetail(probe.detailId)
        after(1500, () => { probe.feed(); probe.variant.printPage = 0; after(1200, () => probe.printPageAt(0)) })
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
    // A theme switch can rebuild the stage's panels (split / stage arrangements), destroying the body the
    // gallery was laid over. Re-find sdBody after every switch and re-attach if it changed.
    function ensureBody() {
        const bs = []
        probe.walk(probe.Window.window.contentItem, "sdBody", bs)
        if (bs.length < 1) { probe.log("no sdBody after theme switch"); Qt.exit(1); return false }
        if (bs[0] === probe.body && probe.variant) return true
        if (probe.layout === "cast") { probe.body = bs[0]; probe.body.opacity = 1; return true }
        if (probe.variant) probe.variant.destroy()
        probe.body = bs[0]
        probe.variant = galleryComp.createObject(probe.body.parent, {
            x: probe.body.x, y: probe.body.y, width: probe.body.width, height: probe.body.height, z: 100 })
        probe.body.opacity = 0
        probe.log("re-attached to a rebuilt panel " + probe.body.width + "x" + probe.body.height)
        return true
    }
    function next() {
        if (jobs.length === 0) { Theme.themeIndex = startTheme; after(300, () => Qt.exit(0)); return }
        const j = jobs.shift()
        Theme.themeIndex = j.theme
        after(1500, () => { if (!probe.ensureBody()) return; showView(j.view, () => {
            const stem = outDir + "/" + tag + "_" + probe.layout + "_" + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + "_" + j.view + (j.state ? "_" + j.state : "")
            if (probe.variant.demoState !== undefined) probe.variant.demoState = j.state
            after(j.state ? 900 : 0, () => grab(Overlay.overlay.parent, stem + "_window.png",
                () => grab(probe.layout === "cast" ? probe.host : probe.variant, stem + "_panel.png", next)))
        }) })
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            // Silence (tests and probes never sound): the app's own players go to volume 0 before a
            // session loads, since picking a shot would otherwise ring them.
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
            Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', probe.Window.window.contentItem)
            for (const t of probe.themes) for (const vw of probe.views) for (const st of (probe.states.length ? probe.states : [""])) probe.jobs.push({ theme: t, view: vw, state: st })
            probe.applyLayout()
            navController.navigate(2)
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
                        const bs = []
                        probe.walk(probe.Window.window.contentItem, "sdBody", bs)
                        if (bs.length < 1) { probe.log("no sdBody"); Qt.exit(1); return }
                        probe.body = bs[0]
                        if (probe.printMode !== "") { probe.startPrint(galleryComp); return }
                        probe.variant = galleryComp.createObject(probe.body.parent, {
                            x: probe.body.x, y: probe.body.y, width: probe.body.width, height: probe.body.height, z: 100 })
                        if (!probe.variant) { probe.log("gallery create failed: " + galleryComp.errorString()); Qt.exit(2); return }
                        probe.body.opacity = 0
                        probe.log("gallery at " + probe.body.x + "," + probe.body.y + " " + probe.body.width + "x" + probe.body.height)
                        if (probe.layout === "cast") {
                            // The second-screen cast: a window of its own (~55 % × 72 % of a 1920×1080 display),
                            // hosted here as a 1040×760 sheet over the app so the gallery reads the live body.
                            probe.variant.destroy()
                            probe.body.opacity = 1
                            probe.host = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: 1000000; x: 60; y: 60; width: 1040; height: 760; color: Theme.colorBg; border.width: 1; border.color: Theme.colorBorderMid }', probe.Window.window.contentItem)
                            probe.variant = galleryComp.createObject(probe.host, { x: 24, y: 24, width: 1040 - 48, height: 760 - 48 })
                            if (probe.variant.surface !== undefined) probe.variant.surface = "cast"
                        }
                        probe.next()
                    })
                })
            })
        }
    }

    // A caption naming the block under it (probe furniture, not a block: styled from Theme directly).
    component Caption: Text {
        property string block: ""
        property string note: ""
        width: parent ? parent.width : 0
        topPadding: Theme.sp(14); bottomPadding: Theme.sp(4); wrapMode: Text.WordWrap
        text: "▸ " + block + (note ? "  ·  " + note : "")
        font.family: Theme.fontBody; font.pixelSize: Theme.sp(8.5); color: Theme.colorText3
    }

    // ═══════════════ the gallery, made over the real body (it satisfies the register contract) ═══════════════
    Component {
        id: galleryComp
        Item {
            id: root
            // ── harness contract ──
            property string view: "shot"
            property var readout: null
            property var cards: []
            property var source: null
            property var header: null
            property var detail: null
            property string medium: "screen"
            property string printOrientation: ""
            property int printPage: 0
            readonly property int printPageCount: printLoader.item ? printLoader.item.pageCount : 1
            property var body: null
            property string demoState: ""
            property string surface: "panel"
            readonly property bool isPrint: medium === "print"

            readonly property var conds: (readout && readout.conditions) || []
            readonly property var fired: conds.filter(c => c.stateKind === "fired")
            readonly property var clean: conds.filter(c => c.stateKind === "clean")
            readonly property var notMeasured: conds.filter(c => c.stateKind === "notAssessable")
            readonly property var watching: (source && source.watching) || []
            readonly property var detailCard: detail ? ro.byId(cards || [], detail.id) || (detail.header || null) : null
            property string sessionFilter: "all"
            property var openPaths: ({})
            readonly property var shownCards: (cards || []).filter(c => sessionFilter === "all" || (sessionFilter === "ball" && c.reachesBall) || (sessionFilter === "roots" && c.rootHere))

            // ── round 4: synthetic data built from the real ──
            // a 45-shot session: every run tripled, ids renumbered, x positions recomputed (D7)
            readonly property int longK: 3
            function longTicks(t) { const out = [], n = (t || []).length; for (let k = 0; k < longK; ++k) (t || []).forEach((x, i) => out.push(Object.assign({}, x, { shotId: k * n + i + 1, selected: false }))); return out }
            function longSpread(sp) {
                if (!sp) return sp
                const run = sp.run || [], n = run.length, out = [], med = []
                for (let k = 0; k < longK; ++k) {
                    run.forEach((r, i) => out.push(Object.assign({}, r, { shotId: k * n + i + 1, index: k * n + i, fx: (k * n + i + 0.5) / (n * longK), current: !!r.current && k === 0 })))
                    ;(sp.median || []).forEach(m => med.push(Object.assign({}, m, { fx: (k + m.fx) / longK })))
                }
                return Object.assign({}, sp, { run: out, median: med })
            }
            readonly property var longCards: (cards || []).map(c => Object.assign({}, c, { ticks: longTicks(c.ticks), spread: longSpread(c.spread) }))
            readonly property var longReadout: readout ? Object.assign({}, readout, { shotCount: readout.shotCount * longK,
                                                  conditions: (readout.conditions || []).map(c => Object.assign({}, c, { ticks: longTicks(c.ticks) })) }) : null
            // the real paths, regraded through all five grades in turn, with the strokes the model would publish (D6)
            readonly property var gradeDetail: {
                if (!detail) return null
                const gs = ro.linkGradeOrder
                let n = 0
                const fix = ps => (ps || []).map(p => Object.assign({}, p, { links: (p.links || []).map(l => {
                    const g = gs[n++ % gs.length], t = ro.linkGrades[g]
                    return Object.assign({}, l, { grade: g, word: t.word, width: t.width, arrow: t.arrow, opacity: t.opacity,
                                                  stroke: g === "movedTogether" ? "solidAccent" : g === "unanchored" ? "dottedFaint" : t.stroke })
                }) }))
                return Object.assign({}, detail, { causes: fix(detail.causes), effects: fix(detail.effects) })
            }
            readonly property var demoExpectations: (source && source.expectations && source.expectations.length) ? source.expectations
                : [{ name: "Over the top", text: "Your profile: the hands come down outside the path they went up on, on most sessions." },
                   { name: "Early extension", text: "Your profile: the pelvis moves toward the ball through impact." }]
            property bool pickerOpen: true
            property string demoMiss: "slice"

            // ── wiring: every click goes somewhere real ──
            function openCondition(id) { if (body && id) body._openDetail(id) }
            function pickShot(dir, id) { if (body && id > 0) body.shotRequested(dir || br.dirOf(id), id) }

            // ── demo states (renderable interaction) ──
            onDemoStateChanged: applyDemo()
            function applyDemo() {
                br.clear()
                br.pulseHold = false; br.pulseIds = []
                if (demoState === "pulse") { br.pulseHold = true; br.pulse(root.fired.map(c => c.id), root.fired.length > 1 ? root.fired[1].id : "") }
                else if (demoState === "page2" || demoState === "page3") flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, (demoState === "page2" ? 1 : 2) * flick.height * 0.92))
                else if (demoState === "openPath") root.openPaths = { "causes:0": true, "effects:1": true }
                if (demoState === "hoverShot12") br.hoveredShot = 12
                else if (demoState === "hoverCondition") br.hoveredCondition = "early_extension"
                else if (demoState === "question-clean" || demoState === "question-together") {
                    const card = root.view === "detail" ? root.detailCard : (root.cards || [])[5]
                    const qs = questionsHidden.forCondition(card, root.readout, root.source)
                    const q = qs.filter(x => x.id === demoState.slice(9))[0]
                    if (q) questionsHidden.ask(q)
                }
            }

            // The harness sizes the root once from the body; follow the body we lie over (A's fix).
            Component.onCompleted: {
                if (!parent || isPrint) return
                const kids = parent.children || []
                for (let i = 0; i < kids.length; ++i) {
                    const b = kids[i]
                    if (b !== root && b.objectName === "sdBody") {
                        root.width = Qt.binding(() => b.width); root.height = Qt.binding(() => b.height)
                        root.x = Qt.binding(() => b.x); root.y = Qt.binding(() => b.y)
                        break
                    }
                }
            }


            // ═══════════════════════════ SCREEN ═══════════════════════════
            Item {
                anchors.fill: parent
                visible: !root.isPrint
                PpTabs {
                    id: tabs
                    visible: root.view !== "detail"
                    width: parent.width
                    roles: ro; brush: br
                    current: root.view
                    note: root.header ? ro.cap1(root.header.reviewNote) : ""
                    tabs: [{ key: "shot", label: "This shot", count: root.fired.length },
                           { key: "session", label: "Session", count: (root.cards || []).length },
                           { key: "watching", label: "Watching", count: root.watching.length }]
                }
                PpDetailBar {
                    id: dbar
                    visible: root.view === "detail"
                    width: parent.width
                    roles: ro; brush: br
                    crumb: root.detail ? root.detail.name || "" : ""
                    focused: !!(root.detailCard && root.detailCard.focused)
                    onBackActivated: if (root.body) root.body._closeDetail()
                    onFocusActivated: if (root.body && root.detail) root.body._declareFocus(root.detail.id, !focused)
                }
                Flickable {
                    id: flick
                    anchors { top: tabs.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; topMargin: ro.u(4) }
                    contentWidth: width; contentHeight: col.height + ro.u(24)
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Column {
                        id: col
                        width: flick.width - ro.u(8)

                        // ── THIS SHOT ──
                        Loader { active: root.view === "shot"; width: col.width; visible: active; sourceComponent: shotC }
                        Loader { active: root.view === "session"; width: col.width; visible: active; sourceComponent: sessionC }
                        Loader { active: root.view === "watching"; width: col.width; visible: active; sourceComponent: watchingC }
                        Loader { active: root.view === "detail"; width: col.width; visible: active; sourceComponent: detailC }
                    }
                }
            }

            Component {
                id: shotC
                Column {
                    Caption { block: "PpStandfirst"; note: "B · head + sentence from data" }
                    PpStandfirst { width: parent.width; roles: ro; brush: br
                                 kicker: root.readout ? ro.cap1(ro.shotOfText(root.readout)) + " · " + String(root.readout.club).toLowerCase() : ""
                                 head: ro.cap1(ro.word(root.fired.length)) + " outside the corridor"
                                 standfirst: ro.shotStandfirst(root.readout, ro.shows("longStandfirst", width)) }
                    Caption { block: "PpPageKey"; note: "C · set shot" }
                    PpPageKey { width: parent.width; roles: ro; readout: root.readout; set: "shot" }
                    Caption { block: "PpDotPlot"; note: "C · filter fired (Replay); the scale is the whole plot's" }
                    PpDotPlot { width: parent.width; roles: ro; brush: br; readout: root.readout; filter: "fired"
                              onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpShotRuler"; note: "round 4 · the shot axis (Replay); counts from every condition" }
                    PpShotRuler { roles: ro; brush: br; readout: root.readout; onShotActivated: (d, s) => root.pickShot(d, s) }
                    Caption { block: "PpDotPlot"; note: "round 4 · showTier: at 50 em the tier folds onto the name before the plot goes tight" }
                    PpDotPlot { width: Math.min(parent.width, 50 * ro.em); roles: ro; brush: br; readout: root.readout; filter: "fired"
                              onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpShotRuler"; note: "round 4 · 45 shots fitted to the width (D7): narrows, numbers thin, then wraps" }
                    PpShotRuler { roles: ro; brush: br; readout: root.longReadout; availableWidth: parent.width }
                    Caption { block: "PpLedgerTable"; note: "A · kind fired" }
                    PpLedgerTable { width: parent.width; roles: ro; brush: br; kind: "fired"; rows: root.fired; readout: root.readout
                                  title: "Outside the corridor"
                                  onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s) }
                    PpNotes { width: parent.width; roles: ro; keepPrev: true
                            text: "Out by: distance past the corridor edge, as a multiple of the distance to it.  Fired: shots outside the corridor, of those measurable." }
                    Caption { block: "PpEntryFlow"; note: "B · kind fired" }
                    PpEntryFlow { width: parent.width; roles: ro; brush: br; kind: "fired"; items: root.fired; cards: root.cards; readout: root.readout
                                label: "The session’s patterns and the watched"
                                onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpIndexList"; note: "A · leader dots, " + ro.flows.index + " em columns" }
                    PpIndexList { width: parent.width; roles: ro; brush: br; conditions: root.clean
                                onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpDotPlot"; note: "C · all conditions" }
                    PpDotPlot { width: parent.width; roles: ro; brush: br; readout: root.readout
                              onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpNotes" }
                    PpNotes { width: parent.width; roles: ro
                            text: "Not measurable on this shot (" + root.notMeasured.length + "): " + root.notMeasured.map(c => ro.esc(c.name) + " — " + ro.esc(c.reason)).join("; ") + "." }
                }
            }
            Component {
                id: sessionC
                Column {
                    spacing: 0
                    Caption { block: "PpStandfirst"; note: "B · with filter chips" }
                    PpStandfirst { width: parent.width; roles: ro; brush: br
                                 kicker: "The session · " + (root.readout ? root.readout.shotCount : 15) + " shots"
                                 head: ro.cap1(ro.word((root.cards || []).length)) + " patterns"
                                 standfirst: ro.sessionStandfirst(root.cards, root.readout, ro.shows("longStandfirst", width))
                                 chips: [{ key: "all", label: "All", n: (root.cards || []).length, on: root.sessionFilter === "all" },
                                         { key: "ball", label: "Reaches the ball", n: (root.cards || []).filter(c => c.reachesBall).length, on: root.sessionFilter === "ball" },
                                         { key: "roots", label: "Roots", n: (root.cards || []).filter(c => c.rootHere).length, on: root.sessionFilter === "roots" }]
                                 onChipActivated: k => root.sessionFilter = k }
                    Caption { block: "PpSparkHead + PpSparkRow"; note: "Replay: name · PpShotStrip · this shot · PpSparkline (from compact); one head, same geometry" }
                    PpSparkHead { width: parent.width; roles: ro; readout: root.readout; shots: root.readout ? root.readout.shotCount : 0 }
                    Repeater {
                        model: root.shownCards.slice(0, 4)
                        delegate: PpSparkRow {
                            required property var modelData
                            width: parent.width; roles: ro; brush: br; card: modelData
                            onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s)
                        }
                    }
                    Caption { block: "PpLedgerTable"; note: "A · kind session, tools, direction line · round 4: showFocus, showEvidence (medium up)" }
                    PpLedgerTable { width: parent.width; roles: ro; brush: br; kind: "session"; rows: root.shownCards; readout: root.readout; source: root.source
                                  showFocus: true; showEvidence: true
                                  onFocusActivated: (id, on) => { if (root.body) root.body._declareFocus(id, on) }
                                  title: "Pattern"
                                  tools: [{ t: "All", key: "all", on: root.sessionFilter === "all" }, { t: "Reaches the ball", key: "ball", on: root.sessionFilter === "ball" },
                                          { t: "Roots", key: "roots", on: root.sessionFilter === "roots" }]
                                  onToolActivated: k => root.sessionFilter = k
                                  onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s) }
                    Caption { block: "A 45-shot session"; note: "round 4 · D7: the real runs tripled — strips narrow then wrap, labels from the data, never clipped" }
                    PpSparkHead { width: parent.width; roles: ro; readout: root.longReadout }
                    Repeater {
                        model: root.longCards.slice(0, 3)
                        delegate: PpSparkRow { required property var modelData; width: parent.width; roles: ro; brush: br; card: modelData }
                    }
                    PpLedgerTable { width: parent.width; roles: ro; brush: br; kind: "session"; rows: root.longCards.slice(0, 3); readout: root.longReadout; source: root.source; title: "Pattern" }
                    PpSmallMultiples { width: parent.width; roles: ro; brush: br; cards: root.longCards.slice(0, 2); readout: root.longReadout; source: root.source }
                    Caption { block: "PpExpectations"; note: "round 4 · Cold: expectations to test, not findings (omitted when empty)" }
                    PpExpectations { width: parent.width; roles: ro; items: root.demoExpectations }
                    Caption { block: "PpSmallMultiples"; note: "C · tiles ≥ " + ro.flows.tile + " em" }
                    PpSmallMultiples { width: parent.width; roles: ro; brush: br; cards: root.shownCards; readout: root.readout; source: root.source
                                     onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s) }
                    Caption { block: "PpEntryFlow"; note: "B · kind session, marginal PpSparkline" }
                    PpEntryFlow { width: parent.width; roles: ro; brush: br; kind: "session"; items: root.shownCards; readout: root.readout
                                onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s)
                                onFocusActivated: id => { if (root.body) root.body._declareFocus(id, true) } }
                    Caption { block: "PpQuestions"; note: "Analyse only · for " + ((root.cards || [])[5] ? root.cards[5].name : "") }
                    PpQuestions { id: questions; width: parent.width; roles: ro; brush: br
                                questions: forCondition((root.cards || [])[5], root.readout, root.source)
                                onStepRequested: ids => { if (ids.length) root.pickShot("", ids[0]) } }
                }
            }
            Component {
                id: watchingC
                Column {
                    Caption { block: "PpStandfirst" }
                    PpStandfirst { width: parent.width; roles: ro; brush: br
                                 kicker: "Watching · " + root.watching.length + " conditions"; head: "Seen, not yet a pattern"
                                 standfirst: ro.watchStandfirst(root.source, root.readout, ro.shows("longStandfirst", width)) }
                    Caption { block: "PpBarcode"; note: "C · rows are ShotStrips" }
                    PpBarcode { width: parent.width; roles: ro; brush: br; rows: root.watching; readout: root.readout; source: root.source
                              onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s) }
                    Caption { block: "PpBarcode"; note: "round 4 · 45 shots (D7): the cell narrows and numbers thin, then the shots wrap into bands" }
                    PpBarcode { width: parent.width; roles: ro; brush: br; rows: root.watching.slice(0, 5); rowsAll: root.watching; readout: root.longReadout; source: root.source; showMarginNotes: false }
                    Caption { block: "PpTabs + PpInlinePicker"; note: "round 4 · the miss chip at the right of the tab row; its picker opens in place" }
                    PpTabs { width: parent.width; roles: ro; current: "watching"; chip: root.demoMiss ? "Miss: " + ro.cap1(root.demoMiss) : "Declare miss"; chipOn: root.pickerOpen
                           tabs: [{ key: "shot", label: "This shot" }, { key: "session", label: "Session" }, { key: "watching", label: "Watching" }]
                           onChipActivated: root.pickerOpen = !root.pickerOpen }
                    PpInlinePicker { width: parent.width; roles: ro; open: root.pickerOpen; current: root.demoMiss
                                   items: [{ id: "slice", label: "Slice", note: "curves right" }, { id: "hook", label: "Hook", note: "curves left" }, { id: "fat", label: "Fat", note: "ground before ball" }]
                                   onPicked: id => { root.demoMiss = id; root.pickerOpen = false } }
                    Caption { block: "PpLedgerTable"; note: "A · kind watching" }
                    PpLedgerTable { width: parent.width; roles: ro; brush: br; kind: "watching"; readout: root.readout
                                  rows: root.watching.slice().sort((a, b) => (ro.rate(b.recurrence) - ro.rate(a.recurrence)) || a.name.localeCompare(b.name))
                                  onConditionActivated: id => root.openCondition(id); onShotActivated: (d, s) => root.pickShot(d, s) }
                }
            }
            Component {
                id: detailC
                Column {
                    Caption { block: "PpFeatureHead"; note: "B" }
                    PpFeatureHead { width: parent.width; roles: ro; brush: br; detail: root.detail; readout: root.readout; source: root.source }
                    Caption { block: "PpKeyFigures"; note: "A" }
                    PpKeyFigures { width: parent.width; roles: ro; detail: root.detail; readout: root.readout }
                    Caption { block: "PpFigureRow"; note: "C · PpSpreadStrip | PpRunChart | PpHistoryStrip on one PpFigureScale" }
                    PpFigureRow { width: parent.width; roles: ro; brush: br; detail: root.detail; readout: root.readout
                                onShotActivated: (d, s) => root.pickShot(d, s) }
                    Caption { block: "PpQuestions"; note: "Analyse only" }
                    PpQuestions { id: dq; width: parent.width; roles: ro; brush: br
                                questions: forCondition(root.detailCard, root.readout, root.source) }
                    Caption { block: "PpPathChains"; note: "B · which both · round 4: the five grades (the real paths regraded in turn), Open in place" }
                    PpPathChains { width: parent.width; roles: ro; brush: br; detail: root.gradeDetail; readout: root.readout; which: "both"
                                 openPaths: root.openPaths
                                 onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpCauseGraph"; note: "round 4 · the five grades as strokes; arrowheads only on moved together / conditionally dependent" }
                    PpCauseGraph { width: parent.width; roles: ro; brush: br; detail: root.gradeDetail; which: "effects"
                                 onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpPageKey"; note: "set paths" }
                    PpPageKey { width: parent.width; roles: ro; set: "paths" }
                    Caption { block: "PpCauseGraph"; note: "C · causes (graph or outline)" }
                    PpCauseGraph { width: parent.width; roles: ro; brush: br; detail: root.detail; which: "causes"
                                 onConditionActivated: id => root.openCondition(id) }
                    Caption { block: "PpLedgerTable"; note: "A · kind path" }
                    PpLedgerTable { width: parent.width; roles: ro; brush: br; kind: "path"; rows: (root.detail && root.detail.effects) || []; detail: root.detail; readout: root.readout }
                }
            }
            // computes and applies a question for demoState (the visible PpQuestions blocks follow br.question)
            PpQuestions { id: questionsHidden; visible: false; roles: ro; brush: br }

            // ═══════════════════════════ PRINT ═══════════════════════════
            Loader {
                id: printLoader
                active: root.isPrint
                anchors.fill: parent
                sourceComponent: printC
            }
            Component {
                id: printC
            PpPager {
                id: pager
                roles: ro; brush: br
                readout: root.readout; source: root.source
                printPage: root.printPage
                footer: root.header ? ro.cap1(root.header.reviewNote) : ""
                PpMasthead { width: pager.textW; roles: ro; readout: root.readout; cards: root.cards; source: root.source }
                PpSectionHead { width: pager.textW; roles: ro; number: "1"; title: "Session patterns"; sub: root.header ? root.header.countLine : "" }
                PpLedgerTable { width: pager.textW; roles: ro; medium: "print"; kind: "session"; rows: root.cards || []; readout: root.readout; source: root.source
                              xrefId: root.detail ? root.detail.id : ""; xrefPage: root.isPrint ? pager.pageOf(detailHead) : 0 }
                PpSectionHead { width: pager.textW; roles: ro; number: "2"; title: "This shot"; sub: root.header ? root.header.shotLabel : "" }
                PpStandfirst { width: pager.textW; roles: ro; medium: "print"; standfirst: ro.shotStandfirst(root.readout, true) }
                PpDotPlot { width: pager.textW; roles: ro; medium: "print"; readout: root.readout }
                PpIndexList { width: pager.textW; roles: ro; medium: "print"; conditions: root.clean; grouped: true }
                PpSectionHead { width: pager.textW; roles: ro; number: "3"; title: "Watching"; sub: root.watching.length + " conditions seen, not yet a pattern" }
                PpBarcode { width: pager.textW; roles: ro; medium: "print"; rows: root.watching; readout: root.readout; source: root.source; showMarginNotes: false }
                PpSectionHead { id: detailHead; width: pager.textW; roles: ro; number: "4"; title: "The pattern in detail"; breakBefore: true }
                PpFeatureHead { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout; source: root.source }
                PpKeyFigures { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout }
                PpFigureRow { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout }
                PpQuestions { width: pager.textW; roles: ro; medium: "print"; questions: forCondition(root.detailCard, root.readout, root.source) }
                PpPathChains { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout; which: "both" }
                PpCauseGraph { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; which: "causes" }
            }
            }

            // ── the two shared objects, made once and passed down. Declared LAST so they are destroyed last:
            //    a block whose roles is torn down before it would re-evaluate against null. ──
            PpDiagRoles { id: ro; medium: root.medium }
            PpBrush {
                id: br
                cards: root.cards || []
                pickedShot: root.readout ? root.readout.shotId : -1
                pickedCondition: root.view === "detail" && root.detail ? (root.detail.id || "") : ""
            }
        }
    }
}
