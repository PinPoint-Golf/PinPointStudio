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

// Whole-window renders of screens at each Density (Settings → Appearance → Density), so the
// compact / default / spacious policy in docs/design/system/README.md can be checked by eye and by
// pixel diff. Run offscreen on a COPY of the settings:
//   XDG_CONFIG_HOME=<scratch> QT_QPA_PLATFORM='offscreen:configfile=<json: one 1680×1050 screen>' \
//       PinPointStudio --probe-qml <this file> --probe-screens home,sessions,settings,athletes \
//       --probe-densities compact,default,spacious --probe-out <dir>
// Writes <out>/<screen>_<density>.png and logs Theme.gap(16) / rowH per density.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var screens: arg("--probe-screens", "home,sessions,settings,athletes").split(",")
    readonly property var densities: arg("--probe-densities", "compact,default,spacious").split(",")
    readonly property string outDir: arg("--probe-out", "/tmp")
    property var jobs: []
    property string startDensity: "default"
    function log(s) { console.warn("PROBE: " + s) }

    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    property var bg: null
    property var frame: null
    property var held: null
    function grabWindow(path, then) {
        const root = Overlay.overlay.parent
        if (!probe.bg)
            probe.bg = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { z: -1000; anchors.fill: parent; color: Theme.colorBg }', root)
        if (!probe.frame)
            probe.frame = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { x: -100000; color: Theme.colorBg; property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true; hideSource: false } }', probe)
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
    function showScreen(name, then) {
        const map = { home: AppScreen.Home, sessions: AppScreen.Sessions, settings: AppScreen.Settings,
                      athletes: AppScreen.Athletes, system: AppScreen.System }
        if (map[name] === undefined) { probe.log("unknown screen " + name); then(); return }
        navController.navigate(map[name]); after(1500, then)
    }
    function next() {
        if (jobs.length === 0) { Theme.density = startDensity; after(300, () => Qt.exit(0)); return }
        const j = jobs.shift()
        Theme.density = j.density
        log("density=" + Theme.density + " densityScale=" + Theme.densityScale + " gap(16)=" + Theme.gap(16)
            + " rowH=" + Theme.rowH + " rowPadH=" + Theme.rowPadH)
        showScreen(j.screen, () => grabWindow(outDir + "/" + j.screen + "_" + j.density + ".png", next))
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startDensity = Theme.density
            for (const sc of probe.screens) for (const d of probe.densities) probe.jobs.push({ screen: sc, density: d })
            probe.next()
        }
    }
}
