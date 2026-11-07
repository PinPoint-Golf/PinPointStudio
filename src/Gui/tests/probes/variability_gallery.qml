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
import QtQuick.Shapes
import PinPointStudio

// THE VARIABILITY GALLERY (docs/design/variability-mark/spec.md §4, §5): every graded PpVariabilityMark
// (Dispersion, Overlay, Needle × Icon, Cell × grades 0–5) and PpVariabilityBadge, for looking at. Born
// as the S2.1 rendering spike (docs/design/variability-mark/spike/README.md); its clip and table modes
// stay. Needs no session: the gallery covers the app. Same contract as components_gallery.qml:
//   XDG_CONFIG_HOME=<scratch settings> [QT_QPA_PLATFORM='offscreen:configfile=<screen json>'] \
//       PinPointStudio --probe-qml <this file> --probe-mode grid|sheet|clip|table|funnel|typicality --probe-themes 0,1 --probe-out <dir>
//   --probe-mode      grid (default) · sheet · clip · table · funnel · typicality
//   --probe-data      funnel and typicality: the gallery fixture JSON (build/variability-run/gallery_real.json); read
//                     with an XMLHttpRequest GET, which needs QML_XHR_ALLOW_FILE_READ=1
//   --probe-themes    comma list of theme indices (grid, sheet: one PNG each; clip / table: the first)
//   --probe-renderer  table only: curve (default) | geometry. The mark itself is CurveRenderer only; this
//                     gallery overrides its delegates' Shapes (forceRenderer), so a GPU check (the Session 5
//                     Windows D3D11 re-check) can still compare the two. The clip mode always draws both.
//   --probe-out       <dir>
// Writes
//   grid   <out>/grid_<aesthetic>_<light|dark>_dpr<n>.png: every variant × form × grade at 12, 16, 20
//          and 24 px, then the badges (both forms, both layers, dense, measurement-limited), and print:
//          the medium alone, then with a PpTypeset `roles` (medium print, fixedAesthetic folio);
//          grabbed at the grid's own size, which can be larger than the window
//   sheet  <out>/sheet_<aesthetic>_<light|dark>_dpr<n>.png: every variant × form × grade with its word,
//          compact, grabbed at the sheet's own size (the 14-theme sheet, and its greyscale copy)
//   clip   <out>/clip_before_dpr<n>.png, <out>/clip_after_dpr<n>.png, <out>/clip_regions_dpr<n>.json
//          (the clip and band rects in IMAGE pixels, for a PIL count of pixels outside the clip)
//   table  <out>/table_<curve|geometry>.json (frame ms p50 / p95 / p99 / max, delegates, ms per delegate)
//   funnel <out>/funnel_<aesthetic>_<light|dark>_dpr<n>.png: PpVariabilityFunnel (§6.1), the fixture's
//          real funnels compact and wide on both layers, then synthetic cases (a break, too few after a
//          break, 14 sessions, every grade's tone, print); grabbed at the sheet's own size
//   typicality <out>/typicality_<aesthetic>_<light|dark>_dpr<n>.png: the typicality variants (§6.2), Mark's
//          real 7 Oct readings (three swings each, with their ghosts) and the fixture's trace envelopes,
//          then synthetic cases (|z| 0 … 3.5 and −3.5, showCorridor off and on, the Icon form, badges on
//          both layers, the TypicalityTrace Icon fallback, print with and without a PpTypeset roles)
// JSON is written with an XMLHttpRequest PUT, which needs QML_XHR_ALLOW_FILE_WRITE=1; every JSON is
// also logged whole on one "PROBE-JSON <name> {…}" line, so a run without that variable loses nothing.
//
// The basis strings on the technical-layer badges here are the §7.5 EXAMPLES, typed in for the probe.
// On a screen a basis string is always the grader's, never hand-written.
//
// ⚠ OFFSCREEN DRAWS WITH THE SOFTWARE SCENE GRAPH. There, every Shape is Shape.SoftwareRenderer
// (QPainter) whatever it asks for, so an offscreen run checks layout and words, never the
// CurveRenderer. Forcing RHI offscreen (QT_QUICK_BACKEND=rhi) builds a Metal QRhi but never presents
// a frame, so no grab ever returns. Every PNG and JSON names the renderer that actually drew it.
// The CurveRenderer answers need a windowed run (no QT_QPA_PLATFORM), which this file supports as is.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string mode: arg("--probe-mode", "grid")
    readonly property string rendererArg: arg("--probe-renderer", "curve")
    readonly property int tableRenderer: rendererArg === "geometry" ? Shape.GeometryRenderer : Shape.CurveRenderer
    readonly property var themes: arg("--probe-themes", "0,1").split(",").map(Number)
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property real dpr: Screen.devicePixelRatio
    readonly property string dprTag: "dpr" + String(dpr).replace(".", "_")
    property var jobs: []
    property int startTheme: 0
    property var held: null
    function log(s) { console.warn("PROBE: " + s) }

    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    function fileUrl(path) {
        const p = path.replace(/\\/g, "/")
        return (p.charAt(0) === "/" ? "file://" : "file:///") + p
    }
    // Asynchronous on purpose: a synchronous file PUT creates the file and writes nothing (Qt 6.11).
    function writeJson(name, obj, then) {
        const text = JSON.stringify(obj, null, 2)
        console.warn("PROBE-JSON " + name + " " + JSON.stringify(obj))
        try {
            const x = new XMLHttpRequest()
            x.onreadystatechange = function () {
                if (x.readyState !== XMLHttpRequest.DONE) return
                log("wrote " + outDir + "/" + name)
                then()
            }
            x.open("PUT", fileUrl(outDir + "/" + name))
            x.send(text)
        } catch (e) {
            log("could not write " + name + " (set QML_XHR_ALLOW_FILE_WRITE=1): " + e)
            then()
        }
    }
    function rendererName(t) {
        return t === Shape.CurveRenderer ? "CurveRenderer" : t === Shape.GeometryRenderer ? "GeometryRenderer"
             : t === Shape.SoftwareRenderer ? "SoftwareRenderer" : "UnknownRenderer"
    }
    // The first descendant of `item` named `name` (a badge holds its mark two levels down).
    function findNamed(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const f = findNamed(item.children[i], name)
            if (f) return f
        }
        return null
    }
    // GALLERY ONLY: ask every mark Shape under `item` for `type` (the spike's renderer comparison). Called from a
    // delegate's Component.onCompleted, before its first frame. The mark itself never offers this.
    function forceRenderer(item, type) {
        if (!item) return
        if (item.objectName === "variabilityShape") item.preferredRendererType = type
        for (let i = 0; i < item.children.length; ++i) forceRenderer(item.children[i], type)
    }
    // The renderer a mark's Shape actually got (the Shape under its Loader; a badge's, under its mark).
    function actualRenderer(m) {
        const shape = findNamed(m, "variabilityShape")
        return shape ? rendererName(shape.rendererType) : "none"
    }
    function graphicsApi() {
        const a = probe.GraphicsInfo.api
        return a === GraphicsInfo.Software ? "Software" : a === GraphicsInfo.Metal ? "Metal"
             : a === GraphicsInfo.Direct3D11 ? "Direct3D11" : a === GraphicsInfo.Direct3D12 ? "Direct3D12"
             : a === GraphicsInfo.Vulkan ? "Vulkan" : a === GraphicsInfo.OpenGL ? "OpenGL" : String(a)
    }
    function themeTag() { return Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") }

    // ── The whole-window grab (components_gallery.qml's, at device pixels) ─────────────────────────────────────
    property var bg: null
    property var frame: null
    function overlayRoot() { return Overlay.overlay.parent }
    function grabWindow(path, then) {
        if (probe.stay) { stayHere(); return }
        const root = overlayRoot()
        if (!probe.bg)
            probe.bg = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', root)
        if (!probe.frame)
            probe.frame = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { x: -100000; color: Theme.colorBg; property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true; hideSource: false } }', probe)
        probe.frame.width = root.width; probe.frame.height = root.height
        // In DEVICE pixels: a plain grab is logical-size, so a dpr 1.5 run would come out at 1×.
        probe.frame.src.textureSize = Qt.size(root.width * probe.dpr, root.height * probe.dpr)
        probe.frame.src.sourceItem = root
        probe.frame.src.scheduleUpdate()
        after(250, () => {
            probe.frame.grabToImage(function (img) {
                probe.held = img
                img.saveToFile(path); probe.log("saved " + path); then()
            }, Qt.size(root.width * probe.dpr, root.height * probe.dpr))
        })
    }
    // ── --probe-stay: look around instead of grabbing ───────────────────────────────────────────────────────
    // With --probe-stay the first grab does not save or quit: the mode's view is put in a scrollable pane over the
    // app and left open. T / Shift+T step through the 14 themes, Q quits. build/run-me/variability-look.sh.
    readonly property bool stay: Qt.application.arguments.indexOf("--probe-stay") >= 0
    property var stayPane: null
    function stayHere() {
        if (probe.stayPane || !probe.view) return
        const root = overlayRoot()
        Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { anchors.fill: parent; z: 99999; color: Theme.colorBg }', root)
        probe.stayPane = Qt.createQmlObject('import QtQuick; import QtQuick.Controls; Flickable { anchors.fill: parent; z: 100000; clip: true; '
            + 'boundsBehavior: Flickable.StopAtBounds; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn } '
            + 'ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded } }', root)
        const v = probe.view
        v.parent = probe.stayPane.contentItem
        v.x = 0; v.y = 0
        probe.stayPane.contentWidth = Qt.binding(() => v.width)
        probe.stayPane.contentHeight = Qt.binding(() => v.height)
        probe.log("staying open: scroll to look around · T next theme · Shift+T previous · Q quit")
    }
    Shortcut { enabled: probe.stay; sequence: "T"; onActivated: Theme.themeIndex = (Theme.themeIndex + 1) % Theme.themeCount }
    Shortcut { enabled: probe.stay; sequence: "Shift+T"; onActivated: Theme.themeIndex = (Theme.themeIndex + Theme.themeCount - 1) % Theme.themeCount }
    Shortcut { enabled: probe.stay; sequence: "Q"; onActivated: Qt.quit() }

    // One item at its own size, in device pixels (the sheet).
    function grabItem(item, path, then) {
        if (probe.stay) { stayHere(); return }
        after(250, () => {
            item.grabToImage(function (img) {
                probe.held = img
                img.saveToFile(path); probe.log("saved " + path); then()
            }, Qt.size(item.width * probe.dpr, item.height * probe.dpr))
        })
    }
    function finish() { if (probe.stay) return; Theme.themeIndex = startTheme; after(300, () => Qt.exit(0)) }

    property var view: null
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            probe.log("mode " + probe.mode + " · platform " + Qt.platform.pluginName + " · api " + probe.graphicsApi()
                      + " · dpr " + probe.dpr)
            if (probe.mode === "grid") probe.runGrid()
            else if (probe.mode === "sheet") probe.runSheet()
            else if (probe.mode === "clip") probe.runClip()
            else if (probe.mode === "table") probe.runTable()
            else if (probe.mode === "funnel") probe.runFunnel()
            else if (probe.mode === "typicality") probe.runTypicality()
            else { probe.log("unknown --probe-mode " + probe.mode); Qt.exit(2) }
        }
    }

    readonly property var sizes: [12, 16, 20, 24]
    readonly property var grades: [0, 1, 2, 3, 4, 5]
    readonly property var variants: [PpVariabilityMark.Dispersion, PpVariabilityMark.Overlay, PpVariabilityMark.Needle]
    function variantName(v) {
        return v === PpVariabilityMark.Dispersion ? "Dispersion" : v === PpVariabilityMark.Overlay ? "Overlay"
             : v === PpVariabilityMark.Needle ? "Needle" : v === PpVariabilityMark.Typicality ? "Typicality" : "TypicalityTrace"
    }
    // The §7.5 example basis strings, for the probe's technical-layer badges only. §7.5 has no
    // measurement row, and the grader gives the Needle an empty basis string, so its badge shows none.
    function sampleBasis(v) {
        return v === PpVariabilityMark.Overlay ? "σ peak 18 ms · n 24"
             : v === PpVariabilityMark.Needle ? ""
             : "σ 2.1° · 0.42 of corridor · n 24"
    }

    component Cap: PpMicro { color: Theme.colorText3 }

    // ── grid ─────────────────────────────────────────────────────────────────────────────────
    function runGrid() {
        probe.view = gridComp.createObject(overlayRoot())
        for (const t of themes) jobs.push(t)
        nextGrid()
    }
    function nextGrid() {
        if (jobs.length === 0) { finish(); return }
        Theme.themeIndex = jobs.shift()
        after(900, () => {
            probe.view.actual = actualRenderer(probe.view.sample)
            probe.log("accessible name, Dispersion grade 4: " + probe.view.sample.Accessible.name)
            // At the grid's own size, not the window's: it is taller and wider than a window at a large text size.
            after(100, () => grabItem(probe.view.sheet, outDir + "/grid_" + themeTag() + "_" + dprTag + ".png", nextGrid))
        })
    }

    // One variant × form block: a row per size, a column per grade.
    component MarkBlock: Column {
        id: block
        property int variant: PpVariabilityMark.Dispersion
        property int form: PpVariabilityMark.Icon
        property Item sample: null
        spacing: Theme.gap(6)
        Cap { text: probe.variantName(block.variant) + " · " + (block.form === PpVariabilityMark.Cell ? "Cell" : "Icon") }
        Grid {
            columns: 7
            columnSpacing: Theme.gap(block.form === PpVariabilityMark.Cell ? 10 : 12); rowSpacing: Theme.gap(6)
            verticalItemAlignment: Grid.AlignVCenter
            Cap { text: "px \\ grade" }
            Repeater { model: probe.grades; Cap { required property int modelData; text: String(modelData) } }
            Repeater {
                model: probe.sizes.length * 7
                Item {
                    id: slot
                    required property int index
                    readonly property int sz: probe.sizes[Math.floor(index / 7)]
                    readonly property int col: index % 7
                    width: col === 0 ? lbl.implicitWidth : mk.implicitWidth
                    height: Math.max(lbl.implicitHeight, mk.implicitHeight)
                    Cap { id: lbl; visible: slot.col === 0; text: slot.sz + " px"; anchors.verticalCenter: parent.verticalCenter }
                    PpVariabilityMark {
                        id: mk
                        visible: slot.col > 0
                        anchors.verticalCenter: parent.verticalCenter
                        variant: block.variant
                        form: block.form
                        grade: Math.max(0, slot.col - 1)
                        size: Theme.sp(slot.sz)
                        Component.onCompleted: if (slot.index === 19) block.sample = mk   // 20 px, grade 4
                    }
                }
            }
        }
    }
    // A labelled row of badges.
    component BadgeRow: Row {
        id: br
        property string caption: ""
        property int variant: PpVariabilityMark.Dispersion
        property int form: PpVariabilityMark.Icon
        property string contentLayer: "golfer"
        property bool dense: false
        spacing: Theme.gap(18)
        Cap { width: Theme.sp(250); text: br.caption; anchors.verticalCenter: parent.verticalCenter }
        Repeater {
            model: [5, 4, 3, 2, 1, 0]
            PpVariabilityBadge {
                required property int modelData
                anchors.verticalCenter: parent.verticalCenter
                variant: br.variant; form: br.form; contentLayer: br.contentLayer; dense: br.dense; grade: modelData
                basisText: br.contentLayer === "technical" && modelData > 0 ? probe.sampleBasis(br.variant) : ""
            }
        }
    }

    Component {
        id: gridComp
        Rectangle {
            id: grid
            property string actual: "?"
            readonly property Item sample: dispIcon.sample
            readonly property Item sheet: grid
            width: gridCol.implicitWidth + 2 * Theme.gap(24)
            height: gridCol.implicitHeight + 2 * Theme.gap(16)
            z: 100000
            color: Theme.colorSurface
            Column {
                id: gridCol
                x: Theme.gap(24); y: Theme.gap(16)
                spacing: Theme.gap(12)
                Cap {
                    text: "PpVariabilityMark + PpVariabilityBadge · " + Theme.aesthetic + " " + (Theme.dark ? "dark" : "light")
                          + " · dpr " + probe.dpr + " · " + Qt.platform.pluginName + " / " + probe.graphicsApi()
                          + " · requested CurveRenderer, drew with " + grid.actual
                }
                Row {
                    spacing: Theme.gap(36)
                    MarkBlock { id: dispIcon; variant: PpVariabilityMark.Dispersion }
                    MarkBlock { variant: PpVariabilityMark.Overlay }
                    MarkBlock { variant: PpVariabilityMark.Needle }
                }
                Row {
                    spacing: Theme.gap(36)
                    MarkBlock { variant: PpVariabilityMark.Dispersion; form: PpVariabilityMark.Cell }
                    MarkBlock { variant: PpVariabilityMark.Overlay; form: PpVariabilityMark.Cell }
                }
                Row {
                    spacing: Theme.gap(36)
                    MarkBlock { variant: PpVariabilityMark.Needle; form: PpVariabilityMark.Cell }
                    Column {   // print medium: the same marks, translucent fills × variabilityPrintAlphaScale
                        spacing: Theme.gap(6)
                        Cap { text: "Print (fills × " + Theme.variabilityPrintAlphaScale + ", clamped to 1) · screen · print · print with a PpTypeset roles (Folio, on its paper) · grade 3" }
                        Repeater {
                            model: ["screen", "print"]
                            Row {
                                id: medRow
                                required property string modelData
                                spacing: Theme.gap(12)
                                Cap { width: Theme.sp(44); text: medRow.modelData; anchors.verticalCenter: parent.verticalCenter }
                                Repeater {
                                    model: probe.variants
                                    Row {
                                        id: medPair
                                        required property int modelData
                                        spacing: Theme.gap(8)
                                        PpVariabilityMark { variant: medPair.modelData; grade: 3; size: Theme.sp(24); medium: medRow.modelData
                                                            anchors.verticalCenter: parent.verticalCenter }
                                        PpVariabilityBadge { variant: medPair.modelData; form: PpVariabilityMark.Cell; grade: 3
                                                             medium: medRow.modelData; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                }
                            }
                        }
                        // On paper with a page's roles: its inks, sizes and type (§9.5.4), on its paper.
                        Rectangle {
                            width: rolesCol.implicitWidth + 2 * Theme.gap(8)
                            height: rolesCol.implicitHeight + 2 * Theme.gap(6)
                            color: printTs.paper
                            Column {
                                id: rolesCol
                                x: Theme.gap(8); y: Theme.gap(6)
                                spacing: Theme.gap(6)
                                Row {
                                    spacing: Theme.gap(12)
                                    Cap { width: Theme.sp(44); text: "roles"; anchors.verticalCenter: parent.verticalCenter }
                                    Repeater {
                                        model: probe.variants
                                        Row {
                                            id: rolesPair
                                            required property int modelData
                                            spacing: Theme.gap(8)
                                            PpVariabilityMark { variant: rolesPair.modelData; grade: 3; roles: printTs
                                                                anchors.verticalCenter: parent.verticalCenter }
                                            PpVariabilityBadge { variant: rolesPair.modelData; form: PpVariabilityMark.Cell; grade: 3
                                                                 roles: printTs; anchors.verticalCenter: parent.verticalCenter }
                                        }
                                    }
                                }
                                Repeater {
                                    model: [PpVariabilityMark.Icon, PpVariabilityMark.Cell]
                                    Row {
                                        id: rolesGrades
                                        required property int modelData
                                        spacing: Theme.gap(12)
                                        Repeater {
                                            model: [5, 4, 3, 2, 1, 0]
                                            PpVariabilityBadge {
                                                required property int modelData
                                                anchors.verticalCenter: parent.verticalCenter
                                                form: rolesGrades.modelData; grade: modelData; roles: printTs
                                                contentLayer: rolesGrades.modelData === PpVariabilityMark.Cell ? "technical" : "golfer"
                                                basisText: modelData > 0 ? "n 24" : ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Cap { text: "Badges · grades 5 → 0" }
                BadgeRow { caption: "Dispersion · Icon · golfer" }
                BadgeRow { caption: "Dispersion · Icon · golfer · dense"; dense: true }
                BadgeRow { caption: "Needle · Icon · golfer"; variant: PpVariabilityMark.Needle }
                BadgeRow { caption: "Overlay · Cell · golfer"; variant: PpVariabilityMark.Overlay; form: PpVariabilityMark.Cell }
                Row {
                    spacing: Theme.gap(36)
                    Column {
                        spacing: Theme.gap(8)
                        Cap { text: "Technical layer (basis strings are the §7.5 examples)" }
                        PpVariabilityBadge { grade: 4; contentLayer: "technical"; basisText: probe.sampleBasis(PpVariabilityMark.Dispersion) }
                        PpVariabilityBadge { grade: 2; contentLayer: "technical"; form: PpVariabilityMark.Cell; variant: PpVariabilityMark.Overlay
                                             basisText: probe.sampleBasis(PpVariabilityMark.Overlay) }
                        PpVariabilityBadge { grade: 5; contentLayer: "technical"; form: PpVariabilityMark.Cell; variant: PpVariabilityMark.Needle
                                             basisText: probe.sampleBasis(PpVariabilityMark.Needle) }
                    }
                    Column {
                        spacing: Theme.gap(8)
                        Cap { text: "Measurement-limited (§8.6), grade 4 · golfer, then technical" }
                        PpVariabilityBadge { grade: 4; measurementLimited: true }
                        PpVariabilityBadge { grade: 4; measurementLimited: true; contentLayer: "technical"
                                             basisText: "σ 2.1° · 0.42 of corridor · n 24 · measurement-limited" }
                        PpVariabilityBadge { grade: 4; measurementLimited: true; form: PpVariabilityMark.Cell }
                        PpVariabilityBadge { grade: 4; measurementLimited: true; form: PpVariabilityMark.Cell; contentLayer: "technical"
                                             basisText: "σ 2.1° · 0.42 of corridor · n 24 · measurement-limited" }
                    }
                }
            }
            // Last in the item (children are destroyed in creation order); never id `roles`.
            PpTypeset { id: printTs; medium: "print"; fixedAesthetic: "folio" }
        }
    }

    // ── sheet ────────────────────────────────────────────────────────────────────────────────
    // Every graded variant × form × grade at its default size, each over its word, on the surface; one
    // PNG per theme at the sheet's own size, for the 14-theme sheet and its greyscale copy (§9.5.4:
    // shape and word must carry the grade without colour).
    function runSheet() {
        probe.view = sheetComp.createObject(overlayRoot())
        for (const t of themes) jobs.push(t)
        nextSheet()
    }
    function nextSheet() {
        if (jobs.length === 0) { finish(); return }
        Theme.themeIndex = jobs.shift()
        after(900, () => {
            probe.view.actual = actualRenderer(probe.view.sample)
            after(100, () => grabItem(probe.view.sheet, outDir + "/sheet_" + themeTag() + "_" + dprTag + ".png", nextSheet))
        })
    }
    Component {
        id: sheetComp
        Item {
            id: sheetView
            property string actual: "?"
            property Item sample: null
            property alias sheet: sheetRect
            anchors.fill: parent
            z: 100000
            Rectangle {
                id: sheetRect
                width: sheetCol.implicitWidth + 2 * Theme.gap(16)
                height: sheetCol.implicitHeight + 2 * Theme.gap(14)
                color: Theme.colorSurface
                Column {
                    id: sheetCol
                    x: Theme.gap(16); y: Theme.gap(14)
                    spacing: Theme.gap(10)
                    Cap { text: Theme.aesthetic + " " + (Theme.dark ? "dark" : "light") + " · dpr " + probe.dpr
                                + " · drew with " + sheetView.actual }
                    Repeater {
                        model: 6   // Dispersion, Overlay, Needle × Icon, Cell
                        Row {
                            id: sheetRow
                            required property int index
                            readonly property int variant: probe.variants[Math.floor(index / 2)]
                            readonly property int form: index % 2 === 0 ? PpVariabilityMark.Icon : PpVariabilityMark.Cell
                            spacing: Theme.gap(14)
                            Cap { width: Theme.sp(96); anchors.verticalCenter: parent.verticalCenter
                                  text: probe.variantName(sheetRow.variant) + " · " + (sheetRow.form === PpVariabilityMark.Cell ? "Cell" : "Icon") }
                            Repeater {
                                model: [5, 4, 3, 2, 1, 0]
                                Column {
                                    id: sheetCell
                                    required property int modelData
                                    spacing: Theme.gap(4)
                                    width: Theme.sp(sheetRow.form === PpVariabilityMark.Cell ? 112 : 92)
                                    PpVariabilityMark {
                                        id: sm
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        variant: sheetRow.variant; form: sheetRow.form; grade: sheetCell.modelData
                                        size: sheetRow.form === PpVariabilityMark.Cell ? Theme.sp(Theme.variabilityCellH)
                                                                                     : Theme.sp(Theme.variabilityIconSize)
                                        Component.onCompleted: if (sheetRow.index === 0 && sheetCell.modelData === 4) sheetView.sample = sm
                                    }
                                    PpCaption {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: sm.label
                                        color: sheetCell.modelData === 0 ? Theme.colorText3 : sm.tone
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── clip ─────────────────────────────────────────────────────────────────────────────────
    // Three clipped Flickables: marks under CurveRenderer, the same marks forced to GeometryRenderer
    // (forceRenderer), and a CONTROL of plain Rectangles (no Shape) that tells a Shape-specific leak from
    // a grab that ignores clips. Each is ringed by a solid band
    // DIRECTLY outside its clip rect and drawn UNDER it, so a mark that escapes the clip lands on the
    // band. Grabbed at rest, then scrolled by half the viewport's height (and a quarter of its width)
    // and grabbed again. The JSON gives every rect in image pixels.
    readonly property int clipW: 360
    readonly property int clipH: 240
    readonly property int bandT: 24
    function runClip() {
        if (themes.length) Theme.themeIndex = themes[0]
        probe.view = clipComp.createObject(overlayRoot())
        after(900, () => {
            grabWindow(outDir + "/clip_before_" + dprTag + ".png", () => {
                for (const f of probe.view.flicks) {
                    f.contentY = probe.clipH / 2
                    f.contentX = probe.clipW / 4
                }
                after(600, () => {
                    const root = overlayRoot(), panes = []
                    for (let i = 0; i < probe.view.flicks.length; ++i) {
                        const f = probe.view.flicks[i], p = f.mapToItem(root, 0, 0), s = probe.dpr
                        const c = [p.x * s, p.y * s, f.width * s, f.height * s], b = probe.bandT * s
                        panes.push({
                            requested: i === 0 ? "CurveRenderer" : i === 1 ? "GeometryRenderer" : "control: Rectangle",
                            actual: i < 2 ? actualRenderer(probe.view.sample[i]) : "n/a",
                            contentX: f.contentX, contentY: f.contentY,
                            clip: c,
                            bands: { top: [c[0] - b, c[1] - b, c[2] + 2 * b, b], bottom: [c[0] - b, c[1] + c[3], c[2] + 2 * b, b],
                                     left: [c[0] - b, c[1], b, c[3]], right: [c[0] + c[2], c[1], b, c[3]] }
                        })
                    }
                    grabWindow(outDir + "/clip_after_" + dprTag + ".png", () => {
                        writeJson("clip_regions_" + dprTag + ".json", {
                            theme: themeTag(), dpr: probe.dpr,
                            platform: Qt.platform.pluginName, api: probe.graphicsApi(),
                            image: outDir + "/clip_after_" + dprTag + ".png",
                            imageBefore: outDir + "/clip_before_" + dprTag + ".png",
                            bandColor: String(Theme.colorText),
                            tones: [String(Theme.colorGood), String(Theme.colorAttention), String(Theme.colorWarn)],
                            panes: panes
                        }, finish)
                    })
                })
            })
        })
    }
    Component {
        id: clipComp
        Rectangle {
            id: clipView
            property var flicks: [flickL, flickR, flickC]
            property var sample: [sampleL, sampleR]
            property Item sampleL: null
            property Item sampleR: null
            anchors.fill: parent
            z: 100000
            color: Theme.colorBg
            Cap { x: Theme.gap(24); y: Theme.gap(20)
                  text: "Clip: each viewport (" + probe.clipW + " × " + probe.clipH + ") is ringed by a solid colorText band under it; "
                        + "a mark on the band escaped the clip · " + Qt.platform.pluginName + " / " + probe.graphicsApi() }
            Repeater {
                model: 3
                Item {
                    id: pane
                    required property int index
                    // Even logical coordinates, so the rects stay on whole pixels at dpr 1.5.
                    x: 80 + index * (probe.clipW + 160); y: 100
                    width: probe.clipW; height: probe.clipH
                    Cap { y: -probe.bandT - Theme.gap(18)
                          text: pane.index === 0 ? "PpVariabilityMark (CurveRenderer requested)" : pane.index === 1
                                ? "GeometryRenderer + antialiasing (gallery override)" : "Control: plain Rectangles, no Shape" }
                    Rectangle {   // the band, directly outside the clip rect
                        x: -probe.bandT; y: -probe.bandT
                        width: parent.width + 2 * probe.bandT; height: parent.height + 2 * probe.bandT
                        color: Theme.colorText
                    }
                    Rectangle { anchors.fill: parent; color: Theme.colorSurface }   // the viewport's own ground
                    Flickable {
                        id: fl
                        anchors.fill: parent
                        clip: true
                        interactive: false
                        contentWidth: probe.clipW * 1.5
                        contentHeight: probe.clipH * 3
                        Grid {
                            columns: 6
                            spacing: 2
                            Repeater {
                                model: 6 * 14
                                Item {
                                    id: slot
                                    required property int index
                                    readonly property bool cell: Math.floor(index / 6) % 2 === 1
                                    width: 88; height: 26
                                    Rectangle {   // the control pane's stand-in for a mark
                                        visible: pane.index === 2
                                        width: slot.cell ? 80 : 24; height: 24; radius: 12
                                        color: Qt.alpha(Theme.colorWarn, 0.85)
                                    }
                                    PpVariabilityMark {
                                        id: cm
                                        visible: pane.index < 2
                                        variant: probe.variants[slot.index % 3]
                                        form: slot.cell ? PpVariabilityMark.Cell : PpVariabilityMark.Icon
                                        grade: 1 + slot.index % 5
                                        size: 24
                                        Component.onCompleted: {
                                            if (pane.index === 1) probe.forceRenderer(cm, Shape.GeometryRenderer)
                                            if (slot.index === 1) { if (pane.index === 0) clipView.sampleL = cm; else if (pane.index === 1) clipView.sampleR = cm }
                                        }
                                    }
                                }
                            }
                        }
                        Component.onCompleted: if (pane.index === 0) clipView.flickL = fl
                                               else if (pane.index === 1) clipView.flickR = fl
                                               else clipView.flickC = fl
                    }
                }
            }
            // Filled by the panes above (a Repeater's children have no id in this scope).
            property Item flickL: null
            property Item flickR: null
            property Item flickC: null
        }
    }

    // ── table ────────────────────────────────────────────────────────────────────────────────
    // A TableView of 500 rows (reuseItems), each a text cell and two Cell badges (mark and word),
    // auto-scrolled top → bottom → top over 10 s by setting contentY every frame. Records every frame
    // interval (FrameAnimation.frameTime, and frameSwapped wall-clock deltas), the delegates created
    // and reused, then times delegate creation on its own (200 rows created synchronously).
    readonly property int tableRows: 500
    readonly property real scrollSeconds: 10
    property int created: 0
    property int reused: 0
    property var frameMs: []
    property var swapMs: []
    property real lastSwap: 0
    property bool recording: false
    property var createdPerFrame: []
    Connections {
        target: probe.Window.window
        enabled: probe.recording
        function onFrameSwapped() {
            const now = Date.now()
            if (probe.lastSwap > 0) probe.swapMs.push(now - probe.lastSwap)
            probe.lastSwap = now
        }
    }
    function pct(arr, p) {
        if (!arr.length) return null
        const s = arr.slice().sort((a, b) => a - b)
        return s[Math.min(s.length - 1, Math.max(0, Math.ceil(p / 100 * s.length) - 1))]
    }
    function stats(arr) {
        if (!arr.length) return { n: 0 }
        let sum = 0; for (const v of arr) sum += v
        return { n: arr.length, mean: sum / arr.length, p50: pct(arr, 50), p95: pct(arr, 95), p99: pct(arr, 99),
                 max: Math.max.apply(null, arr) }
    }
    function runTable() {
        if (themes.length) Theme.themeIndex = themes[0]
        probe.view = tableComp.createObject(overlayRoot())
        after(1500, () => {
            probe.created = 0; probe.reused = 0
            probe.frameMs = []; probe.swapMs = []; probe.createdPerFrame = []; probe.lastSwap = 0
            probe.recording = true
            probe.view.start()
        })
    }
    function tableDone() {
        probe.recording = false
        const tv = probe.view.table
        const createdInScroll = probe.created, reusedInScroll = probe.reused
        // Creation on its own: 200 row delegates, synchronously, timed together.
        const N = 200, holder = probe.view.bench
        const t0 = Date.now(), made = []
        for (let i = 0; i < N; ++i) made.push(probe.view.table.delegate.createObject(holder, { row: i }))
        const t1 = Date.now()
        for (const o of made) o.destroy()
        // Frames that loaded rows against frames that did not.
        const withNew = [], without = []
        for (let i = 0; i < probe.frameMs.length; ++i)
            (probe.createdPerFrame[i] > 0 ? withNew : without).push(probe.frameMs[i])
        const fs = stats(probe.frameMs)
        writeJson("table_" + probe.rendererArg + ".json", {
            label: "OFFSCREEN-unless-platform-says-otherwise: frame times here are not GPU-representative when platform is offscreen",
            platform: Qt.platform.pluginName, api: probe.graphicsApi(), dpr: probe.dpr,
            theme: themeTag(),
            requestedRenderer: probe.rendererName(probe.tableRenderer),
            actualRenderer: probe.view.sampleRenderer(),
            rows: probe.tableRows, rowHeight: Theme.itemRowH, viewportHeight: tv.height, scrollSeconds: probe.scrollSeconds,
            frameMs: { p50: fs.p50, p95: fs.p95, p99: fs.p99, max: fs.max, mean: fs.mean, frames: fs.n },
            frameMsSource: "FrameAnimation.frameTime × 1000",
            frameMsWhileLoadingRows: stats(withNew), frameMsOtherwise: stats(without),
            swapIntervalMs: stats(probe.swapMs),
            swapIntervalSource: "frameSwapped, Date.now() deltas (1 ms resolution)",
            delegatesCreatedDuringScroll: createdInScroll, delegatesReusedDuringScroll: reusedInScroll,
            delegateCreateMsMean: (t1 - t0) / N, delegateCreateBench: { rows: N, totalMs: t1 - t0,
                note: "row delegate (text + 2 Cell badges) created synchronously; excludes polish (Shape path processing) and render" }
        }, finish)
    }

    Component {
        id: tableComp
        Rectangle {
            id: tableView
            property alias table: tv
            property alias bench: benchHolder
            anchors.fill: parent
            z: 100000
            color: Theme.colorSurface
            function start() { scroller.t0 = -1; scroller.running = true }
            function sampleRenderer() {
                for (const c of tv.contentItem.children)
                    if (c.markA) return probe.actualRenderer(c.markA)
                return "none"
            }
            Cap { x: Theme.gap(24); y: Theme.gap(12)
                  text: "Table: " + probe.tableRows + " rows, reuseItems · PpVariabilityBadge Cell × 2 per row · requested "
                        + probe.rendererName(probe.tableRenderer) + " · "
                        + Qt.platform.pluginName + " / " + probe.graphicsApi() }
            TableView {
                id: tv
                x: Theme.gap(24); y: Theme.gap(40)
                width: Theme.sp(560); height: parent.height - y - Theme.gap(24)
                clip: true
                reuseItems: true
                interactive: false
                // A ListModel, not `model: 500`: with an integer (or JS array) model the rows' context
                // object (QQmlDMListAccessorData) answered unqualified names, so `probe`, `Math` and
                // `qsTr` came out undefined inside the rows AND inside PpVariabilityMark (Qt 6.11.1).
                model: ListModel {
                    Component.onCompleted: { for (let i = 0; i < probe.tableRows; ++i) append({ n: i }) }
                }
                columnWidthProvider: function (c) { return width }
                rowHeightProvider: function (r) { return Theme.itemRowH }
                // The row: a text cell and two Cell badges. Declared here, where `probe` resolves (from a
                // Component declared on the probe root, it came out undefined in the rows).
                delegate: Item {
                    id: rowItem
                    required property int row
                    readonly property int gradeA: (row * 7 + 2) % 6
                    readonly property int gradeB: (row * 5 + 4) % 6
                    implicitWidth: Theme.sp(560)
                    implicitHeight: Theme.itemRowH
                    Component.onCompleted: {
                        probe.created++
                        if (probe.tableRenderer !== Shape.CurveRenderer) probe.forceRenderer(rowItem, probe.tableRenderer)
                    }
                    TableView.onReused: probe.reused++
                    Row {
                        x: Theme.gap(12)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.gap(16)
                        PpCaption { text: "Swing " + (rowItem.row + 1); color: Theme.colorText; width: Theme.sp(90)
                                    anchors.verticalCenter: parent.verticalCenter }
                        PpVariabilityBadge { id: ba; variant: PpVariabilityMark.Dispersion; form: PpVariabilityMark.Cell
                                             grade: rowItem.gradeA; width: Theme.sp(210)
                                             anchors.verticalCenter: parent.verticalCenter }
                        PpVariabilityBadge { variant: PpVariabilityMark.Overlay; form: PpVariabilityMark.Cell
                                             grade: rowItem.gradeB; anchors.verticalCenter: parent.verticalCenter }
                    }
                    PpDivider { anchors.bottom: parent.bottom; width: parent.width }
                    property alias markA: ba
                }
            }
            Item { id: benchHolder; x: -100000; width: Theme.sp(560); height: Theme.itemRowH }
            FrameAnimation {
                id: scroller
                property real t0: -1
                property int lastCreated: 0
                running: false
                onTriggered: {
                    if (t0 < 0) { t0 = elapsedTime; lastCreated = probe.created; return }
                    probe.frameMs.push(frameTime * 1000)
                    probe.createdPerFrame.push(probe.created - lastCreated)
                    lastCreated = probe.created
                    const u = (elapsedTime - t0) / probe.scrollSeconds
                    if (u >= 1) { running = false; tv.contentY = 0; probe.tableDone(); return }
                    const maxY = Math.max(0, tv.contentHeight - tv.height)
                    tv.contentY = maxY * (u < 0.5 ? 2 * u : 2 - 2 * u)
                }
            }
        }
    }
    // ── funnel ───────────────────────────────────────────────────────────────────────────────
    // PpVariabilityFunnel (§6.1). The fixture's funnels are Mark's 7-iron (gallery_real.json: ρ per
    // session, its grade and the python trend word). The synthetic rows are typed in below.
    readonly property string dataPath: arg("--probe-data", "")
    property var fixture: null
    function runFunnel() {
        if (probe.dataPath === "") { probe.log("funnel needs --probe-data <gallery_real.json>"); Qt.exit(2); return }
        const x = new XMLHttpRequest()
        x.onreadystatechange = function () {
            if (x.readyState !== XMLHttpRequest.DONE) return
            try {
                probe.fixture = JSON.parse(x.responseText)
            } catch (e) {
                probe.log("could not read " + probe.dataPath + " (set QML_XHR_ALLOW_FILE_READ=1): " + e)
                Qt.exit(2); return
            }
            probe.log("fixture: " + probe.fixture.funnels.length + " funnels, " + probe.fixture.athlete + " " + probe.fixture.club)
            probe.view = funnelComp.createObject(overlayRoot())
            for (const t of themes) jobs.push(t)
            nextFunnel()
        }
        try {
            x.open("GET", fileUrl(probe.dataPath))
            x.send()
        } catch (e) {
            probe.log("could not read " + probe.dataPath + " (set QML_XHR_ALLOW_FILE_READ=1): " + e)
            Qt.exit(2)
        }
    }
    function nextFunnel() {
        if (jobs.length === 0) { finish(); return }
        Theme.themeIndex = jobs.shift()
        after(900, () => {
            const sh = findNamed(probe.view.sample, "funnelShape")
            probe.view.actual = sh ? rendererName(sh.rendererType) : "none"
            probe.log("funnel accessible name: " + probe.view.sample.Accessible.name)
            after(100, () => grabItem(probe.view.sheet, outDir + "/funnel_" + themeTag() + "_" + dprTag + ".png", nextFunnel))
        })
    }
    // TEST DATA, for display only: the fixture's trend basis, spelled here as the grader's
    // TrendResult::basisString spells it ("ρ 0.62 → 0.38 over 6 sessions"). On a screen the basis is
    // always the grader's string; QML never formats ρ.
    function fixtureBasis(t) {
        return t && t.word !== "insufficient"
             ? "ρ " + Number(t.rhoFirst).toFixed(2) + " → " + Number(t.rhoLast).toFixed(2) + " over " + t.sessions + " sessions"
             : ""
    }
    // The sessions a trend is taken over (test data only): the latest unbroken run, its latest 12.
    function trendRun(s) {
        let start = s.length - 1
        while (start > 0 && !s[start].breakBefore) --start
        return s.slice(Math.max(start, s.length - 12))
    }
    // Synthetic sessions: grades oldest first, ρ walking from `from` to `to`, a break before index `brk`.
    function synth(grades, from, to, brk) {
        const out = [], n = grades.length
        for (let i = 0; i < n; ++i)
            out.push({ rho: n > 1 ? from + (to - from) * i / (n - 1) : from, grade: grades[i], breakBefore: i === brk })
        return out
    }
    readonly property var synthetic: [
        { caption: "A break mid-series (3 + 4)", trend: "tightening", s: synth([2, 2, 3, 3, 4, 4, 4], 1.1, 0.4, 3) },
        { caption: "2 after a break: trendAfter3", trend: "insufficient", s: synth([3, 3, 3, 3, 4], 0.9, 0.5, 3) },
        { caption: "14 sessions: 12 drawn", trend: "tightening", s: synth([1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5], 1.4, 0.2, -1) },
        { caption: "1 session", trend: "insufficient", s: synth([4], 0.4, 0.4, -1) },
        { caption: "Grade 5 Grooved", trend: "tightening", s: synth([4, 4, 5, 5], 0.4, 0.15, -1) },
        { caption: "Grade 4 Repeatable", trend: "holding", s: synth([4, 4, 4, 4], 0.45, 0.42, -1) },
        { caption: "Grade 3 Settling", trend: "holding", s: synth([3, 3, 3, 3], 0.7, 0.68, -1) },
        { caption: "Grade 2 Variable", trend: "loosening", s: synth([3, 3, 2, 2], 0.7, 1.0, -1) },
        { caption: "Grade 1 Scattered", trend: "loosening", s: synth([2, 1, 1, 1], 1.0, 1.4, -1) },
        { caption: "Grade 0 Not enough swings (synthetic: real data would skip a session with no ρ)", trend: "holding", s: synth([4, 4, 4, 0], 0.5, 0.5, -1) }
    ]
    // One case: compact golfer, compact technical, wide golfer, wide technical.
    component FunnelRow: Row {
        id: fr
        property string caption: ""
        property var sessions: []
        property string trend: ""
        property string basis: ""
        property string measure: ""
        property string medium: "screen"
        property var roles: null   // a PpTypeset: its medium wins over `medium`
        property Item first: compactG
        spacing: Theme.gap(24)
        Cap { width: Theme.sp(200); text: fr.caption; anchors.verticalCenter: parent.verticalCenter; wrapMode: Text.WordWrap }
        PpVariabilityFunnel { id: compactG; width: Theme.sp(330); sessions: fr.sessions; trend: fr.trend; measureName: fr.measure
                              roles: fr.roles; medium: fr.roles ? fr.roles.medium : fr.medium
                              anchors.verticalCenter: parent.verticalCenter }
        PpVariabilityFunnel { width: Theme.sp(560); sessions: fr.sessions; trend: fr.trend; measureName: fr.measure
                              roles: fr.roles; medium: fr.roles ? fr.roles.medium : fr.medium
                              contentLayer: "technical"; basisText: fr.basis; anchors.verticalCenter: parent.verticalCenter }
        PpVariabilityFunnel { wide: true; width: Theme.sp(340); sessions: fr.sessions; trend: fr.trend; measureName: fr.measure
                              roles: fr.roles; medium: fr.roles ? fr.roles.medium : fr.medium
                              anchors.verticalCenter: parent.verticalCenter }
        PpVariabilityFunnel { wide: true; width: Theme.sp(480); sessions: fr.sessions; trend: fr.trend; measureName: fr.measure
                              roles: fr.roles; medium: fr.roles ? fr.roles.medium : fr.medium
                              contentLayer: "technical"; basisText: fr.basis
                              anchors.verticalCenter: parent.verticalCenter }
    }
    Component {
        id: funnelComp
        Rectangle {
            id: fsheet
            property string actual: "?"
            property Item sample: null
            readonly property Item sheet: fsheet
            width: fcol.implicitWidth + 2 * Theme.gap(24)
            height: fcol.implicitHeight + 2 * Theme.gap(16)
            z: 100000
            color: Theme.colorSurface
            Column {
                id: fcol
                x: Theme.gap(24); y: Theme.gap(16)
                spacing: Theme.gap(14)
                Cap {
                    text: "PpVariabilityFunnel · " + Theme.aesthetic + " " + (Theme.dark ? "dark" : "light") + " · dpr " + probe.dpr
                          + " · " + Qt.platform.pluginName + " / " + probe.graphicsApi() + " · requested CurveRenderer, drew with " + fsheet.actual
                }
                Cap { text: "Columns: compact golfer · compact technical · wide golfer · wide technical" }
                Cap { text: "Real: " + (probe.fixture ? probe.fixture.athlete + " · " + probe.fixture.club : "") + " (gallery_real.json; trend word from the fixture's python trend)" }
                Repeater {
                    model: probe.fixture ? probe.fixture.funnels : []
                    FunnelRow {
                        required property var modelData
                        required property int index
                        caption: modelData.label + " " + modelData.phase + " · trend " + modelData.trend.word
                        sessions: modelData.sessions
                        trend: modelData.trend.word
                        basis: probe.fixtureBasis(modelData.trend)
                        measure: modelData.label
                        Component.onCompleted: if (index === 0) fsheet.sample = first
                    }
                }
                Cap { text: "Synthetic (typed in for the probe)" }
                Repeater {
                    model: probe.synthetic
                    FunnelRow {
                        required property var modelData
                        caption: modelData.caption
                        sessions: modelData.s
                        trend: modelData.trend
                        // Test data: the basis spelled from the synthetic run as the grader takes it: the
                        // latest unbroken run, capped at its latest 12 sessions (§8.8, the §6.1 cap).
                        readonly property var trendRun: probe.trendRun(modelData.s)
                        basis: modelData.trend === "insufficient" ? ""
                             : probe.fixtureBasis({ word: modelData.trend, rhoFirst: trendRun[0].rho,
                                                    rhoLast: trendRun[trendRun.length - 1].rho, sessions: trendRun.length })
                        measure: "Pelvis rotation at top"
                    }
                }
                Cap { text: "Print medium: band fill × " + Theme.variabilityPrintAlphaScale + " (clamped to 1), strokes unchanged · top screen, bottom print" }
                FunnelRow { caption: "screen · grade 3"; sessions: probe.synth([2, 3, 3, 3], 0.9, 0.65, -1); trend: "holding"
                            basis: "ρ 0.90 → 0.65 over 4 sessions"; measure: "Stance width" }
                FunnelRow { caption: "print · grade 3"; medium: "print"; sessions: probe.synth([2, 3, 3, 3], 0.9, 0.65, -1); trend: "holding"
                            basis: "ρ 0.90 → 0.65 over 4 sessions"; measure: "Stance width" }
                // On paper with a page's roles (§9.5.4): its sizes, spaces, type and inks, on its paper.
                Cap { text: "Print with a PpTypeset roles (Folio, on its paper): grade 3 with a break, grade 4, too few after a break" }
                Rectangle {
                    width: fRolesCol.implicitWidth + 2 * Theme.gap(8)
                    height: fRolesCol.implicitHeight + 2 * Theme.gap(6)
                    color: funnelTs.paper
                    Column {
                        id: fRolesCol
                        x: Theme.gap(8); y: Theme.gap(6)
                        spacing: Theme.gap(10)
                        FunnelRow { caption: "roles · grade 3, a break"; roles: funnelTs; trend: "holding"
                                    sessions: probe.synth([2, 2, 3, 3, 3], 1.0, 0.65, 2)
                                    basis: "ρ 0.74 → 0.65 over 3 sessions"; measure: "Stance width" }
                        FunnelRow { caption: "roles · grade 4"; roles: funnelTs; trend: "tightening"
                                    sessions: probe.synth([3, 4, 4, 4], 0.7, 0.4, -1)
                                    basis: "ρ 0.70 → 0.40 over 4 sessions"; measure: "Stance width" }
                        FunnelRow { caption: "roles · too few"; roles: funnelTs; trend: "insufficient"
                                    sessions: probe.synth([3, 3, 3, 4, 4], 0.9, 0.5, 3); measure: "Stance width" }
                    }
                }
            }
            // Last in the item (children are destroyed in creation order); never id `roles`.
            PpTypeset { id: funnelTs; medium: "print"; fixedAesthetic: "folio" }
        }
    }

    // ── typicality ───────────────────────────────────────────────────────────────────────────
    // The typicality variants (§6.2) and their badges. Real: Mark's 7 Oct session (gallery_real.json
    // `typicality`: six scalar readings, every shot's value and z over the session) and the fixture's
    // three trace envelopes, each with its chosen swing (`traceEnvelopes`). Synthetic rows are typed in
    // below. Shares `dataPath` and `fixture` with the funnel mode.
    function runTypicality() {
        if (probe.dataPath === "") { probe.log("typicality needs --probe-data <gallery_real.json>"); Qt.exit(2); return }
        const x = new XMLHttpRequest()
        x.onreadystatechange = function () {
            if (x.readyState !== XMLHttpRequest.DONE) return
            try {
                probe.fixture = JSON.parse(x.responseText)
            } catch (e) {
                probe.log("could not read " + probe.dataPath + " (set QML_XHR_ALLOW_FILE_READ=1): " + e)
                Qt.exit(2); return
            }
            probe.log("fixture: " + probe.fixture.typicality.length + " typicality readings, " + probe.fixture.traceEnvelopes.length
                      + " trace envelopes, session " + probe.fixture.typicalitySession + " (" + probe.fixture.typicalitySessionShots + " shots)")
            probe.view = typComp.createObject(overlayRoot())
            for (const t of themes) jobs.push(t)
            nextTypicality()
        }
        try {
            x.open("GET", fileUrl(probe.dataPath))
            x.send()
        } catch (e) {
            probe.log("could not read " + probe.dataPath + " (set QML_XHR_ALLOW_FILE_READ=1): " + e)
            Qt.exit(2)
        }
    }
    function nextTypicality() {
        if (jobs.length === 0) { finish(); return }
        Theme.themeIndex = jobs.shift()
        after(900, () => {
            probe.view.actual = actualRenderer(probe.view.sample)
            probe.log("typicality accessible name: " + probe.view.sample.Accessible.name)
            after(100, () => grabItem(probe.view.sheet, outDir + "/typicality_" + themeTag() + "_" + dprTag + ".png", nextTypicality))
        })
    }
    // TEST DATA, for display only. §7.4's band from |z|: in the app the band is the C++ grader's
    // (TypicalityResult) and QML never computes one; the gallery fixture's z is the python
    // calibration's, so the probe spells the band here. A non-finite z has no band.
    function bandOf(z) {
        if (typeof z !== "number" || isNaN(z)) return ""
        const a = Math.abs(z)
        return a <= 1 ? "usual" : a <= 2 ? "aLittleOff" : "unusual"
    }
    // TEST DATA: the scalar typicality basis, spelled as the grader's TypicalityResult::basisString
    // spells it ("this swing 5.2 cm · median 4.8 cm"; ° and % attach, other units take a space).
    function unitText(v, unit) {
        const n = Number(v).toFixed(1)
        return unit === "" ? n : (unit.charAt(0) === "°" || unit.charAt(0) === "%") ? n + unit : n + " " + unit
    }
    function typBasis(value, usual, unit) {
        return "this swing " + unitText(value, unit) + " · median " + unitText(usual, unit)
    }
    // A reading's shown swings: the session's latest, its largest |z| and its smallest |z| (distinct),
    // each with the latest 12 OTHER shots as its ghosts, oldest first.
    function typSwings(reading) {
        const shots = reading.shots, n = shots.length
        if (n === 0) return []
        let far = 0, near = 0
        for (let i = 0; i < n; ++i) {
            if (Math.abs(shots[i].z) > Math.abs(shots[far].z)) far = i
            if (Math.abs(shots[i].z) < Math.abs(shots[near].z)) near = i
        }
        const picks = [n - 1]
        for (const i of [far, near]) if (picks.indexOf(i) < 0) picks.push(i)
        return picks.map(i => {
            const others = shots.filter((s, j) => j !== i).slice(-12).map(s => ({ swingId: s.swing, z: s.z }))
            const s = shots[i]
            return { swing: s.swing, z: s.z, value: s.value, fixtureBand: s.band,
                     values: { z: s.z, ghosts: others, corridor: null, band: probe.bandOf(s.z) },
                     basis: probe.typBasis(s.value, reading.mean, reading.unit || "") }
        })
    }
    function zText(z) { return (z < 0 ? "−" : z > 0 ? "+" : "") + Math.abs(z).toFixed(2) }
    // Synthetic ghosts: 12 ids, z spread over about ±1.6 (deterministic), for the synthetic rows.
    readonly property var synthGhosts: {
        const out = []
        for (let i = 0; i < 12; ++i) out.push({ swingId: "synthetic_" + (i + 1), z: 1.6 * Math.sin(i * 1.7 + 0.3) })
        return out
    }
    function synthTyp(z, corridor) {
        return { z: z, ghosts: probe.synthGhosts, corridor: corridor === undefined ? { loZ: -1, hiZ: 1.5 } : corridor, band: probe.bandOf(z) }
    }
    // A synthetic 101-point window: a hump on a positive base; this swing peaks `lead` later and `lift`
    // SDs higher. Its band is typed in with it.
    function synthTrace(lead, lift, band) {
        const mean = [], sd = [], swing = []
        for (let i = 0; i <= 100; ++i) {
            const t = i / 100
            const m = 4 + 30 * Math.exp(-Math.pow((t - 0.55) / 0.18, 2))
            const s = 1.5 + 3 * Math.exp(-Math.pow((t - 0.55) / 0.25, 2))
            mean.push(m); sd.push(s)
            swing.push(4 + 30 * Math.exp(-Math.pow((t - 0.55 - lead) / 0.18, 2)) + lift * s)
        }
        return { mean: mean, sd: sd, swing: swing, corridor: { loT: 0.45, hiT: 0.7 }, band: band }
    }
    readonly property var synthZ: [0, 1.5, 2.5, 3.5, -3.5]

    // One shown swing: its caption, the Cell mark large (for looking), then the Cell badge on the
    // golfer layer and on the technical layer.
    component TypSwing: Column {
        id: ts
        property string caption: ""
        property var values: null
        property string basis: ""
        property bool showCorridor: false
        property alias big: bigMark
        spacing: Theme.gap(4)
        Cap { text: ts.caption }
        PpVariabilityMark { id: bigMark; variant: PpVariabilityMark.Typicality; form: PpVariabilityMark.Cell; size: Theme.sp(48)
                            values: ts.values; showCorridor: ts.showCorridor }
        PpVariabilityBadge { variant: PpVariabilityMark.Typicality; form: PpVariabilityMark.Cell; values: ts.values; showCorridor: ts.showCorridor }
        PpVariabilityBadge { variant: PpVariabilityMark.Typicality; form: PpVariabilityMark.Cell; values: ts.values; showCorridor: ts.showCorridor
                             contentLayer: "technical"; basisText: ts.basis }
    }
    // One trace: the Cell mark large, then its badges on both layers, then the Your-group Icon badge
    // with the trace's RMS z (§6.2: an icon context uses that, never a trace icon).
    component TraceSwing: Column {
        id: trs
        property string caption: ""
        property var values: null
        property string basis: ""
        property real rmsZ: NaN
        property bool showCorridor: false
        spacing: Theme.gap(4)
        Cap { text: trs.caption }
        PpVariabilityMark { variant: PpVariabilityMark.TypicalityTrace; form: PpVariabilityMark.Cell; size: Theme.sp(48)
                            values: trs.values; showCorridor: trs.showCorridor }
        PpVariabilityBadge { variant: PpVariabilityMark.TypicalityTrace; form: PpVariabilityMark.Cell; values: trs.values
                             showCorridor: trs.showCorridor }
        PpVariabilityBadge { variant: PpVariabilityMark.TypicalityTrace; form: PpVariabilityMark.Cell; values: trs.values
                             showCorridor: trs.showCorridor; contentLayer: "technical"; basisText: trs.basis }
        PpVariabilityBadge { visible: !isNaN(trs.rmsZ); variant: PpVariabilityMark.Typicality
                             values: ({ z: trs.rmsZ, ghosts: [], corridor: null, band: trs.values ? trs.values.band : "" }) }
    }

    Component {
        id: typComp
        Rectangle {
            id: tsheet
            property string actual: "?"
            property Item sample: null
            readonly property Item sheet: tsheet
            width: tcol.implicitWidth + 2 * Theme.gap(24)
            height: tcol.implicitHeight + 2 * Theme.gap(16)
            z: 100000
            color: Theme.colorSurface
            Column {
                id: tcol
                x: Theme.gap(24); y: Theme.gap(16)
                spacing: Theme.gap(14)
                Cap {
                    text: "PpVariabilityMark Typicality + TypicalityTrace · " + Theme.aesthetic + " " + (Theme.dark ? "dark" : "light")
                          + " · dpr " + probe.dpr + " · " + Qt.platform.pluginName + " / " + probe.graphicsApi()
                          + " · requested CurveRenderer, drew with " + tsheet.actual
                }
                Cap { text: "Real: " + (probe.fixture ? probe.fixture.athlete + " · " + probe.fixture.club + " · " + probe.fixture.typicalitySession
                                                        + " · " + probe.fixture.typicalitySessionShots + " shots" : "")
                            + " · each swing: the session's latest, largest |z|, smallest |z|; ghosts = the latest 12 other shots; band from |z| (§7.4, probe-computed)" }
                Repeater {
                    model: probe.fixture ? probe.fixture.typicality : []
                    Row {
                        id: readingRow
                        required property var modelData
                        required property int index
                        spacing: Theme.gap(28)
                        Cap { width: Theme.sp(200); wrapMode: Text.WordWrap
                              text: readingRow.modelData.label + " · " + readingRow.modelData.phase + "\n" + readingRow.modelData.basisString }
                        Repeater {
                            model: probe.typSwings(readingRow.modelData)
                            TypSwing {
                                required property var modelData
                                required property int index
                                caption: modelData.swing + " · z " + probe.zText(modelData.z) + " · fixture band " + modelData.fixtureBand
                                values: modelData.values
                                basis: modelData.basis
                                Component.onCompleted: if (readingRow.index === 0 && index === 0) tsheet.sample = big
                            }
                        }
                    }
                }
                Cap { text: "Real trace envelopes (traceEnvelopes: mean ± SD over the window, the fixture's chosen swing; band and basis are the fixture's)" }
                Row {
                    spacing: Theme.gap(28)
                    Repeater {
                        model: probe.fixture ? probe.fixture.traceEnvelopes : []
                        TraceSwing {
                            required property var modelData
                            caption: modelData.label + " · " + modelData.window + "\n" + modelData.swing.swing + " · RMS z "
                                     + probe.zText(modelData.swing.rmsZ) + " (" + modelData.swing.selection + ")"
                            values: ({ mean: modelData.mean, sd: modelData.sd, swing: modelData.swing.trace, corridor: null,
                                       band: modelData.swing.band })
                            basis: modelData.swing.basisString
                            rmsZ: modelData.swing.rmsZ
                        }
                    }
                }
                Cap { text: "Synthetic (typed in for the probe) · z 0, 1.5, 2.5, 3.5, −3.5 · top: showCorridor off; bottom: on (corridor z −1 … 1.5)" }
                Repeater {
                    model: [false, true]
                    Row {
                        id: synthRow
                        required property bool modelData
                        spacing: Theme.gap(28)
                        Repeater {
                            model: probe.synthZ
                            TypSwing {
                                required property real modelData
                                caption: "z " + probe.zText(modelData) + (synthRow.modelData ? " · showCorridor" : "")
                                values: probe.synthTyp(modelData)
                                showCorridor: synthRow.modelData
                                basis: "this swing " + (4.8 + modelData * 0.6).toFixed(1) + " cm · median 4.8 cm"
                            }
                        }
                    }
                }
                Cap { text: "Icon form · z 0, 1.5, 2.5, 3.5, −3.5 · at 48, 24, 20, 16 px · then showCorridor (z −1 … 1.5) at 48" }
                Row {
                    spacing: Theme.gap(28)
                    Repeater {
                        model: probe.synthZ
                        Row {
                            id: iconSet
                            required property real modelData
                            spacing: Theme.gap(8)
                            Repeater {
                                model: [48, 24, 20, 16]
                                PpVariabilityMark { required property int modelData; anchors.verticalCenter: parent.verticalCenter
                                                    variant: PpVariabilityMark.Typicality; size: Theme.sp(modelData)
                                                    values: probe.synthTyp(iconSet.modelData) }
                            }
                            PpVariabilityMark { anchors.verticalCenter: parent.verticalCenter; variant: PpVariabilityMark.Typicality
                                                size: Theme.sp(48); showCorridor: true; values: probe.synthTyp(iconSet.modelData) }
                        }
                    }
                }
                Cap { text: "Synthetic traces · on time, 8 % late and higher, 10 % early and lower · top: showCorridor off; bottom: on (t 0.45 … 0.7)" }
                Repeater {
                    model: [false, true]
                    Row {
                        id: synthTraceRow
                        required property bool modelData
                        spacing: Theme.gap(28)
                        TraceSwing { caption: "usual"; showCorridor: synthTraceRow.modelData; values: probe.synthTrace(0, 0.4, "usual")
                                     basis: "peak on time" }
                        TraceSwing { caption: "a little off"; showCorridor: synthTraceRow.modelData; values: probe.synthTrace(0.08, 1.6, "aLittleOff")
                                     basis: "peak 25 ms late, higher" }
                        TraceSwing { caption: "unusual"; showCorridor: synthTraceRow.modelData; values: probe.synthTrace(-0.1, -1.4, "unusual")
                                     basis: "peak 45 ms early, lower" }
                    }
                }
                Cap { text: "Badges · Your usual, A little off, Unusual · Icon golfer · Icon dense · Icon technical · Cell golfer · Cell technical" }
                Repeater {
                    model: [{ form: PpVariabilityMark.Icon, layer: "golfer", dense: false },
                            { form: PpVariabilityMark.Icon, layer: "golfer", dense: true },
                            { form: PpVariabilityMark.Icon, layer: "technical", dense: false },
                            { form: PpVariabilityMark.Cell, layer: "golfer", dense: false },
                            { form: PpVariabilityMark.Cell, layer: "technical", dense: false }]
                    Row {
                        id: badgeSet
                        required property var modelData
                        spacing: Theme.gap(28)
                        Repeater {
                            model: [0.4, 1.4, 2.3]
                            PpVariabilityBadge {
                                required property real modelData
                                anchors.verticalCenter: parent.verticalCenter
                                variant: PpVariabilityMark.Typicality; form: badgeSet.modelData.form; dense: badgeSet.modelData.dense
                                contentLayer: badgeSet.modelData.layer; values: probe.synthTyp(modelData)
                                basisText: "this swing " + (4.8 + modelData * 0.6).toFixed(1) + " cm · median 4.8 cm"
                            }
                        }
                    }
                }
                Cap { text: "TypicalityTrace asked for an Icon: warns once (§6.2: no icon form) and draws the Cell" }
                PpVariabilityBadge { variant: PpVariabilityMark.TypicalityTrace; form: PpVariabilityMark.Icon
                                     values: probe.synthTrace(0, 0.4, "usual") }
                Cap { text: "Print: medium print (fills × " + Theme.variabilityPrintAlphaScale + ", clamped to 1), then a PpTypeset roles (Folio, on its paper) · showCorridor on" }
                Row {
                    spacing: Theme.gap(28)
                    PpVariabilityMark { variant: PpVariabilityMark.Typicality; form: PpVariabilityMark.Cell; size: Theme.sp(48); medium: "print"
                                        showCorridor: true; values: probe.synthTyp(3.5) }
                    PpVariabilityMark { variant: PpVariabilityMark.TypicalityTrace; form: PpVariabilityMark.Cell; size: Theme.sp(48); medium: "print"
                                        showCorridor: true; values: probe.synthTrace(0.08, 1.6, "aLittleOff") }
                    PpVariabilityBadge { anchors.verticalCenter: parent.verticalCenter; variant: PpVariabilityMark.Typicality
                                         form: PpVariabilityMark.Cell; medium: "print"; values: probe.synthTyp(1.4) }
                }
                Rectangle {
                    width: typRolesCol.implicitWidth + 2 * Theme.gap(8)
                    height: typRolesCol.implicitHeight + 2 * Theme.gap(6)
                    color: typTs.paper
                    Column {
                        id: typRolesCol
                        x: Theme.gap(8); y: Theme.gap(6)
                        spacing: Theme.gap(6)
                        Row {
                            spacing: Theme.gap(20)
                            PpVariabilityMark { variant: PpVariabilityMark.Typicality; form: PpVariabilityMark.Cell; roles: typTs
                                                size: typTs.u(48); showCorridor: true; values: probe.synthTyp(3.5) }
                            PpVariabilityMark { variant: PpVariabilityMark.TypicalityTrace; form: PpVariabilityMark.Cell; roles: typTs
                                                size: typTs.u(48); showCorridor: true; values: probe.synthTrace(0.08, 1.6, "aLittleOff") }
                            PpVariabilityMark { anchors.verticalCenter: parent.verticalCenter; variant: PpVariabilityMark.Typicality
                                                roles: typTs; values: probe.synthTyp(1.4) }
                        }
                        Row {
                            spacing: Theme.gap(20)
                            Repeater {
                                model: [0.4, 1.4, 2.3]
                                PpVariabilityBadge { required property real modelData; anchors.verticalCenter: parent.verticalCenter
                                                     variant: PpVariabilityMark.Typicality; form: PpVariabilityMark.Cell; roles: typTs
                                                     values: probe.synthTyp(modelData); contentLayer: "technical"
                                                     basisText: "this swing " + (4.8 + modelData * 0.6).toFixed(1) + " cm · median 4.8 cm" }
                            }
                            PpVariabilityBadge { anchors.verticalCenter: parent.verticalCenter; variant: PpVariabilityMark.Typicality
                                                 roles: typTs; values: probe.synthTyp(0.4) }
                        }
                    }
                }
            }
            // Last in the item (children are destroyed in creation order); never id `roles`.
            PpTypeset { id: typTs; medium: "print"; fixedAesthetic: "folio" }
        }
    }
}
