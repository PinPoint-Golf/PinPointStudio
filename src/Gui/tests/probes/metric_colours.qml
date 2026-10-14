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

// END-TO-END: A METRIC IS DRAWN IN ITS OWN COLOUR, AND THE USER'S CHOICES REACH THE CHART.
//
// Loads a session, opens a swing in Replay, finds the Charts panel's body (chBody) on This shot, and
// checks every trace (chTrace:<key> — its curve as painted, and its label's swatch) is
// Theme.metricColor(its key) — then re-points a metric, retunes a palette name and switches
// theme, and checks the traces FOLLOW each change (a binding that read the colour once
// would pass the first check and fail these). Then opens the metric in Settings → Diagnostic Model
// and checks the inspector's plot-colour row, and that Appearance offers the twelve tiles.
// REDESIGNED 14 Oct 2026 for the new Charts panel (build/charts-build/STEP3.md, INVENTORY §5 Probes):
// it used to read PpMetricChart's private `_curves`; it now reads what the panel draws, by objectName.
//
// Run with a COPY of the settings — it writes metricColors/metricPalette/themeIndex:
//   XDG_CONFIG_HOME=<scratch> QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <this file> \
//       --probe-session <session dir> [--probe-ordinal 3]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. build/run-me/verify-metric-colours.sh wraps it.

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
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "3"))

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    function finish() {
        appSettings.metricColors  = ({})
        appSettings.metricPalette = ({})
        Theme.themeIndex = probe.startTheme
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }

    function walk(item, pred, out) {
        if (!item) return
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out)
    }
    function find(pred) {
        const out = []
        walk(probe.Window.window.contentItem, pred, out)
        return out
    }
    // The Charts body on screen (the stage rebuilds it on a theme or mode switch: found afresh each step).
    function onScreen(item, top) {
        let i = item
        while (i && i !== top) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function chartBody() {
        const bs = find(it => it.objectName === "chBody" && it.visible && it.width > 0)
        return bs.length ? bs[0] : null
    }
    // This shot's traces: [{ key, curve, swatch }] — the curve's colour as handed to the painter (the
    // row's own line layer) and the label swatch beside its name.
    function traces(b) {
        const rows = []
        walk(b, it => /^chTrace:./.test(it.objectName) && it.objectName !== "chTrace:overlay" && onScreen(it, b), rows)
        return rows.map(r => {
            const key = r.objectName.slice(8)
            const plot = (r.children || []).find(k => k.layers !== undefined && k.xOf !== undefined)
            const line = plot ? plot.layers.find(l => l.k === "line" && l.z === 3) : null
            const lab = []
            walk(r, it => it.objectName === "chTraceLabel:" + key, lab)
            const sw = lab.length && lab[0].children.length ? lab[0].children[0] : null
            return { key: key, curve: line ? String(line.color).toUpperCase() : "", swatch: sw ? String(sw.color).toUpperCase() : "" }
        })
    }
    function curveColour(b, key) {
        const t = traces(b).find(x => x.key === key)
        return t ? t.curve : ""
    }
    function same(a, b) { return String(a).toUpperCase() === String(b).toUpperCase() }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    property int startTheme: 0
    property string key: ""
    property int step: 0
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const s = probe.step
            if (s === 1) {
                probe.startTheme = Theme.themeIndex
                appSettings.metricColors  = ({})
                appSettings.metricPalette = ({})
                // Theme-level resolution, no chart needed.
                probe.check(Theme.metricColorNames.length === 12, "twelve palette names reach QML")
                probe.check(Theme.metricColorName("clubheadSpeed") === "cornflower", "clubheadSpeed resolves to cornflower")
                probe.check(same(Theme.metricColor("clubheadSpeed"), Theme.paletteDefault("cornflower")),
                            "…and draws as this theme's cornflower " + Theme.metricColor("clubheadSpeed"))
                const u1 = Theme.metricColorName("not.a.metric"), u2 = Theme.metricColorName("not.a.metric")
                probe.check(u1 !== "" && u1 === u2, "an uncatalogued key gets one stable name (" + u1 + ")")
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                navController.navigate(2)
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (s === 2) {
                probe.check(sessionReviewController.reviewActive, "the session loaded for review")
                let picked = false
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) { SessionMode.enterReplay(o.shotId, o.swingDir); picked = true }
                }
                probe.check(picked, "shot " + probe.ordinal + " is in the carousel")
            } else if (s === 3) {
                const b = probe.chartBody()
                probe.check(b !== null, "the Charts panel is up")
                if (!b) { probe.finish(); return }
                b.tab = "shot"
                // one trace per metric (small multiples): a reader's saved "overlaid" would draw them on one
                // plot. Chosen through the body (saved in this run's COPY of the settings), so the stage
                // rebuilding the panel on the theme switch below keeps it.
                if (b.overlay) b.choose("overlay", false)
            } else if (s === 4) {
                const b = probe.chartBody()
                const curves = b ? probe.traces(b) : []
                // the session's curves are read off the GUI thread: give the panel a few ticks to publish
                if (curves.length === 0 && probe.waits < 8) {
                    probe.waits += 1
                    probe.log("waiting for traces: " + (b ? "register " + b.register + ", view " + b.view + ", shot "
                              + (b.tShot && b.tShot.available ? b.tShot.ordinal + " with " + (b.tShot.rows || []).length + " rows" : "not available")
                              + (b.busy ? ", busy" : "") : "no body"))
                    probe.step -= 1
                    return
                }
                probe.check(curves.length > 0, "This shot draws traces (" + curves.length + ")")
                if (curves.length === 0) { probe.finish(); return }
                let bad = []
                for (let i = 0; i < curves.length; ++i)
                    if (!same(curves[i].curve, Theme.metricColor(curves[i].key)) || !same(curves[i].swatch, Theme.metricColor(curves[i].key)))
                        bad.push(curves[i].key)
                probe.check(bad.length === 0, "every one of " + curves.length + " traces (curve and swatch) is its metric's colour"
                            + (bad.length ? " — not: " + bad.join(",") : ""))
                const keys = curves.map(c => c.key)
                probe.log("traces: " + curves.map(c => c.key + "=" + Theme.metricColorName(c.key)
                                                     + " " + c.curve).join("  "))
                probe.key = keys.indexOf("clubheadSpeed") >= 0 ? "clubheadSpeed" : keys[0]
                // Re-point the metric at another name; the chart must follow.
                const other = Theme.metricColorName(probe.key) === "gold" ? "violet" : "gold"
                appSettings.metricColors = ({ [probe.key]: other })
                probe._other = other
            } else if (s === 5) {
                probe.check(same(probe.curveColour(probe.chartBody(), probe.key), Theme.paletteDefault(probe._other)),
                            "re-pointing " + probe.key + " at " + probe._other + " repaints its curve ("
                            + probe.curveColour(probe.chartBody(), probe.key) + ")")
                // Retune that name in this theme; the curve must follow again.
                appSettings.metricPalette = ({ [Theme.paletteKey(probe._other)]: "#FF00FF" })
            } else if (s === 6) {
                probe.check(same(probe.curveColour(probe.chartBody(), probe.key), "#FF00FF"),
                            "retuning " + probe._other + " in " + Theme.aesthetic + " repaints the curve ("
                            + probe.curveColour(probe.chartBody(), probe.key) + ")")
                appSettings.metricColors  = ({})
                appSettings.metricPalette = ({})
                // Another theme: same NAME, that theme's value.
                probe._before = probe.curveColour(probe.chartBody(), probe.key)
                Theme.themeIndex = (Theme.themeIndex + 3) % Theme.themeCount   // other aesthetic AND other mode
            } else if (s === 7) {
                const now = probe.curveColour(probe.chartBody(), probe.key)
                probe.check(same(now, Theme.metricColor(probe.key)) && !same(now, probe._before),
                            "switching to " + Theme.aesthetic + (Theme.dark ? " dark" : " light") + " repaints "
                            + probe.key + " " + probe._before + " → " + now)
                Theme.themeIndex = probe.startTheme
                // Settings → Diagnostic Model → this metric.
                MetricRoute.open(probe.key)                           // a metric tile's own click path
                appSettings.metricColors = ({ [probe.key]: "orchid" })
            } else if (s === 8) {
                // Not filtered on `visible`: offscreen the settings page is too narrow to show the
                // inspector pane at all, so visibility is an artefact here — the row's DATA is not.
                const rows = probe.find(it => it.objectName === "inspectorPlotColour" && it.metricKey === probe.key)
                probe.log("plot-colour rows: " + probe.find(it => it.objectName === "inspectorPlotColour")
                                                    .map(r => "'" + r.metricKey + "'").join(","))
                probe.check(rows.length === 1, "the inspector holds a plot-colour row for " + probe.key)
                if (rows.length === 1) {
                    probe.check(rows[0].metricKey === probe.key && rows[0].current === "orchid" && rows[0].overridden,
                                "…reading the user's orchid, marked as theirs")
                    const sw = []
                    probe.walk(rows[0], it => it.objectName && it.objectName.indexOf("metricSwatch_") === 0, sw)
                    probe.check(sw.length === 12, "…with twelve swatches (" + sw.length + ")")
                    rows[0].pick(Theme.metricDefaultColorName(probe.key))
                    probe.check(appSettings.metricColors[probe.key] === undefined,
                                "picking the catalogue's own name clears the override")
                }
                const tiles = probe.find(it => it.objectName && it.objectName.indexOf("paletteTile_") === 0)
                probe.check(tiles.length === 12, "Appearance offers twelve palette tiles (" + tiles.length + ")")
                // A measure is drawn in its metric's colour: open one that reads clubheadSpeed.
                const dm = probe.find(it => typeof it.showMeasure === "function")
                probe.check(dm.length === 1, "the Diagnostic Model panel is up")
                if (dm.length === 1) dm[0].showMeasure("m_clubheadSpeedImpact")
            } else if (s === 9) {
                const rows = probe.find(it => it.objectName === "inspectorPlotColour")
                probe.check(rows.length === 1 && rows[0].metricKey === "clubheadSpeed"
                            && rows[0].current === "cornflower",
                            "the measure m_clubheadSpeedImpact shows its metric's colour ("
                            + (rows.length ? rows[0].metricKey + " " + rows[0].current : "none") + ")")
                probe.finish()
            }
        }
    }
    property string _other: ""
    property int waits: 0
    property string _before: ""
}
