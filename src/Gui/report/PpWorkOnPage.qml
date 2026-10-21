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
import PinPointStudio

// A WORK-ON IN DEPTH — one section per work-on (design §3 pages 4–8): kicker · the goal as the title · the coach's
// term · What we saw (with the run of the swings against the target range) · the pictures · Why it matters ·
// What came with it · What might be behind it (every authored cause in three groups, §3.3) · What to do (the feel,
// a drill card) · Check it yourself · Next time.
//
// FLOWED, NOT FITTED (§9.1 rules 1, 2). The section starts a fresh page and may run on ("Work-on 3, continued");
// it is cut only between atoms: the head (which keeps with what follows), the saw-and-pictures block, each labelled
// paragraph, the causes' lead and each cause group, a drill card. Every block is drawn only when the map has its
// words (rule 6: a work-on with no feel has no "The feel" line; no cause spotted, no "What we spotted").
// Pictures (§6): two stills at most, for the first three work-ons only, and none without photo approval — then the
// run chart takes the picture's place at full width.
Column {
    id: wp
    property QtObject ro: null
    property var wo: ({})
    property int rank: 1
    property bool photos: true
    readonly property var stills: photos && rank <= 3 ? (wo.stills || []).slice(0, 2) : []
    readonly property bool hasStills: stills.length > 0
    readonly property var causes: wo.causes || ({})
    readonly property var drills: wo.drills || []
    readonly property bool hasToDo: !!wo.cue || drills.length > 0 || !!wo.check
    // DENSE: a work-on with two or more cause groups sets its paragraphs a size down and its pictures and run chart
    // smaller (the mock's rule), so a typical work-on still opens and closes on one page.
    // TIGHT: a work-on that would run on by less than a fifth of a page is set once more, tighter (closer leading and
    // gaps, smaller pictures), rather than strand its last paragraph on a page of its own. Decided ONCE from the
    // laid-out height (a one-shot check, so no binding loop), and only ever tightens: content that needs a second
    // page still flows onto it ("Work-on 3, continued").
    readonly property bool denseByContent: (causes.rows || []).length >= 2
    property int squeeze: 0                             // 0 as laid out; 1 tight; 2 tighter (a second, last step)
    readonly property bool tight: squeeze > 0
    property real pageH: 950
    readonly property bool dense: denseByContent || tight
    readonly property real lead: squeeze > 1 ? 1.2 : tight ? 1.24 : 1.32
    function gap(n) { return squeeze > 1 ? Math.round(n * 0.4) : tight ? Math.round(n * 0.55) : (dense ? Math.round(n * 0.8) : n) }
    readonly property font bodyFont: dense ? ro.fBodyD : ro.fBody
    Timer { id: fitCheck; interval: 40; onTriggered: if (wp.squeeze < 2 && wp.height > wp.pageH + 1 && wp.height <= wp.pageH * 1.2) wp.squeeze++ }
    onHeightChanged: fitCheck.restart()
    onWoChanged: squeeze = 0
    width: 600

    // the labelled paragraphs' label column: sized from the widest label (it wraps beyond 12 em)
    FontMetrics { id: labFm; font: wp.ro.fLabel }
    readonly property real labelW: {
        let w = 0
        for (const s of ["What came with it", "Check it yourself", "The feel", "Next time"]) w = Math.max(w, labFm.advanceWidth(s))
        return Math.min(12 * ro.em, w + 2.5 * ro.em)
    }

    // A labelled paragraph: the label in the margin column (or above, `stacked`), the text in the measure.
    component Para: Item {
        id: pa
        property QtObject ro: null
        property string label: ""
        property string text: ""
        property real labelW: 118
        property bool stacked: false
        property bool bold: false
        property bool dense: false
        property real lead: 1.32
        property bool atom: true
        width: 300
        visible: text !== ""
        height: visible ? (stacked ? pl.height + 4 + pt.height : Math.max(pl.height, pt.height)) : 0
        Text { id: pl; y: pa.stacked ? 0 : 2; width: pa.stacked ? pa.width : pa.labelW - 12; text: pa.label; font: pa.ro.fLabel; color: pa.ro.ink; wrapMode: Text.WordWrap }
        Text { id: pt; x: pa.stacked ? 0 : pa.labelW; y: pa.stacked ? pl.height + 4 : 0; width: pa.width - x; text: pa.text
               textFormat: Text.StyledText; font: pa.bold ? (pa.dense ? pa.ro.fBodyDM : pa.ro.fBodyM) : (pa.dense ? pa.ro.fBodyD : pa.ro.fBody); color: pa.ro.ink; wrapMode: Text.WordWrap; lineHeight: pa.lead }
    }

    // The run of the swings against the target range: one dot per swing (fired filled warn, clean a ring, not
    // measured a stub on the baseline), the range as the accent wash with its edges ticked.
    component RunChart: Item {
        id: rc
        property QtObject ro: null
        property var fig: ({})
        property string label: ""
        property real plotH: 92
        height: plotH + (label !== "" ? 34 : 20)
        readonly property var pts: fig.points || []
        readonly property var target: {
            const g = (fig.bands || []).filter(b => b.grade === "ideal" || b.grade === "good")
            return g.length ? { lo: Math.min.apply(null, g.map(b => b.lo)), hi: Math.max.apply(null, g.map(b => b.hi)) } : null
        }
        readonly property var vals: pts.filter(p => p.value !== null && p.value !== undefined && isFinite(p.value)).map(p => p.value)
        readonly property var span: {
            const all = vals.concat(target ? [target.lo, target.hi] : [])
            if (!all.length) return { lo: 0, hi: 1 }
            const lo = Math.min.apply(null, all), hi = Math.max.apply(null, all), pad = Math.max(1e-6, (hi - lo) * 0.08)
            return { lo: lo - pad, hi: hi + pad }
        }
        readonly property real px0: 34
        readonly property real pw: width - px0 - 62
        readonly property real pitch: pw / Math.max(1, pts.length)
        readonly property real dot: Math.max(4, Math.min(9, pitch * 0.9))
        readonly property int step: { for (const s of [1, 2, 5, 10, 20]) if (s * pitch >= 16) return s; return 50 }
        function fx(i) { return px0 + (i + 0.5) * pitch }
        function fy(v) { return plotH * (1 - (v - span.lo) / (span.hi - span.lo)) }
        function tick(v) {
            const d = Math.abs(v - Math.round(v)) < 1e-6 ? 0 : Math.max(0, fig.decimals || 0)
            const s = Math.abs(v).toFixed(d)
            const u = String(fig.unit || "")
            return (v < 0 ? "−" : "") + s + (u.length <= 2 ? u : "")
        }
        Rectangle {
            visible: !!rc.target
            x: rc.px0; width: rc.pw
            y: rc.target ? rc.fy(Math.min(rc.span.hi, rc.target.hi)) : 0
            height: rc.target ? rc.fy(Math.max(rc.span.lo, rc.target.lo)) - y : 0
            color: rc.ro.wash
            Rectangle { width: parent.width; height: rc.ro.hw; color: Qt.alpha(rc.ro.accent, 0.5) }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: rc.ro.hw; color: Qt.alpha(rc.ro.accent, 0.5) }
        }
        Text {
            visible: !!rc.target
            x: rc.px0 + rc.pw + 8
            y: rc.target ? (rc.fy(Math.min(rc.span.hi, rc.target.hi)) + rc.fy(Math.max(rc.span.lo, rc.target.lo))) / 2 - height / 2 : 0
            text: "target\nrange"; font: rc.ro.fMicro; color: rc.ro.accent; lineHeight: 1.1
        }
        Repeater {
            model: rc.target ? [rc.target.lo, rc.target.hi].filter(v => v > rc.span.lo && v < rc.span.hi) : []
            delegate: Text {
                required property var modelData
                x: 0; width: rc.px0 - 6; horizontalAlignment: Text.AlignRight
                y: rc.fy(modelData) - height / 2
                text: rc.tick(modelData); font: rc.ro.fFigS; color: rc.ro.ink3
            }
        }
        Rectangle { x: rc.px0; y: rc.plotH; width: rc.pw; height: rc.ro.hw; color: rc.ro.hair }
        Repeater {
            model: rc.pts
            delegate: Item {
                required property var modelData
                required property int index
                readonly property bool measured: modelData.value !== null && modelData.value !== undefined && isFinite(modelData.value)
                PpSwingGrid.SwingMark {
                    ro: rc.ro
                    x: rc.fx(index) - width / 2
                    y: parent.measured ? rc.fy(modelData.value) - height / 2 : rc.plotH - 6
                    state: parent.measured ? (modelData.state || "clean") : "na"; d: rc.dot
                }
                Text {
                    visible: index === 0 || (index + 1) % rc.step === 0
                    x: rc.fx(index) - width / 2; y: rc.plotH + 5
                    text: modelData.swing; font: rc.ro.fFigS; color: rc.ro.ink3
                }
            }
        }
        Text { visible: rc.label !== ""; x: rc.px0; y: rc.plotH + 20; width: rc.width - rc.px0; text: rc.label; font: rc.ro.fFolio; color: rc.ro.ink3; wrapMode: Text.WordWrap }
    }

    // ═══ the head: kicker, the goal, the coach's term ═══
    Column {
        objectName: "reportWorkOn" + wp.rank
        property bool atom: true
        property bool breakBefore: true
        property bool keepNext: true
        property string section: "Work-on " + wp.rank
        width: wp.width
        Text { width: parent.width; text: wp.wo.kicker || ""; font: wp.ro.fKicker; color: wp.ro.accent; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: wp.wo.goal || wp.wo.name || ""; font: wp.ro.fH1; color: wp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 5; visible: !!wp.wo.coachTerm }
        Text { visible: !!wp.wo.coachTerm; width: parent.width; text: wp.wo.coachTerm || ""; textFormat: Text.StyledText
               font: wp.ro.fSmall; color: wp.ro.ink3; wrapMode: Text.WordWrap }
        Item { width: 1; height: wp.gap(18) }
    }

    // ═══ what we saw: the pictures, the words, the run of the swings, why it matters ═══
    Item {
        id: sawBlock
        objectName: "reportSaw" + wp.rank
        property bool atom: true
        width: wp.width
        height: Math.max(stillRow.visible ? stillRow.height : 0, sawCol.height)
        Row {
            id: stillRow
            visible: wp.hasStills
            spacing: 10
            readonly property real w: wp.stills.length > 1 ? (wp.squeeze > 1 ? 96 : wp.tight ? 106 : wp.dense ? 118 : 126) : (wp.squeeze > 1 ? 100 : wp.tight ? 112 : wp.dense ? 128 : 160)
            Repeater {
                model: wp.stills
                delegate: PpStill {
                    required property var modelData
                    ro: wp.ro; w: stillRow.w
                    src: modelData.src || ""; caption: modelData.caption || ""; value: modelData.value || ""; note: modelData.note || ""
                }
            }
        }
        Column {
            id: sawCol
            x: stillRow.visible ? stillRow.width + 24 : 0
            width: wp.width - x; spacing: wp.gap(12)
            Para { ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "What we saw"; text: wp.wo.saw || ""; stacked: true; width: parent.width }
            RunChart { visible: !!wp.wo.figure && (wp.wo.figure.points || []).length > 0; width: parent.width; ro: wp.ro; plotH: wp.squeeze > 1 ? 54 : wp.tight ? 62 : wp.dense ? 76 : 92; fig: wp.wo.figure || ({}); label: wp.wo.figureLabel || "" }
            Para { ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "Why it matters"; text: wp.wo.why || ""; stacked: true; width: parent.width }
        }
    }

    Item {
        property bool atom: true
        visible: !!wp.wo.cameWith || (wp.causes.rows || []).length > 0 || !!wp.causes.lead
        width: wp.width; height: visible ? wp.gap(24) + 1 : 0
        Rectangle { y: wp.gap(12); width: parent.width; height: wp.ro.hw; color: wp.ro.hair }
    }
    Para { objectName: "reportCameWith" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "What came with it"; text: wp.wo.cameWith || ""; labelW: wp.labelW; width: wp.width }

    // ═══ what might be behind it (§3.3): the lead for the case, then each group ═══
    Item { width: 1; height: wp.gap(14); visible: !!wp.wo.cameWith && causesHead.visible }
    Column {
        id: causesHead
        objectName: "reportCauses" + wp.rank
        property bool atom: true
        property bool keepNext: (wp.causes.rows || []).length > 0
        visible: !!wp.causes.lead || (wp.causes.rows || []).length > 0
        width: wp.width; spacing: 5
        Text { text: "What might be behind it"; font: wp.ro.fLabel; color: wp.ro.ink }
        Text { visible: !!wp.causes.lead; width: parent.width; text: wp.causes.lead || ""; font: wp.bodyFont; color: wp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32 }
        Item { width: 1; height: 2 }
    }
    Repeater {
        model: wp.causes.rows || []
        delegate: Item {
            id: cr
            required property var modelData
            property bool atom: true
            objectName: "reportCause_" + wp.rank + "_" + modelData.kind
            readonly property real titleW: 14.5 * wp.ro.em
            readonly property var items: (modelData.items || []).slice(0, modelData.kind === "possible" ? 6 : (modelData.kind === "ruledOut" ? 4 : 99))
            width: wp.width; height: Math.max(ct.height, ci.height) + wp.gap(8)
            Rectangle {
                y: 6; width: 7; height: 7; radius: 3.5
                color: cr.modelData.kind === "seen" ? wp.ro.warn : "transparent"
                border.color: cr.modelData.kind === "seen" ? "transparent" : (cr.modelData.kind === "possible" ? wp.ro.accent : wp.ro.ink3)
                border.width: cr.modelData.kind === "possible" ? 1.5 : 1
            }
            Text { id: ct; x: 13; y: 2; width: cr.titleW - 23; text: cr.modelData.title || ""; font: wp.ro.fSmallM; color: wp.ro.ink; wrapMode: Text.WordWrap }
            Text {
                id: ci
                x: cr.titleW; y: 1; width: wp.width - cr.titleW
                textFormat: Text.StyledText; wrapMode: Text.WordWrap; lineHeight: 1.28
                font: wp.ro.fSmall; color: wp.ro.ink
                text: cr.items.join(" · ")
                      + (cr.modelData.more ? (cr.modelData.kind === "possible" ? "<br>" : " · ") + "<font color=\"" + wp.ro.ink3 + "\">" + cr.modelData.more + "</font>" : "")
                      + (cr.modelData.note ? "  <font color=\"" + wp.ro.ink3 + "\">— " + cr.modelData.note + "</font>" : "")
            }
        }
    }

    // ═══ what to do ═══
    Item {
        objectName: "reportToDo" + wp.rank
        property bool atom: true
        property bool keepNext: true
        visible: wp.hasToDo
        width: wp.width; height: visible ? todo.height + wp.gap(30) : 0
        Text { id: todo; y: wp.gap(18); text: "What to do"; font: wp.ro.fH2; color: wp.ro.ink }
    }
    Para { objectName: "reportFeel" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "The feel"; text: wp.wo.cue ? "<i>" + wp.wo.cue + "</i>" : ""; labelW: wp.labelW; width: wp.width }
    Item { width: 1; height: wp.gap(12); visible: !!wp.wo.cue && drill.visible }
    PpDrillCard {
        id: drill
        visible: wp.drills.length > 0
        width: wp.width; ro: wp.ro; dense: wp.dense
        drill: wp.drills[0] || ({})
        also: wp.drills.length > 1 ? wp.drills[1] : null
    }
    Item { width: 1; height: wp.gap(14); visible: !!wp.wo.check && (drill.visible || !!wp.wo.cue) }
    Para { objectName: "reportCheck" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "Check it yourself"; text: wp.wo.check || ""; labelW: wp.labelW; width: wp.width }
    Item { width: 1; height: wp.gap(10); visible: !!wp.wo.target }
    Para { objectName: "reportNextTime" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "Next time"; text: wp.wo.target || ""; bold: true; labelW: wp.labelW; width: wp.width }
}
