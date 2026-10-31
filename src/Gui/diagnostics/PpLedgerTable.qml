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

// LEDGERTABLE — A's fold-don't-drop table. Columns come from a tier spec (roles.tiers.<kind>): a column
// whose tier fits the width given is laid on the grid; the rest FOLD, in `fo` order and with their
// label, into the row's second line. Nothing is dropped and nothing is ellipsised. While the strip is a
// column the picked shot is a shaded column down the table; folded, each strip shades its own cell.
// Built-in kinds read the live shapes directly:
//   "fired"    rows = readout.conditions (any subset)    → line 2 = folded columns
//   "session"  rows = cards                               → line 2 = B's direction sentence (+ xref in print)
//   "watching" rows = source.watching (needs readout)     → values/strips from readout
//   "path"     rows = detail.causes or detail.effects     → No. · typeset chain · links · Open
// or pass `spec` + `getter(row, key, foldedKeys) → { t, c, f, rich, ticks, gate }` for a custom table.
// A cell whose reading a gate or rule refused (`gate` = the model's gateNote { subject, gate, words,
// swingDir }) prints the gate's words in place of "-", quiet; a record's words are a link (" ›") that
// opens the reading in the Trust panel, else in place under the row (trust stage 6 session 2). A rule
// has no reading to open: words only. Print: words only.
Item {
    id: tb
    objectName: kind === "fired" ? "sdFiredTable" : kind === "session" ? "sdSessionTable" : kind === "watching" ? "sdWatchingTable" : "sdPathTable"
    readonly property string rowName: kind === "fired" ? "sdFiredRow" : kind === "session" ? "sdSessionRow" : kind === "watching" ? "sdWatchingRow" : "sdPathRow"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    readonly property bool isPrint: medium === "print"
    property bool interactive: !isPrint

    property string kind: "fired"
    property var rows: []
    property var spec: roles.tiers[kind] || []
    property var getter: null
    property var readout: null
    property var source: null
    property var detail: null                  // "path": the open condition (its own name is not repeated in colour)
    property string title: ""
    property var tools: []                     // [{ t, on, key }] — filter links beside the title (screen only)
    property var extra: null                   // function(row) → sentence for line 2; default per kind
    property bool showDirection: kind === "session"
    property string xrefId: ""                 // print: the row that gets "Detail on p. N"
    property int xrefPage: 0
    property bool closingRule: true
    // ── round 4 (all optional, safe defaults) ──
    property bool showEvidence: false          // session: the evidence sentence (F30) as a third line, from medium up
    property bool showFocus: false             // session: the quiet focus link (F33); the focused row is marked at rest
    property string printGroup: ""             // print: name the list so the PpPager repeats its head after a break
    readonly property int shotNo: roles.shotNo(readout)
    readonly property int shotCount: {
        let n = readout && readout.shotCount !== undefined ? readout.shotCount : 0
        const rs = rows || []
        for (let i = 0; i < rs.length; ++i) { const t = kind === "watching" ? ((cond(rs[i].id) || {}).ticks || []) : (rs[i].ticks || []); n = Math.max(n, t.length) }
        return n
    }
    function cellName(key) {
        const m = { rank: "Rank", name: "Name", frac: "Fired", shot: "Shot", median: "Median", unit: "Unit", trend: "Trend", typical: "Typical",
                    worst: "Worst", best: "Best", value: "Value", outby: "OutBy", corridor: "Corridor", tier: "Tier", strip: "Strip", no: "No", path: "Path", links: "Links", open: "Open" }
        return rowName + (m[key] || key)
    }

    signal conditionActivated(string id)
    signal conditionHovered(string id)
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)
    signal pathActivated(int index)
    signal toolActivated(string key)
    signal focusActivated(string id, bool on)

    // the page model: this block is made of atoms (head, rows) — print may cut between rows
    readonly property var keyKinds: ["strip"]

    readonly property string k: roles.sizeClass(width)
    readonly property real g: roles.gutter(width)
    readonly property var vis: spec.filter(c => roles.shows(c.tier, width) && !(isPrint && c.screenOnly))
    readonly property var fold: spec.filter(c => !roles.shows(c.tier, width) && !(isPrint && c.screenOnly))
                                    .slice().sort((a, b) => (a.fo || 0) - (b.fo || 0))
    readonly property var foldKeys: fold.map(c => c.key)
    function colW(c) { return c.kind === "strip" ? stripW : c.w * roles.em }
    function fixedCol(c) { return c.kind === "strip" || c.w > 0 }
    // any session length (D7): the strip column holds shotCount shots, narrowing, then wrapping
    readonly property real stripW: roles.stripWidthFor(shotCount || 15)
    readonly property var stripFit: roles.stripFit(shotCount, stripW)
    readonly property bool stripColumn: stripFit.rows === 1          // one line: the pick is a column down the table
    readonly property var widths: {
        const fixed = vis.reduce((s, c) => s + (fixedCol(c) ? colW(c) : 0), 0)
        const flex = Math.max(roles.em * 6, width - fixed - Math.max(0, vis.length - 1) * g)
        return vis.map(c => fixedCol(c) ? colW(c) : flex)
    }
    readonly property var xs: { const o = []; let x = 0; for (let i = 0; i < vis.length; ++i) { o.push(x); x += widths[i] + g } return o }
    readonly property int nameIdx: Math.max(0, vis.findIndex(c => c.kind === "name" || c.kind === "rich"))
    readonly property int stripIdx: vis.findIndex(c => c.kind === "strip")
    readonly property var shotIds: {
        const rs = rows || []
        for (let i = 0; i < rs.length; ++i) { const t = kind === "watching" ? ((cond(rs[i].id) || {}).ticks || []) : (rs[i].ticks || []); if (t.length >= shotCount && t.length) return t.map((x, k) => x.shotId !== undefined ? x.shotId : k + 1) }
        const o = []; for (let k = 0; k < shotCount; ++k) o.push(k + 1); return o
    }
    readonly property int pickIndex: readout && readout.shotIndex !== undefined ? readout.shotIndex : (brush ? shotIds.indexOf(brush.pickedShot) : -1)
    readonly property int hoverIndex: brush && brush.hoveredShot > 0 && brush.hoveredShot !== brush.pickedShot ? shotIds.indexOf(brush.hoveredShot) : -1
    readonly property var groups: {
        const out = []
        for (let i = 0; i < vis.length; ++i) {
            const gname = vis[i].group
            if (!gname) continue
            if (out.length && out[out.length - 1].name === gname && out[out.length - 1].to === i - 1) out[out.length - 1].to = i
            else out.push({ name: gname, from: i, to: i })
        }
        return out
    }
    function headText(c) {
        if (c.key === "value" || c.key === "shot") return "Shot " + shotNo
        if (c.kind === "strip") return shotCount > 0 ? "Shots 1–" + shotCount : "Shots"
        return c.head
    }
    function rowId(r) { return kind === "path" ? "" : (r && r.id) || "" }
    // the gated cell open in place (Trust panel off): its row, and the reading under it
    property string gateOpenRow: ""
    property string gateOpenSubject: ""
    property string gateOpenSwingDir: ""
    function gateCell(g) { return g && g.words ? { t: g.words, gate: g } : null }
    function openGate(rid, g) {
        if (!g || !g.subject) return
        if (TrustFocus.open(g.subject, g.swingDir || "")) return
        const same = gateOpenRow === rid && gateOpenSubject === g.subject
        gateOpenRow = same ? "" : rid
        gateOpenSubject = same ? "" : g.subject
        gateOpenSwingDir = same ? "" : (g.swingDir || "")
    }
    function gateHtml(g) {
        const w = roles.esc(g.words)
        return (!g.subject || isPrint) ? w : w + "<font color='" + roles.hex(roles.link) + "'> ›</font>"
    }

    // ── built-in getters ──
    function cond(id) { return roles.byId((readout && readout.conditions) || [], id) }
    function bookend(id) { return roles.byId((source && source.bookends) || [], id) }
    readonly property var marks: {
        if (kind !== "path") return []
        const syms = ["*", "†", "‡", "§", "‖", "¶"], out = []
        ;(rows || []).forEach(p => (p.nodes || []).forEach(n => {
            if (n.mark && !out.some(m => m.text === n.mark) && out.length < syms.length) out.push({ text: n.mark, sym: syms[out.length] })
        }))
        return out
    }
    function symFor(mark) { for (const m of marks) if (m.text === mark) return m.sym; return "" }
    function linkSummary(p) {
        const counts = {}, order = []
        ;(p.links || []).forEach(l => { const w = String(l.word || "").toLowerCase(); if (!counts[w]) { counts[w] = 0; order.push(w) } counts[w]++ })
        return order.map(w => counts[w] > 1 ? w + " ×" + counts[w] : w).join(", ")
    }
    function pathHtml(p, i) {
        const base = p.primary ? roles.ink : roles.ink2
        const self = detail ? detail.id : ""
        const ls = p.links || []
        return (p.nodes || []).map((n, k) => {
            const col = (n.state === "fired" && n.id !== self) ? roles.warn : base
            const s = symFor(n.mark)
            const st = k > 0 ? roles.linkStyle(ls[k - 1]) : null
            return (st ? "<span style='color:" + roles.hex(st.color) + "'>&nbsp; " + st.glyph + " &nbsp;</span>" : "")
                   + "<span style='color:" + roles.hex(col) + "'>" + roles.esc(n.name) + "</span>"
                   + (s ? "<span style='color:" + roles.hex(roles.ink3) + "'>" + roles.esc(s) + "</span>" : "")
        }).join("")
    }
    function get(r, key, folded) {
        if (getter) return getter(r, key, folded)
        if (kind === "fired") {
            switch (key) {
            case "name": return { t: r.name }
            case "tier": return { t: r.tierTag }
            case "value": return gateCell(r.gated ? r.gateNote : null)
                                 || { t: roles.fig(r.valueText), c: r.stateKind === "fired" ? roles.warn : r.stateKind === "notAssessable" ? roles.ink3 : roles.ink }
            case "unit": return { t: roles.unitOf(r.valueText) }
            case "corridor": return { t: roles.corridorShort(r.corridorText) }
            // a composite's multiple IS its number, never shown (posture_metrics_design.md §8): its meter step instead
            case "outby": return { t: r.stateKind === "fired" && r.strengthKnown ? (r.strengthSharp ? "sharp" : r.composite ? r.strength + "/5" : r.strengthExcess.toFixed(1) + "×") : "-" }
            case "strip": return { ticks: r.ticks }
            case "frac": return { t: roles.firedFrac(r) }
            }
        } else if (kind === "session") {
            const sp = r.spread || {}, bk = bookend(r.id), unitFolded = folded.indexOf("unit") >= 0
            switch (key) {
            case "rank": return { t: String(r.rankText).replace("#", "") }
            case "name": return { t: "<span>" + roles.esc(r.name) + "</span>&nbsp;&nbsp;<span style='color:" + roles.hex(roles.ink3)
                                     + "; font-size:" + roles.fFigS.pixelSize + "px'>" + roles.esc(r.phase || "") + "</span>"
                                     + (r.fresh ? "&nbsp;&nbsp;<i style='color:" + roles.hex(roles.ink2) + "; font-size:" + roles.fSmall.pixelSize + "px'>new</i>" : ""),
                                  f: roles.fBodyM, rich: true }
            case "strip": return { ticks: r.ticks }
            case "frac": return { t: roles.firedFrac(r) }
            case "trend": return { t: roles.trendWord(r), c: r.trend === "worsening" ? roles.ink : roles.ink2 }
            case "median": return { t: (sp.medianNumber || "-") + (unitFolded && folded.indexOf("median") >= 0 ? " " + (sp.unit || "") : "") }
            // an unmeasured shot is "-" in ink3, so it never reads as a clean value (D5, INVENTORY #47)
            case "shot": { const gc = gateCell(sp.currentGated ? sp.currentGate : (r.gated && r.thisShot === "notAssessable" ? r.gateNote : null))
                           if (gc) return gc
                           const t = sp.currentNumber || roles.fig(r.valueText)
                           return { t: t, c: r.thisShot === "fired" ? roles.warn : (t === "-" || r.thisShot === "notAssessable") ? roles.ink3 : roles.ink } }
            case "unit": return { t: unitFolded && folded.indexOf("median") >= 0 ? "" : (sp.unit || roles.unitOf(r.valueText)) }
            case "typical": return { t: bk ? bk.representativeShotId : "-", shot: bk ? bk.representativeShotId : -1 }
            case "worst": return { t: bk ? bk.worstShotId : "-", shot: bk ? bk.worstShotId : -1 }
            case "best": return { t: bk ? bk.bestShotId : "-", shot: bk ? bk.bestShotId : -1 }
            }
        } else if (kind === "watching") {
            const c = cond(r.id)
            switch (key) {
            case "name": return { t: r.name }
            case "value": return gateCell(c && c.gated ? c.gateNote : null)
                                 || { t: c ? roles.fig(c.valueText) : "-", c: c && c.stateKind === "fired" ? roles.warn : roles.ink2 }
            case "unit": return { t: c ? roles.unitOf(c.valueText) : "" }
            case "strip": return { ticks: c ? c.ticks : [] }
            case "frac": return { t: roles.firedFrac(r) }
            }
        } else if (kind === "path") {
            const i = (rows || []).indexOf(r)
            switch (key) {
            case "no": return { t: i + 1 }
            case "path": return { t: pathHtml(r, i) }
            case "links": return { t: linkSummary(r) }
            case "open": return { t: "Open" }
            }
        }
        return {}
    }
    function line2(r) {
        if (extra) return extra(r)
        if (kind === "session" && showDirection) {
            // review tense ("3 more firings after this shot"), else the live one ("last fired 2 shots ago", F29)
            const after = r.firingsAfterText || r.recencyText || ""
            // a session-scope pattern has no strip and no fired fraction to read its count from: say it
            const count = r.sessionScope ? r.recurrence || "" : ""
            return [roles.sentence(count), roles.sentence(r.directionText), after ? roles.sentence(after) : ""].filter(x => !!x).join("  ")
                   + (isPrint && xrefPage > 0 && r.id === xrefId ? "  Detail on p. " + xrefPage + "." : "")
        }
        return ""
    }
    function line3(r) { return kind === "session" && showEvidence && roles.shows(3, width) && r.evidence ? String(r.evidence) : "" }

    implicitHeight: col.height
    height: implicitHeight

    // the picked shot / hovered shot as a column through the whole table, while the strip is a column
    Rectangle {
        objectName: "sdPickColumn"
        visible: tb.stripIdx >= 0 && tb.stripColumn && tb.pickIndex >= 0
        x: (tb.xs[tb.stripIdx] || 0) + tb.pickIndex * tb.stripFit.pitch
        y: head.height; width: tb.stripFit.pitch; height: col.height - head.height
        color: tb.roles.pickWash
    }
    Rectangle {
        visible: tb.stripIdx >= 0 && tb.stripColumn && tb.hoverIndex >= 0
        x: (tb.xs[tb.stripIdx] || 0) + tb.hoverIndex * tb.stripFit.pitch
        y: head.height; width: tb.stripFit.pitch; height: col.height - head.height
        color: tb.roles.hoverWash
    }

    Column {
        id: col
        width: tb.width

        // ── head (tools that do not fit beside the title move to a line above it) ──
        Item {
            id: head
            objectName: "sdLedgerHead"
            property bool atom: true
            property bool keepNext: true
            readonly property bool hasTools: (tb.tools || []).length > 0 && !tb.isPrint
            readonly property bool toolsAbove: hasTools && (ttl.implicitWidth + tb.roles.u(18) + toolsRow.implicitWidth > (tb.widths[tb.nameIdx] || 0))
            width: tb.width
            height: ((tb.groups.length || toolsAbove) ? tb.roles.u(34) : tb.roles.u(21)) + tb.roles.r.figureRuleTop
            Rectangle { width: parent.width; height: tb.roles.r.figureRuleTop; color: tb.roles.rule }
            Text {
                id: ttl
                visible: tb.title.length > 0
                x: tb.xs[tb.nameIdx] || 0
                anchors.bottom: parent.bottom; anchors.bottomMargin: tb.roles.u(5)
                text: tb.title; font: tb.roles.fSection; color: tb.roles.ink
            }
            Row {
                id: toolsRow
                visible: head.hasTools
                x: head.toolsAbove ? ttl.x : ttl.x + ttl.width + tb.roles.u(18)
                y: head.toolsAbove ? tb.roles.u(5) + tb.roles.r.figureRuleTop : ttl.y + ttl.baselineOffset - tb.roles.fLabel.pixelSize
                spacing: tb.roles.u(12)
                Repeater {
                    model: tb.roles ? (head.hasTools ? tb.tools : []) : []
                    delegate: Text {
                        required property var modelData
                        objectName: "sdFilterTool"
                        readonly property string key: modelData.key || ""
                        readonly property bool on: !!modelData.on
                        text: modelData.t
                        font: modelData.on ? tb.roles.fLabel : tb.roles.fLabelL
                        color: modelData.on ? tb.roles.ink : tb.roles.link
                        MouseArea { enabled: tb.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tb.toolActivated(modelData.key) }
                    }
                }
            }
            Repeater {
                model: tb.roles ? (tb.vis) : []
                delegate: Text {
                    required property var modelData
                    required property int index
                    visible: !(index === tb.nameIdx && tb.title.length > 0)
                    x: tb.xs[index] || 0; width: tb.widths[index] || 0
                    anchors.bottom: parent.bottom; anchors.bottomMargin: tb.roles.u(5)
                    horizontalAlignment: modelData.align === "r" ? Text.AlignRight : Text.AlignLeft
                    text: tb.headText(modelData)
                    font: tb.roles.fLabel; color: tb.roles.ink3
                }
            }
            Repeater {
                model: tb.roles ? (tb.groups) : []
                delegate: Item {
                    required property var modelData
                    x: tb.xs[modelData.from] + tb.roles.u(6)
                    width: tb.xs[modelData.to] + tb.widths[modelData.to] - tb.xs[modelData.from] - tb.roles.u(6)
                    y: tb.roles.u(4); height: gl.height + tb.roles.u(3)
                    Text {
                        id: gl; width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: tb.isPrint ? "Shots" : modelData.name
                        font: tb.roles.fLabel; color: tb.roles.ink3
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: tb.roles.hw; color: tb.roles.hair }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: tb.roles.hw; color: tb.roles.hair }
        }

        // ── rows ──
        Repeater {
            model: tb.roles ? (tb.rows || []) : []
            delegate: Item {
                id: rw
                objectName: tb.rowName
                property bool atom: true
                required property var modelData
                required property int index
                readonly property string rid: tb.rowId(modelData)
                // keys tests select by (INVENTORY §5)
                readonly property string conditionId: rid
                readonly property string cardId: rid
                readonly property string stateKind: tb.kind === "fired" ? (modelData.stateKind || "") : tb.kind === "session" ? (modelData.thisShot || "") : ""
                readonly property string rankText: String(modelData.rankText || "").replace("#", "")
                readonly property string thisShot: modelData.thisShot || ""
                readonly property bool focused: !!modelData.focused
                readonly property bool fresh: !!modelData.fresh
                readonly property real padT: tb.roles.u(4)
                readonly property string extraText: tb.line2(modelData)
                readonly property string evidenceText: tb.line3(modelData)
                readonly property bool focusLink: tb.showFocus && !tb.isPrint && tb.kind === "session" && (rw.focused || (tb.interactive && rw.hot))
                readonly property var foldItems: tb.fold.map(c => ({ c: c, v: tb.get(modelData, c.key, tb.foldKeys) || {} }))
                                                     .filter(o => o.c.kind === "strip" || (o.v.t !== undefined && String(o.v.t).length))
                readonly property bool hasLine2: extraText.length > 0 || foldItems.length > 0 || evidenceText.length > 0 || (tb.showFocus && tb.kind === "session" && !tb.isPrint)
                readonly property bool last: index === (tb.rows || []).length - 1
                readonly property bool hot: tb.brush && rid !== "" && (tb.brush.hoveredCondition === rid)
                readonly property bool open: tb.brush && rid !== "" && tb.brush.pickedCondition === rid
                width: tb.width
                height: padT + l1.height + (hasLine2 ? tb.roles.u(2) + l2.height : 0)
                        + (gateInl.open ? tb.roles.u(4) + gateInl.height : 0) + tb.roles.u(5)
                opacity: tb.brush && rid !== "" ? tb.brush.conditionOpacity(rid, tb.roles.dim) : 1
                Behavior on opacity { NumberAnimation { duration: tb.roles.normal } }

                PpPulseWash { anchors.fill: parent; roles: tb.roles; brush: tb.brush; conditionId: rw.rid }
                Rectangle { anchors.fill: parent; color: tb.roles.rowHover; visible: rw.hot && !tb.isPrint
                            Behavior on opacity { NumberAnimation { duration: tb.roles.fast } } }
                // the focused pattern, marked at rest (F33): a short ink bar at the row's left edge
                Rectangle { objectName: "sdSessionRowFocusBar"; visible: tb.kind === "session" && rw.focused
                            x: -tb.roles.u(6); y: rw.padT; width: 2; height: parent.height - rw.padT - tb.roles.u(5); color: tb.roles.ink }
                // (marker for tests: present only on a fresh pattern; the word itself is in the name cell)
                Item { objectName: rw.fresh ? tb.rowName + "Fresh" : ""; visible: rw.fresh; width: 0; height: 0
                       readonly property string text: "new" }
                // The row's hover is a HoverHandler, not the MouseArea's containsMouse: a rich-text cell (the
                // name, a folded figure) accepts hover itself and kept it from a MouseArea beneath, so the row
                // never went hot under the pointer where a reader points — its name (qml_ui test_27, 11 Oct).
                HoverHandler {
                    enabled: tb.interactive
                    onHoveredChanged: {
                        if (rw.rid !== "" && tb.brush) tb.brush.hoverCondition(rw.rid, hovered)
                        if (rw.rid !== "") tb.conditionHovered(hovered ? rw.rid : "")
                    }
                }
                MouseArea {
                    enabled: tb.interactive; visible: enabled
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tb.kind === "path" ? tb.pathActivated(rw.index) : tb.conditionActivated(rw.rid)
                }
                Item {
                    id: l1
                    y: rw.padT; width: tb.width; height: childrenRect.height
                    Repeater {
                        model: tb.roles ? (tb.vis) : []
                        delegate: Item {
                            id: cl
                            objectName: modelData.kind === "strip" ? "" : tb.cellName(modelData.key)
                            required property var modelData
                            required property int index
                            readonly property var v: tb.get(rw.modelData, modelData.key, tb.foldKeys) || ({})
                            readonly property string text: v.t !== undefined ? String(v.t) : "-"
                            readonly property int shotId: v.shot !== undefined ? v.shot : -1
                            x: tb.xs[index] || 0; width: tb.widths[index] || 0
                            height: modelData.kind === "strip" ? sl.y + sl.height : tx.y + tx.height
                            Text {
                                id: tx
                                readonly property bool gated: !!cl.v.gate
                                visible: cl.modelData.kind !== "strip"
                                width: parent.width
                                y: tb.roles.fBody.pixelSize * 0.95 - baselineOffset
                                text: gated ? tb.gateHtml(cl.v.gate) : cl.v.t !== undefined ? String(cl.v.t) : "-"
                                textFormat: gated ? Text.StyledText : (cl.modelData.kind === "rich" || cl.v.rich) ? Text.RichText : Text.PlainText
                                wrapMode: (gated || cl.modelData.kind === "name" || cl.modelData.kind === "rich" || cl.modelData.kind === "small") ? Text.WordWrap : Text.NoWrap
                                lineHeight: 1.08
                                horizontalAlignment: cl.modelData.align === "r" ? Text.AlignRight : Text.AlignLeft
                                font: tx.gated ? tb.roles.fSmall : cl.v.f || (cl.modelData.kind === "fig" ? tb.roles.fFig
                                      : cl.modelData.kind === "figs" || cl.modelData.kind === "shot" ? tb.roles.fFigS
                                      : cl.modelData.kind === "small" ? tb.roles.fSmall
                                      : cl.modelData.kind === "link" ? tb.roles.fSmallL
                                      : rw.open ? tb.roles.fBodyM : tb.roles.fBody)
                                color: tx.gated ? tb.roles.ink3 : cl.v.c || (cl.modelData.ink3 ? tb.roles.ink3
                                       : cl.modelData.kind === "figs" || cl.modelData.kind === "small" ? tb.roles.ink2
                                       : cl.modelData.kind === "link" || (cl.modelData.kind === "shot" && !tb.isPrint) ? tb.roles.link : tb.roles.ink)
                                MouseArea {
                                    // a shot figure (typical / worst / best) puts that shot in the carousel
                                    enabled: tb.interactive && cl.modelData.kind === "shot" && (cl.v.shot || -1) > 0
                                    visible: enabled
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onContainsMouseChanged: if (tb.brush) tb.brush.hoverShot(cl.v.shot, containsMouse)
                                    onClicked: tb.shotActivated(tb.brush ? tb.brush.dirOf(cl.v.shot) : "", cl.v.shot)
                                }
                                MouseArea {
                                    // a gate's words open the reading it refused (Trust panel, else in place)
                                    objectName: "sdGateCell"
                                    enabled: tb.interactive && tx.gated && !!cl.v.gate.subject
                                    visible: enabled
                                    anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor
                                    onClicked: tb.openGate(rw.rid, cl.v.gate)
                                }
                            }
                            PpShotStrip {
                                id: sl
                                visible: cl.modelData.kind === "strip"
                                roles: tb.roles; brush: tb.brush; medium: tb.medium; interactive: tb.interactive
                                ticks: visible ? (cl.v.ticks || []) : []
                                availableWidth: tb.stripW
                                pickBand: !tb.stripColumn
                                y: tb.roles.fBody.pixelSize * 0.6 - height / 2
                                onShotActivated: (d, s) => tb.shotActivated(d, s)
                                onShotHovered: s => tb.shotHovered(s)
                            }
                        }
                    }
                }
                Column {
                    id: l2
                    visible: rw.hasLine2
                    x: tb.xs[tb.nameIdx] || 0; width: tb.width - x
                    y: rw.padT + l1.height + tb.roles.u(2)
                    spacing: tb.roles.u(2)
                    Item {
                        objectName: tb.rowName + "Line2"
                        readonly property string text: rw.extraText
                        visible: rw.extraText.length > 0 || rw.focusLink
                        width: parent.width; height: Math.max(l2t.visible ? l2t.height : 0, fl.visible ? fl.height : 0)
                        Text {
                            id: l2t
                            visible: rw.extraText.length > 0
                            width: parent.width - (tb.showFocus && tb.kind === "session" && !tb.isPrint ? fl.implicitWidth + tb.roles.u(16) : 0); wrapMode: Text.WordWrap
                            text: rw.extraText; font: tb.roles.fSmallI; color: tb.roles.ink2
                        }
                        // the quiet focus link (F33): shown on hover and, once focused, at rest
                        Text {
                            id: fl
                            objectName: "sdSessionRowFocus"
                            readonly property bool focused: rw.focused
                            visible: rw.focusLink
                            anchors.right: parent.right
                            text: rw.focused ? "Focused · clear" : "Focus"
                            font: rw.focused ? tb.roles.fSmall : tb.roles.fSmallL; color: rw.focused ? tb.roles.ink : tb.roles.link
                            MouseArea { enabled: tb.interactive; anchors.fill: parent; anchors.margins: -tb.roles.u(3); cursorShape: Qt.PointingHandCursor
                                        onClicked: tb.focusActivated(rw.rid, !rw.focused) }
                        }
                    }
                    Text {
                        objectName: tb.rowName + "Evidence"
                        visible: rw.evidenceText.length > 0
                        width: Math.min(parent.width, tb.roles.flows.measure * tb.roles.em * 1.3); wrapMode: Text.WordWrap
                        text: rw.evidenceText; font: tb.roles.fSmall; color: tb.roles.ink3; lineHeight: tb.roles.r.smallLeading
                    }
                    Flow {
                        visible: rw.foldItems.length > 0
                        width: parent.width + tb.roles.u(12)
                        Repeater {
                            model: tb.roles ? (rw.foldItems) : []
                            delegate: Item {
                                id: fi
                                objectName: isStrip ? "" : tb.cellName(modelData.c.key)
                                required property var modelData
                                readonly property string text: modelData.v.t !== undefined ? String(modelData.v.t) : ""
                                readonly property bool isStrip: modelData.c.kind === "strip"
                                width: (isStrip ? fstrip.width : ft.implicitWidth) + tb.roles.u(12)
                                height: Math.max(ft.implicitHeight, fi.isStrip ? fstrip.height + 6 : tb.roles.stripH + 6)
                                PpShotStrip {
                                    id: fstrip
                                    visible: fi.isStrip
                                    roles: tb.roles; brush: tb.brush; medium: tb.medium; interactive: tb.interactive
                                    ticks: fi.isStrip ? (fi.modelData.v.ticks || []) : []
                                    availableWidth: Math.max(tb.roles.u(60), tb.width - (tb.xs[tb.nameIdx] || 0) - tb.roles.u(12))
                                    pickBand: true
                                    anchors.verticalCenter: parent.verticalCenter
                                    onShotActivated: (d, s) => tb.shotActivated(d, s)
                                    onShotHovered: s => tb.shotHovered(s)
                                }
                                Text {
                                    id: ft
                                    visible: !fi.isStrip
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.StyledText
                                    font: tb.roles.fSmall
                                    color: fi.modelData.v.c || tb.roles.ink2
                                    text: (fi.modelData.c.fold ? "<font color='" + tb.roles.hex(tb.roles.ink3) + "'>" + tb.roles.esc(fi.modelData.c.fold) + "</font> " : "")
                                          + tb.roles.esc(fi.modelData.v.t !== undefined ? fi.modelData.v.t : "")
                                }
                            }
                        }
                    }
                }
                // the gated cell's reading, in place under the row (Trust panel off)
                PpTrustInlineFor {
                    id: gateInl
                    x: tb.xs[tb.nameIdx] || 0; width: tb.width - x
                    y: rw.padT + l1.height + (rw.hasLine2 ? tb.roles.u(2) + l2.height : 0) + tb.roles.u(4)
                    roles: tb.roles
                    subject: !tb.isPrint && tb.gateOpenRow !== "" && tb.gateOpenRow === rw.rid ? tb.gateOpenSubject : ""
                    swingDir: tb.gateOpenSwingDir
                    onClosed: { tb.gateOpenRow = ""; tb.gateOpenSubject = "" }
                }
                Rectangle {
                    anchors.bottom: parent.bottom; width: parent.width
                    height: rw.last && tb.closingRule ? 1 : tb.roles.hw
                    color: rw.last && tb.closingRule ? tb.roles.rule : tb.roles.hair
                }
            }
        }
    }
}
