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

// FIGUREROW — C's chart-led detail figure: PpSpreadStrip | PpRunChart | PpHistoryStrip on ONE value axis,
// with the axis labels on the left, the zone labels once at the right of the last panel, and a caption
// under each panel. The three sit in a row while the run panel keeps roles.flows.runPanel em; else
// spread + run, then history, stacked. At wide the notes go to a right margin; below that they follow
// the figure in prose columns (≥ roles.flows.prose em). Same grammar at every size.
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   detail     the open condition (header.spread, header.ticks, history)
//   readout    for the picked shot's label
//   showNotes  draw the notes (margin or below); false when the register sets them elsewhere
//   roles, brush, medium, interactive
//   signals shotActivated(swingDir, shotId), shotHovered(shotId), sessionActivated(sessionDir)
Item {
    id: fg
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var detail: null
    property var readout: null
    property bool showNotes: true
    property bool showTravel: true             // (round 3, optional) the in-chart distance to travel; Analyse states it in its key figures
    property bool atom: true
    objectName: "sdFigureRow"
    readonly property var keyKinds: ["fired", "clean", "ring", "median", "fault", "pass", "corridor"]
    // the run caption READS the hovered shot (F53): run[hovered].readout while a shot is under the pointer,
    // else the picked shot's line (spread.currentReadout)
    readonly property string hoverReadout: {
        const id = brush ? brush.hoveredShot : -1
        if (id < 0) return ""
        const r = (s.run || []).filter(q => q.shotId === id)[0]
        return r ? (r.readout || (r.assessable === false ? "shot " + (r.index + 1) + " · not measured" : "")) : ""
    }
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)
    signal sessionActivated(string sessionDir)

    readonly property var hdr: (detail && detail.header) || ({})
    readonly property var s: hdr.spread || ({})
    readonly property var hist: (detail && detail.history) || ({})
    readonly property bool sym: { const u = String(hist.unit || ""); return u.length > 0 && u.length <= 2 && !/[a-z]/i.test(u) }
    readonly property bool margin: showNotes && roles.shows("margin", width)
    readonly property real marginW: roles.u(236)
    readonly property real mainW: margin ? width - marginW - roles.gutter(width) : width
    readonly property real zoneW: roles.u(96)
    readonly property real axisW: roles.u(34)
    readonly property real pg: roles.u(20)
    readonly property real p3: roles.u(100)
    readonly property bool rowMode: mainW - axisW - roles.u(104) - p3 - 2 * pg - zoneW >= roles.flows.runPanel * roles.em
    readonly property real p1: rowMode && mainW > 60 * roles.em ? roles.u(132) : roles.atLeast(mainW, "compact") ? roles.u(104) : roles.u(84)
    readonly property real p2: rowMode ? Math.max(roles.flows.runPanel * roles.em, Math.min(roles.u(440), mainW - axisW - p1 - p3 - 2 * pg - zoneW))
                                       : Math.max(roles.flows.runPanel * roles.em, mainW - axisW - p1 - pg - zoneW)
    // THREE FORMS, by the width given (house rule 8: below its minimum a chart changes form):
    //   row   — spread | run | history side by side (rowMode, as before)
    //   pair  — spread | run, history under it, while the run keeps ~13 em beside the spread
    //   stack — spread, run and history each alone at the full width; below ~290 px the zone-label
    //           gutter (fault / pass edge / ≈ compressed) moves under the figure as a one-line key
    readonly property real pairMin: axisW + roles.u(84) + pg + zoneW + 13 * roles.em
    readonly property string form: rowMode ? "row" : (mainW >= pairMin ? "pair" : "stack")
    readonly property bool zoneBelow: form === "stack" && mainW < roles.u(232)
    readonly property real stackW: Math.max(roles.u(40), mainW - axisW - (zoneBelow ? 0 : zoneW + roles.u(10)))
    readonly property real topY: roles.u(40)
    // FAULT-FORWARD: a condition that fired spends a full figure on where its shots are; a clean one
    // (opened from the index) gets the economical figure — its story is the margin to the pass edge.
    readonly property var shotFs: {
        const out = []
        const run = (s.run || []).filter(q => q.assessable !== false)
        for (let i = 0; i < run.length; ++i) out.push(run[i].fy)
        const dots = s.dots || []
        for (let i = 0; i < dots.length; ++i) out.push(dots[i].f)
        const cols = hist.columns || []
        for (let i = 0; i < cols.length; ++i) {
            const vs = cols[i].values || []
            for (let j = 0; j < vs.length; ++j) out.push(vs[j].f)
        }
        return out
    }
    readonly property bool isFault: (s.run || []).some(q => q.state === "fired") || (s.dots || []).some(q => q.state === "fired")
    readonly property real fH: medium === "print" ? (isFault ? roles.u(132) : roles.u(92)) : (isFault ? roles.u(170) : roles.u(112))
    readonly property int shotNo: roles.shotNo(readout)

    implicitHeight: Math.max(figCol.height + (notesBelow.visible ? notesBelow.height + roles.u(16) : 0), margin ? notesM.height + roles.u(40) : 0)
    height: implicitHeight

    PpFigureScale { id: sc; bands: fg.s.bands || []; faultLines: fg.s.faultLines || []; yTicks: fg.hist.yTicks || fg.s.yTicks || []; plotTop: fg.topY; plotHeight: fg.fH
                  values: fg.shotFs; axisLo: fg.s.axisLo !== undefined ? fg.s.axisLo : NaN; axisHi: fg.s.axisHi !== undefined ? fg.s.axisHi : NaN
                  bandPx: fg.roles.u(16); breakPx: fg.roles.u(8); clean: !fg.isFault }
    readonly property string unitShort: sym ? (hist.unit || "") : ""
    function bandText(r) {
        // a band says what it holds, in the measure's own units, and that it is compressed
        const edge = r === 0 ? sc.win.lo : sc.win.hi
        const v = sc.valueOf(edge)
        const faults = sc.edgesIn(r).filter(e => e.kind === "fault")
        let t = "≈ " + (r === 0 ? "below " : "above ") + roles.num(v) + unitShort
        if (faults.length) t += "\nfault " + faults.map(e => roles.num(e.value) + unitShort).join(", ")
        return t
    }

    component ValueRow: Item {
        id: vr
        property var kinds: []
        property var widths: []
        property bool showMeasure: true
        readonly property var xs: { let x = fg.axisW; const out = []; for (let i = 0; i < widths.length; ++i) { out.push(x); x += widths[i] + fg.pg } return out }
        readonly property real zX: xs.length ? xs[xs.length - 1] + widths[widths.length - 1] + fg.roles.u(10) : 0
        function caption(k) {
            if (k === "spread") return fg.s.captionShort || ""
            if (k === "run") return fg.hoverReadout || fg.s.currentReadout || ""
            const c = (fg.hist.columns || []).filter(x => x.current)[0]
            return c ? c.label + " · " + c.medianText : ""
        }
        implicitHeight: fg.topY + fg.fH + fg.roles.u(26) + caps.height
        Text {
            visible: vr.showMeasure && fg.form !== "stack"
            y: fg.topY - fg.roles.u(19); width: vr.zX + fg.roles.u(80); elide: Text.ElideNone; wrapMode: Text.WordWrap
            text: (fg.s.measure || fg.hist.measure || "") + (fg.hist.unit && !fg.sym ? ", in " + fg.hist.unit : "")
            font: fg.roles.fSmall; color: fg.roles.ink3
        }
        Repeater {
            model: fg.roles ? (sc.yTicks) : []
            delegate: Text {
                required property var modelData
                objectName: "sdRunYTick"
                visible: sc.tickVisible(modelData.f)
                x: fg.axisW - width - fg.roles.u(8); y: sc.y(modelData.f) - height / 2
                text: modelData.text + (fg.sym ? fg.hist.unit : ""); font: fg.roles.fFigLabel; color: fg.roles.ink3
            }
        }
        Repeater {
            model: fg.roles ? (sc.edges) : []
            delegate: Row {
                required property var modelData
                // pass and aim edges are labelled where the scale is true; a fault line always, marked ≈ in a band
                visible: !fg.zoneBelow && (modelData.kind === "fault" || ((modelData.kind === "pass" || modelData.kind === "aim") && sc.region(modelData.f) === 1))
                x: vr.zX; y: sc.y(modelData.f) - height / 2
                spacing: fg.roles.u(5)
                Text { text: fg.roles.num(modelData.value); font: fg.roles.fFigLabel; color: modelData.kind === "fault" ? fg.roles.warn : fg.roles.ink3 }
                Text { text: modelData.kind === "fault" ? (sc.region(modelData.f) === 1 ? "fault" : "fault ≈") : modelData.kind === "pass" ? "pass edge" : ""; font: fg.roles.fLabel
                       color: modelData.kind === "fault" ? fg.roles.warn : fg.roles.ink3 }
            }
        }
        // each compressed band, labelled once at the right with what it holds; "≈" on the axis
        Repeater {
            model: fg.roles ? [sc.bandLo ? 0 : -1, sc.bandHi ? 2 : -1].filter(r => r >= 0) : []
            delegate: Item {
                required property var modelData
                readonly property real yc: modelData === 0 ? sc.loBandTop + sc.bandPx / 2 : sc.plotTop + sc.bandPx / 2
                Text { visible: !fg.zoneBelow && sc.edgesIn(modelData).filter(e => e.kind === "fault").length === 0
                       x: vr.zX; y: parent.yc - height / 2; text: "≈ compressed"; font: fg.roles.fLabel; color: fg.roles.ink3 }
                Text { x: fg.axisW - width - fg.roles.u(8); y: parent.yc - height / 2; text: "≈"; font: fg.roles.fFigLabel; color: fg.roles.ink3 }
            }
        }
        Repeater {
            model: fg.roles ? (vr.kinds) : []
            delegate: Loader {
                required property var modelData
                required property int index
                x: vr.xs[index] || 0; width: vr.widths[index] || 0; height: vr.implicitHeight
                active: !!fg.roles
                sourceComponent: modelData === "spread" ? cSpread : modelData === "run" ? cRun : cHist
            }
        }
        Row {
            id: caps
            x: fg.axisW; y: fg.topY + fg.fH + fg.roles.u(26)
            spacing: fg.pg
            Repeater {
                model: fg.roles ? (vr.kinds) : []
                delegate: Text {
                    required property var modelData
                    required property int index
                    objectName: modelData === "run" ? "sdDetailHoverReadout" : modelData === "hist" ? "sdHistoryCaption" : "sdSpreadCaption"
                    width: fg.form === "stack" ? fg.mainW - fg.axisW : vr.widths[index] + (index === vr.kinds.length - 1 ? fg.roles.u(60) : 0)
                    wrapMode: Text.WordWrap; font: fg.roles.fSmall; color: fg.roles.ink3; text: vr.caption(modelData)
                }
            }
        }
    }
    Component { id: cSpread; PpSpreadStrip { roles: fg.roles; brush: fg.brush; medium: fg.medium; interactive: fg.interactive; spread: fg.s; scale: sc
                                           onShotActivated: (d, i) => fg.shotActivated(d, i); onShotHovered: i => fg.shotHovered(i) } }
    Component { id: cRun; PpRunChart { roles: fg.roles; brush: fg.brush; medium: fg.medium; interactive: fg.interactive; spread: fg.s; ticks: fg.hdr.ticks || []; scale: sc; unit: fg.unitShort; showDistance: fg.showTravel
                                     leftRoom: parent ? parent.x : 1e6; rightRoom: parent ? fg.mainW - parent.x - parent.width : 1e6
                                     onShotActivated: (d, i) => fg.shotActivated(d, i); onShotHovered: i => fg.shotHovered(i) } }
    Component { id: cHist; PpHistoryStrip { roles: fg.roles; brush: fg.brush; medium: fg.medium; interactive: fg.interactive; history: fg.hist; scale: sc
                                          onSessionActivated: d => fg.sessionActivated(d) } }

    Column {
        id: figCol
        width: fg.mainW
        spacing: fg.roles.u(18)
        // stacked, the measure line may wrap: it stands above the figures instead of inside the first one
        Text {
            visible: fg.form === "stack"
            width: fg.mainW; wrapMode: Text.WordWrap
            text: (fg.s.measure || fg.hist.measure || "") + (fg.hist.unit && !fg.sym ? ", in " + fg.hist.unit : "")
            font: fg.roles.fSmall; color: fg.roles.ink3
        }
        ValueRow { width: fg.mainW
                   kinds: fg.form === "row" ? ["spread", "run", "hist"] : fg.form === "pair" ? ["spread", "run"] : ["spread"]
                   widths: fg.form === "row" ? [fg.p1, fg.p2, fg.p3] : fg.form === "pair" ? [fg.p1, fg.p2] : [fg.stackW] }
        ValueRow { visible: fg.form === "stack"; height: visible ? implicitHeight : 0; showMeasure: false; width: fg.mainW; kinds: fg.form === "stack" ? ["run"] : []; widths: [fg.stackW] }
        ValueRow { visible: !fg.rowMode; height: visible ? implicitHeight : 0; showMeasure: false; width: fg.mainW; kinds: fg.rowMode ? [] : ["hist"]; widths: [fg.form === "stack" ? fg.stackW : fg.p3] }
        // below ~290 px the zone labels leave the right gutter and stand under the figure as one key line
        Flow {
            visible: fg.zoneBelow
            width: fg.mainW
            spacing: fg.roles.u(12)
            Repeater {
                model: fg.roles && fg.zoneBelow ? sc.edges.filter(e => e.kind === "fault" || (e.kind === "pass" && sc.region(e.f) === 1)) : []
                delegate: Row {
                    required property var modelData
                    spacing: fg.roles.u(5)
                    Text { text: fg.roles.num(modelData.value); font: fg.roles.fFigLabel; color: modelData.kind === "fault" ? fg.roles.warn : fg.roles.ink3 }
                    Text { text: modelData.kind === "fault" ? (sc.region(modelData.f) === 1 ? "fault" : "fault ≈") : "pass edge"
                           font: fg.roles.fLabel; color: modelData.kind === "fault" ? fg.roles.warn : fg.roles.ink3 }
                }
            }
            Text { visible: sc.broken; text: "≈ compressed"; font: fg.roles.fLabel; color: fg.roles.ink3 }
        }
    }

    component FigNotes: Flow {
        id: fn
        spacing: fg.roles.u(9)
        readonly property real gap: fg.roles.u(24)
        readonly property int cols: fg.roles.columns(width, fg.roles.flows.prose, gap)
        readonly property real colW: (width - (cols - 1) * gap) / cols
        Text { width: fn.colW; wrapMode: Text.WordWrap; font: fg.roles.fSmall; color: fg.roles.ink2
               text: "One mark per shot; the ring is shot " + fg.shotNo + (fg.readout ? " of " + fg.readout.shotCount : "") + ". The panels share one value axis"
                     + (sc.broken ? ", true to scale where the shots are; the empty part of the axis is compressed at the break (≈)." : ".") }
        Text { width: fn.colW; wrapMode: Text.WordWrap; font: fg.roles.fSmall; color: fg.roles.ink3; visible: !!fg.s.highMeans; text: "Higher: " + (fg.s.highMeans || "") + "." }
        Text { width: fn.colW; wrapMode: Text.WordWrap; font: fg.roles.fSmall; color: fg.roles.ink3; text: fg.s.normTagLong || "" }
        Text { width: fn.colW; wrapMode: Text.WordWrap; font: fg.roles.fSmall; color: fg.roles.ink3
               text: "The line through the run is the median of the last " + (fg.s.medianWindow || 5) + " shots; a short stub on the baseline is a shot that could not be measured, never a gap." }
        Text { width: fn.colW; wrapMode: Text.WordWrap; font: fg.roles.fSmall; color: fg.roles.ink3; text: fg.roles.cap1(fg.hist.caption || "") + (fg.hist.caption ? "." : "") }
    }
    FigNotes { id: notesM; visible: fg.margin; x: fg.mainW + fg.roles.gutter(fg.width); y: fg.roles.u(40); width: fg.marginW }
    FigNotes { id: notesBelow; visible: fg.showNotes && !fg.margin; y: figCol.height + fg.roles.u(16); width: fg.width }
}
