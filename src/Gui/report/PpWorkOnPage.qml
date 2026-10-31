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
    PpReportSqueeze { id: fit; target: wp; pageH: wp.pageH; max: 2 }
    readonly property int squeeze: fit.level            // 0 as laid out; 1 tight; 2 tighter (a second, last step)
    readonly property bool tight: squeeze > 0
    property real pageH: 950
    readonly property bool dense: denseByContent || tight
    readonly property real lead: squeeze > 1 ? 1.2 : tight ? 1.24 : 1.32
    function gap(n) { return squeeze > 1 ? Math.round(n * 0.4) : tight ? Math.round(n * 0.55) : (dense ? Math.round(n * 0.8) : n) }
    readonly property font bodyFont: dense ? ro.fBodyD : ro.fBody
    onWoChanged: fit.level = 0
    width: 600

    // the labelled paragraphs' label column: sized from the widest label (it wraps beyond 12 em)
    FontMetrics { id: labFm; font: wp.ro.fLabel }
    readonly property real labelW: {
        let w = 0
        for (const s of ["What came with it", "Check it yourself", "The feel", "Next time"]) w = Math.max(w, labFm.advanceWidth(s))
        return Math.min(12 * ro.em, w + 2.5 * ro.em)
    }

    // The run of the swings against the target range: the Charts run (PpChRunBox) in the Charts print roles — a mark per
    // swing (warn where it fired), a stub on the foot where it could not be measured, the swing numbers thinned as on
    // screen — with the target range as the accent wash, its edges ticked on the value axis and labelled at the right;
    // the figure's label under it. The run's scale holds the swings and the range (8 % of room). It keeps the height the
    // page always gave it (the plot plus the swing numbers, and the label's line), so the page breaks do not move.
    component RunChart: Item {
        id: rc
        property QtObject ro: null
        property QtObject roles: null
        property var fig: ({})
        property string label: ""
        property real plotH: 92
        height: plotH + (label !== "" ? 34 : 20)
        readonly property var pts: fig.points || []
        readonly property var target: {
            const g = (fig.bands || []).filter(b => b.grade === "ideal" || b.grade === "good")
            return g.length ? { lo: Math.min.apply(null, g.map(b => b.lo)), hi: Math.max.apply(null, g.map(b => b.hi)) } : null
        }
        readonly property var vals: pts.filter(p => roles.fin(p.value)).map(p => p.value)
        readonly property var span: {
            const all = vals.concat(target ? [target.lo, target.hi] : [])
            if (!all.length) return { lo: 0, hi: 1 }
            const lo = Math.min.apply(null, all), hi = Math.max.apply(null, all), pad = Math.max(1e-6, (hi - lo) * 0.08)
            return { lo: lo - pad, hi: hi + pad }
        }
        // the run in PpChRunBox's shape: a point's place is its order, its label the report's swing number
        readonly property var run: ({ readingId: "workOnRun", key: "", unit: "", unitText: "", caption: "", yRange: span, box: null, drift: null,
                                      points: pts.map((p, i) => ({ ordinal: i + 1, label: p.swing, value: roles.fin(p.value) ? p.value : null,
                                                                   measured: roles.fin(p.value), fired: p.state === "fired" })) })
        // the value axis: the target range's edges inside the scale, in the run's unit when it is short ("°", "%")
        // the figure by the house rule (PpTypeset.figure: U+2212, never "−0"); a short unit closed up after it
        function tick(v) {
            const d = Math.abs(v - Math.round(v)) < 1e-6 ? 0 : Math.max(0, fig.decimals || 0)
            const u = String(fig.unit || "")
            return rc.roles.figure(v, d) + (u.length <= 2 ? u : "")
        }
        function ticks(lo, hi, n, unit) {
            return rc.target ? [rc.target.lo, rc.target.hi].filter(v => v > rc.span.lo && v < rc.span.hi).map(v => ({ v: v, text: rc.tick(v) })) : []
        }
        PpChRunBox {
            width: parent.width
            roles: rc.roles; brush: null; ticker: rc; interactive: false
            run: rc.run; showCaption: false; showBox: false
            // the plot fills the height the old chart had: less the run's own head room and its swing-number line
            plotH: rc.plotH + 20 - rc.roles.u(6) - rc.roles.u(22)
            band: rc.target ? { lo: rc.target.lo, hi: rc.target.hi, label: "target\nrange" } : null
            bandFill: rc.ro.wash; bandEdge: Qt.alpha(rc.ro.accent, 0.5); bandInk: rc.ro.accent
        }
        Text { visible: rc.label !== ""; x: 34; y: rc.plotH + 20; width: rc.width - x; text: rc.label; font: rc.ro.fFolio; color: rc.ro.ink3; wrapMode: Text.WordWrap }
    }

    // ═══ the head: kicker, the goal, the coach's term ═══
    PpReportSectionHead {
        objectName: "reportWorkOn" + wp.rank
        ro: wp.ro; kicker: wp.wo.kicker || ""; title: wp.wo.goal || wp.wo.name || ""; showStandfirst: false
        section: "Work-on " + wp.rank
        width: wp.width
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
            PpReportPara { ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "What we saw"; text: wp.wo.saw || ""; stacked: true; width: parent.width }
            RunChart { visible: !!wp.wo.figure && (wp.wo.figure.points || []).length > 0; width: parent.width; ro: wp.ro; roles: chRoles; plotH: wp.squeeze > 1 ? 54 : wp.tight ? 62 : wp.dense ? 76 : 92; fig: wp.wo.figure || ({}); label: wp.wo.figureLabel || "" }
            PpReportPara { ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "Why it matters"; text: wp.wo.why || ""; stacked: true; width: parent.width }
        }
    }

    Item {
        property bool atom: true
        visible: !!wp.wo.cameWith || (wp.causes.rows || []).length > 0 || !!wp.causes.lead
        width: wp.width; height: visible ? wp.gap(24) + 1 : 0
        Rectangle { y: wp.gap(12); width: parent.width; height: wp.ro.hw; color: wp.ro.hair }
    }
    PpReportPara { objectName: "reportCameWith" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "What came with it"; text: wp.wo.cameWith || ""; labelW: wp.labelW; width: wp.width }

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
    PpReportPara { objectName: "reportFeel" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "The feel"; text: wp.wo.cue ? "<i>" + wp.wo.cue + "</i>" : ""; labelW: wp.labelW; width: wp.width }
    Item { width: 1; height: wp.gap(12); visible: !!wp.wo.cue && drill.visible }
    PpDrillCard {
        id: drill
        visible: wp.drills.length > 0
        width: wp.width; ro: wp.ro; dense: wp.dense
        drill: wp.drills[0] || ({})
        also: wp.drills.length > 1 ? wp.drills[1] : null
    }
    Item { width: 1; height: wp.gap(14); visible: !!wp.wo.check && (drill.visible || !!wp.wo.cue) }
    PpReportPara { objectName: "reportCheck" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "Check it yourself"; text: wp.wo.check || ""; labelW: wp.labelW; width: wp.width }
    Item { width: 1; height: wp.gap(10); visible: !!wp.wo.target }
    PpReportPara { objectName: "reportNextTime" + wp.rank; ro: wp.ro; dense: wp.dense; lead: wp.lead; label: "Next time"; text: wp.wo.target || ""; bold: true; labelW: wp.labelW; width: wp.width }

    // the Charts print roles in Folio light, for the run (PpChRunBox and its marks). LAST in the Column: children are
    // destroyed first-declared first, so the run goes before its roles (see PpPosturePage).
    PpChRoles { id: chRoles; medium: "print"; fixedAesthetic: "folio" }
}
