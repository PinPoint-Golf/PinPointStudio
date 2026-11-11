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

// TENDENCIES — the Your tendencies page of the session report (docs/design/shot_outcomes_design.md §7.2): Home's rows on
// paper, as they stood at this session. After the order of work; absent when the map has no `tendencies`
// (report_tendencies.h). A row per club and swing length: club · usual distance · strike · direction · flight, each factor
// its usual over its small band bar and the bar's words, with what each factor's fault is seen with, or that it is still
// building, beneath (only the factors with something to say).
// FLOWED as the Consistency page: the head starts a fresh page, each row is one atom, the column heads keep with the first
// row. Every sentence is the map's. FOLIO LIGHT through `ro` and `cr`: nothing reads a live theme token. Colour is never
// the only channel: a fault's label, building or earned, is in the fault tone and named; Low flight's, a value to watch,
// in the watch tone; the bar has its words. A shot-mix part's tone by its kind, as Home's Tendencies row (Mark, 10 Nov
// 2026: one scheme everywhere): good → good, value → cool, watch → attention (amber), every fault (main, other) → warn.
// objectNames: reportTendencies (the head), reportTendencyRow, reportTendencyFoot.
Column {
    id: tp
    property QtObject ro: null
    property var td: null
    readonly property var m: td || ({})
    readonly property bool on: !!td
    readonly property var rows: m.rows || []
    visible: on
    width: 600

    readonly property real gutter: cr.g(8)
    readonly property real g3: cr.g(3)
    readonly property real g5: cr.g(5)
    readonly property real g6: cr.g(6)
    readonly property real clubW: cr.u(92)
    readonly property real distanceW: cr.u(84)
    readonly property real factorW: Math.max(0, (width - clubW - distanceW - 4 * gutter) / 3)
    readonly property real barH: cr.u(5)
    readonly property color toneGood: cr.good
    readonly property color toneValue: cr.cool
    readonly property color toneMain: cr.warn
    readonly property color toneOther: cr.attention
    readonly property var factors: [{ key: "strike", label: "Strike" }, { key: "direction", label: "Direction" },
                                    { key: "flight", label: "Flight" }]

    function tone(kind) { return kind === "good" ? toneGood : kind === "value" ? toneValue : kind === "watch" ? toneOther : toneMain }
    function factorOf(row, key) { return row && row[key] ? row[key] : ({}) }
    // each factor's seen line, named, for the factors with something seen
    function seenLines(row) {
        return factors.map(f => ({ label: f.label, seen: String(factorOf(row, f.key).seen || "") }))
                      .filter(x => x.seen !== "").map(x => x.label + ": " + x.seen).join("\n")
    }

    PpReportSectionHead {
        objectName: "reportTendencies"
        ro: tp.ro; kicker: tp.m.kicker || ""; title: "Your tendencies"; standfirst: tp.m.standfirst || ""
        visible: tp.on
        width: tp.width
        Item { width: 1; height: cr.g(12) }
    }

    Item {
        property bool atom: true
        property bool keepNext: true
        visible: tp.on
        width: tp.width
        height: headRow.height + tp.g5
        Row {
            id: headRow
            spacing: tp.gutter
            Text { width: tp.clubW; text: "Club"; font: tp.ro.fLabel; color: tp.ro.ink3 }
            Text { width: tp.distanceW; text: "Usual distance"; font: tp.ro.fLabel; color: tp.ro.ink3 }
            Repeater {
                model: tp.factors
                Text { required property var modelData; width: tp.factorW; text: modelData.label; font: tp.ro.fLabel; color: tp.ro.ink3 }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: tp.ro.hw; color: tp.ro.hair }
    }

    Repeater {
        model: tp.rows
        delegate: Item {
            id: rw
            required property var modelData
            objectName: "reportTendencyRow"
            readonly property string rowKey: modelData.key || ""
            property bool atom: true
            width: tp.width
            height: line.y + line.height + (seenText.visible ? seenText.height + (tp ? tp.g3 : 0) : 0) + (tp ? tp.g6 : 0)   // a delegate outlives `tp` at teardown
            Row {
                id: line
                y: tp.g6
                spacing: tp.gutter
                Text { width: tp.clubW; text: rw.modelData.club || ""; font: tp.ro.fBody; color: tp.ro.ink; wrapMode: Text.WordWrap }
                Text { width: tp.distanceW; text: rw.modelData.distance || "-"; font: tp.ro.fFig; color: rw.modelData.distance ? tp.ro.ink : tp.ro.ink3 }
                Repeater {
                    model: tp.factors
                    delegate: Column {
                        id: cellCol
                        required property var modelData
                        readonly property var fx: tp.factorOf(rw.modelData, modelData.key)
                        readonly property string label: fx.label && fx.label !== "-" ? String(fx.label) : "-"
                        width: tp.factorW
                        spacing: tp.g3
                        Text {
                            width: parent.width
                            text: cellCol.label
                            font: tp.ro.fBody
                            color: cellCol.label === "-" ? tp.ro.ink3 : cellCol.fx.fault ? tp.toneMain : cellCol.fx.watch ? tp.toneOther : tp.ro.ink
                            wrapMode: Text.WordWrap
                        }
                        PpBandBar {
                            visible: (cellCol.fx.mixParts || []).length > 0
                            width: parent.width
                            height: tp.barH
                            segments: (cellCol.fx.mixParts || []).map(p => ({ count: p.count, tone: tp.tone(p.kind) }))
                        }
                        Text { visible: text !== ""; width: parent.width; text: cellCol.fx.mixLine || ""; font: tp.ro.fSmall; color: tp.ro.ink2; wrapMode: Text.WordWrap }
                    }
                }
            }
            Text {
                id: seenText
                y: line.y + line.height + (tp ? tp.g3 : 0)
                visible: text !== ""
                width: parent.width
                text: tp ? tp.seenLines(rw.modelData) : ""
                font: tp.ro.fSmall; color: tp.ro.ink2; wrapMode: Text.WordWrap
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: tp.ro ? tp.ro.hw : 1; color: tp.ro ? tp.ro.hair : "transparent" }
        }
    }

    Item {
        objectName: "reportTendencyFoot"
        property bool atom: true
        property bool keepPrev: true
        visible: tp.on && (tp.m.foot || "") !== ""
        width: tp.width
        height: footText.y + footText.height + tp.g6
        Text { id: footText; y: tp.g6; width: parent.width; text: tp.m.foot || ""; font: tp.ro.fSmall; color: tp.ro.ink3; wrapMode: Text.WordWrap }
    }

    // THE FLOW (tendencies_flow_design.md §6.1): every club on the page, strike → direction → flight, one atom under the
    // table, the map Home's flow reads (report_tendencies.h `flow`). Folio light through `cr`, as the rest of the page; each
    // ribbon group carries a greyscale-safe pattern over its colour (ideal plain, flight off dotted, off line hatched,
    // mis-struck cross-hatched) and the legend shows the same, so a mono print still reads. Under the minimum, one line.
    readonly property var flow: tp.m.flow || null
    function flowTone(g)    { return g === 0 ? cr.good : g === 1 ? cr.attention : cr.warn }
    function flowStrength(g) { return g === 0 ? 0.62 : g === 1 ? 0.6 : g === 2 ? 0.42 : 0.78 }
    function flowPattern(g) { return g === 0 ? "" : g === 1 ? "dots" : g === 2 ? "hatch" : "cross" }
    function flowNodeTone(kind) { return kind === "good" ? cr.good : kind === "watch" ? cr.attention : kind === "fault" ? cr.warn : tp.ro.ink }
    Item {
        id: flowBlock
        objectName: "reportTendencyFlow"
        property bool atom: true
        visible: tp.on && !!tp.flow && (tp.flow.total || 0) + (tp.flow.excluded || 0) > 0
        width: tp.width
        height: flowCol.y + flowCol.height + tp.g6
        Column {
            id: flowCol
            y: cr.g(14)
            width: parent.width
            spacing: tp.g5
            Text { width: parent.width; text: tp.flow ? (tp.flow.micro || "").toUpperCase() : ""; font: tp.ro.fLabel; color: tp.ro.ink3 }
            Text {
                visible: !!tp.flow && !!tp.flow.enough
                width: parent.width
                text: tp.flow ? tp.flow.headline || "" : ""
                font: tp.ro.fBodyM; color: tp.ro.ink; wrapMode: Text.WordWrap
            }
            Text {
                visible: !!tp.flow && !!tp.flow.enough
                width: parent.width
                text: tp.flow ? tp.flow.subtitle || "" : ""
                font: tp.ro.fSmall; color: tp.ro.ink2; wrapMode: Text.WordWrap
            }
            PpSankey {
                objectName: "reportTendencyFlowSankey"
                visible: !!tp.flow && !!tp.flow.enough
                width: parent.width
                roles: cr
                patterns: true
                plotHeight: cr.u(250)
                layoutHeight: tp.flow ? tp.flow.layoutHeight || 300 : 300
                nodeWidth: cr.u(8)
                labelGutter: cr.u(108)
                columns: tp.flow ? tp.flow.columns || [] : []
                nodes: tp.flow ? (tp.flow.nodes || []).map(n => Object.assign({}, n, { tone: tp.flowNodeTone(n.kind) })) : []
                links: tp.flow ? (tp.flow.links || []).map(l => Object.assign({}, l, { tone: tp.flowTone(l.group), strength: tp.flowStrength(l.group),
                                                                                     pattern: tp.flowPattern(l.group) })) : []
                legend: tp.flow ? (tp.flow.groups || []).filter(g => g.count > 0)
                                      .map(g => ({ tone: tp.flowTone(g.group), strength: tp.flowStrength(g.group), pattern: tp.flowPattern(g.group),
                                                   name: g.name, line: g.line })) : []
            }
            Text {
                visible: text !== ""
                width: parent.width
                text: !tp.flow ? "" : !tp.flow.enough ? (tp.flow.emptyLine || "") : (tp.flow.excludedLine || "")
                font: tp.ro.fSmall; color: tp.ro.ink3; wrapMode: Text.WordWrap
            }
        }
    }

    // the Charts print roles in Folio light: this page's units and tones. LAST in the Column: children are destroyed
    // first-declared first, so everything above goes before its roles (see PpPosturePage).
    PpChRoles { id: cr; medium: "print"; fixedAesthetic: "folio" }
}
