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

// Whole-window renders of the app for a list of themes × screens (how Folio and the original
// themes were compared pixel for pixel, design system §14.1). Run offscreen on a COPY of the
// settings, with a screen big enough for the window:
//   XDG_CONFIG_HOME=<scratch> QT_QPA_PLATFORM='offscreen:configfile=<json: one 1680×1050 screen>' \
//       PinPointStudio --probe-qml <this file> --probe-themes … --probe-screens … --probe-out <dir>
//   --probe-themes 0,1,12,13   --probe-screens home,diagnostics,settings,wrist
//   --probe-out <dir>   [--probe-session <dir> --probe-ordinal N]   (wrist needs a session)
// Writes <out>/<aesthetic>_<light|dark>_<screen>.png
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var themes: arg("--probe-themes", "12,13").split(",").map(Number)
    readonly property var screens: arg("--probe-screens", "home").split(",")
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "3"))
    property var jobs: []
    property int startTheme: 0
    function log(s) { console.warn("PROBE: " + s) }

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
    property var held: null
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
    function showScreen(name, then) {
        if (name === "home") { navController.navigate(0); after(1200, then) }
        else if (name === "diagnostics") { navController.navigate(11); after(1500, then) }
        else if (name === "settings") { navController.navigate(9); after(1200, then) }
        else if (name === "athletes") { navController.navigate(7); after(1200, then) }
        else if (name === "setup") { navController.navigate(10); after(2000, then) }
        else if (name === "system") { navController.navigate(8); after(1200, then) }
        else if (name === "wrist" || name === "wristreplay") {
            navController.navigate(2)
            if (name === "wrist") SessionMode.enterAnalyse()
            after(3500, then)
        } else { probe.log("unknown screen " + name); then() }
    }
    function next() {
        if (jobs.length === 0) { Theme.themeIndex = startTheme; after(300, () => Qt.exit(0)); return }
        const j = jobs.shift()
        Theme.themeIndex = j.theme
        showScreen(j.screen, () => {
            grabWindow(outDir + "/" + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + "_" + j.screen + ".png", next)
        })
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            for (const sc of probe.screens) for (const t of probe.themes) probe.jobs.push({ theme: t, screen: sc })
            const needSession = probe.screens.some(s => s.startsWith("wrist"))
            if (!needSession) { probe.next(); return }
            navController.navigate(2)
            SessionMode.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            probe.after(4000, () => {
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
                }
                probe.after(4000, () => probe.next())
            })
        }
    }
}
