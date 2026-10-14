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

// RELATIONS — "what moves together, and what drifts?" (design §2.3, B).
//   Analyse: the head ("What moves with clubhead speed") and its standfirst; quiet Up / Across / Facet
//     controls whose reading pickers open IN PLACE (no menu, no dialog); the key; the scatter of the two
//     readings (or, faceted by a fault, its small multiples, each with its own r and the facet's sentence)
//     beside the Up reading's run and box; the chance line said out loud; the "Moves with" ledger (ten, then
//     the rest on demand); its note; up to three questions; the family matrix; the drifts that clear chance.
//   Replay: the head and its short form, the scatter, the chance sentence and the top three "moves with".
// Every sentence ends where the presenter ends it: co-occurrence, not a cause. The presenter's refusal (n < 3)
// is what an empty view says.
// objectName: chRelationsView.
Column {
    id: rv
    objectName: "chRelationsView"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    property real pageH: 500
    property string openPicker: ""               // renders only: "up" | "across"
    readonly property bool replay: reg === "Replay"
    readonly property var rl: body ? body.rRel : ({})
    readonly property bool available: !!rl && rl.available === true
    readonly property bool side: !!roles && width >= 66 * roles.em
    property string pickerOpen: ""
    readonly property string picker: openPicker !== "" ? openPicker : pickerOpen
    property bool allRows: false
    spacing: roles ? roles.u(10) : 10

    PpChText { objectName: "chRelationsWhy"; visible: !rv.available && present; roles: rv.roles; sentence: rv.rl ? rv.rl.refusal : null
               font: rv.roles ? rv.roles.fBody : Qt.font({}); tone: rv.roles ? rv.roles.ink2 : "grey" }
    Column {
        visible: rv.available
        width: rv.width; spacing: rv.roles ? rv.roles.u(4) : 4
        Text { visible: !rv.cast && !!rv.body && !!rv.body.rStatus.kicker; text: rv.body ? (rv.body.rStatus.kicker || "") : ""; font: rv.roles ? rv.roles.fKicker : Qt.font({}); color: rv.roles ? rv.roles.ink3 : "grey" }
        PpChText { roles: rv.roles; sentence: rv.rl.head; measure: false; font: rv.roles ? rv.roles.fTitle : Qt.font({}); tone: rv.roles ? rv.roles.ink : "black"; lineHeight: 1.08 }
        PpChText { roles: rv.roles; sentence: rv.replay ? rv.rl.short : rv.rl.standfirst; font: rv.replay ? (rv.roles ? rv.roles.fSmall : Qt.font({})) : (rv.roles ? rv.roles.fStand : Qt.font({}))
                   lineHeight: rv.roles ? rv.roles.r.leading : 1 }
    }
    // the quiet axis controls: each value a link that opens its picker in place
    PpChChoices {
        objectName: "chRelationsControls"
        visible: rv.available && !rv.replay
        width: rv.width; roles: rv.roles; interactive: rv.interactive
        sets: !rv.available ? [] : [
            { key: "up", label: "Up", opens: true, open: rv.picker === "up", options: [{ key: "open", label: rv.rl.up ? rv.rl.up.text : "-" }] },
            { key: "across", label: "Across", opens: true, open: rv.picker === "across", options: [{ key: "open", label: rv.rl.across ? rv.rl.across.text : "-" }] },
            { key: "facet", label: "Facet", current: rv.body ? rv.body.relFacet : "",
              options: [{ key: "", label: "none" }].concat((rv.rl.facets || []).map(function (f) { return { key: f.id, label: f.label } })) }
        ]
        onPicked: (set, opt) => {
            if (set === "up" || set === "across") { rv.pickerOpen = rv.pickerOpen === set ? "" : set; return }
            if (set === "facet" && rv.body) { rv.body.choose("relFacet", opt); rv.body.clearAsk(rv.brush) }
        }
    }
    PpChReadingPicker {
        objectName: rv.picker === "up" ? "chUpPicker" : "chAcrossPicker"
        visible: !rv.replay && rv.picker !== ""
        open: visible; width: rv.width; roles: rv.roles; interactive: rv.interactive
        title: rv.picker === "up" ? "The reading up the side" : "The reading across the foot"
        catalogue: rv.available ? (rv.rl.catalogue || []) : []
        current: rv.picker === "up" ? (rv.rl.upId || "") : (rv.rl.acrossId || "")
        exclude: rv.picker === "up" ? (rv.rl.acrossId || "") : (rv.rl.upId || "")
        onPicked: (id) => { if (rv.body) { if (rv.picker === "up") rv.body.chooseUp(id); else rv.body.choose("acrossId", id) } rv.pickerOpen = "" }
    }
    PpPageKey {
        id: chKey1
        // the Charts words for the marks (PpPageKey.extraWords, live from the final build; until then the
        // module's PpPageKey has no such property and keeps its own words)
        Component.onCompleted: if (chKey1.extraWords !== undefined) chKey1.extraWords = { clean: "a shot past the whiskers", fired: "the facet's fault fired on it" }
        visible: rv.available && !rv.replay
        width: rv.width; roles: rv.roles
        kinds: rv.body && rv.body.relFacet !== "" && rv.rl.facet && rv.rl.facet.kind === "condition" ? ["ring", "fired", "clean", "box", "fit", "chance"] : ["ring", "clean", "box", "fit", "chance"]
    }
    // the figure row: the pair (or its facet panels) beside the Up reading through the session
    PpChText { visible: rv.available && !rv.replay && present; roles: rv.roles; sentence: rv.rl.facet ? rv.rl.facet.sentence : null; tone: rv.roles ? rv.roles.ink2 : "grey" }
    Flow {
        visible: rv.available
        width: rv.width; spacing: rv.roles ? rv.roles.u(24) : 24
        readonly property real half: rv.side && !rv.replay ? Math.floor((width - (rv.roles ? rv.roles.u(24) : 24)) * 0.55) : width
        Item {
            id: figBox
            width: parent.half; height: fcol.height
            readonly property var panels: rv.rl.facet && rv.rl.facet.panels ? rv.rl.facet.panels : null
            readonly property bool across: !!panels && panels.length > 1 && !!rv.roles && width >= 2 * rv.roles.flows.facet * rv.roles.em
            Flow {
                id: fcol
                width: parent.width; spacing: rv.roles ? rv.roles.u(10) : 10
                Repeater {
                    model: !rv.available ? [] : figBox.panels && !rv.replay ? figBox.panels : [{ title: "", scatter: rv.rl.scatter }]
                    delegate: PpChScatter {
                        required property var modelData
                        required property int index
                        width: figBox.panels && figBox.across && !rv.replay ? Math.floor((fcol.width - rv.roles.u(10)) / 2) : fcol.width
                        roles: rv.roles; brush: rv.brush; ticker: rv.body ? rv.body.relationsP : null; interactive: rv.interactive
                        scatter: modelData.scatter; title: modelData.title || ""
                        showYLabels: !(figBox.across && index > 0)
                        plotH: rv.roles ? rv.roles.u(figBox.panels ? 130 : rv.replay ? Math.max(110, Math.min(170, rv.pageH * 0.35 / rv.roles.u(1))) : 176) : 170
                        onShotActivated: (o) => { if (rv.body) rv.body.pickOrdinal(o) }
                    }
                }
            }
        }
        PpChRunBox {
            visible: rv.available && !rv.replay
            width: rv.side ? parent.width - parent.half - (rv.roles ? rv.roles.u(24) : 24) : parent.width
            roles: rv.roles; brush: rv.brush; ticker: rv.body ? rv.body.relationsP : null; interactive: rv.interactive
            run: rv.available ? rv.rl.upRun : null
            plotH: rv.roles ? rv.roles.u(rv.side ? 150 : 100) : 100
            onShotActivated: (o) => { if (rv.body) rv.body.pickOrdinal(o) }
        }
    }
    PpChText { objectName: "chChance"; visible: rv.available && present; roles: rv.roles; sentence: rv.rl.chance ? (rv.replay ? rv.rl.chance.sentence : rv.rl.chance.line) : null
               tone: rv.roles ? rv.roles.ink2 : "grey" }
    PpSectionHead { visible: rv.available && !rv.replay; width: rv.width; roles: rv.roles
                    title: rv.rl.head ? rv.rl.head.text : ""; sub: "ranked by |r|; a row puts it across the foot" }
    PpChMovesWith {
        visible: rv.available
        width: rv.width; roles: rv.roles; interactive: rv.interactive && !rv.replay
        rows: !rv.available ? [] : rv.replay ? (rv.rl.ledger || []).slice(0, 3) : rv.allRows ? (rv.rl.ledger || []) : (rv.rl.ledger || []).slice(0, 10)
        acrossId: rv.rl.acrossId || ""
        onRowActivated: (id) => { if (rv.body) rv.body.choose("acrossId", id) }
    }
    Text {
        visible: rv.available && !rv.replay && (rv.rl.ledger || []).length > 10
        text: rv.allRows ? "Show the first ten" : (rv.rl.restText || "")
        font: rv.roles ? (rv.interactive ? rv.roles.fSmallL : rv.roles.fSmall) : Qt.font({}); color: rv.roles ? (rv.interactive ? rv.roles.link : rv.roles.ink3) : "grey"
        MouseArea { anchors.fill: parent; enabled: rv.interactive; cursorShape: Qt.PointingHandCursor; onClicked: rv.allRows = !rv.allRows }
    }
    PpChText { visible: rv.available && !rv.replay && present; roles: rv.roles; sentence: rv.rl.ledgerNote; tone: rv.roles ? rv.roles.ink3 : "grey" }
    PpChQuestions {
        visible: rv.available && !rv.replay
        width: rv.width; roles: rv.roles; interactive: rv.interactive
        questions: rv.available ? (rv.rl.asks || []) : []
        active: rv.body ? rv.body.askedId : ""
        onAsked: (q) => { if (rv.body) rv.body.ask(q, rv.brush) }
    }
    PpSectionHead { visible: rv.available && !rv.replay && !!rv.rl.matrix && (rv.rl.matrix.readings || []).length >= 3; width: rv.width; roles: rv.roles
                    title: rv.rl.matrix ? (rv.rl.matrix.group || "") : ""; sub: "r between every pair; a figure puts the pair on the scatter" }
    PpChMatrix {
        visible: rv.available && !rv.replay && n >= 3
        width: rv.width; roles: rv.roles; interactive: rv.interactive
        matrix: rv.available ? rv.rl.matrix : null
        onPairActivated: (x, y) => { if (rv.body) { rv.body.chooseUp(y); rv.body.choose("acrossId", x) } }
    }
    PpSectionHead { visible: rv.available && !rv.replay && !!rv.rl.drifts; width: rv.width; roles: rv.roles; title: "Drifting through the session"; sub: "each reading against shot order" }
    PpChText { visible: rv.available && !rv.replay && present; roles: rv.roles; sentence: rv.rl.drifts ? rv.rl.drifts.sentence : null }
    Flow {
        id: drf
        objectName: "chDrift"
        visible: rv.available && !rv.replay
        width: rv.width; spacing: rv.roles ? rv.roles.u(18) : 18
        readonly property int cols: rv.roles ? rv.roles.columns(width, rv.roles.flows.facet, rv.roles.u(18)) : 1
        Repeater {
            model: rv.available && !rv.replay && rv.rl.drifts ? (rv.rl.drifts.items || []).slice(0, drf.cols * 2) : []
            delegate: Column {
                required property var modelData
                width: Math.floor((drf.width - (drf.cols - 1) * rv.roles.u(18)) / drf.cols)
                spacing: rv.roles.u(2)
                PpChText { roles: rv.roles; sentence: modelData.sentence; measure: false; width: parent.width; tone: rv.roles.ink }
                PpChRunBox {
                    width: parent.width; roles: rv.roles; brush: rv.brush; ticker: rv.body ? rv.body.relationsP : null; interactive: rv.interactive
                    run: modelData.run; showCaption: false; plotH: rv.roles.u(70)
                    onShotActivated: (o) => { if (rv.body) rv.body.pickOrdinal(o) }
                }
            }
        }
    }
}
