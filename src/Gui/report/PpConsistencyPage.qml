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

// CONSISTENCY — the Consistency page of the session report (docs/design/consistency_design.md §3, Print): the Consistency
// tab's reading of the session, printed. After the Club delivery page and before "Between now and next time"; absent when
// the map has no `consistency` (report_consistency.h: no word in the table, no ball line and nothing flagged). Blocks:
//   · the head: the kicker, "Consistency", the headline, the ball line, what the table cannot say;
//   · the table (PpChMatrix's rectangle, in the Charts print roles, nothing to press) and its key; no table when no cell
//     has a word;
//   · "Swings the launch monitor flagged": each one's line and sentence (a guarded swing's ⚠ with them), then its rows,
//     ball first, against the other shots: where, this swing, the median, its place. A row's second line holds its
//     fired word and its ⚠. The map caps the swings and each swing's rows and words the "N more".
// FLOWED as the Posture, Wrist and Club delivery pages: the section starts a fresh page (breakBefore), a swing's rows are a
// printGroup whose head is the column heads (repeated on a page that opens inside it), each row one atom, and a heading
// keeps with what follows; a continued page says "Consistency, continued" (the pager). Every sentence and figure is the
// map's (report_consistency.h); the register sets only the fixed headings. FOLIO LIGHT through `ro` (the report's palette
// and type) and `cr` (the Charts print roles in the same fixed aesthetic, for PpChMatrix, and the units of this page's
// space: on paper g(n) is u(n)): nothing reads a live theme token. No affordance: nothing here is a link or a fold.
// THE VARIABILITY WORDS are the vocabularies', put in here (charts_consistency_presenter.h, THE GRADES): each graded cell's
// word, the key's and the marks' templates, the headline's halves. Paper is the GOLFER layer: a measurement-limited cell
// reads "Checking", a headline whose half is measurement-limited says only which area moves most, and a flagged swing's
// swing rows carry a Typicality badge, word only (no figures, no tooltip on paper), in the page's `cr`. The
// rows' value, median and place stay (R9). Ball flight has no badge (R4): the carry has none.
// objectNames: reportConsistency (the head), reportConsistencyBall, reportConsistencyEmpty, reportConsistencyTable,
// reportConsistencyKey, reportConsistencyNote, reportConsistencyFlagged (the heading), reportConsistencySwing (+ swing),
// reportConsistencyRow (+ rowKey, tag, value), reportConsistencyFoot.
Column {
    id: cp
    property QtObject ro: null
    property var cs: null
    readonly property var m: cs || ({})
    readonly property bool on: !!cs
    readonly property var table: m.table || null
    readonly property var notes: (m.notes || []).slice(0, 4)
    readonly property var flagged: (m.flagged || []).slice(0, 6)                    // the map's cap

    // ── the vocabularies' words into the presenter's grades (as PpConsistencyView, on the golfer layer) ──
    function _fill(t) {
        if (!t || t.indexOf("%1") < 0) return t || ""     // a plain string (an older payload) is shown as it is
        return t.arg(ConsistencyVocabulary.word(5)).arg(ConsistencyVocabulary.word(4)).arg(ConsistencyVocabulary.word(3))
                .arg(ConsistencyVocabulary.word(2)).arg(ConsistencyVocabulary.word(1)).arg(ConsistencyVocabulary.word(0))
                .arg(MeasurementVocabulary.word(0))
    }
    function _cells(rows) {
        return (rows || []).map(r => ({ id: r.id, name: r.name, cells: (r.cells || []).map(c => {
            const v = c.result
            if (!v || !v.graded) return c
            const o = Object.assign({}, c)
            o.text = v.measurementLimited ? MeasurementVocabulary.word(0) : ConsistencyVocabulary.word(v.grade)
            return o
        }) }))
    }
    // the golfer layer: a measurement-limited half is not given a word ("Checking" does not read in the sentence)
    readonly property string headText: {
        const k = m.headKind || ""
        if (k === "grade0") return ConsistencyVocabulary.line(0)
        if (k !== "halves") return m.head || ""
        const gs = m.headGrades || []
        if (gs.some(g => g.measurementLimited)) return m.headFallback || ""
        let t = m.head || ""
        for (const g of gs) t = t.arg(ConsistencyVocabulary.word(g.grade).toLowerCase())
        return t
    }
    readonly property var tableRows: cs && table ? _cells(table.rows) : []
    visible: on
    width: 600

    // a swing's columns: the reading takes what the others leave
    readonly property real gutter: cr.g(6)
    // a delegate's space, read here: a delegate's binding re-runs at teardown, after `cr` has gone
    readonly property real g2: cr.g(2)
    readonly property real g3: cr.g(3)
    readonly property real g4: cr.g(4)
    readonly property real g5: cr.g(5)
    readonly property real g6: cr.g(6)
    readonly property real g10: cr.g(10)
    readonly property real atW: cr.u(38)
    readonly property real valueW: cr.u(62)
    readonly property real medianW: cr.u(46)
    readonly property real placeW: cr.u(90)
    readonly property real labelW: width - atW - valueW - medianW - placeW - 4 * gutter

    // ═══ the head ═══
    PpReportSectionHead {
        objectName: "reportConsistency"
        ro: cp.ro; kicker: cp.m.kicker || ""; title: "Consistency"; standfirst: cp.headText
        visible: cp.on
        width: cp.width
        Item { width: 1; height: cr.g(8); visible: ballText.visible }
        Text { id: ballText; objectName: "reportConsistencyBall"; width: parent.width; visible: text !== ""; text: cp.m.ball || ""
               font: cp.ro.fBody; color: cp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.25 }
        Item { width: 1; height: cr.g(4); visible: emptyText.visible }
        Text { id: emptyText; objectName: "reportConsistencyEmpty"; width: parent.width; visible: text !== ""; text: cp.m.empty || ""
               font: cp.ro.fBody; color: cp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25 }
        Item { width: 1; height: cr.g(12) }
    }

    // ═══ the table ═══
    PpChMatrix {
        objectName: "reportConsistencyTable"
        visible: cp.on && !!cp.table
        width: cp.width
        roles: cr
        columns: cp.table ? (cp.table.columns || []) : []
        rows: cp.tableRows
        keyText: cp._fill(cp.m.keyMarks)
        interactive: false
    }

    // ═══ its key, and what the head left out ═══
    Column {
        objectName: "reportConsistencyKey"
        property bool atom: true
        property bool keepPrev: true
        visible: cp.on && (keyText.visible || cp.notes.length > 0)
        width: cp.width
        spacing: cr.g(3)
        Item { width: 1; height: cr.g(3) }
        Text { id: keyText; width: parent.width; visible: text !== ""; text: cp._fill(cp.m.key)
               font: cp.ro.fMicro; color: cp.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.2 }
        Repeater {
            model: cp.notes
            delegate: Text { required property var modelData; objectName: "reportConsistencyNote"; width: cp.width; text: String(modelData)
                             font: cp.ro.fSmall; color: cp.ro.ink2; wrapMode: Text.WordWrap }
        }
    }

    // ═══ the swings the launch monitor flagged ═══
    Column {
        objectName: "reportConsistencyFlagged"
        property bool atom: true
        property bool keepNext: true
        visible: cp.on && cp.flagged.length > 0
        width: cp.width
        Item { width: 1; height: cr.g(16) }
        Text { width: parent.width; text: "Swings the launch monitor flagged"; font: cp.ro.fH3; color: cp.ro.ink; wrapMode: Text.WordWrap }
    }
    Repeater {
        model: cp.on ? cp.flagged : []
        delegate: Column {
            id: sw
            required property var modelData
            readonly property var groups: (modelData.groups || []).slice(0, 3)
            objectName: "reportConsistencySwing"
            readonly property int swing: modelData.swing || 0
            width: cp.width

            // its line, its sentence and any ⚠, kept with its rows
            Column {
                property bool atom: true
                property bool keepNext: sw.groups.length > 0
                width: cp.width
                Item { width: 1; height: cp.g10 }
                Text { width: parent.width; text: sw.modelData.title || ""; font: cp.ro.fBodyDM; color: cp.ro.ink; wrapMode: Text.WordWrap }
                Text { width: parent.width; visible: text !== ""; text: sw.modelData.lm || ""; font: cp.ro.fSmall; color: cp.ro.ink3; wrapMode: Text.WordWrap }
                Item { width: 1; height: cp.g3 }
                Text { width: parent.width; text: sw.modelData.sentence || ""; font: cp.ro.fBody; color: cp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.25 }
                Text { width: parent.width; visible: text !== ""; topPadding: cp.g2; text: sw.modelData.guardNote || ""
                       font: cp.ro.fSmall; color: cp.ro.ink2; wrapMode: Text.WordWrap }
                Item { width: 1; height: cp.g5 }
            }

            // its rows: the column heads, then a crosshead per group; the pager repeats the heads on a page that opens inside
            Column {
                readonly property var keyKinds: []
                property string printGroup: "consistencySwing" + sw.swing
                visible: sw.groups.length > 0
                width: cp.width

                Item {
                    property bool atom: true
                    width: cp.width; height: heads.height + cp.g4
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: cp.ro.hw; color: cp.ro.hair }
                    Row {
                        id: heads
                        spacing: cp.gutter
                        Text { width: cp.labelW; text: sw.modelData.headLabel || ""; font: cp.ro.fLabel; color: cp.ro.ink3; wrapMode: Text.WordWrap }
                        Text { width: cp.atW; text: "At"; font: cp.ro.fLabel; color: cp.ro.ink3 }
                        Text { width: cp.valueW; horizontalAlignment: Text.AlignRight; text: sw.modelData.shotLabel || ""; font: cp.ro.fLabel; color: cp.ro.ink3 }
                        Text { width: cp.medianW; horizontalAlignment: Text.AlignRight; text: "Median"; font: cp.ro.fLabel; color: cp.ro.ink3 }
                        Text { width: cp.placeW; text: "Place"; font: cp.ro.fLabel; color: cp.ro.ink3 }
                    }
                }
                Repeater {
                    model: sw.groups
                    delegate: Column {
                        id: grp
                        required property var modelData
                        readonly property var rows: (modelData.rows || []).slice(0, 8)      // the map's cap
                        width: cp.width
                        Item {
                            property bool atom: true
                            property bool keepNext: true
                            width: cp.width; height: grpName.height + cp.g6
                            Text { id: grpName; y: cp.g5; width: parent.width; text: grp.modelData.name || ""; font: cp.ro.fSmallM; color: cp.ro.ink2 }
                        }
                        Repeater {
                            model: grp.rows
                            delegate: Item {
                                id: rw
                                required property var modelData
                                objectName: "reportConsistencyRow"
                                readonly property string rowKey: modelData.key || ""
                                readonly property string tag: modelData.tag || ""
                                readonly property string value: modelData.value || "-"
                                readonly property string fired: modelData.fired === true ? (modelData.firedWord || "") : ""
                                readonly property string caution: modelData.cautionText || ""
                                property bool atom: true
                                width: cp.width
                                height: line.height + (under.visible ? under.height : 0) + (cp ? cp.g6 : 0)   // a delegate outlives the page at teardown
                                readonly property var typicality: modelData.typicality || null
                                Row {
                                    id: line
                                    y: cp.g3
                                    spacing: cp.gutter
                                    Text { width: cp.labelW; text: rw.modelData.label || ""; font: cp.ro.fSmall; color: cp.ro.ink; wrapMode: Text.WordWrap }
                                    Text { width: cp.atW; text: rw.tag; font: cp.ro.fSmall; color: cp.ro.ink3 }
                                    Text { width: cp.valueW; horizontalAlignment: Text.AlignRight; text: rw.value; font: cp.ro.fFigM; wrapMode: Text.WordWrap
                                           color: rw.modelData.fired === true ? cp.ro.warn : rw.value !== "-" ? cp.ro.ink : cp.ro.ink3 }
                                    Text { width: cp.medianW; horizontalAlignment: Text.AlignRight; text: rw.modelData.median || "-"; font: cp.ro.fFigM; color: cp.ro.ink2 }
                                    Text { width: cp.placeW; text: rw.modelData.place || ""; font: cp.ro.fSmall; color: cp.ro.ink2; wrapMode: Text.WordWrap }
                                }
                                // its typicality's word (golfer layer, R8); a fired value is a mark AND a word; a ⚠ stays with its reading
                                Row {
                                    id: under
                                    // inset: an Unusual ring sits past the icon's edge (the mark's geometry)
                                    x: cr.u(3)
                                    y: line.y + line.height
                                    visible: rowBadge.visible || underText.text !== ""
                                    spacing: cp.g6
                                    PpVariabilityBadge {
                                        id: rowBadge
                                        objectName: "reportConsistencyTypicality"
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: !!rw.typicality
                                        variant: PpVariabilityMark.Typicality
                                        dense: true
                                        contentLayer: "golfer"
                                        values: rw.typicality
                                        roles: cr
                                    }
                                    Text { id: underText; anchors.verticalCenter: parent.verticalCenter
                                           width: cp.width - under.x - (rowBadge.visible ? rowBadge.width + cp.g6 : 0); visible: text !== ""
                                           text: rw.fired + (rw.fired !== "" && rw.caution !== "" ? "  ·  " : "") + rw.caution
                                           font: cp.ro.fMicro; color: cp.ro.ink2; wrapMode: Text.WordWrap }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: cp.ro.hw; color: cp.ro.hair }
                            }
                        }
                    }
                }
                Item {
                    property bool atom: true
                    property bool keepPrev: true
                    visible: !!sw.modelData.more
                    width: cp.width; height: visible ? moreText.height + cp.g3 : 0
                    Text { id: moreText; y: cp.g3; width: parent.width; text: sw.modelData.more || ""; font: cp.ro.fMicro; color: cp.ro.ink3; wrapMode: Text.WordWrap }
                }
            }
        }
    }

    // ═══ the swings past the cap, and the same-shot note, said once ═══
    Column {
        objectName: "reportConsistencyFoot"
        property bool atom: true
        property bool keepPrev: true
        visible: cp.on && cp.flagged.length > 0 && (moreFlagged.visible || footNote.visible)
        width: cp.width
        spacing: cr.g(3)
        Item { width: 1; height: cr.g(4) }
        Text { id: moreFlagged; width: parent.width; visible: text !== ""; text: cp.m.flaggedMore || ""; font: cp.ro.fSmall; color: cp.ro.ink3; wrapMode: Text.WordWrap }
        Text { id: footNote; width: parent.width; visible: text !== ""; text: cp.m.note || ""; font: cp.ro.fSmall; color: cp.ro.ink2; wrapMode: Text.WordWrap }
    }

    // the Charts print roles in Folio light: the table's design values and inks, and this page's units. LAST in the Column:
    // children are destroyed first-declared first, so everything above goes before its roles (see PpPosturePage).
    PpChRoles { id: cr; medium: "print"; fixedAesthetic: "folio" }
}
