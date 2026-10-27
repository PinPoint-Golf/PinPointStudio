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

// TYPESET FIGURES — the fault-forward figure blocks under test (PpFigureRow on one PpFigureScale;
// PpSmallMultiples, PpSparkHead + PpSparkRow, bare PpSparkline at three widths), each with a 45-shot copy
// of the real session (D7) and the not-measured stub (D5). Laid over the REAL Session diagnostics panel
// and fed the real data. Ported from the round-4 mock library's FigureGallery.qml and the render_variant3
// harness, onto the built types. It renders, it does not assert: judge the PNGs; errors appear in the log.
//
//   XDG_CONFIG_HOME=<scratch cfg> QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM="offscreen:configfile=<screen.json>" \
//     PinPointStudio --probe-qml <this file> --probe-session <SCRATCH copy of a session> --probe-ordinal 8 \
//     --probe-layout full|stage|half|third --probe-mode analyse --probe-themes 7,12 \
//     --probe-views detail,session --probe-detail over_the_top [--probe-states hoverShot12] \
//     --probe-out <dir> --probe-tag figures
//   (detail cases: over_the_top = high-side fault, early_extension = low-side, stance_wide = clean)
// Loading a session regrades and WRITES into it: always point --probe-session at a scratch copy.
// PNGs: <out>/<tag>_<layout>_<aesthetic>_<light|dark>_<view>[_<state>]_{window,panel}.png
// Silent: the app's ting players are muted before the session loads.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var themes: arg("--probe-themes", "7,12").split(",").map(Number)
    readonly property var views: arg("--probe-views", "detail,session").split(",")
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string tag: arg("--probe-tag", "figures")
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
        const others = ["camera", "swing3d", "launchMonitor", "charts", "table", "markup"]
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
            navController.navigate(AppScreen.Session)
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
        topPadding: Theme.sp(12); bottomPadding: Theme.sp(4); wrapMode: Text.WordWrap
        text: "▸ " + block + (note ? "  ·  " + note : "")
        font.family: Theme.fontBody; font.pixelSize: Theme.sp(8.5); color: Theme.colorText3
    }

    // ═══════════════ the gallery, made over the real body (it satisfies the register contract) ═══════════════
    Component {
        id: galleryComp
        Item {
            id: root
            property string view: "detail"
            property var readout: null
            property var cards: []
            property var source: null
            property var header: null
            property var detail: null
            property string medium: "screen"
            property string printOrientation: ""
            property int printPage: 0
            readonly property int printPageCount: 1
            property var body: null
            property string demoState: ""
            property string surface: "panel"

            Component.onCompleted: {
                if (!parent || medium === "print") return
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
            function pickShot(dir, id) { if (body && id > 0) body.shotRequested(dir || br.dirOf(id), id) }
            onDemoStateChanged: { br.clear(); if (demoState === "hoverShot12") br.hoveredShot = 12 }
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
            readonly property var longReadout: readout ? Object.assign({}, readout, { shotCount: readout.shotCount * longK }) : null
            readonly property var longDetail: detail && detail.header ? Object.assign({}, detail, { header: Object.assign({}, detail.header, { ticks: longTicks(detail.header.ticks), spread: longSpread(detail.header.spread) }) }) : null


            Flickable {
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: col.height + ro.u(24)
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: col
                    width: parent.width
                    spacing: ro.u(6)
                    // ── DETAIL ──
                    Text {
                        visible: root.view === "detail"
                        width: parent.width; wrapMode: Text.WordWrap
                        text: root.detail ? (root.detail.name || "") : ""
                        font: ro.fTitle; color: ro.ink
                    }
                    Caption {
                        visible: root.view === "detail"
                        block: "PpFigureRow"
                        note: root.detail && root.detail.header ? ((root.detail.header.recurrence || "") + " · " + (root.detail.header.valueText || "") + " · " + (root.detail.header.corridorText || "")) : ""
                    }
                    PpFigureRow {
                        visible: root.view === "detail"
                        width: parent.width
                        roles: ro; brush: br; detail: root.detail; readout: root.readout
                        onShotActivated: (d, i) => root.pickShot(d, i)
                    }
                    Caption {
                        visible: root.view === "detail"
                        block: "PpFigureRow · a 45-shot session"
                        note: "round 4 · D7: the run's pitch and numbers come from the shot count; stubs on the baseline (D5)"
                    }
                    PpFigureRow {
                        visible: root.view === "detail"
                        width: parent.width; showNotes: false
                        roles: ro; brush: br; detail: root.view === "detail" ? root.longDetail : null; readout: root.longReadout
                    }
                    // ── SESSION ──
                    Caption { visible: root.view === "session"; block: "PpSmallMultiples"; note: "fault-forward common scale; tiles ≥ " + ro.flows.tile + " em" }
                    PpSmallMultiples {
                        visible: root.view === "session"
                        width: parent.width
                        roles: ro; brush: br; cards: root.view === "session" ? (root.cards || []) : []; readout: root.readout; source: root.source
                        onShotActivated: (d, s) => root.pickShot(d, s)
                    }
                    Caption { visible: root.view === "session"; block: "PpSmallMultiples · 45 shots"; note: "round 4 · D7" }
                    PpSmallMultiples {
                        visible: root.view === "session"
                        width: parent.width
                        roles: ro; brush: br; cards: root.view === "session" ? root.longCards.slice(0, 4) : []; readout: root.longReadout; source: root.source
                    }
                    Caption { visible: root.view === "session"; block: "PpSparkHead + PpSparkRow"; note: "Replay rows under one head" }
                    PpSparkHead { visible: root.view === "session"; width: col.width; roles: ro; readout: root.readout }
                    Repeater {
                        model: root.view === "session" ? (root.cards || []) : []
                        delegate: PpSparkRow {
                            required property var modelData
                            width: col.width
                            roles: ro; brush: br; card: modelData
                            onShotActivated: (d, s) => root.pickShot(d, s)
                        }
                    }
                    Caption { visible: root.view === "session"; block: "PpSparkline"; note: "bare, at 7 em, 12 em and 20 em" }
                    Repeater {
                        model: root.view === "session" ? (root.cards || []).slice(0, 4) : []
                        delegate: Row {
                            required property var modelData
                            spacing: ro.u(24)
                            Repeater {
                                model: [7, 12, 20]
                                delegate: PpSparkline {
                                    required property var modelData
                                    width: modelData * ro.em
                                    roles: ro; brush: br; card: parent.modelData
                                }
                            }
                        }
                    }
                }
            }

            PpDiagRoles { id: ro; medium: root.medium }
            PpBrush { id: br; cards: root.cards; pickedShot: root.readout ? root.readout.shotId : -1; pickedCondition: root.detail ? (root.detail.id || "") : "" }
        }
    }
}
