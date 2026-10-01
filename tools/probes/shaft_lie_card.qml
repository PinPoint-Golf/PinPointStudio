// shaft_lie_card.qml — does the "Club & speed" summary squeeze shaft lie onto shaft lean's card?
//
//   QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
//     build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
//     --probe-qml "$PWD/tools/probes/shaft_lie_card.qml" \
//     --probe-swing <swing dir whose document carries shaftLie> 2>&1 | grep LIEPROBE | awk '!seen[$0]++'
//
// Loads the swing exactly as plumb_bob_chart.qml does (session → screen → replay focus →
// analysisDetail), then feeds the "Club & speed" group's series to a PRIVATE PpChartSummary —
// the real component, the same code the chart panel instantiates — and walks its cards: each
// card's name and every tile's label/value. Reads:
//   LIEPROBE card 'Shaft lean' tiles=5: @ IMPACT=… | Δ P1→P7=… | LIE @ ADDRESS=… | LIE @ IMPACT=… | Δ LIE=…
//   LIEPROBE VERDICT: PASS — one card, shaft lie's three tiles on shaft lean's, no card of its own
// `⛔` marks a failure. ⚠ THE PROBE PATH MUST BE ABSOLUTE (see ks_overlay_chart.md).

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
    readonly property string groupName:  probe._arg("--probe-group", "Club & speed")
    readonly property int    sessionType: parseInt(probe._arg("--probe-session-type", "1"))
    readonly property int    stepMs:     parseInt(probe._arg("--probe-step-ms", "2500"))

    function w(s) { console.warn("LIEPROBE " + s) }
    function safe(name, f) { try { f() } catch (e) { probe.w("⛔ step " + name + " threw: " + e) } }

    ChartMetrics { id: cm }

    property var  series:   []
    property var  phases:   []
    property real axStartUs: 0
    property real axEndUs:   0
    property real impactUs:  -1
    property int  shotId:    -1
    property int  step:      0
    property bool pass:      false

    Instantiator {
        id: rows
        model: { try { return sessionReviewController.activeShots } catch (e) { return null } }
        delegate: QtObject {
            property int    sid:  (model && model.shotId   !== undefined) ? model.shotId   : -1
            property string sdir: (model && model.swingDir !== undefined) ? model.swingDir : ""
        }
    }

    // The component under test, fed the group's series only — what the chart panel hands it
    // after a preset is applied. Width matters: the card grid wraps on it.
    PpChartSummary {
        id: summary
        width: 1200
        visible: true
        series:   probe.groupSeries
        phases:   probe.phases
        startUs:  probe.axStartUs
        endUs:    probe.axEndUs
        impactUs: probe.impactUs
        segmentName: probe.groupName
    }
    property var groupSeries: []

    function stepLoad() {
        probe.w("═══ 1. LOAD " + probe.sessionDir + " ═══")
        sessionReviewController.loadSession(probe.sessionDir)
        probe.w("reviewActive=" + sessionReviewController.reviewActive + " shots=" + sessionReviewController.activeShotCount)
        navController.navigate(probe.sessionType + 1)
    }
    function stepFocus() {
        probe.w("═══ 2. FOCUS " + probe.swingDir + " ═══")
        probe.shotId = -1
        for (var i = 0; i < rows.count; ++i) {
            var o = rows.objectAt(i)
            if (o && o.sdir === probe.swingDir) { probe.shotId = o.sid; break }
        }
        if (probe.shotId < 0) { probe.w("NOTE: no review row matches; using shotId 0"); probe.shotId = 0 }
        SessionMode.enterReplay(probe.shotId, probe.swingDir, false)
    }
    function stepCollect() {
        probe.w("═══ 3. COLLECT ═══")
        var d = shotReplay.analysisDetail
        if (!d || !d.series || d.series.length === 0) {
            probe.w("shotReplay gave no series — document reader fallback")
            d = sessionReviewController.activeShots.analysisDetailForSwingDir(probe.swingDir)
        }
        if (!d || !d.series || d.series.length === 0) { probe.w("⛔ no analysisDetail.series"); return }
        probe.series = d.series
        probe.phases = d.phases || []
        probe.axStartUs = shotReplay.startUs; probe.axEndUs = shotReplay.endUs; probe.impactUs = shotReplay.impactUs
        if (!(probe.axEndUs > probe.axStartUs)) {
            var lo = Infinity, hi = -Infinity
            for (var i = 0; i < probe.series.length; ++i) {
                var t = probe.series[i].t_us || []
                if (t.length) { lo = Math.min(lo, t[0]); hi = Math.max(hi, t[t.length - 1]) }
            }
            probe.axStartUs = lo; probe.axEndUs = hi
            for (var p = 0; p < probe.phases.length; ++p) if (probe.phases[p].phase === 5) probe.impactUs = probe.phases[p].t_us
        }
        probe.w("series=" + probe.series.length + " phases=" + probe.phases.length
                + " axis=[" + probe.axStartUs + "," + probe.axEndUs + "] impact=" + probe.impactUs)
        var keys = []
        for (var k = 0; k < probe.series.length; ++k) keys.push(probe.series[k].key)
        probe.w("has shaftLie=" + (keys.indexOf("shaftLie") >= 0) + " has impactShaftLean=" + (keys.indexOf("impactShaftLean") >= 0))
    }
    function stepGroup() {
        probe.w("═══ 4. GROUP '" + probe.groupName + "' ═══")
        var groups = cm.seriesGroups(probe.series), want = null
        for (var i = 0; i < groups.length; ++i) if (groups[i].group === probe.groupName) want = groups[i]
        if (!want) { probe.w("⛔ group not offered"); return }
        probe.w("group keys = " + want.keys.join(", "))
        var out = []
        for (var j = 0; j < probe.series.length; ++j)
            if (want.keys.indexOf(probe.series[j].key) >= 0) out.push(probe.series[j])
        probe.groupSeries = out
        probe.w("cardSpec shaftLie.mergeInto = '" + cm.cardSpecFor("shaftLie").mergeInto + "'")
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
    function stepReport() {
        probe.w("═══ 5. CARDS ═══")
        var cards = []
        probe.walk(summary, cards)
        probe.w("cards drawn = " + cards.length)
        var lean = null, lieOwn = null
        for (var i = 0; i < cards.length; ++i) {
            var c = cards[i], parts = []
            for (var j = 0; j < c.tiles.length; ++j)
                parts.push(c.tiles[j].label + "=" + c.tiles[j].text + (c.tiles[j].unit ? " " + c.tiles[j].unit : ""))
            probe.w("card '" + c.nm + "' key=" + c.modelData.key + " tiles=" + c.tiles.length + ": " + parts.join(" | "))
            if (c.modelData.key === "impactShaftLean") lean = c
            if (c.modelData.key === "shaftLie") lieOwn = c
        }
        var labels = []
        if (lean) for (var k = 0; k < lean.tiles.length; ++k) labels.push(lean.tiles[k].label)
        var ok = !!lean && !lieOwn
              && labels.indexOf("LIE @ ADDRESS") >= 0 && labels.indexOf("LIE @ IMPACT") >= 0 && labels.indexOf("Δ LIE") >= 0
        var measured = 0
        if (lean) for (var m = 0; m < lean.tiles.length; ++m) if (lean.tiles[m].ok) ++measured
        probe.pass = ok
        probe.w("VERDICT: " + (ok ? "PASS" : "⛔ FAIL") + " — " + (lean ? "shaft lean card has " + lean.tiles.length + " tiles, "
                + measured + " measured" : "no shaft lean card") + (lieOwn ? "; ⛔ shaft lie ALSO drew its own card" : ""))
    }

    Timer {
        interval: probe.stepMs; running: true; repeat: true
        onTriggered: {
            ++probe.step
            switch (probe.step) {
            case 1: probe.safe("load",    probe.stepLoad);    break
            case 2: probe.safe("focus",   probe.stepFocus);   break
            case 3: probe.safe("collect", probe.stepCollect); break
            case 4: probe.safe("group",   probe.stepGroup);   break
            case 5: probe.safe("report",  probe.stepReport);  break
            default: probe.w("done"); Qt.quit()
            }
        }
    }
    Timer {
        interval: probe.stepMs * 8 + 20000; running: true; repeat: false
        onTriggered: { probe.w("⛔ WATCHDOG — quitting at step " + probe.step); Qt.quit() }
    }
}
