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

// THE QUIT PINS' PROBE (SE-1b; the quit_pin.<point> ctests, src/Gui/tests/quit_pins/quit_pin.py).
//
// Loaded by Main.qml's --probe-qml hook inside the real app, started headless through tools/probe/run_headless.sh. It
// does one thing: it quits the app at the moment the pin asks for, the way a golfer's quit arrives, and says so in the
// log ("QUIT-PIN: quit requested"), so the driver can time the exit from that line. The app arguments after the probe:
//
//   --quit-pin-at startup        quit while main() is still building (Component.onCompleted runs inside the QML load,
//                                before exec()); Qt.quit() is queued, so it is delivered as the event loop starts
//   --quit-pin-at ms:<n>         quit <n> ms after the event loop starts (a Timer; the first tick is inside exec())
//   --quit-pin-at trigger:<dir>  quit as soon as a file appears in <dir>: the driver writes one when the app log shows
//                                the pin's busy marker (the warm-up building, a re-analysis running), so the quit lands
//                                while that thread is busy
//   --quit-pin-route quit|close  quit: Qt.quit(), the route of Cmd+Q and the app menu's Quit (APP-T9; no prompt).
//                                close: the window's close, the route of the window's ✕ (APP-T6/T9). Default quit.
//
// It never ends a session, opens a dialog or plays a sound; it reads nothing but its arguments.

import QtQuick
import Qt.labs.folderlistmodel

Item {
    id: pin
    anchors.fill: parent

    function log(s) { console.warn("QUIT-PIN: " + s) }
    function arg(name, fallback) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : fallback
    }
    readonly property string at: arg("--quit-pin-at", "")
    readonly property string route: arg("--quit-pin-route", "quit")
    property bool requested: false
    property bool armed: false   // trigger mode, once the folder is set

    function windowOf(item) { let t = item; while (t && !t.Window.window) t = t.parent; return t ? t.Window.window : null }

    function quitNow(why) {
        if (requested) return
        requested = true
        log("quit requested (" + why + ", route " + route + ")")
        if (route === "close") {
            const w = windowOf(pin)
            if (w) { w.close(); return }
            log("no window to close: quitting instead")
        }
        Qt.quit()
    }

    Component.onCompleted: {
        log("probe loaded, at=" + at + " route=" + route)
        if (at === "startup")
            quitNow("startup: inside the QML load, before exec()")
        else if (at.indexOf("ms:") === 0) {
            timer.interval = Math.max(1, parseInt(at.substring(3)))
            timer.start()
        } else if (at.indexOf("trigger:") === 0) {
            const dir = at.substring(8)
            folder.folder = (dir.charAt(0) === "/" ? "file://" : "file:///") + dir
            armed = true
        } else if (at !== "") {
            log("unknown --quit-pin-at '" + at + "': quitting at once")
            quitNow("bad argument")
        }
    }

    Timer {
        id: timer
        repeat: false
        onTriggered: pin.quitNow("ms:" + interval + " after the event loop started")
    }

    // The driver's trigger: a file named "quit" written into the folder. FolderListModel watches it
    // (QFileSystemWatcher). Its folder defaults to the working directory, so it counts only once the pin has set it.
    FolderListModel {
        id: folder
        showDirs: false
        nameFilters: ["quit"]
        onCountChanged: if (pin.armed && count > 0) pin.quitNow("trigger: the driver saw the busy marker")
    }
    // A backstop poll, in case the watcher's event is missed: the model re-reads the folder every 250 ms.
    Timer {
        interval: 250; repeat: true
        running: pin.armed && !pin.requested
        onTriggered: {
            const f = folder.folder
            folder.folder = ""
            folder.folder = f
        }
    }
}
