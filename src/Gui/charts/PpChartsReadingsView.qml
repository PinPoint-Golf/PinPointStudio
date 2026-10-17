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

// READINGS — every reading against the session, as its own tab (Mark, 17 Oct 2026: it lets you look at any curve,
// and at the foot of This shot nobody found it). By family, two columns at wide, with a filter on the name. A row
// opens IN PLACE: the picked shot's curve with the session's box at every P-position, or, for a reading with no
// curve, its run through the session (PpChProfile; the relations presenter's thisShot.groups and openRow). It
// answers for the shot and the session at once, so both registers have it.
// Beside the name, this shot's verdict (Mark, 17 Oct 2026: "just glance at the readings that need attention"): All ·
// Faults (past the fault line) · Outside (out of the corridor, faults included) · Inside (graded clean), each with its
// count under the name filter; a reading no condition graded shows under All only (row.verdict, the presenter's).
// objectName: chReadingsView, chReadingsFilter, chReadingsVerdict:<all|fault|outside|inside>.
Column {
    id: rv
    objectName: "chReadingsView"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    property real pageH: 500
    readonly property var rs: body ? body.rShot : ({})
    readonly property bool available: !!rs && rs.available === true && (rs.groups || []).length > 0
    property string filter: ""
    property string verdict: "all"               // all | fault | outside | inside
    spacing: roles ? roles.u(12) : 12

    function inVerdict(r, v) {
        return v === "all" || (v === "outside" ? (r.verdict === "fault" || r.verdict === "outside") : r.verdict === v)
    }
    // the groups with the rows whose name holds the filter (case-insensitive); a family left empty goes
    readonly property var named: {
        const gs = available ? (rs.groups || []) : []
        const f = filter.trim().toLowerCase()
        if (f === "") return gs
        const out = []
        for (const g of gs) {
            const rows = (g.rows || []).filter(function (r) {
                return String(r.label || "").toLowerCase().indexOf(f) >= 0 || String(r.short || "").toLowerCase().indexOf(f) >= 0
                       || String(g.name || "").toLowerCase().indexOf(f) >= 0 })
            if (rows.length) out.push(Object.assign({}, g, { rows: rows }))
        }
        return out
    }
    // … and in the verdict picked
    readonly property var groups: {
        if (verdict === "all") return named
        const out = []
        for (const g of named) {
            const rows = (g.rows || []).filter(function (r) { return rv.inVerdict(r, rv.verdict) })
            if (rows.length) out.push(Object.assign({}, g, { rows: rows }))
        }
        return out
    }
    // the count under each verdict, of the rows the name lets through
    readonly property var counts: {
        const n = { all: 0, fault: 0, outside: 0, inside: 0 }
        for (const g of named) for (const r of (g.rows || [])) {
            ++n.all
            if (r.verdict === "fault") { ++n.fault; ++n.outside }
            else if (r.verdict === "outside") ++n.outside
            else if (r.verdict === "inside") ++n.inside
        }
        return n
    }
    readonly property int shownCount: { let n = 0; for (const g of groups) n += (g.rows || []).length; return n }
    readonly property int allCount: { let n = 0; for (const g of (available ? rs.groups || [] : [])) n += (g.rows || []).length; return n }
    // nothing graded on this shot (no ledger, no shot picked): the verdicts have nothing to say
    readonly property bool graded: { for (const g of (available ? rs.groups || [] : [])) for (const r of (g.rows || [])) if (r.verdict) return true; return false }

    PpChText { objectName: "chReadingsWhy"; visible: !rv.available && !!rv.body && present; roles: rv.roles
               sentence: rv.body && rv.body.rStatus ? rv.body.rStatus.refusal : null
               font: rv.roles ? rv.roles.fBody : Qt.font({}); tone: rv.roles ? rv.roles.ink2 : "grey" }
    Column {
        visible: rv.available
        width: rv.width; spacing: rv.roles ? rv.roles.u(4) : 4
        Text { visible: !rv.cast && !!rv.body && !!rv.body.rStatus && !!rv.body.rStatus.kicker
               text: rv.body && rv.body.rStatus ? (rv.body.rStatus.kicker || "") : ""
               font: rv.roles ? rv.roles.fKicker : Qt.font({}); color: rv.roles ? rv.roles.ink3 : "grey" }
        Text { width: parent.width; wrapMode: Text.WordWrap; text: "Every reading against the session"
               font: rv.roles ? rv.roles.fTitle : Qt.font({}); color: rv.roles ? rv.roles.ink : "black" }
        Text { width: parent.width; wrapMode: Text.WordWrap
               text: "Pick a reading to see this shot's curve against the session's at every P-position; a reading without a curve shows its run through the session."
               font: rv.roles ? rv.roles.fStand : Qt.font({}); color: rv.roles ? rv.roles.ink2 : "grey" }
    }
    // the filter: one quiet line — the name, then the verdicts with their counts (the count alone when nothing is graded);
    // it wraps under the name when narrow
    Flow {
        visible: rv.available && !rv.cast
        width: rv.width
        spacing: rv.roles ? rv.roles.u(12) : 12
        Item {
            width: rv.roles ? rv.roles.u(260) : 260; height: fi.implicitHeight + (rv.roles ? rv.roles.u(8) : 8)
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: rv.roles ? rv.roles.hw : 1; color: rv.roles ? rv.roles.hair : "grey" }
            TextInput {
                id: fi
                objectName: "chReadingsFilter"
                anchors.verticalCenter: parent.verticalCenter; width: parent.width
                enabled: rv.interactive
                font: rv.roles ? rv.roles.fBody : Qt.font({}); color: rv.roles ? rv.roles.ink : "black"
                selectByMouse: true; clip: true
                onTextChanged: rv.filter = text
                Keys.onEscapePressed: text = ""
                Text { visible: fi.text === "" && !fi.activeFocus; text: "Filter by name"; font: fi.font; color: rv.roles ? rv.roles.ink3 : "grey" }
            }
        }
        Text { visible: !rv.graded && rv.verdict === "all"
               height: fi.implicitHeight + (rv.roles ? rv.roles.u(8) : 8); verticalAlignment: Text.AlignBottom; bottomPadding: rv.roles ? rv.roles.u(5) : 5
               text: rv.filter.trim() === "" ? rv.allCount + " readings" : rv.shownCount + " of " + rv.allCount
               font: rv.roles ? rv.roles.fSmall : Qt.font({}); color: rv.roles ? rv.roles.ink3 : "grey" }
        // the verdicts: a word and its count each, the picked one in ink and underlined; one with nothing under it rests
        Row {
            visible: rv.graded || rv.verdict !== "all"
            height: fi.implicitHeight + (rv.roles ? rv.roles.u(8) : 8)
            spacing: rv.roles ? rv.roles.u(14) : 14
            Repeater {
                model: [{ v: "all", word: "All" }, { v: "fault", word: "Faults" }, { v: "outside", word: "Outside" }, { v: "inside", word: "Inside" }]
                delegate: Item {
                    id: chip
                    required property var modelData
                    readonly property int n: rv.counts[modelData.v] || 0
                    readonly property bool on: rv.verdict === modelData.v
                    readonly property bool live: on || n > 0
                    readonly property bool warm: n > 0 && (modelData.v === "fault" || modelData.v === "outside")
                    objectName: "chReadingsVerdict:" + modelData.v
                    width: cr.implicitWidth; height: parent.height
                    Row {
                        id: cr
                        anchors.bottom: parent.bottom; anchors.bottomMargin: rv.roles ? rv.roles.u(5) : 5
                        spacing: rv.roles ? rv.roles.u(4) : 4
                        Text { id: cw; text: chip.modelData.word; font: rv.roles ? rv.roles.fSmall : Qt.font({})
                               color: !rv.roles ? "grey" : chip.on ? rv.roles.ink : chip.live ? rv.roles.ink2 : rv.roles.ink3 }
                        Text { anchors.baseline: cw.baseline; text: chip.n; font: rv.roles ? rv.roles.fFigLabel : Qt.font({})
                               color: !rv.roles ? "grey" : chip.warm ? rv.roles.warn : chip.on ? rv.roles.ink : rv.roles.ink3 }
                    }
                    Rectangle { visible: chip.on; anchors.bottom: parent.bottom; width: parent.width
                                height: rv.roles ? Math.max(1, 2 * rv.roles.hw) : 2; color: rv.roles ? rv.roles.ink : "black" }
                    MouseArea { anchors.fill: parent; enabled: rv.interactive && chip.live; hoverEnabled: true
                                cursorShape: chip.live ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: rv.verdict = chip.modelData.v }
                }
            }
        }
    }
    Text { visible: rv.available && rv.groups.length === 0; width: rv.width; wrapMode: Text.WordWrap
           text: rv.named.length === 0 ? "No reading's name holds “" + rv.filter.trim() + "”."
               : rv.verdict === "fault" ? "No reading" + (rv.filter.trim() ? " named “" + rv.filter.trim() + "”" : "") + " is past its fault line on this shot."
               : rv.verdict === "outside" ? "No reading" + (rv.filter.trim() ? " named “" + rv.filter.trim() + "”" : "") + " is out of its corridor on this shot."
               : "No reading" + (rv.filter.trim() ? " named “" + rv.filter.trim() + "”" : "") + " was graded inside its corridor on this shot."
           font: rv.roles ? rv.roles.fBody : Qt.font({}); color: rv.roles ? rv.roles.ink3 : "grey" }
    // by family, two balanced columns at wide, never splitting a family
    Flow {
        id: pf
        visible: rv.available
        width: rv.width; spacing: rv.roles ? rv.roles.u(28) : 28
        readonly property bool two: !!rv.roles && rv.roles.sizeClass(width) === "wide"
        readonly property real colW: two ? Math.floor((width - spacing) / 2) : width
        readonly property var cols: {
            const gs = rv.groups
            if (!two) return [gs]
            let total = 0; for (const g of gs) total += (g.rows || []).length + 1
            let acc = 0, cut = 0, bd = 1e9
            for (let i = 0; i < gs.length; ++i) { acc += (gs[i].rows || []).length + 1; const d = Math.abs(acc - total / 2); if (d < bd) { bd = d; cut = i + 1 } }
            return [gs.slice(0, cut), gs.slice(cut)]
        }
        Repeater {
            model: pf.cols
            delegate: PpChProfile {
                required property var modelData
                width: pf.colW; roles: rv.roles; brush: rv.brush; interactive: rv.interactive
                presenter: rv.body ? rv.body.relationsP : null
                groups: modelData
                shotLabel: rv.body && rv.body.rStatus ? (rv.body.rStatus.shotLabel || "") : ""
                countLabel: !rv.body || !rv.body.rStatus ? "" : rv.roles && !rv.roles.atLeast(pf.colW, "compact") ? (rv.body.rStatus.countLabelShort || "") : (rv.body.rStatus.countLabel || "")
                openId: rv.body ? rv.body.openId : ""; openRow: rv.body ? rv.body.rOpen : null
                playMs: rv.body && rv.body.rStatus && rv.body.rStatus.pickedImpactUs >= 0 && rv.body.playUs >= 0 ? (rv.body.playUs - rv.body.rStatus.pickedImpactUs) / 1000 : NaN
                playText: rv.body && rv.body.rPlayhead ? (rv.body.rPlayhead.openValueText || "") : ""
                onRowActivated: (id) => { if (rv.body) rv.body.openReading(id) }
                onShotActivated: (o) => { if (rv.body) rv.body.pickOrdinal(o) }
            }
        }
    }
}
