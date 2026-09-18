import QtQml
import QtQuick
import PinPointStudio

// KSPROBE — does the "Kinematic sequence" preset draw the sequence ON the plot?
// Loads a session, focuses a swing, drives its own PpMetricChart (sessionType −1: no persisted
// writes), applies the preset, then walks the chart for the overlay items by objectName and
// reports each one's visibility, position and text, beside ChartMetrics.sequenceOverlay's answer.
Item {
    id: probe
    anchors.fill: parent
    function _arg(name, dflt) {
        var i = Qt.application.arguments.indexOf(name)
        return (i >= 0 && i + 1 < Qt.application.arguments.length) ? Qt.application.arguments[i + 1] : dflt
    }
    readonly property string swingDir: probe._arg("--probe-swing", "/mnt/swingdata/corpus/swings/2026-07-04_Mark-Liversedge_Wrist_01/swing_0001")
    readonly property string sessionDir: probe.swingDir.substring(0, probe.swingDir.lastIndexOf("/"))
    readonly property int    stepMs: parseInt(probe._arg("--probe-step-ms", "2500"))
    function w(s) { console.warn("KSPROBE " + s) }
    function ms(us) { var n = Number(us); return isFinite(n) ? (n / 1000).toFixed(1) + "ms" : String(us) }

    ChartMetrics { id: cm }
    property var detail: ({})
    property var series: []
    property var phases: []
    property real axStartUs: 0
    property real axEndUs: 0
    property real impactUs: 0
    property int shotId: -1

    PpMetricChart {
        id: chart
        width: 1400; height: 900
        visible: true
        sessionType: -1
        splitMode: probe._arg("--probe-split", "1") === "1"
        seriesList: probe.series
        phases: probe.phases
        startUs: probe.axStartUs
        endUs: probe.axEndUs
        impactUs: probe.impactUs
        playheadUs: probe.impactUs
        showPlayhead: false
        seekable: false
        kinematicSequence: (probe.detail && probe.detail.kinematicSequence) ? probe.detail.kinematicSequence : null
    }
    Instantiator {
        id: rows
        model: { try { return sessionReviewController.activeShots } catch (e) { return null } }
        delegate: QtObject {
            property int sid: (model && model.shotId !== undefined) ? model.shotId : -1
            property string sdir: (model && model.swingDir !== undefined) ? model.swingDir : ""
        }
    }
    function stepLoadSession() {
        sessionReviewController.loadSession(probe.sessionDir)
        probe.w("session loaded: shots = " + sessionReviewController.activeShotCount)
        navController.navigate(2)
    }
    function stepFocusSwing() {
        probe.shotId = 0
        for (var i = 0; i < rows.count; ++i) { var o = rows.objectAt(i); if (o && o.sdir === probe.swingDir) { probe.shotId = o.sid; break } }
        SessionMode.enterReplay(probe.shotId, probe.swingDir, false)
        probe.w("focused shotId " + probe.shotId)
    }
    function stepCollectDetail() {
        var d = shotReplay.analysisDetail
        if (d && d.series && d.series.length > 0) {
            probe.detail = d; probe.axStartUs = shotReplay.startUs; probe.axEndUs = shotReplay.endUs; probe.impactUs = shotReplay.impactUs
        } else {
            d = sessionReviewController.activeShots.analysisDetailForSwingDir(probe.swingDir)
            probe.detail = d; probe.axStartUs = 0; probe.axEndUs = 0
            var ps = d.phases || []; for (var i = 0; i < ps.length; ++i) if (ps[i].phase === 5) probe.impactUs = ps[i].t_us
        }
        probe.series = probe.detail.series; probe.phases = probe.detail.phases || []
        var ks = probe.detail.kinematicSequence
        probe.w("series " + probe.series.length + "  impact " + probe.ms(probe.impactUs)
                + "  ks nodes " + (ks && ks.nodes ? ks.nodes.length : "NONE") + "  order " + (ks ? JSON.stringify(ks.order) : "?"))
        if (ks && ks.nodes) for (var j = 0; j < ks.nodes.length; ++j) {
            var n = ks.nodes[j]
            probe.w("  node " + n.segment + " placed=" + n.placed + " before=" + Number(n.beforeImpactMs).toFixed(0)
                    + " noEarlier=" + (n.peakNoEarlierThanMs !== undefined ? Number(n.peakNoEarlierThanMs).toFixed(0) : "-")
                    + " noLater=" + (n.peakNoLaterThanMs !== undefined ? Number(n.peakNoLaterThanMs).toFixed(0) : "-"))
        }
        var ov = cm.sequenceOverlay(ks || {})
        probe.w("overlay peaks=" + ov.peaks.length + " gaps=" + ov.gaps.length + " chain='" + ov.chainText + "'")
        for (var k = 0; k < ov.peaks.length; ++k) probe.w("  peak " + ov.peaks[k].seriesKey + " '" + ov.peaks[k].text + "' placed=" + ov.peaks[k].placed + " t=" + probe.ms(ov.peaks[k].tPeakUs) + " v=" + Number(ov.peaks[k].peakDps).toFixed(0))
        for (var g = 0; g < ov.gaps.length; ++g) probe.w("  gap '" + ov.gaps[g].text + "' " + probe.ms(ov.gaps[g].fromUs) + " → " + probe.ms(ov.gaps[g].toUs))
    }
    function stepApplyPreset() {
        chart._applyPreset("Kinematic sequence", false)
        probe.w("chart.preset '" + chart.preset + "'  overlay bound to plots: " + (chart._sequenceOverlay ? "yes" : "NO")
                + "  view [" + probe.ms(chart.viewStartUs) + ", " + probe.ms(chart.viewEndUs) + "]")
    }
    function stepWalk() {
        var found = 0, seen = 0
        function walk(it, depth) {
            if (!it || depth > 40 || seen > 20000) return
            seen++
            try {
                var on = it.objectName || ""
                if (on.indexOf("sequencePeak:") === 0 || on.indexOf("sequenceBound:") === 0) {
                    found++
                    var txt = ""
                    var ch = it.children
                    for (var c = 0; c < ch.length; ++c) if (ch[c].objectName === "sequencePeakText" || ch[c].objectName === "sequenceBoundText") txt = ch[c].text
                    var p = it.mapToItem(null, 0, 0)
                    probe.w("  DRAWN " + on + " visible=" + it.visible + " x=" + it.x.toFixed(0) + " w=" + it.width.toFixed(0)
                            + (on.indexOf("sequencePeak:") === 0 ? " ring@(" + it.cx.toFixed(0) + "," + it.cy.toFixed(0) + ") σpx=" + it.sig.toFixed(1) : "")
                            + " text='" + txt + "' placed=" + (it.modelData.placed !== undefined ? it.modelData.placed : "?") + " col=" + it.col)
                } else if (on === "sequenceGapText") {
                    found++; probe.w("  DRAWN gap text '" + it.text + "' visible=" + it.visible + " parentW=" + it.parent.width.toFixed(0))
                } else if (on === "sequenceVerdict") {
                    probe.w("  STRIP verdict: '" + it.text + "' visible=" + it.visible)
                } else if (on === "sequenceStripHeader") {
                    probe.w("  STRIP header: '" + it.text + "'")
                } else if (on.indexOf("sequenceChip:") === 0) {
                    probe.w("  ⛔ a chip is still drawn: " + on)
                }
            } catch (e) {}
            var kids = null
            try { kids = it.children } catch (e) { return }
            if (!kids) return
            for (var i = 0; i < kids.length; ++i) walk(kids[i], depth + 1)
        }
        walk(chart, 0)
        probe.w("walked " + seen + " items, overlay items found " + found)
    }
    property int step: 0
    function safe(name, fn) { try { fn() } catch (e) { probe.w("⛔ THREW in " + name + ": " + e + (e && e.stack ? "\n" + e.stack : "")) } }
    Timer {
        interval: probe.stepMs; running: true; repeat: true
        onTriggered: {
            probe.step++
            switch (probe.step) {
            case 1: probe.safe("loadSession", probe.stepLoadSession); break
            case 2: probe.safe("focusSwing", probe.stepFocusSwing); break
            case 3: probe.safe("collectDetail", probe.stepCollectDetail); break
            case 4: probe.safe("applyPreset", probe.stepApplyPreset); break
            case 5: probe.safe("walk", probe.stepWalk); break
            default: probe.w("DONE"); Qt.quit()
            }
        }
    }
    Timer { interval: probe.stepMs * 8 + 20000; running: true; repeat: false; onTriggered: { probe.w("⛔ WATCHDOG at step " + probe.step); Qt.quit() } }
    Component.onCompleted: probe.w("KS OVERLAY PROBE swing=" + probe.swingDir)
}
