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

// YOUR WRIST — the Wrist page of the session report (docs/design/session_report_design.md "The Wrist page";
// docs/design/wrist_motion_into_charts.md §8.2): the Charts Wrist section's SESSION reading, printed. After the Posture
// page and before "Between now and next time"; absent when the map has no `wrist` (report_wrist.h: no swing of the
// session reads a wrist DOF). Blocks, in the Posture page's order:
//   · the head: the kicker, "Wrist", the head line, the Style line (the resemblance index's clause; absent with no
//     index: the head already says the style counts) and the Score line;
//   · "Checkpoints across the session": PpReportProfile (the Posture page's profile, shared): one row per wrist DOF,
//     the DOMINANT style's band as a wash in the DOF's own metric colour, the session's box per checkpoint, under each
//     the median ("-" where too few swings), how wide the middle half is or — outside the band — the side it falls on,
//     in warn and underlined; the caption says whose band is shaded;
//   · "Wrist score, swing by swing": PpChRunBox in the Charts print roles, non-interactive — a mark per swing, a stub
//     where a swing has no score, the session's box beside, the value axis from the payload's ticks (paper has no
//     ticker), the swing numbers the report's;
//   · "Wrist style by swing": the resemblance index per swing and style, fifteen swings a line (each line one atom),
//     the style each swing was graded against in bold, then the counts; with no index, the presenter's note alone;
//   · "What keeps firing": one atom per finding — its name and count words, then a mark per swing in the Posture
//     strip's idiom (filled where it fired, a hairline outline where it did not), the report's swing numbers under;
//     then "N more";
//   · "Repeatable?";
//   · "What moves with the lead wrist at impact": a PRINT SIBLING of PpChMovesWith reading the same ledger rows (the
//     screen's draws each reading's swatch through the live theme, and its rows are not pager atoms): the column heads
//     (repeated on a page that opens inside the list), one atom per reading — rank, the swatch in the reading's own
//     colour (Folio light), its name and checkpoint, r (ink when it clears chance, ink3 inside it), the 95 % interval
//     drawn on a −1…1 rule with its figures under, n, the slope, the pair — then the chance line and the note. When it
//     is refused, the refusal sentence alone under the heading;
//   · "How far can I trust this?": the session's trust lines, open (paper has no fold), then "N more". The small-session
//     words are one of them: `sizeLine` is not printed again.
// FLOWED (§9.1 rules 1, 2) as the Posture page: the section starts a fresh page; every block is one or more atoms; a
// heading keeps with what follows; a continued page says "Wrist, continued" (the pager). Every sentence and figure is
// the map's `wrist` (report_wrist.h); the register sets only the fixed headings. FOLIO LIGHT through `ro` (the report's
// palette and type) and `cr` (the Charts print roles in the same fixed aesthetic, for the boxes, the run and its
// marks): nothing reads a live theme token. No affordance: nothing here is a link or a fold.
// objectNames: reportWrist (the head), reportWristProfile (+ reportWristRow, reportWristCell — PpReportProfile's),
// reportWristScore, reportWristStyles (+ reportWristStylesLine: from, to; reportWristStyleCell: swing, styleKey, text,
// graded), reportWristRecurring (+ reportWristFinding: findingId, name, countText; reportWristMark: swing, fired),
// reportWristRepeat, reportWristMoves (+ reportWristMovesRow: readingId, rText), reportWristTrust (+ reportWristTrustLine).
Column {
    id: wp
    property QtObject ro: null
    property var wr: null
    readonly property var m: wr || ({})
    readonly property bool on: !!wr
    readonly property var prof: m.profile || ({})
    readonly property var rows: (prof.rows || []).slice(0, 5)                       // the map's caps (report_wrist.h)
    readonly property var positions: (prof.positions || []).slice(0, 8)
    readonly property var sr: m.scoreRun || null
    readonly property var st: m.styles || ({})
    readonly property var stCols: (st.columns || []).slice(0, 120)
    readonly property var stRows: st.rows || []
    readonly property var rec: (m.recurring || []).slice(0, 8)
    readonly property var mw: m.movesWith || null
    readonly property bool mwOk: !!mw && mw.available === true
    readonly property var mwRows: mwOk ? (mw.rows || []).slice(0, 7) : []
    readonly property var trust: (m.trust || []).slice(0, 16)
    visible: on
    width: 600

    readonly property int perLine: 15                   // the style table: swings to a line
    readonly property int marksPerLine: 30              // a finding's marks to a line (the Posture strip's)
    function chunks(list, n) {
        const out = []
        for (let i = 0; i < list.length; i += n) out.push({ from: i, items: list.slice(i, i + n) })
        return out
    }
    function fin(v) { return v !== null && v !== undefined && isFinite(v) }

    // the score run in PpChRunBox's RUN shape: a point's place is its order (the drift line's x is the same), its
    // label the report's swing number
    readonly property var run: {
        const s = wp.sr
        if (!s) return null
        const pts = (s.points || []).slice(0, 120)
        return { readingId: "wristScore", key: "", unit: "", unitText: "", caption: s.caption || "",
                 yRange: s.yRange || ({ lo: 0, hi: 100 }), box: s.box || null, drift: s.drift || null,
                 points: pts.map(function (p, i) { return { ordinal: i + 1, label: p.swing, value: p.value, measured: p.measured === true, fired: false } }) }
    }

    // ═══ the head ═══
    Column {
        objectName: "reportWrist"
        property bool atom: true
        property bool keepNext: true
        property bool breakBefore: true
        property string section: "Wrist"
        visible: wp.on
        width: wp.width
        Text { width: parent.width; text: wp.m.kicker || ""; font: wp.ro.fKicker; color: wp.ro.accent; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: "Wrist"; font: wp.ro.fH1; color: wp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 10 }
        Text { width: parent.width; text: wp.m.head || ""; font: wp.ro.fStand; color: wp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.2 }
        Item { width: 1; height: 8; visible: styleLine.visible }
        Item {
            id: styleLine
            objectName: "reportWristStyleLine"
            visible: !!wp.m.styleLine
            width: parent.width; height: visible ? Math.max(styleLab.height, styleText.height) : 0
            Text { id: styleLab; y: 1; text: "Style"; font: wp.ro.fLabel; color: wp.ro.ink3 }
            Text { id: styleText; x: 64; width: parent.width - x; text: wp.m.styleLine || ""; font: wp.ro.fBody; color: wp.ro.ink; wrapMode: Text.WordWrap }
        }
        Item { width: 1; height: 6; visible: scoreLine.visible }
        Item {
            id: scoreLine
            objectName: "reportWristScoreLine"
            visible: !!wp.m.scoreLine
            width: parent.width; height: visible ? Math.max(scoreLab.height, scoreText.height) : 0
            Text { id: scoreLab; y: 1; text: "Score"; font: wp.ro.fLabel; color: wp.ro.ink3 }
            Text { id: scoreText; x: 64; width: parent.width - x; text: wp.m.scoreLine || ""; font: wp.ro.fBody; color: wp.ro.ink; wrapMode: Text.WordWrap }
        }
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "Checkpoints across the session"; font: wp.ro.fH3; color: wp.ro.ink; wrapMode: Text.WordWrap }
        Item { width: 1; height: 3 }
        Text { width: parent.width; visible: text !== ""; text: wp.prof.sub || ""; font: wp.ro.fSmall; color: wp.ro.ink3; wrapMode: Text.WordWrap }
        Item { width: 1; height: 8 }
    }

    // ═══ the profile: the column heads (repeated on a continued page), one atom per DOF, the caption ═══
    PpReportProfile {
        ro: wp.ro; roles: cr
        prefix: "reportWrist"; printGroup: "wrist"
        on: wp.on; rows: wp.rows; positions: wp.positions; caption: wp.prof.caption || ""
        width: wp.width
    }

    // ═══ the score, swing by swing: a mark per swing, the session's box beside ═══
    Column {
        objectName: "reportWristScore"
        property bool atom: true
        readonly property int points: wp.run ? wp.run.points.length : 0
        visible: wp.on && points > 0
        width: wp.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: wp.run ? wp.run.caption : ""; font: wp.ro.fH3; color: wp.ro.ink; wrapMode: Text.WordWrap }
        Item { width: 1; height: 3 }
        Text { width: parent.width; visible: text !== ""; text: wp.sr ? (wp.sr.legend || "") : ""; font: wp.ro.fSmall; color: wp.ro.ink3; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
        PpChRunBox {
            width: parent.width
            roles: cr; brush: null; ticker: tk; interactive: false
            run: wp.run; showCaption: false
            plotH: cr.u(60)                     // a 0–100 score: three ticks need no more on paper
            hueOf: function (k) { return wp.ro.ink2 }
        }
    }

    // ═══ the wrist style by swing: the resemblance index, fifteen swings a line; the style graded against in bold ═══
    Column {
        id: sty
        objectName: "reportWristStyles"
        readonly property bool table: wp.stCols.length > 0 && wp.stRows.length > 0
        readonly property var lines: wp.chunks(wp.stCols, wp.perLine)
        readonly property real lw: 80
        readonly property real cw: (wp.width - lw) / wp.perLine
        readonly property real rowH: 18
        visible: wp.on && (table || !!wp.st.note)
        width: wp.width
        Column {
            property bool atom: true
            property bool keepNext: sty.table
            width: wp.width
            Item { width: 1; height: 18 }
            Text { width: parent.width; text: "Wrist style by swing"; font: wp.ro.fH3; color: wp.ro.ink }
            Item { width: 1; height: 3 }
            // with no index, the presenter's one quiet line instead of a table of "-"
            Text { width: parent.width; visible: text !== ""; text: sty.table ? (wp.st.legend || "") : (wp.st.note || "")
                   font: sty.table ? wp.ro.fSmall : wp.ro.fBody; color: sty.table ? wp.ro.ink3 : wp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: sty.table ? 1 : 1.25 }
            Item { width: 1; height: 6 }
        }
        Repeater {
            model: sty.table ? sty.lines : []
            delegate: Item {
                id: sl
                required property var modelData
                objectName: "reportWristStylesLine"
                property bool atom: true
                readonly property int from: modelData.from
                readonly property var cols: modelData.items
                width: wp.width; height: sty.rowH * (1 + wp.stRows.length) + 8
                // the swing numbers
                Text { y: 2; width: sty.lw - 8; text: "Swing"; font: wp.ro.fLabel; color: wp.ro.ink3 }
                Repeater {
                    model: sl.cols
                    delegate: Text { required property var modelData
                        required property int index
                        x: sty.lw + index * sty.cw; y: 2; width: sty.cw - 4; horizontalAlignment: Text.AlignRight
                        text: modelData.short || ""; font: wp.ro.fFigS; color: wp.ro.ink3 }
                }
                Rectangle { y: sty.rowH; width: parent.width; height: wp.ro.hw; color: wp.ro.hair }
                Repeater {
                    model: wp.stRows
                    delegate: Item {
                        id: srow
                        required property var modelData
                        required property int index
                        y: sty.rowH * (index + 1); width: sl.width; height: sty.rowH
                        Text { y: 3; width: sty.lw - 8; text: srow.modelData.name || ""; font: wp.ro.fSmall; color: wp.ro.ink; wrapMode: Text.WordWrap }
                        Repeater {
                            model: (srow.modelData.cells || []).slice(sl.from, sl.from + sl.cols.length)
                            delegate: Text {
                                required property var modelData
                                required property int index
                                objectName: "reportWristStyleCell"
                                readonly property int swing: (sl.cols[index] || {}).swing || 0
                                readonly property string styleKey: srow.modelData.key || ""
                                readonly property bool graded: modelData.graded === true
                                x: sty.lw + index * sty.cw; y: 3; width: sty.cw - 4; horizontalAlignment: Text.AlignRight
                                text: modelData.text || "-"
                                font: graded ? wp.ro.fFigB : wp.ro.fFig; color: graded ? wp.ro.ink : wp.ro.ink3
                            }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: wp.ro.hw; color: wp.ro.faint }
                    }
                }
            }
        }
        // the foot: the styles graded against, then the swings beyond the cap
        Column {
            property bool atom: true
            property bool keepPrev: true
            visible: sty.table && (!!wp.st.countsLine || !!wp.st.more)
            width: wp.width
            Item { width: 1; height: 2 }
            Text { width: parent.width; visible: text !== ""; text: wp.st.countsLine || ""; font: wp.ro.fSmall; color: wp.ro.ink2; wrapMode: Text.WordWrap }
            Text { width: parent.width; visible: text !== ""; text: wp.st.more || ""; font: wp.ro.fMicro; color: wp.ro.ink3; wrapMode: Text.WordWrap }
        }
    }

    // ═══ what keeps firing: one atom per finding, a mark per swing (filled where it fired) ═══
    Column {
        id: recBlock
        objectName: "reportWristRecurring"
        readonly property real pitch: Math.min(22, wp.width / wp.marksPerLine)
        visible: wp.on && (wp.rec.length > 0 || !!wp.m.recurringEmpty)
        width: wp.width
        Column {
            property bool atom: true
            property bool keepNext: wp.rec.length > 0
            width: wp.width
            Item { width: 1; height: 18 }
            Text { width: parent.width; text: "What keeps firing"; font: wp.ro.fH3; color: wp.ro.ink }
            Item { width: 1; height: 3 }
            Text { width: parent.width; visible: text !== ""; text: wp.rec.length > 0 ? (wp.m.recurringLegend || "") : (wp.m.recurringEmpty || "")
                   font: wp.rec.length > 0 ? wp.ro.fSmall : wp.ro.fBody; color: wp.rec.length > 0 ? wp.ro.ink3 : wp.ro.ink2; wrapMode: Text.WordWrap }
            Item { width: 1; height: 4 }
        }
        Repeater {
            model: wp.rec
            delegate: Column {
                id: fd
                required property var modelData
                objectName: "reportWristFinding"
                property bool atom: true
                readonly property string findingId: modelData.id || ""
                readonly property string name: modelData.name || ""
                readonly property string countText: modelData.countText || ""
                readonly property var lines: wp.chunks((modelData.marks || []).slice(0, 120), wp.marksPerLine)
                width: wp.width
                Item {
                    width: parent.width; height: Math.max(fn.height, fc.height) + 6
                    Text { id: fn; y: 4; width: wp.width * 0.42; text: fd.name; font: wp.ro.fBodyDM; color: wp.ro.ink; wrapMode: Text.WordWrap }
                    Text { id: fc; x: wp.width * 0.42 + 12; y: 5; width: wp.width - x; text: fd.countText; font: wp.ro.fSmall; color: wp.ro.ink2; wrapMode: Text.WordWrap }
                }
                Repeater {
                    model: fd.lines
                    delegate: Item {
                        required property var modelData
                        width: wp.width; height: 28
                        Repeater {
                            model: modelData.items
                            delegate: Item {
                                required property var modelData
                                required property int index
                                objectName: "reportWristMark"
                                readonly property int swing: modelData.swing || 0
                                readonly property bool fired: modelData.fired === true
                                x: index * recBlock.pitch; width: recBlock.pitch; height: 28
                                // fired: a filled bar; not fired: a hairline outline (the Posture strip's marks)
                                Rectangle {
                                    x: (parent.width - width) / 2; y: 2
                                    width: Math.max(4, recBlock.pitch * 0.5); height: 12
                                    radius: 1
                                    color: parent.fired ? wp.ro.ink2 : "transparent"
                                    border.width: parent.fired ? 0 : 0.75
                                    border.color: wp.ro.hair
                                }
                                Text { visible: parent.swing === 1 || parent.swing % 5 === 0
                                       width: parent.width; y: 16; horizontalAlignment: Text.AlignHCenter
                                       text: parent.swing; font: wp.ro.fFigS; color: wp.ro.ink3 }
                            }
                        }
                    }
                }
                Rectangle { width: parent.width; height: wp.ro.hw; color: wp.ro.hair }
                // clear of the rule: a page that opens on the next atom must not show this one's last hairline
                Item { width: 1; height: 4 }
            }
        }
        Item {
            property bool atom: true
            property bool keepPrev: true
            visible: !!wp.m.recurringMore
            width: wp.width; height: visible ? rm.height + 6 : 0
            Text { id: rm; y: 4; width: parent.width; text: wp.m.recurringMore || ""; font: wp.ro.fMicro; color: wp.ro.ink3; wrapMode: Text.WordWrap }
        }
    }

    // ═══ repeatable? ═══
    Column {
        objectName: "reportWristRepeat"
        property bool atom: true
        visible: wp.on && !!wp.m.repeat
        width: wp.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "Repeatable?"; font: wp.ro.fH3; color: wp.ro.ink }
        Item { width: 1; height: 4 }
        Text { width: parent.width; text: wp.m.repeat || ""; font: wp.ro.fBody; color: wp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    }

    // ═══ what moves with the lead wrist at impact: the Relations ledger's rows, printed (see the header) ═══
    Column {
        id: mv
        objectName: "reportWristMoves"
        readonly property real gap: 10
        readonly property real rankW: 18
        readonly property real rW: 34
        readonly property real ivW: 110
        readonly property real nW: 22
        readonly property real slW: 96
        readonly property real spW: 56
        readonly property real nameW: wp.width - rankW - rW - ivW - nW - slW - spW - 6 * gap
        readonly property real xName: rankW + gap
        readonly property real xR: xName + nameW + gap
        readonly property real xIv: xR + rW + gap
        readonly property real xN: xIv + ivW + gap
        readonly property real xSl: xN + nW + gap
        readonly property real xSp: xSl + slW + gap
        visible: wp.on && !!wp.mw && (wp.mwOk ? wp.mwRows.length > 0 : !!wp.mw.refusal)
        width: wp.width
        // the heading; refused, the refusal alone under it
        Column {
            property bool atom: true
            property bool keepNext: wp.mwOk
            width: wp.width
            Item { width: 1; height: 18 }
            Text { width: parent.width; text: wp.mwOk && wp.mw.head ? wp.mw.head : "What moves with the lead wrist at impact"; font: wp.ro.fH3; color: wp.ro.ink; wrapMode: Text.WordWrap }
            Item { width: 1; height: 3 }
            Text { width: parent.width; visible: text !== ""; text: wp.mwOk ? (wp.mw.sub || "") : ""; font: wp.ro.fSmall; color: wp.ro.ink3; wrapMode: Text.WordWrap }
            Text { objectName: "reportWristMovesRefusal"; width: parent.width; visible: !wp.mwOk && text !== ""; topPadding: 1
                   text: wp.mw ? (wp.mw.refusal || "") : ""; font: wp.ro.fBody; color: wp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            Item { width: 1; height: 6 }
        }
        Column {
            readonly property var keyKinds: []
            property string printGroup: "wristMoves"
            visible: wp.mwOk
            width: wp.width
            // the column heads (repeated on a page that opens inside the list)
            Item {
                property bool atom: true
                width: wp.width; height: 20
                Rectangle { anchors.fill: parent; color: wp.ro.paper }
                Rectangle { width: parent.width; height: wp.ro.hw; color: wp.ro.hair }
                Text { x: mv.xName; y: 5; width: mv.nameW; text: "Reading"; font: wp.ro.fLabel; color: wp.ro.ink3 }
                Text { x: mv.xR; y: 5; width: mv.rW; horizontalAlignment: Text.AlignRight; text: "r"; font: wp.ro.fLabel; color: wp.ro.ink3 }
                Text { x: mv.xIv; y: 5; width: mv.ivW; horizontalAlignment: Text.AlignHCenter; text: "95 % interval"; font: wp.ro.fLabel; color: wp.ro.ink3 }
                Text { x: mv.xN; y: 5; width: mv.nW; horizontalAlignment: Text.AlignRight; text: "n"; font: wp.ro.fLabel; color: wp.ro.ink3 }
                Text { x: mv.xSl; y: 5; width: mv.slW; text: "Slope"; font: wp.ro.fLabel; color: wp.ro.ink3 }
                Text { x: mv.xSp; y: 5; width: mv.spW; text: "The pair"; font: wp.ro.fLabel; color: wp.ro.ink3 }
            }
            Repeater {
                model: wp.mwRows
                delegate: Item {
                    id: mr
                    required property var modelData
                    objectName: "reportWristMovesRow"
                    property bool atom: true
                    readonly property var r: modelData
                    readonly property string readingId: r.readingId || ""
                    readonly property string rText: r.rText || "-"
                    readonly property bool clear: r.clear === true
                    width: wp.width; height: Math.max(nm.height, ivCol.height) + 13
                    Text { y: 5; width: mv.rankW; horizontalAlignment: Text.AlignRight; text: mr.r.rank > 0 ? String(mr.r.rank) : ""; font: wp.ro.fFigS; color: wp.ro.ink3 }
                    Rectangle { x: mv.xName; y: 9; width: 7; height: 7; radius: 3.5; color: wp.ro.hue(mr.r.key || "") }
                    Text { id: nm; x: mv.xName + 12; y: 5; width: mv.nameW - 12 - tg.width - 6; text: (mr.r.label || "") + (mr.r.same ? " " + (mr.r.sameMark || "") : "")
                           font: wp.ro.fBodyD; color: wp.ro.ink; wrapMode: Text.WordWrap }
                    Text { id: tg; x: mv.xName + mv.nameW - width; y: 6; text: mr.r.tag || ""; font: wp.ro.fFigS; color: wp.ro.ink3 }
                    Text { x: mv.xR; y: 5; width: mv.rW; horizontalAlignment: Text.AlignRight; text: mr.rText; font: wp.ro.fFig; color: mr.clear ? wp.ro.ink : wp.ro.ink3 }
                    // the interval on a −1…1 rule, the r as a dot, the figures under
                    Column {
                        id: ivCol
                        x: mv.xIv; y: 4; width: mv.ivW
                        Item {
                            id: iv
                            width: parent.width; height: 12
                            function px(v) { return Math.round((v + 1) / 2 * width) }
                            Rectangle { width: parent.width; height: wp.ro.hw; y: 6; color: wp.ro.hair }
                            Rectangle { x: Math.round(parent.width / 2); width: wp.ro.hw; height: parent.height; color: wp.ro.ink3; opacity: 0.6 }
                            Rectangle {
                                visible: wp.fin(mr.r.lo) && wp.fin(mr.r.hi)
                                x: visible ? iv.px(mr.r.lo) : 0; width: visible ? Math.max(1, iv.px(mr.r.hi) - iv.px(mr.r.lo)) : 0
                                y: 5; height: 1.5; color: mr.clear ? wp.ro.ink2 : wp.ro.ink3
                            }
                            Rectangle { visible: wp.fin(mr.r.r); width: 5; height: 5; radius: 2.5; y: 4
                                        x: visible ? iv.px(mr.r.r) - 2.5 : 0; color: mr.clear ? wp.ro.ink : wp.ro.ink3 }
                        }
                        Text { width: parent.width; visible: text !== ""; horizontalAlignment: Text.AlignHCenter; text: mr.r.ciText || ""; font: wp.ro.fFigS; color: wp.ro.ink3 }
                    }
                    Text { x: mv.xN; y: 6; width: mv.nW; horizontalAlignment: Text.AlignRight; text: mr.r.n !== undefined ? String(mr.r.n) : "-"; font: wp.ro.fFigS; color: wp.ro.ink2 }
                    Text { x: mv.xSl; y: 6; width: mv.slW; text: mr.r.slopeText || "-"; font: wp.ro.fFigS; color: wp.ro.ink2; wrapMode: Text.WordWrap }
                    // the pair as a sparkline scatter: each swing one dot, on each reading's session range
                    Canvas {
                        x: mv.xSp; y: 4; width: mv.spW; height: 18
                        readonly property var pr: mr.r.pair || null
                        readonly property var deps: [pr, width]
                        onDepsChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            if (!pr || !pr.xs) return
                            const xr = pr.xRange || {}, yr = pr.yRange || {}
                            if (!wp.fin(xr.lo) || !wp.fin(xr.hi) || !wp.fin(yr.lo) || !wp.fin(yr.hi)) return
                            ctx.fillStyle = wp.ro.ink2
                            for (let i = 0; i < pr.xs.length; ++i) {
                                if (!wp.fin(pr.xs[i]) || !wp.fin(pr.ys[i])) continue
                                const xx = 2 + (pr.xs[i] - xr.lo) / Math.max(1e-9, xr.hi - xr.lo) * (width - 4)
                                const yy = height - 2 - (pr.ys[i] - yr.lo) / Math.max(1e-9, yr.hi - yr.lo) * (height - 4)
                                ctx.fillRect(Math.round(xx) - 1, Math.round(yy) - 1, 2.5, 2.5)
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; width: parent.width; height: wp.ro.hw; color: wp.ro.hair }
                }
            }
            // the chance line and the note, kept with the last reading
            Column {
                property bool atom: true
                property bool keepPrev: true
                visible: !!wp.mw && (!!wp.mw.chanceLine || !!wp.mw.note)
                width: wp.width
                Item { width: 1; height: 4 }
                Text { width: parent.width; visible: text !== ""; text: wp.mw ? (wp.mw.chanceLine || "") : ""; font: wp.ro.fSmall; color: wp.ro.ink3; wrapMode: Text.WordWrap }
                Text { width: parent.width; visible: text !== ""; text: wp.mw ? (wp.mw.note || "") : ""; font: wp.ro.fSmall; color: wp.ro.ink3; wrapMode: Text.WordWrap }
            }
        }
    }

    // ═══ how far can I trust this? said once, open ═══
    Column {
        objectName: "reportWristTrust"
        property bool atom: true
        visible: wp.on && wp.trust.length > 0
        width: wp.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "How far can I trust this?"; font: wp.ro.fH3; color: wp.ro.ink }
        Item { width: 1; height: 4 }
        Repeater {
            model: wp.trust
            delegate: Text {
                required property var modelData
                objectName: "reportWristTrustLine"
                width: wp.width; leftPadding: 12; bottomPadding: 3
                text: String(modelData); font: wp.ro.fSmall; color: wp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25
            }
        }
        Text { width: parent.width; visible: text !== ""; leftPadding: 12; text: wp.m.trustMore || ""; font: wp.ro.fMicro; color: wp.ro.ink3; wrapMode: Text.WordWrap }
    }

    // the score run's value axis: the payload's ticks (PpChRunBox asks a ticker; paper has no presenter to ask)
    QtObject {
        id: tk
        function ticks(lo, hi, n, unit) { return wp.sr ? (wp.sr.ticks || []) : [] }
    }
    // the Charts print roles in Folio light: the boxes', the run's and the marks' design values and inks. LAST in the
    // Column: children are destroyed first-declared first, so everything above goes before its roles (see PpPosturePage).
    PpChRoles { id: cr; medium: "print"; fixedAesthetic: "folio" }
}
