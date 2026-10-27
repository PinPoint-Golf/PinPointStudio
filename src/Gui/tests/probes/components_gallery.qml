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

// A gallery of the Phase 4a components (the §2.5 visual language) on fixed fake data, one
// whole-window PNG per theme. Needs no session: the gallery covers the app. Run offscreen on a COPY
// of the settings, with a screen big enough for the window:
//   XDG_CONFIG_HOME=<scratch> QT_QPA_PLATFORM='offscreen:configfile=<json: one 1680×1050 screen>' \
//       PinPointStudio --probe-qml <this file> --probe-themes 0,1,12,13 --probe-out <dir>
//   --probe-themes  comma list of theme indices (default: all fourteen)
//   --probe-out     <dir>
// Writes <out>/gallery_<aesthetic>_<light|dark>.png
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var themes: arg("--probe-themes", "0,1,2,3,4,5,6,7,8,9,10,11,12,13").split(",").map(Number)
    readonly property string outDir: arg("--probe-out", "/tmp")
    property var jobs: []
    property int startTheme: 0
    function log(s) { console.warn("PROBE: " + s) }

    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    property var bg: null
    property var frame: null
    property var held: null
    property var gallery: null
    function grabWindow(path, then) {
        const root = Overlay.overlay.parent
        if (!probe.bg) {
            probe.bg = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', root)
        }
        if (!probe.frame) {
            probe.frame = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { x: -100000; color: Theme.colorBg; property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true; hideSource: false } }', probe)
        }
        probe.frame.width = root.width; probe.frame.height = root.height
        probe.frame.src.sourceItem = root
        probe.frame.src.scheduleUpdate()
        after(250, () => {
            probe.frame.grabToImage(function (img) {
                probe.held = img
                img.saveToFile(path); probe.log("saved " + path); then()
            })
        })
    }
    function next() {
        if (jobs.length === 0) { Theme.themeIndex = startTheme; after(300, () => Qt.exit(0)); return }
        Theme.themeIndex = jobs.shift()
        after(800, () => {
            grabWindow(outDir + "/gallery_" + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + ".png", next)
        })
    }
    // Attached Overlay resolves on an Item, not on the Timer below.
    function overlayRoot() { return Overlay.overlay.parent }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            probe.gallery = galleryComp.createObject(probe.overlayRoot())
            for (const t of probe.themes) probe.jobs.push(t)
            probe.next()
        }
    }

    // ── Fake data ────────────────────────────────────────────────────────────
    readonly property var areas: [
        { name: "Posture",  bands: [4, 0, 12, 0], trend: -1, fault: "Early extension", workOns: 2, open: true  },
        { name: "Sequence", bands: [4, 1, 10, 1], trend:  1, fault: "Arms first",      workOns: 1, open: false },
        { name: "Setup",    bands: [0, 3, 16, 0], trend:  0, fault: "Ball position",   workOns: 0, open: false }
    ]
    readonly property var nextUp: [
        { text: "Early extension", freq: "most swings",  share: 0.9,  trend: 1, marks: [true, true, false, true, true, true, true] },
        { text: "Arms first",      freq: "often",        share: 0.7,  trend: 0, marks: [false, true, true, false, true, true] },
        { text: "Open clubface",   freq: "about half",   share: 0.55, trend: 0, marks: [true, false, true, true, false] }
    ]
    function bandSegments(b) {
        return [{ count: b[0], tone: Theme.bandTone("habit") },  { count: b[1], tone: Theme.bandTone("occasional") },
                { count: b[2], tone: Theme.bandTone("clean") },  { count: b[3], tone: Theme.bandTone("unclear") }]
    }
    function trendText(n) { return n > 0 ? "+" + n : n < 0 ? "−" + (-n) : "±0" }

    component Cap: PpMicro { color: Theme.colorText3 }
    component Cell: Text {
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; font.weight: Theme.fontBodyWeight
        color: Theme.colorText; elide: Text.ElideRight
    }

    Component {
        id: galleryComp
        Rectangle {
            id: gal
            anchors.fill: parent
            z: 100000
            color: Theme.colorBg
            Column {
                x: Theme.sp(40); y: Theme.sp(32)
                width: Math.min(gal.width - 2 * Theme.sp(40), Theme.sp(960))
                spacing: Theme.sp(10)

                Cap { text: "PpTabs  size: title · noteAfterTabs · chip" }
                PpTabs {
                    width: parent.width
                    roles: galTs
                    size: "title"
                    tabs: [{ key: "coaching", label: "Coaching" }, { key: "swing", label: "Your swing" }]
                    current: "coaching"
                    note: "from 180 swings over 6 sessions"
                    noteAfterTabs: true
                    chip: "All work-ons ▸"
                }

                Item { width: 1; height: Theme.sp(8) }
                Cap { text: "PpCard table · PpExpandRow · PpBandBar (Theme.bandTone) · trend (Theme.trendTone)" }
                PpCard {
                    width: parent.width
                    table: true
                    columns: [{ title: "",              width: Theme.sp(14) },
                              { title: "Area",          fill: true },
                              { title: "Habits",        width: Theme.sp(230) },
                              { title: "Trend",         width: Theme.sp(56), align: "right" },
                              { title: "Biggest fault", width: Theme.sp(190) },
                              { title: "Work-ons",      width: Theme.sp(80), align: "right" }]
                    Repeater {
                        model: probe.areas
                        PpExpandRow {
                            id: area
                            required property var modelData
                            expanded: modelData.open
                            accessibleName: modelData.name
                            detailIndent: Theme.sp(Theme.itemIndent)
                            detail: Component {
                                Column {
                                    Repeater {
                                        model: [{ t: "Early extension", s: 0.8 }, { t: "Loss of posture", s: 0.4 }]
                                        Item {
                                            required property var modelData
                                            width: parent.width; height: Theme.sp(Theme.itemRowH)
                                            Rectangle { width: parent.width; height: 1; color: Theme.colorHairline }
                                            PpBadge { id: ib; kind: "ring"; plain: true; anchors.verticalCenter: parent.verticalCenter }
                                            Cell { x: ib.width + Theme.sp(10); anchors.verticalCenter: parent.verticalCenter; text: modelData.t }
                                            PpMeter { x: parent.width - width - Theme.sp(Theme.rowPadH); anchors.verticalCenter: parent.verticalCenter
                                                      share: modelData.s; tone: Theme.colorWarn }
                                        }
                                    }
                                }
                            }
                            Item { width: 1; height: 1 }    // the chevron's column
                            Cell { text: area.modelData.name; font.weight: Font.DemiBold; width: area.headerRow.columnWidth(1) }
                            Row {
                                spacing: Theme.sp(10)
                                PpBandBar { width: Theme.sp(130); anchors.verticalCenter: parent.verticalCenter
                                            segments: probe.bandSegments(area.modelData.bands) }
                                Cell { text: area.modelData.bands[0] + " habits"; color: Theme.colorText2; font.pixelSize: Theme.fontSzBody2 }
                            }
                            Cell { text: probe.trendText(area.modelData.trend); color: Theme.trendTone(area.modelData.trend)
                                   font.family: Theme.fontData }
                            Cell { text: area.modelData.fault; width: area.headerRow.columnWidth(4); color: Theme.colorText2 }
                            Cell { text: String(area.modelData.workOns); font.family: Theme.fontData }
                        }
                    }
                }

                Item { width: 1; height: Theme.sp(8) }
                Cap { text: "PpCard table + title · PpTableRow secondLine · PpBadge ring · PpMeter · PpChip trend · PpPips maxPips 5" }
                PpCard {
                    width: parent.width
                    table: true
                    title: "Next on your list"
                    tone: Theme.colorText2
                    columns: [{ width: Theme.sp(20) }, { fill: true }, { width: Theme.sp(100) },
                              { width: Theme.sp(100) }, { width: Theme.sp(60), align: "right" }]
                    Repeater {
                        model: probe.nextUp
                        PpTableRow {
                            id: nr
                            required property var modelData
                            secondLine: true
                            PpBadge { kind: "ring"; plain: true }
                            Column {
                                width: nr.columnWidth(1)
                                Cell { text: nr.modelData.text; width: parent.width }
                                Cell { text: nr.modelData.freq; width: parent.width; color: Theme.colorText3; font.pixelSize: Theme.fontSzBody2 }
                            }
                            PpMeter { share: nr.modelData.share; tone: Theme.colorWarn }
                            PpChip { trend: nr.modelData.trend }
                            PpPips { marks: nr.modelData.marks; maxPips: 5 }
                        }
                    }
                }

                Item { width: 1; height: Theme.sp(8) }
                Cap { text: "PpBadge check · target · dot · ring · easing · unconfirmed · plain check / dot / ring   |   PpChip trend +1 / −1   |   PpMeter 0 · 0.5 · 1" }
                Row {
                    spacing: Theme.sp(14)
                    Repeater {
                        model: ["check", "target", "dot", "ring", "easing", "unconfirmed"]
                        PpBadge { required property string modelData; kind: modelData; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Repeater {
                        model: ["check", "dot", "ring"]
                        PpBadge { required property string modelData; kind: modelData; plain: true; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Item { width: Theme.sp(16); height: 1 }
                    PpChip { trend: 1;  anchors.verticalCenter: parent.verticalCenter }
                    PpChip { trend: -1; anchors.verticalCenter: parent.verticalCenter }
                    Item { width: Theme.sp(16); height: 1 }
                    Repeater {
                        model: [0, 0.5, 1]
                        PpMeter { required property real modelData; share: modelData; tone: Theme.colorWarn
                                  anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
            // Declared last: children are destroyed in creation order, and the tabs read it to the end.
            PpTypeset { id: galTs }
        }
    }
}
