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

// END-TO-END: A CONDITION READ BY SEVERAL MEASURES SHOWS EACH ON ITS OWN TAB.
//
// Early extension is the pelvis toward the ball OR the spine standing up. The detail page used to
// draw only the measure that decided the first shot, and dropped every shot the other decided.
// This loads a real session, opens early_extension's detail and requires: two measure tabs, each
// with shots placed on its own ruler, a default that is the measure that fired most, a tab pick
// the model honours, and the pick surviving a Replay → Analyse → Replay rebuild of the panel.
//
// Run on a COPY of the settings (XDG_CONFIG_HOME): build/run-me/verify-measure-tabs.sh
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))
    readonly property string cond: arg("--probe-condition", "early_extension")

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }

    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function theBody() {
        const bodies = []
        walk(probe.Window.window.contentItem, "sdBody", bodies)
        const shown = bodies.filter(function (b) { return b.visible && b.width > 0 })
        return shown.length === 1 ? shown[0] : null
    }
    function visibleTabs(b) {
        const o = []
        walk(b, "sdDetailMeasureTab", o)
        return o.filter(function (t) { return t.visible && t.width > 0 })
    }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    property var before: null
    property string picked: ""
    property int step: 0
    property int waits: 0
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const s = probe.step
            if (s === 1) {
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                for (const m of [SessionMode.replay, SessionMode.analyse]) {
                    ViewLayout.setArrangement(m, "split")
                    if (!ViewLayout.isPanelOn(m, "sessionDiagnostics"))
                        ViewLayout.setPanel(m, "sessionDiagnostics", true)
                }
                navController.navigate(2)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (s === 2) {
                let ok = false
                for (let i = 0; i < shots.count; ++i) {
                    const sh = shots.objectAt(i)
                    if (sh.ordinal === probe.ordinal) { SessionMode.enterReplay(sh.shotId, sh.swingDir); ok = true }
                }
                probe.check(ok, "shot " + probe.ordinal + " is in the carousel")
            } else if (s === 3) {
                // The schema bump regrades every shot on open; wait for it.
                const b = probe.theBody()
                if ((!b || !b.source || b.source.busy || (b.cards || []).length === 0) && probe.waits++ < 60) {
                    probe.step -= 1; return
                }
                probe.check(!!b, "the panel is up")
                if (!b) { probe.finish(); return }
                b._openDetail(probe.cond)
            } else if (s === 4) {
                const b = probe.theBody()
                const d = b.source.detail
                probe.check(d && d.id === probe.cond, "detail open on " + probe.cond)
                const ms = (d && d.measures) || []
                probe.check(ms.length === 2, ms.length + " measure tabs in the model")
                probe.check(probe.visibleTabs(b).length === 2, "…and two drawn")
                let most = null
                for (const m of ms) {
                    probe.log("tab " + m.id + " '" + m.label + "' " + m.countText)
                    if (!most || m.fired > most.fired || (m.fired === most.fired && m.read > most.read)) most = m
                }
                probe.check(!!most && d.measureId === most.id, "default tab " + d.measureId + " is the one that fired most")
                for (const m of ms) {
                    b.source.setDetailMeasure(m.id)
                    const d2 = b.source.detail
                    const sp = d2.header && d2.header.spread
                    probe.check(d2.measureId === m.id, "tab " + m.id + " picked")
                    probe.check(!!sp && (sp.placed || 0) === m.read && m.read > 0,
                                "…its strip places " + (sp ? sp.placed : "-") + " shots (read " + m.read + ")")
                    probe.check(!!sp && sp.measure === m.label, "…on its own ruler: " + (sp ? sp.measure : "-"))
                    const fired = (sp && sp.run || []).filter(function (r) { return r.state === "fired" }).length
                    probe.check(fired === m.fired, "…with " + fired + " of its own firings marked (want " + m.fired + ")")
                }
                // Leave the tab that is NOT the default picked, to prove the pick survives.
                const other = ms.filter(function (m) { return m.id !== most.id })[0]
                b.source.setDetailMeasure(other.id)
                probe.picked = other.id
                probe.before = b
            } else if (s === 5) {
                SessionMode.enterAnalyse()
            } else if (s === 6) {
                SessionMode.showReplay()
            } else if (s === 7) {
                const b = probe.theBody()
                probe.check(!!b && b !== probe.before, "the panel WAS rebuilt")
                const d = b && b.source.detail
                probe.check(!!d && d.id === probe.cond, "detail still open after the rebuild")
                probe.check(!!d && d.measureId === probe.picked,
                            "picked tab kept: " + (d ? d.measureId : "-") + " (want " + probe.picked + ")")
                SessionMode.enterCapture()
                probe.finish()
            }
        }
    }
}
