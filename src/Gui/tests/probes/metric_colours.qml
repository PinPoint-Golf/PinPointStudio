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
// Loads a session, opens a swing in Replay, finds the metric chart, and checks every curve is
// Theme.metricColor(its key) — then re-points a metric, retunes a palette name and switches
// theme, and checks the chart's curves FOLLOW each change (a binding that read the colour once
// would pass the first check and fail these). Then opens the metric in Settings → Diagnostic Model
// and checks the inspector's plot-colour row, and that Appearance offers the twelve tiles.
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
    function charts() {
        return find(it => it._curves !== undefined && it._color !== undefined && it._plottable !== undefined)
    }
    function curveColour(chart, key) {
        const c = chart._curves
        for (let i = 0; i < c.length; ++i) if (c[i].key === key) return String(c[i].color).toUpperCase()
        return ""
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
    property var chart: null
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
                const cs = probe.charts().filter(c => c._curves.length > 0)
                probe.check(cs.length > 0, "a metric chart with curves is up (" + cs.length + ")")
                if (cs.length === 0) { probe.finish(); return }
                probe.chart = cs[0]
                const curves = probe.chart._curves
                let bad = []
                for (let i = 0; i < curves.length; ++i)
                    if (!same(curves[i].color, Theme.metricColor(curves[i].key))) bad.push(curves[i].key)
                probe.check(bad.length === 0, "every one of " + curves.length + " curves is its metric's colour"
                            + (bad.length ? " — not: " + bad.join(",") : ""))
                const keys = curves.map(c => c.key)
                probe.log("curves: " + curves.map(c => c.key + "=" + Theme.metricColorName(c.key)
                                                     + " " + String(c.color).toUpperCase()).join("  "))
                probe.key = keys.indexOf("clubheadSpeed") >= 0 ? "clubheadSpeed" : keys[0]
                // Re-point the metric at another name; the chart must follow.
                const other = Theme.metricColorName(probe.key) === "gold" ? "violet" : "gold"
                appSettings.metricColors = ({ [probe.key]: other })
                probe._other = other
            } else if (s === 4) {
                probe.check(same(probe.curveColour(probe.chart, probe.key), Theme.paletteDefault(probe._other)),
                            "re-pointing " + probe.key + " at " + probe._other + " repaints its curve ("
                            + probe.curveColour(probe.chart, probe.key) + ")")
                // Retune that name in this theme; the curve must follow again.
                appSettings.metricPalette = ({ [Theme.paletteKey(probe._other)]: "#FF00FF" })
            } else if (s === 5) {
                probe.check(same(probe.curveColour(probe.chart, probe.key), "#FF00FF"),
                            "retuning " + probe._other + " in " + Theme.aesthetic + " repaints the curve ("
                            + probe.curveColour(probe.chart, probe.key) + ")")
                appSettings.metricColors  = ({})
                appSettings.metricPalette = ({})
                // Another theme: same NAME, that theme's value.
                probe._before = probe.curveColour(probe.chart, probe.key)
                Theme.themeIndex = (Theme.themeIndex + 3) % Theme.themeCount   // other aesthetic AND other mode
            } else if (s === 6) {
                const now = probe.curveColour(probe.chart, probe.key)
                probe.check(same(now, Theme.metricColor(probe.key)) && !same(now, probe._before),
                            "switching to " + Theme.aesthetic + (Theme.dark ? " dark" : " light") + " repaints "
                            + probe.key + " " + probe._before + " → " + now)
                Theme.themeIndex = probe.startTheme
                // Settings → Diagnostic Model → this metric.
                MetricRoute.open(probe.key)                           // a metric tile's own click path
                appSettings.metricColors = ({ [probe.key]: "orchid" })
            } else if (s === 7) {
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
            } else if (s === 8) {
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
    property string _before: ""
}
