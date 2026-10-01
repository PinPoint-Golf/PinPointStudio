// instant_cards.qml — which metrics a preset DRAWS and which it only CARDS (MetricCardSpec::drawsCurve).
//
//   QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
//     build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
//     --probe-qml "$PWD/tools/probes/instant_cards.qml" \
//     --probe-swing <swing dir> [--probe-presets "Club & speed;Club delivery;All"] 2>&1 | grep ICPROBE | awk '!seen[$0]++'
//
// Loads the swing as plumb_bob_chart.qml does, feeds its series to a PRIVATE PpMetricChart
// (sessionType -1: persists nothing), applies each preset and prints: the legend (curves offered),
// the visible curves, and the summary cards with their tiles. Reads, per preset:
//   ICPROBE [Club & speed] legend: clubheadSpeed, handSpeed, lagAngle
//   ICPROBE [Club & speed] cards: Club speed, Hand speed, Lag, Shaft lean(5 tiles: …)
// and a VERDICT per preset: an instant-only metric (cm.drawsCurve false) must not be in the legend
// and must have a card; a series with no curve but a phase sample must have a card.
// ⚠ THE PROBE PATH MUST BE ABSOLUTE (ks_overlay_chart.md).

import QtQml
import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function _arg(name, dflt) {
        var i = Qt.application.arguments.indexOf(name)
        return (i >= 0 && i + 1 < Qt.application.arguments.length) ? Qt.application.arguments[i + 1] : dflt
    }
    readonly property string swingDir:   probe._arg("--probe-swing", "")
    readonly property string sessionDir: probe.swingDir.substring(0, probe.swingDir.lastIndexOf("/"))
    readonly property var    presets:    probe._arg("--probe-presets", "Club & speed;Club delivery;All").split(";")
    readonly property int    sessionType: parseInt(probe._arg("--probe-session-type", "1"))
    readonly property int    stepMs:     parseInt(probe._arg("--probe-step-ms", "2500"))

    function w(s) { console.warn("ICPROBE " + s) }
    function safe(name, f) { try { f() } catch (e) { probe.w("⛔ step " + name + " threw: " + e) } }

    ChartMetrics { id: cm }

    property var  series:    []
    property var  phases:    []
    property real axStartUs: 0
    property real axEndUs:   0
    property real impactUs:  -1
    property int  shotId:    -1
    property int  step:      0
    property int  presetIx:  0
    property int  failures:  0

    Instantiator {
        id: rows
        model: { try { return sessionReviewController.activeShots } catch (e) { return null } }
        delegate: QtObject {
            property int    sid:  (model && model.shotId   !== undefined) ? model.shotId   : -1
            property string sdir: (model && model.swingDir !== undefined) ? model.swingDir : ""
        }
    }

    PpMetricChart {
        id: chart
        width: 1400; height: 900
        visible: true
        sessionType: -1
        seriesList: probe.series
        phases:     probe.phases
        startUs:    probe.axStartUs
        endUs:      probe.axEndUs
        impactUs:   probe.impactUs
        playheadUs: probe.impactUs
        showPlayhead: false
        seekable:   false
    }

    function stepLoad() {
        probe.w("═══ 1. LOAD " + probe.sessionDir + " ═══")
        sessionReviewController.loadSession(probe.sessionDir)
        navController.navigate(probe.sessionType + 1)
    }
    function stepFocus() {
        probe.w("═══ 2. FOCUS " + probe.swingDir + " ═══")
        probe.shotId = -1
        for (var i = 0; i < rows.count; ++i) {
            var o = rows.objectAt(i)
            if (o && o.sdir === probe.swingDir) { probe.shotId = o.sid; break }
        }
        if (probe.shotId < 0) probe.shotId = 0
        SessionMode.enterReplay(probe.shotId, probe.swingDir, false)
    }
    function stepCollect() {
        probe.w("═══ 3. COLLECT ═══")
        var d = shotReplay.analysisDetail
        if (!d || !d.series || d.series.length === 0)
            d = sessionReviewController.activeShots.analysisDetailForSwingDir(probe.swingDir)
        if (!d || !d.series || d.series.length === 0) { probe.w("⛔ no analysisDetail.series"); return }
        probe.series = d.series
        probe.phases = d.phases || []
        probe.axStartUs = shotReplay.startUs; probe.axEndUs = shotReplay.endUs; probe.impactUs = shotReplay.impactUs
        probe.w("series=" + probe.series.length + " curves=" + chart._curves.length
                + " plottable=" + chart._plottable.length + " groups=" + chart._groups.length)
        var instant = []
        for (var i = 0; i < chart._curves.length; ++i)
            if (!cm.drawsCurve(chart._curves[i].key)) instant.push(chart._curves[i].key)
        probe.w("instant-only curves on this swing: " + instant.join(", "))
        // --probe-inspect key[,key]: why a reading at P7 is or is not measured.
        var insp = probe._arg("--probe-inspect", "")
        if (insp !== "") {
            var want = insp.split(",")
            for (var q = 0; q < chart._curves.length; ++q) {
                var cs = chart._curves[q]
                if (want.indexOf(cs.key) < 0) continue
                var p7 = -1
                for (var pp = 0; pp < probe.phases.length; ++pp) if (probe.phases[pp].phase === 5) p7 = probe.phases[pp].t_us
                var best = -1, bd = 1e18
                for (var si = 0; si < cs.t_us.length; ++si) { var dd = Math.abs(cs.t_us[si] - p7); if (dd < bd) { bd = dd; best = si } }
                probe.w("inspect " + cs.key + ": domain [" + cs.validFromUs + ", " + cs.validToUs + "] P7=" + p7
                        + " n=" + cs.t_us.length + " t[0]=" + cs.t_us[0] + " t[n-1]=" + cs.t_us[cs.t_us.length - 1]
                        + " nearest=" + (best >= 0 ? cs.t_us[best] : "?") + " d=" + bd
                        + " value=" + (best >= 0 ? cs.value[best] : "?")
                        + " valid=" + (cs.valid && cs.valid.length > best ? cs.valid[best] : "(none)")
                        + " validLen=" + (cs.valid ? cs.valid.length : 0)
                        + " phaseSamples=" + JSON.stringify(cs.phaseSamples))
            }
        }
    }
    function walk(item, found) {
        if (!item) return
        var kids = item.children || []
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (c.tiles !== undefined && c.nm !== undefined) found.push(c)
            probe.walk(c, found)
        }
    }
    function stepPreset() {
        var name = probe.presets[probe.presetIx]
        probe.w("═══ PRESET '" + name + "' ═══")
        chart._applyPreset(name, false)
        var leg = [], vis = [], cardKeys = []
        for (var i = 0; i < chart._legendSeries.length; ++i) leg.push(chart._legendSeries[i].key)
        for (var j = 0; j < chart._visible.length; ++j) vis.push(chart._visible[j].key)
        for (var k = 0; k < chart._cardSeries.length; ++k) cardKeys.push(chart._cardSeries[k].key)
        probe.w("[" + name + "] legend: " + (leg.length ? leg.join(", ") : "(none)"))
        probe.w("[" + name + "] visible curves: " + (vis.length ? vis.join(", ") : "(none)") + "   plots=" + chart._plots.length)
        probe.w("[" + name + "] card series: " + (cardKeys.length ? cardKeys.join(", ") : "(none)"))
        var cards = []
        probe.walk(chart, cards)
        for (var c = 0; c < cards.length; ++c) {
            var cd = cards[c], parts = []
            for (var t = 0; t < cd.tiles.length; ++t)
                parts.push(cd.tiles[t].label + "=" + cd.tiles[t].text)
            probe.w("[" + name + "] card '" + cd.nm + "' key=" + cd.modelData.key + " curve=" + cd.hasCurve
                    + " tiles=" + cd.tiles.length + ": " + parts.join(" | "))
        }
        // Verdict: nothing instant-only in the legend; every instant-only / sample-only member carded.
        var bad = []
        for (var l = 0; l < leg.length; ++l) if (!cm.drawsCurve(leg[l])) bad.push("legend has instant-only " + leg[l])
        var keys = name === "All" ? null : chart._keysFor(name)
        if (keys) for (var m = 0; m < keys.length; ++m) {
            var key = keys[m], s = null
            for (var n = 0; n < probe.series.length; ++n) if (probe.series[n].key === key) s = probe.series[n]
            if (!s) continue
            var hasCurve = s.t_us && s.t_us.length > 1, hasPs = s.phaseSamples && s.phaseSamples.length > 0
            var expectCard = hasCurve ? true : hasPs
            if (expectCard && cardKeys.indexOf(key) < 0) bad.push("no card for " + key)
        }
        if (bad.length) ++probe.failures
        probe.w("[" + name + "] VERDICT: " + (bad.length ? "⛔ FAIL — " + bad.join("; ") : "PASS"))
    }

    Timer {
        interval: probe.stepMs; running: true; repeat: true
        onTriggered: {
            ++probe.step
            switch (probe.step) {
            case 1: probe.safe("load",    probe.stepLoad);    break
            case 2: probe.safe("focus",   probe.stepFocus);   break
            case 3: probe.safe("collect", probe.stepCollect); break
            default:
                if (probe.presetIx < probe.presets.length) { probe.safe("preset", probe.stepPreset); ++probe.presetIx }
                else { probe.w("done — " + probe.failures + " failing preset(s)"); Qt.quit() }
            }
        }
    }
    Timer {
        interval: probe.stepMs * 12 + 20000; running: true; repeat: false
        onTriggered: { probe.w("⛔ WATCHDOG — quitting at step " + probe.step); Qt.quit() }
    }
}
