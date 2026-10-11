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

// CAUSEGRAPH — C's causes / effects as a layered graph: every path merged into one diagram, columns by
// distance from the condition, links drawn by the model's FIVE grades (round 4, D6, ported from
// PpChainLink): moved together = solid, heaviest, ink, arrow; conditionally dependent = solid, ink2,
// arrow; coherent = dashed, no arrow; present together = dotted, no arrow; unanchored = dotted at half
// opacity, no arrow. An arrowhead ONLY where the evidence supports a direction. The likely driver's path
// is drawn at full strength, the others recede. A screened root asks for its screen (screenRequested),
// never opens a page. Below its minimum (a column < roles.flows.dagCol
// em, or below medium) the diagram CHANGES FORM into an indented outline rather than shrinking.
// At wide its key and notes sit in a right margin; otherwise they follow it.
// which: "causes" | "effects". Clicking a node → conditionActivated(id); hovering brushes it.
Item {
    id: dg
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    readonly property bool isPrint: medium === "print"
    property bool interactive: !isPrint
    property var detail: null
    property string which: "causes"
    property bool showKey: !isPrint
    property bool atom: true
    objectName: "sdCauseGraph"
    readonly property var keyKinds: ["n-fired", "n-clean", "n-root", "n-ghost", "n-outcome"].concat(gradesUsed.map(g => "l-" + g))
    signal conditionActivated(string id)
    signal pathActivated(string which, int index)
    signal screenRequested(string id)             // a screened root: the physical screen would settle it (F64)
    readonly property var gradesUsed: {
        const seen = {}
        ;(paths || []).forEach(p => (p.links || []).forEach(l => seen[roles.linkStyle(l).grade] = true))
        return roles.linkGradeOrder.filter(g => seen[g])
    }

    readonly property bool causes: which === "causes"
    readonly property bool margin: roles.shows("margin", width)
    readonly property real marginW: roles.u(236)
    readonly property real mainW: margin ? width - marginW - roles.gutter(width) : width
    readonly property var paths: detail ? ((causes ? detail.causes : detail.effects) || []) : []
    readonly property var g: buildDag(paths, causes)
    readonly property var tree: buildTree(paths, causes)
    readonly property bool graph: roles.shows("causeGraph", width) && g.cols > 0 && mainW / g.cols >= roles.flows.dagCol * roles.em
    readonly property string subject: (detail && detail.id) || ""

    function buildDag(ps, isCauses) {
        const nodes = {}, order = [], edges = {}, elist = []
        for (let p = 0; p < (ps || []).length; ++p) {
            const ns = ps[p].nodes || [], ls = ps[p].links || []
            for (let i = 0; i < ns.length; ++i) {
                const n = ns[i], id = n.id || n.name
                if (!nodes[id]) { nodes[id] = { id: id, name: n.name, kind: n.kind || "", state: n.state || "", primary: false, layer: 0, pos: 0, bc: 0 }; order.push(id) }
                if (ps[p].primary) nodes[id].primary = true
                if (i + 1 < ns.length) {
                    const to = ns[i + 1].id || ns[i + 1].name, key = id + ">" + to, l = ls[i] || {}
                    if (!edges[key]) { edges[key] = { from: id, to: to, grade: l.grade || "", link: l, primary: false }; elist.push(edges[key]) }
                    if (ps[p].primary) edges[key].primary = true
                }
            }
        }
        for (let it = 0; it < 16; ++it)
            for (let k = 0; k < elist.length; ++k) {
                const e = elist[k]
                if (isCauses) { if (nodes[e.from].layer < nodes[e.to].layer + 1) nodes[e.from].layer = nodes[e.to].layer + 1 }
                else if (nodes[e.to].layer < nodes[e.from].layer + 1) nodes[e.to].layer = nodes[e.from].layer + 1
            }
        let maxL = 0
        for (let i = 0; i < order.length; ++i) maxL = Math.max(maxL, nodes[order[i]].layer)
        const cols = []
        for (let c = 0; c <= maxL; ++c) cols.push([])
        for (let i = 0; i < order.length; ++i) { const n = nodes[order[i]]; n.col = isCauses ? maxL - n.layer : n.layer; cols[n.col].push(n.id) }
        function place(c) { cols[c].forEach((id, i) => nodes[id].pos = i - (cols[c].length - 1) / 2) }
        for (let c = 0; c <= maxL; ++c) place(c)
        for (let sweep = 0; sweep < 8; ++sweep) {
            const fwd = sweep % 2 === 0
            for (let k = 1; k <= maxL; ++k) {
                const c = fwd ? k : maxL - k, nb = fwd ? c - 1 : c + 1
                for (let j = 0; j < cols[c].length; ++j) {
                    const id = cols[c][j]
                    let s = 0, m = 0
                    for (let q = 0; q < elist.length; ++q) {
                        const e = elist[q], other = e.from === id ? e.to : (e.to === id ? e.from : null)
                        if (other !== null && nodes[other].col === nb) { s += nodes[other].pos; ++m }
                    }
                    nodes[id].bc = m ? s / m : nodes[id].pos
                }
                cols[c].sort((a, b) => nodes[a].bc - nodes[b].bc)
                place(c)
            }
        }
        let maxRows = 0
        for (let c = 0; c <= maxL; ++c) maxRows = Math.max(maxRows, cols[c].length)
        const out = [], idx = {}
        for (let c = 0; c <= maxL; ++c) cols[c].forEach((id, i) => { const n = nodes[id]; n.row = i + (maxRows - cols[c].length) / 2; idx[id] = out.length; out.push(n) })
        return { nodes: out, edges: elist.map(e => ({ a: idx[e.from], b: idx[e.to], grade: e.grade, link: e.link, primary: e.primary })), rows: maxRows, cols: order.length ? maxL + 1 : 0 }
    }
    function buildTree(ps, isCauses) {
        const top = { kids: [], kidMap: {} }
        for (let p = 0; p < (ps || []).length; ++p) {
            const ns = (ps[p].nodes || []).slice(), ls = (ps[p].links || []).slice()
            if (isCauses) { ns.reverse(); ls.reverse() }
            let cur = top
            for (let i = 0; i < ns.length; ++i) {
                const n = ns[i], id = n.id || n.name
                let k = cur.kidMap[id]
                if (!k) { k = { id: id, n: n, kids: [], kidMap: {}, grade: i > 0 ? ((ls[i - 1] || {}).grade || "") : "", link: i > 0 ? (ls[i - 1] || {}) : null, primary: false }; cur.kidMap[id] = k; cur.kids.push(k) }
                if (ps[p].primary) k.primary = true
                cur = k
            }
        }
        const rows = []
        function walk(t, depth, parent) {
            const me = rows.length
            rows.push({ depth: depth, parent: parent, id: t.id, name: t.n.name, state: t.n.state || "", kind: t.n.kind || "", grade: t.grade, link: t.link, primary: t.primary })
            for (let i = 0; i < t.kids.length; ++i) walk(t.kids[i], depth + 1, me)
        }
        for (let i = 0; i < top.kids.length; ++i) walk(top.kids[i], 0, -1)
        return rows
    }
    // the MARK a node draws. The node's kind leads (a fired screened root is still a screened root, an
    // outcome still an outcome); a ghost is an authored, unmeasured node; a live or watched node is a
    // reading — fired, clean, or (not measured on this shot) the hollow "no reading" mark, never a ghost.
    function nodeKind(n) {
        if (n.kind === "screenedRoot") return "root"
        if (n.kind === "outcome") return "outcome"
        if (n.kind === "ghost") return "ghost"
        if (n.state === "fired") return "fired"
        if (n.state === "notAssessable") return "ghost"
        return "clean"
    }
    // style = roles.linkStyle(link); fade multiplies its opacity (the non-primary paths recede)
    function strokeStyleOf(st, fade) { return Qt.alpha(st.color, st.opacity * (fade === undefined ? 1 : fade)) }
    function strokePolyline(ctx, pts, st, fade, wMul) {
        ctx.strokeStyle = strokeStyleOf(st, fade); ctx.lineWidth = st.width * (wMul || 1); ctx.lineCap = st.stroke === "dotted" ? "round" : "butt"
        const d = roles.dashFor(st.stroke)
        const pat = d ? (st.stroke === "dotted" ? [0.1, d[1] + 0.6] : d) : null
        ctx.beginPath(); ctx.moveTo(pts[0][0], pts[0][1])
        if (!pat) { for (let i = 1; i < pts.length; ++i) ctx.lineTo(pts[i][0], pts[i][1]); ctx.stroke(); return }
        let on = true, left = pat[0]
        for (let i = 1; i < pts.length; ++i) {
            let ax = pts[i - 1][0], ay = pts[i - 1][1]
            const bx = pts[i][0], by = pts[i][1]
            let seg = Math.hypot(bx - ax, by - ay)
            while (seg > 1e-6) {
                const step = Math.min(seg, left), nx = ax + (bx - ax) * step / seg, ny = ay + (by - ay) * step / seg
                if (on) ctx.lineTo(nx, ny); else ctx.moveTo(nx, ny)
                ax = nx; ay = ny; seg -= step; left -= step
                if (left <= 1e-6) { on = !on; left = on ? pat[0] : pat[1] }
            }
        }
        ctx.stroke()
    }
    function strokeLink(ctx, p, st, fade) {
        const pts = [], N = 48
        for (let i = 0; i <= N; ++i) {
            const t = i / N, u = 1 - t
            pts.push([u * u * u * p[0][0] + 3 * u * u * t * p[1][0] + 3 * u * t * t * p[2][0] + t * t * t * p[3][0],
                      u * u * u * p[0][1] + 3 * u * u * t * p[1][1] + 3 * u * t * t * p[2][1] + t * t * t * p[3][1]])
        }
        strokePolyline(ctx, pts, st, fade)
    }
    // the arrowhead: drawn ONLY for a grade whose evidence supports a direction
    function arrowHead(ctx, x, y, st, fade, dx, dy) {
        if (!st.arrow) return
        const hl = roles.u(5) + st.width, hw = roles.u(3) + st.width * 0.5
        ctx.fillStyle = strokeStyleOf(st, fade)
        ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x - dx * hl - dy * hw, y - dy * hl + dx * hw); ctx.lineTo(x - dx * hl + dy * hw, y - dy * hl - dx * hw); ctx.closePath(); ctx.fill()
    }

    implicitHeight: Math.max(mainCol.height, margin ? notes.y + notes.height : 0)
    height: implicitHeight

    component NodeMark: Item {
        property string kind: "clean"
        width: dg.roles.u(9); height: dg.roles.u(9)
        PpMark { visible: parent.kind === "fired"; roles: dg.roles; fired: true; cx: parent.width / 2; cy: parent.height / 2 }
        Rectangle { visible: parent.kind === "clean"; anchors.centerIn: parent; width: dg.roles.u(7); height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: dg.roles.ink2 }
        Rectangle { visible: parent.kind === "ghost"; anchors.centerIn: parent; width: dg.roles.u(5); height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: Qt.alpha(dg.roles.ink3, 0.7) }
        Rectangle { visible: parent.kind === "root"; anchors.centerIn: parent; width: dg.roles.u(7); height: width; color: "transparent"; border.width: 1; border.color: dg.roles.ink3 }
        Rectangle { visible: parent.kind === "outcome"; anchors.centerIn: parent; width: dg.roles.u(6); height: width; rotation: 45; color: "transparent"; border.width: 1; border.color: dg.roles.ink3; antialiasing: true }
    }

    Column {
        id: mainCol
        width: dg.mainW
        spacing: dg.roles.u(8)
        Rectangle { width: parent.width; height: dg.roles.hw; color: dg.roles.grid; opacity: dg.roles.gridOpacity }
        Text { text: dg.causes ? "What might have caused this" : "What this leads to"; topPadding: dg.roles.u(6); font: dg.roles.fSection; color: dg.roles.ink }
        Text { objectName: dg.causes ? "sdDetailCauseHeadline" : "sdDetailOutcomeHeadline"
               width: parent.width; wrapMode: Text.WordWrap; font: dg.roles.fSmall; color: dg.roles.ink2
               text: dg.detail ? ((dg.causes ? dg.detail.causeHeadline || dg.detail.noCausesLine : dg.detail.outcomeHeadline || dg.detail.noEffectsLine) || "") : "" }
        Text { visible: !dg.graph && dg.paths.length > 0; font: dg.roles.fLabel; color: dg.roles.ink3
               text: dg.causes ? "Read upward: each line runs from a cause to what it may produce" : "Read downward: each line runs to what it may produce" }
        // ── the layered graph ──
        Item {
            id: dag
            visible: dg.graph
            width: parent.width
            readonly property real colW: dg.g.cols > 0 ? width / dg.g.cols : width
            readonly property real rowP: dg.isPrint ? dg.roles.u(25) : dg.roles.u(30)
            readonly property real lineH: dg.roles.u(15)
            height: visible ? dg.g.rows * rowP + dg.roles.u(6) : 0
            Canvas {
                id: links
                anchors.fill: parent
                readonly property var deps: [dg.g, dag.width, dag.visible, dg.roles.ink, dg.roles.ink3]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset()
                    const es = (dg.g.edges || []).slice().sort((a, b) => (a.primary ? 1 : 0) - (b.primary ? 1 : 0))
                    const anyPrimary = es.some(e => e.primary)
                    for (let i = 0; i < es.length; ++i) {
                        const e = es[i], A = nodeRep.itemAt(e.a), B = nodeRep.itemAt(e.b)
                        if (!A || !B) continue
                        const st = dg.roles.linkStyle(e.link)
                        const fade = !anyPrimary || e.primary ? 1 : 0.6
                        const x0 = A.x + A.textRight + dg.roles.u(6), y0 = A.y + dag.lineH / 2 + 1
                        const x1 = B.x - dg.roles.u(5), y1 = B.y + dag.lineH / 2 + 1, mx = (x0 + x1) / 2
                        dg.strokeLink(ctx, [[x0, y0], [mx, y0], [mx, y1], [x1 - (st.arrow ? dg.roles.u(3) : 0), y1]], st, fade)
                        dg.arrowHead(ctx, x1, y1, st, fade, 1, 0)
                    }
                }
            }
            // one item per authored edge, for tests: the canvas paints, these say what it painted
            Repeater {
                model: dg.roles && dg.graph ? dg.g.edges : []
                delegate: Item {
                    objectName: "sdCauseEdge"
                    required property var modelData
                    readonly property var st: dg.roles.linkStyle(modelData.link)
                    readonly property string grade: st.grade
                    readonly property string strokeStyle: st.stroke
                    readonly property bool hasArrow: st.arrow
                    readonly property real strokeOpacity: st.opacity
                    opacity: st.opacity
                    width: 0; height: 0
                }
            }
            Repeater {
                id: nodeRep
                model: dg.roles ? (dg.graph ? dg.g.nodes : []) : []
                onItemAdded: links.requestPaint()
                delegate: Item {
                    id: nd
                    objectName: "sdCauseNode"
                    required property var modelData
                    readonly property var n: modelData
                    readonly property string conditionId: n.id
                    readonly property string kind: n.kind
                    readonly property string mark: dg.nodeKind(n)
                    readonly property bool isSubject: n.id === dg.subject
                    readonly property bool hot: dg.brush && dg.brush.hoveredCondition === n.id
                    readonly property real textRight: nm.x + nm.contentWidth
                    x: n.col * dag.colW; y: n.row * dag.rowP
                    width: dag.colW; height: dag.rowP
                    opacity: dg.brush ? dg.brush.conditionOpacity(n.id, dg.roles.dim) : 1
                    NodeMark { kind: dg.nodeKind(nd.n); y: (dag.lineH - height) / 2 + 1 }
                    Text {
                        id: nm
                        x: dg.roles.u(15); width: dag.colW - x - dg.roles.u(26)
                        wrapMode: Text.WordWrap; text: nd.n.name
                        font: nd.isSubject || nd.hot ? dg.roles.fSmallM : dg.roles.fSmall
                        color: nd.n.state === "fired" || nd.isSubject || nd.hot ? dg.roles.ink : nd.n.state === "clean" ? dg.roles.ink2 : dg.roles.ink3
                        onContentWidthChanged: links.requestPaint()
                    }
                    MouseArea {
                        enabled: dg.interactive && !nd.isSubject; visible: enabled
                        width: nm.x + nm.contentWidth; height: dag.lineH + 2; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: if (dg.brush) dg.brush.hoverCondition(nd.n.id, containsMouse)
                        onClicked: nd.kind === "screenedRoot" ? dg.screenRequested(nd.n.id) : dg.conditionActivated(nd.n.id)
                    }
                    Text {
                        objectName: "sdScreenCta"
                        readonly property string conditionId: nd.n.id
                        visible: nd.kind === "screenedRoot" && dg.interactive
                        x: nm.x; y: nm.y + nm.contentHeight
                        text: "Run the screen"; font: dg.roles.fLabelL; color: dg.roles.link
                        MouseArea { anchors.fill: parent; anchors.margins: -dg.roles.u(2); cursorShape: Qt.PointingHandCursor; onClicked: dg.screenRequested(nd.n.id) }
                    }
                }
            }
        }
        // ── the outline (the graph's other form) ──
        Item {
            id: pt
            visible: !dg.graph
            width: parent.width
            readonly property real rowP: dg.roles.u(20)
            readonly property real ind: dg.roles.u(18)
            height: visible ? dg.tree.length * rowP + dg.roles.u(4) : 0
            function mx(d) { return d * ind + dg.roles.u(4.5) }
            Canvas {
                anchors.fill: parent
                readonly property var deps: [dg.tree, pt.width, pt.visible, dg.roles.ink, dg.roles.ink3]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset()
                    const rs = dg.tree || []
                    const ord = rs.map((r, i) => i).sort((a, b) => (rs[a].primary ? 1 : 0) - (rs[b].primary ? 1 : 0))
                    for (let k = 0; k < ord.length; ++k) {
                        const i = ord[k], r = rs[i]
                        if (r.parent < 0) continue
                        const p = rs[r.parent]
                        const xp = pt.mx(p.depth), yp = r.parent * pt.rowP + pt.rowP / 2 + dg.roles.u(5)
                        const xc = pt.mx(r.depth) - dg.roles.u(7), yc = i * pt.rowP + pt.rowP / 2
                        const st = dg.roles.linkStyle(r.link)
                        const anyPrimary = rs.some(q => q.primary)
                        const fade = !anyPrimary || (r.primary && p.primary) ? 1 : 0.6
                        // causes read upward (cause → what it may produce): the head sits at the parent's end
                        const back = st.arrow ? dg.roles.u(3) : 0
                        if (dg.causes) {
                            dg.strokePolyline(ctx, [[xp, yp + back], [xp, yc], [xc, yc]], st, fade)
                            dg.arrowHead(ctx, xp, yp, st, fade, 0, -1)
                        } else {
                            dg.strokePolyline(ctx, [[xp, yp], [xp, yc], [xc - back, yc]], st, fade)
                            dg.arrowHead(ctx, xc, yc, st, fade, 1, 0)
                        }
                    }
                }
            }
            Repeater {
                model: dg.roles ? (dg.graph ? [] : dg.tree) : []
                delegate: Item {
                    id: tr
                    objectName: "sdCauseNode"
                    required property var modelData
                    required property int index
                    readonly property var r: modelData
                    readonly property string conditionId: r.id
                    readonly property string kind: r.kind
                    readonly property string mark: dg.nodeKind(r)
                    readonly property bool isSubject: r.id === dg.subject
                    readonly property bool hot: dg.brush && dg.brush.hoveredCondition === r.id
                    y: index * pt.rowP; width: pt.width; height: pt.rowP
                    opacity: dg.brush ? dg.brush.conditionOpacity(r.id, dg.roles.dim) : 1
                    NodeMark { x: pt.mx(tr.r.depth) - width / 2; y: (pt.rowP - height) / 2; kind: dg.nodeKind(tr.r) }
                    Text {
                        id: tt
                        x: pt.mx(tr.r.depth) + dg.roles.u(10); width: pt.width - x; y: (pt.rowP - height) / 2
                        text: tr.r.name
                        font: tr.isSubject || tr.hot ? dg.roles.fSmallM : dg.roles.fSmall
                        color: tr.r.state === "fired" || tr.isSubject || tr.hot ? dg.roles.ink : tr.r.state === "clean" ? dg.roles.ink2 : dg.roles.ink3
                    }
                    MouseArea {
                        enabled: dg.interactive && !tr.isSubject; visible: enabled
                        x: tt.x; width: tt.contentWidth; height: parent.height; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: if (dg.brush) dg.brush.hoverCondition(tr.r.id, containsMouse)
                        onClicked: tr.kind === "screenedRoot" ? dg.screenRequested(tr.r.id) : dg.conditionActivated(tr.r.id)
                    }
                    Text {
                        objectName: "sdScreenCta"
                        readonly property string conditionId: tr.r.id
                        visible: tr.kind === "screenedRoot" && dg.interactive
                        x: tt.x + tt.contentWidth + dg.roles.u(10); y: (pt.rowP - height) / 2
                        text: "Run the screen"; font: dg.roles.fLabelL; color: dg.roles.link
                        MouseArea { anchors.fill: parent; anchors.margins: -dg.roles.u(2); cursorShape: Qt.PointingHandCursor; onClicked: dg.screenRequested(tr.r.id) }
                    }
                }
            }
        }
        Text { objectName: "sdDetailCappedNote"; visible: dg.isPrint && !!(dg.detail && (dg.causes ? dg.detail.causesCapped : dg.detail.effectsCapped)); font: dg.roles.fSmall; color: dg.roles.ink3
               text: "The " + dg.paths.length + " strongest paths are drawn; more exist." }
        Text { objectName: "sdDetailCappedNote"; visible: !dg.isPrint && !!(dg.detail && (dg.causes ? dg.detail.causesCapped : dg.detail.effectsCapped)); font: dg.roles.fLabel; color: dg.roles.ink3
               text: "More paths exist than are drawn." }
        Column {
            visible: !dg.margin
            width: dg.roles.measureW(parent.width)
            spacing: dg.roles.u(8)
            topPadding: dg.roles.u(4)
            PpPageKey { visible: dg.showKey && dg.causes; width: parent.width; roles: dg.roles; kinds: dg.keyKinds }
            Repeater {
                model: dg.roles ? (dg.causes && dg.detail ? (dg.detail.rivals || []) : []) : []
                delegate: Text { required property var modelData; objectName: "sdDetailRival"; width: parent.width; wrapMode: Text.WordWrap; font: dg.roles.fSmall; color: dg.roles.ink3; text: modelData.text }
            }
            Text { visible: !dg.causes; width: parent.width; wrapMode: Text.WordWrap; font: dg.roles.fSmall; color: dg.roles.ink3
                   text: "Outcomes are authored, not measured: this capture has no ball-flight reading of them." }
        }
    }
    Column {
        id: notes
        visible: dg.margin
        x: dg.mainW + dg.roles.gutter(dg.width); y: dg.roles.u(44)
        width: dg.marginW
        spacing: dg.roles.u(12)
        Column {
            visible: dg.causes && dg.showKey
            spacing: dg.roles.u(5)
            Text { text: "Key"; font: dg.roles.fLabel; color: dg.roles.ink2 }
            PpPageKey { width: dg.marginW; roles: dg.roles; kinds: dg.keyKinds; spacing: dg.roles.u(6) }
        }
        Text { visible: dg.causes; width: parent.width; wrapMode: Text.WordWrap; font: dg.roles.fSmall; color: dg.roles.ink3
               text: "Every root here is a physical limitation no camera can see: a screen would confirm it. The likely driver's path is drawn at full strength; an arrowhead only where the evidence gives a direction." }
        Repeater {
            model: dg.roles ? (dg.causes && dg.detail ? (dg.detail.rivals || []) : []) : []
            delegate: Text { required property var modelData; objectName: "sdDetailRival"; width: notes.width; wrapMode: Text.WordWrap; font: dg.roles.fSmall; color: dg.roles.ink3; text: modelData.text }
        }
        Text { visible: !dg.causes; width: parent.width; wrapMode: Text.WordWrap; font: dg.roles.fSmall; color: dg.roles.ink3
               text: "Outcomes are authored, not measured: this capture has no ball-flight reading of them. Marks and lines as above." }
    }
}
