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

// THE HEADLESS START, ON SCREEN (refactor 4-c; headless_probe_test).
//
// Loaded by Main.qml's --probe-qml hook inside the REAL app started with PINPOINT_HEADLESS_PROBE=1: every device
// controller QML binds to is there (made inert), the device lists are empty, and every screen loads. It visits each
// screen the rail reaches and every Settings panel, then exits; the test reads the app log for the rest (the
// "[HeadlessProbe] not started" lines, no "REFUSED", no ReferenceError or TypeError).
//
//   PINPOINT_HEADLESS_PROBE=1 QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 HOME=<scratch> \
//     <PinPointStudio> --probe-qml <abs path to this file>
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
        return ok
    }
    function finish() {
        tick.running = false
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function findAll(item, pred, out) {
        out = out || []
        if (!item) return out
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
        return out
    }
    function rootItem() { let t = probe; while (t.parent) t = t.parent; return t }

    // The screens the rail and the app reach, and Developer (the dev hatch, unreachable from the UI): its audio page
    // reads the voice in and out through `app`.
    readonly property var screens: [AppScreen.Home, AppScreen.Session, AppScreen.Athletes, AppScreen.Athlete,
                                    AppScreen.System, AppScreen.Wizard, AppScreen.Developer, AppScreen.Settings]
    readonly property int settingsPanels: 12   // ScreenSettings' StackLayout slots 0..11
    property int stage: 0
    property int index: 0
    property var settingsScreen: null

    Timer {
        id: tick
        interval: 1200; running: true; repeat: true
        onTriggered: probe.step()
    }

    function step() {
        if (stage === 0) {
            // The device controllers are objects (inert), so every binding on them resolves.
            check(!!imuManager && !!cameraManager && !!launchMonitor, "the device controllers QML reads are there")
            // The five screen-facing objects are reached through `app` (R-QML-1), and their old context names are
            // gone: a read of `app.<name>` that is not a property is undefined without a word, and a bare old name
            // would resolve only while main.cpp still registered it. typeof, so a missing name is not a ReferenceError.
            const appMembers = ["ttsController", "transcriptionController", "profiler", "resourceMonitor", "cudaRuntime"]
            for (let i = 0; i < appMembers.length; ++i)
                check(typeof app[appMembers[i]] === "object" && app[appMembers[i]] !== null,
                      "app." + appMembers[i] + " is an object")
            check(typeof ttsController === "undefined" && typeof controller === "undefined"
                  && typeof profiler === "undefined" && typeof resourceMonitor === "undefined"
                  && typeof cudaRuntime === "undefined", "the five old context names are gone")
            check(imuManager.imuScanActive === false, "no Bluetooth scan is running")
            check(cameraManager.cameraList.length === 0, "no camera is listed")
            stage = 1
            return
        }
        if (stage === 1) {
            if (index < screens.length) {
                app.navController.navigate(screens[index])
                log("screen " + screens[index])
                index += 1
                return
            }
            const s = findAll(rootItem(), function(o) {
                return o.activeNavIndex !== undefined && o.navigateToResult !== undefined
            })
            if (!check(s.length > 0, "the Settings screen is up")) { finish(); return }
            settingsScreen = s[0]
            index = 0
            stage = 2
            return
        }
        if (stage === 2) {
            if (index < settingsPanels) {
                settingsScreen.activeNavIndex = index
                log("settings panel " + index)
                index += 1
                return
            }
            app.navController.navigate(AppScreen.Home)
            stage = 3
            return
        }
        check(imuManager.imuScanActive === false && cameraManager.cameraList.length === 0,
              "still no scan and no camera after every screen")
        finish()
    }
}
