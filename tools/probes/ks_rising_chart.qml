import QtQml
import QtQuick
import PinPointStudio

// KSRISE — the kinematic sequence when the BODY has not peaked by impact.
//
// Sibling of ks_overlay_chart.qml and deliberately SYNTHETIC: no session, no swing directory, no
// disk. The paired trunk route (faceOn+dtl, kinematic_sequence_design.md §5.2) is new and there is
// no captured swing that carries its nodes yet, so the question this probe answers — "what does
// the chart draw, and what does the strip say, when the pelvis and chest are still accelerating at
// the ball?" — cannot be asked of a recording. It is asked of the payload shape the producer
// contracts to emit, driven through the real PpMetricChart / PpChartPlot / PpSequenceStrip inside
// the real app context.
//
// Two payloads, in sequence:
//   RISING — arm and club placed before impact, pelvis and chest unplaced with
//            peakNoEarlierThanMs 0 and no peakNoLaterThanMs. Expect: an open chevron
//            (`sequenceRising:<segment>`) on each trunk curve at the impact edge, labelled
//            "rising"; NO `sequencePeak:pelvis` / `sequencePeak:thorax` ring anywhere; the strip's
//            one line reading the chain then "arms and club peak before the body — hips and chest
//            still speeding up at impact".
//   PLACED — the same route with both trunk nodes placed (a golfer whose body peaks before the
//            ball). Expect: four ordinary rings, no chevron, the proximal-to-distal verdict.
//
// Every assertion prints PASS or ⛔ FAIL and the run ends with a count, so the grep handle alone
// tells you whether it is good. ~5 s, then Qt.quit()s. ⚠ THE PROBE PATH MUST BE ABSOLUTE — a
// relative one resolves to file://tools/… , the loader logs one WARN and the app then runs for
// ever as a normal app with no probe and no quit. Hence the alarm; there is no `timeout` on this
// Mac. Every line prints twice (stderr echo + app log); the awk de-duplicates.
//
//   QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
//     perl -e 'alarm 180; exec @ARGV' -- \
//     build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
//     --probe-qml "$PWD/tools/probes/ks_rising_chart.qml" 2>&1 | grep KSRISE | awk '!seen[$0]++'
//
// See ks_overlay_chart.md for the environment notes (devBuild, no library setting, no persisted
// writes with sessionType −1). Written 2026-09-20 with kinematic_sequence_design.md §8.
Item {
    id: probe
    anchors.fill: parent

    function w(s) { console.warn("KSRISE " + s) }
    property int checks: 0
    property int fails: 0
    function expect(what, got, want) {
        probe.checks++
        if (String(got) === String(want)) probe.w("PASS  " + what + "  = '" + got + "'")
        else { probe.fails++; probe.w("⛔ FAIL " + what + "  got '" + got + "'  want '" + want + "'") }
    }

    ChartMetrics { id: cm }

    // Four synthetic °/s ramps on the catalogue's own keys — enough for the preset to be offered
    // and for each marker to find its curve's colour. Their VALUES are not the subject.
    function curve(key) {
        var t = [], v = []
        for (var i = 0; i <= 40; ++i) { t.push(i * 10000); v.push(i * 25) }
        return { key: key, label: key, unit: "°/s", t_us: t, value: v, phaseSamples: [] }
    }
    readonly property var fourCurves: [
        probe.curve("pelvisAngularSpeed"), probe.curve("thoraxAngularSpeed"),
        probe.curve("leadArmAngularSpeed"), probe.curve("clubAngularSpeed")
    ]

    // The measured shape (pair_span_turn_20260920.md §1): lead arm −115 ms, club −55 ms, pelvis and
    // chest still accelerating AT impact.
    readonly property var risingKs: ({
        impactUs: 300000,
        nodes: [
            { segment: "pelvis", placed: false, tPeakUs: 300000, beforeImpactMs: 0, peakDps: 640,
              tSigmaMs: 0, peakSigmaDps: 70, routeId: "faceOn+dtl", quality: "estimated",
              peakNoEarlierThanMs: 0 },
            { segment: "thorax", placed: false, tPeakUs: 300000, beforeImpactMs: 0, peakDps: 700,
              tSigmaMs: 0, peakSigmaDps: 60, routeId: "faceOn+dtl", quality: "estimated",
              peakNoEarlierThanMs: 0 },
            { segment: "leadArm", placed: true, tPeakUs: 185000, beforeImpactMs: 115, peakDps: 684,
              tSigmaMs: 18, peakSigmaDps: 40, routeId: "faceOn", quality: "estimated" },
            { segment: "club", placed: true, tPeakUs: 245000, beforeImpactMs: 55, peakDps: 1835,
              tSigmaMs: 10, peakSigmaDps: 60, routeId: "faceOnClub", quality: "estimated" }
        ],
        order: ["leadArm", "club"], gapsMs: [60], gainsDps: [1151],
        orderResolved: true, verdict: "partial", routeSummary: "estimated"
    })
    // The same route, both trunk nodes placed.
    readonly property var placedKs: ({
        impactUs: 300000,
        nodes: [
            { segment: "pelvis", placed: true, tPeakUs: 213000, beforeImpactMs: 87, peakDps: 477,
              tSigmaMs: 14, peakSigmaDps: 50, routeId: "faceOn+dtl", quality: "estimated" },
            { segment: "thorax", placed: true, tPeakUs: 232000, beforeImpactMs: 68, peakDps: 727,
              tSigmaMs: 16, peakSigmaDps: 60, routeId: "faceOn+dtl", quality: "estimated" },
            { segment: "leadArm", placed: true, tPeakUs: 235000, beforeImpactMs: 65, peakDps: 980,
              tSigmaMs: 18, peakSigmaDps: 40, routeId: "faceOn", quality: "estimated" },
            { segment: "club", placed: true, tPeakUs: 300000, beforeImpactMs: 0, peakDps: 2254,
              tSigmaMs: 10, peakSigmaDps: 60, routeId: "faceOnClub", quality: "estimated" }
        ],
        order: ["pelvis", "thorax", "leadArm", "club"], gapsMs: [19, 3, 65], gainsDps: [250, 253, 1274],
        orderResolved: true, verdict: "proximalToDistal", routeSummary: "estimated"
    })

    property var ks: null

    PpMetricChart {
        id: chart
        width: 1400; height: 900
        visible: true
        sessionType: -1                 // no persisted preference writes
        splitMode: false                // one plot, all four curves, so the brackets draw too
        seriesList: probe.fourCurves
        phases: []
        startUs: 1000; endUs: 400000; impactUs: 300000
        playheadUs: 300000
        showPlayhead: false
        seekable: false
        kinematicSequence: probe.ks
    }

    // Walk the live item tree for the sequence markers and report what is drawn.
    function scan() {
        var out = { rings: [], rising: [], risingText: [], verdict: "", header: "", chips: [] }
        var seen = 0
        function walk(it, depth) {
            if (!it || depth > 40 || seen > 20000) return
            seen++
            try {
                var on = it.objectName || ""
                if (on.indexOf("sequencePeak:") === 0)       out.rings.push(on.substring(13) + (it.visible ? "" : "(hidden)"))
                else if (on.indexOf("sequenceRising:") === 0) out.rising.push(on.substring(15) + (it.visible ? "" : "(hidden)"))
                else if (on === "sequenceRisingText")         out.risingText.push(it.text)
                else if (on === "sequenceVerdict")            out.verdict = it.text
                else if (on === "sequenceStripHeader")        out.header = it.text
                else if (on.indexOf("sequenceChip:") === 0)   out.chips.push(on)
            } catch (e) {}
            var kids = null
            try { kids = it.children } catch (e) { return }
            if (!kids) return
            for (var i = 0; i < kids.length; ++i) walk(kids[i], depth + 1)
        }
        walk(chart, 0)
        out.rings.sort(); out.rising.sort()
        return out
    }

    function stepRising() {
        probe.ks = probe.risingKs
        chart._applyPreset("Kinematic sequence", false)
        var ov = cm.sequenceOverlay(probe.risingKs)
        probe.w("— RISING — overlay peaks " + ov.peaks.length + " gaps " + ov.gaps.length)
        for (var i = 0; i < ov.peaks.length; ++i)
            probe.w("    " + ov.peaks[i].segment + " placed=" + ov.peaks[i].placed
                    + " rising=" + ov.peaks[i].atImpactRising + " text='" + ov.peaks[i].text + "'")
        var s = probe.scan()
        probe.expect("rising chevrons drawn",          s.rising.join(","), "pelvis,thorax")
        probe.expect("…labelled",                      s.risingText.join(","), "rising,rising")
        probe.expect("no dimmed ring for the trunk",   s.rings.join(","), "club,leadArm")
        probe.expect("no chips",                       s.chips.length, 0)
        probe.expect("the strip's one line",           s.verdict,
                     "Lead arm −115 ms → Club −55 ms (+60 ms) · arms and club peak before the body"
                     + " — hips and chest still speeding up at impact")
        probe.expect("the strip header names the route", s.header, "SEQUENCE · estimated from the camera")
        probe.expect("the pelvis row's sentence",
                     cm.sequenceRows(probe.risingKs)[2].unplacedText, "still accelerating at impact")
        probe.expect("…and its method glyph",
                     cm.sequenceRows(probe.risingKs)[2].glyph, "T")
    }

    function stepPlaced() {
        probe.ks = probe.placedKs
        var s = probe.scan()
        probe.expect("placed pair: four ordinary rings", s.rings.join(","), "club,leadArm,pelvis,thorax")
        probe.expect("placed pair: no chevron",          s.rising.length, 0)
        probe.expect("placed pair: the strip's one line", s.verdict,
                     "Pelvis −87 ms → Chest −68 ms (+19 ms) → Lead arm −65 ms (+3 ms) → Club 0 ms (+65 ms)"
                     + " · pelvis → chest → arm → club")
    }

    property int step: 0
    function safe(name, fn) { try { fn() } catch (e) { probe.fails++; probe.w("⛔ THREW in " + name + ": " + e + (e && e.stack ? "\n" + e.stack : "")) } }
    Timer {
        interval: 1500; running: true; repeat: true
        onTriggered: {
            probe.step++
            switch (probe.step) {
            case 1: probe.safe("rising", probe.stepRising); break
            case 2: probe.safe("placed", probe.stepPlaced); break
            default:
                probe.w((probe.fails ? "⛔ FAILED " : "DONE OK ") + probe.checks + " checks, " + probe.fails + " failure(s)")
                Qt.quit()
            }
        }
    }
    Timer { interval: 60000; running: true; repeat: false; onTriggered: { probe.w("⛔ WATCHDOG at step " + probe.step); Qt.quit() } }
    Component.onCompleted: probe.w("KS RISING PROBE (synthetic payload, no session)")
}
