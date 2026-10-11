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

// PATHCHAINS — B's typeset causal paths: a crosshead ("What might have caused this" / "What this leads
// to"), the headline as a lead sentence, then numbered chains "A ⇒ B ∼ C" with footnote marks (*, †, ‡)
// and a footnote that states the link grades and marks ONCE. Names outside the corridor on this shot
// are in warn. Path 1 carries its evidence quote from compact up (else it sits behind Open).
// which: "causes" | "effects" | "both" (both = side by side from roles.flows.proseTwo em, else stacked).
// Round 4:
//   • LINK GRADES (D6): one glyph per model grade, keyed once — ⇒ moved together, → conditionally
//     dependent (the only two that carry a direction), ╌ coherent, ∼ present together, ⋯ unanchored.
//   • OPEN IN PLACE (F58): "Open" expands the path under itself — each node with its reading and mark,
//     each link with its grade word and the model's note; "Close" folds it. pathActivated still fires.
//   • PRINT: stacked (or print), every section is a list — its crosshead + lead one atom (the list's head,
//     printGroup "paths-<which>"), each path an atom, the key kept with the last path — so a long section
//     splits between paths and repeats its head. (This replaces reg/PrintPaths.)
Item {
    id: pc
    objectName: "sdPathChains"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    readonly property bool isPrint: medium === "print"
    property bool interactive: !isPrint
    property var detail: null
    property var readout: null
    property string which: "causes"
    property string rest: ""                    // effects: the remainder of a short standfirst (PpFeatureHead longForm=false)
    property bool rule: true
    property var openPaths: ({})                // { "causes:0": true } — paths opened in place (screen)
    readonly property bool atom: pair          // a pair never splits; stacked, each section is a list of atoms
    readonly property var keyKinds: []
    signal pathActivated(string which, int index)
    signal conditionActivated(string id)

    readonly property int shotNo: roles.shotNo(readout)
    readonly property bool pair: which === "both" && width >= roles.flows.proseTwo * roles.em
    readonly property var sections: which === "both" ? ["causes", "effects"] : [which]
    readonly property real colGap: roles.u(56)
    readonly property real colW: pair ? (width - colGap) / 2 : width

    function isOpen(k, i) { return !!(openPaths || {})[k + ":" + i] }
    function togglePath(k, i) { const o = Object.assign({}, openPaths || {}); const key = k + ":" + i; if (o[key]) delete o[key]; else o[key] = true; openPaths = o }
    function marksFor(paths) {
        const marks = [], glyphs = ["*", "†", "‡", "§", "‖"]
        ;(paths || []).forEach(p => (p.nodes || []).forEach(n => { if (n.mark && marks.indexOf(n.mark) < 0) marks.push(n.mark) }))
        return marks.map((m, i) => ({ mark: m, glyph: glyphs[i] || "¶" }))
    }
    function glyphFor(marks, m) { for (let i = 0; i < marks.length; ++i) if (marks[i].mark === m) return marks[i].glyph; return "" }
    function linkGlyph(g) { return (roles.linkGrades[g] || roles.linkGrades.unanchored).glyph }
    function linkHtml(l) {
        const st = roles.linkStyle(l || {})
        return "<span style=\"color:" + roles.hex(st.color) + ";\">" + st.glyph + "</span>"
    }
    function chainHtml(p, marks) {
        const R = roles, ns = p.nodes || [], ls = p.links || [], self = detail ? detail.id : ""
        let h = ""
        for (let i = 0; i < ns.length; ++i) {
            const n = ns[i]
            let name = R.esc(n.name)
            if (n.state === "fired" && n.id !== self) name = R.toneHtml(name, R.warn)
            else if (n.state === "notAssessable") name = "<i>" + name + "</i>"
            else name = R.toneHtml(name, R.ink)
            const gl = glyphFor(marks, n.mark)
            h += name.replace(/ /g, "&nbsp;").replace(/&nbsp;/, " ") + (gl ? R.toneHtml(gl, R.ink3) : "")
            if (i < ns.length - 1) h += " " + linkHtml(ls[i]) + " "
        }
        return h
    }
    function keyHtml(paths, marks, k) {
        const R = roles, grades = {}
        ;(paths || []).forEach(p => (p.links || []).forEach(l => {
            const st = R.linkStyle(l)
            if (!grades[st.grade]) grades[st.grade] = { st: st, notes: [] }
            if (st.note && grades[st.grade].notes.indexOf(st.note) < 0) grades[st.grade].notes.push(st.note)
        }))
        const parts = []
        R.linkGradeOrder.forEach(g => {
            const e = grades[g]
            if (!e) return
            parts.push(linkHtml({ grade: g }) + " " + R.esc(String(e.st.word).toLowerCase()) + (e.notes.length ? ": " + R.esc(e.notes.join(", or ")) : ""))
        })
        marks.forEach(m => parts.push(m.glyph + " " + R.esc(m.mark.replace(" · ", ", "))))
        let t = parts.join(". ") + (parts.length ? "." : "")
        if (Object.keys(grades).some(g => grades[g].st.arrow)) t += " An arrow only where the evidence gives a direction."
        const d = detail || {}
        if (d[k + "Capped"]) t += " The first " + R.word(paths.length) + " paths are shown."
        t += " Names in " + R.toneHtml("colour", R.warn) + " were outside the corridor on shot " + R.figHtml(shotNo) + "."
        if (k === "causes") (d.rivals || []).forEach(rv => t += " " + R.esc(rv.text))
        return t
    }
    // the open path, line by line: a node's reading, then the link to the next with its grade and note
    function nodeLine(n) {
        const R = roles, bits = []
        if (n.state === "fired") bits.push(R.toneHtml("outside on this shot", R.warn))
        else if (n.state === "clean") bits.push("inside on this shot")
        else bits.push("no reading on this shot")
        if (n.recurrence) bits.push(R.esc(R.shortCount(n.recurrence)))
        if (n.mark) bits.push(R.esc(n.mark))
        return R.strongHtml(n.name) + "&nbsp;&nbsp;" + "<span style=\"color:" + R.hex(R.ink3) + ";\">" + bits.join(" · ") + "</span>"
    }
    function linkLine(l) {
        const st = roles.linkStyle(l || {})
        return linkHtml(l) + "&nbsp;&nbsp;<span style=\"color:" + roles.hex(roles.ink2) + ";\">" + roles.esc(st.word) + (st.note ? ": " + roles.esc(st.note) : "") + "</span>"
    }

    implicitHeight: grid.y + grid.height
    height: implicitHeight

    // the rule: across the pair; stacked, each section's head carries its own (so a page break never strands it)
    Rectangle { visible: pc.rule && pc.pair; width: parent.width; height: pc.roles.hw; color: pc.roles.rule }
    Grid {
        id: grid
        y: pc.rule && pc.pair ? pc.roles.u(14) : 0
        columns: pc.pair ? 2 : 1
        columnSpacing: pc.colGap
        rowSpacing: pc.roles.u(26)
    Repeater {
        id: secRep
        model: pc.roles ? (pc.sections) : []
        delegate: Column {
            id: sec
            required property var modelData
            required property int index
            readonly property string k: modelData
            readonly property var paths: (pc.detail && pc.detail[k]) || []
            readonly property var marks: pc.marksFor(paths)
            // stacked, a section is a list (head, paths, key) the PpPager may cut; paired, the block is one atom
            readonly property var keyKinds: []
            readonly property string printGroup: "paths-" + k
            width: pc.colW
            spacing: pc.roles.u(10)
            Column {
                property bool atom: true
                property bool keepNext: true
                width: sec.width
                spacing: pc.roles.u(10)
                Rectangle { visible: pc.rule && !pc.pair; width: parent.width; height: pc.roles.hw; color: pc.roles.rule }
                Text {
                    text: sec.k === "causes" ? "What might have caused this" : "What this leads to"
                    font: pc.roles.fTitle; color: pc.roles.ink
                }
                Text {
                    visible: sec.k === "effects" && pc.rest.length > 0
                    width: pc.roles.measureW(parent.width); wrapMode: Text.Wrap
                    text: pc.rest; font: pc.roles.fBody; color: pc.roles.ink2; lineHeight: pc.roles.r.leading
                }
                Text {
                    objectName: sec.k === "causes" ? "sdDetailCauseHeadline" : "sdDetailOutcomeHeadline"
                    width: pc.roles.measureW(parent.width); wrapMode: Text.Wrap; textFormat: Text.RichText
                    text: { const d = pc.detail || {}
                            return sec.k === "causes" ? (sec.paths.length ? pc.roles.headlineHtml(d.causeHeadline) : pc.roles.esc(d.noCausesLine || ""))
                                                      : (sec.paths.length ? pc.roles.headlineHtml(d.outcomeHeadline) : pc.roles.esc(d.noEffectsLine || "")) }
                    font: pc.roles.fBody; color: pc.roles.ink2; lineHeight: pc.roles.r.leading
                }
            }
            Repeater {
                model: pc.roles ? (sec.paths) : []
                delegate: Item {
                    id: pth
                    objectName: "sdPathChain"
                    required property var modelData
                    required property int index
                    property bool atom: true
                    readonly property string which: sec.k
                    readonly property bool open: !pc.isPrint && pc.isOpen(sec.k, index)
                    readonly property var ids: (modelData.nodes || []).map(n => n.id)
                    readonly property bool hot: pc.brush && pc.brush.hoveredCondition !== "" && ids.indexOf(pc.brush.hoveredCondition) >= 0 && pc.brush.hoveredCondition !== (pc.detail ? pc.detail.id : "")
                    width: pc.roles.measureW(sec.width)
                    height: chain.height + (ev.visible ? ev.height + pc.roles.u(3) : 0) + (opened.visible ? opened.height + pc.roles.u(6) : 0)
                    Rectangle { anchors.fill: parent; anchors.margins: -pc.roles.u(2); visible: pth.hot; color: pc.roles.rowHover }
                    Text {
                        text: (pth.index + 1) + "."
                        font: pc.roles.fFig; color: pc.roles.ink3
                        y: chain.baselineOffset - baselineOffset
                    }
                    Text {
                        id: chain
                        x: 1.8 * pc.roles.em; width: parent.width - x
                        wrapMode: Text.Wrap; textFormat: Text.RichText
                        text: pc.chainHtml(pth.modelData, sec.marks)
                              + (pc.isPrint || !pc.interactive ? "" : "&nbsp;&nbsp;&nbsp;<span style=\"color:" + pc.roles.hex(pc.roles.link) + ";" + (pc.roles.r.linkUnderline ? " text-decoration:underline;" : "") + "\">" + (pth.open ? "Close" : "Open") + "</span>")
                        font: pc.roles.fBody; color: pc.roles.ink2; lineHeight: pc.roles.r.leading
                    }
                    Item { objectName: "sdPathOpen"; readonly property bool open: pth.open; readonly property int index: pth.index; visible: !pc.isPrint && pc.interactive; width: 0; height: 0 }
                    Text {
                        id: ev
                        visible: pth.index === 0 && text !== "" && (pc.roles.shows("pathEvidence", pc.width) || pth.open)
                        x: chain.x; y: chain.height + pc.roles.u(3); width: chain.width
                        wrapMode: Text.Wrap
                        text: { const ns = pth.modelData.nodes || []; const n = sec.k === "causes" ? ns[0] : ns[ns.length - 1]; return n && n.evidence ? n.evidence : "" }
                        font: pc.roles.fSmallI; color: pc.roles.ink3; lineHeight: pc.roles.r.smallLeading
                    }
                    // OPEN IN PLACE: the path read node by node, link by link
                    Column {
                        id: opened
                        visible: pth.open
                        x: chain.x + pc.roles.u(6); y: chain.height + (ev.visible ? ev.height + pc.roles.u(3) : 0) + pc.roles.u(6); width: chain.width - pc.roles.u(6)
                        spacing: pc.roles.u(2)
                        Rectangle { width: pc.roles.u(28); height: pc.roles.hw; color: pc.roles.hair }
                        Repeater {
                            model: pth.open ? (pth.modelData.nodes || []) : []
                            delegate: Column {
                                required property var modelData
                                required property int index
                                width: opened.width
                                Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.RichText
                                       text: pc.nodeLine(modelData); font: pc.roles.fSmall; color: pc.roles.ink2; lineHeight: pc.roles.r.smallLeading }
                                Text { visible: index < (pth.modelData.nodes || []).length - 1
                                       x: pc.roles.u(10); width: parent.width - x; wrapMode: Text.Wrap; textFormat: Text.RichText
                                       text: visible ? pc.linkLine((pth.modelData.links || [])[index]) : ""; font: pc.roles.fSmall; color: pc.roles.ink2
                                       lineHeight: pc.roles.r.smallLeading; topPadding: pc.roles.u(1); bottomPadding: pc.roles.u(1) }
                            }
                        }
                    }
                    MouseArea {
                        enabled: pc.interactive; visible: enabled
                        width: parent.width; height: chain.height; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: if (pc.brush && pth.ids.length) pc.brush.hoverCondition(sec.k === "causes" ? pth.ids[0] : pth.ids[pth.ids.length - 1], containsMouse)
                        onClicked: { pc.togglePath(sec.k, pth.index); pc.pathActivated(sec.k, pth.index) }
                    }
                }
            }
            Column {
                property bool atom: true
                property bool keepPrev: true
                visible: sec.paths.length > 0
                width: pc.roles.measureW(sec.width)
                spacing: pc.roles.u(6)
                topPadding: pc.roles.u(4)
                Rectangle { width: pc.roles.u(28); height: pc.roles.hw; color: pc.roles.rule }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; textFormat: Text.RichText
                    text: pc.keyHtml(sec.paths, sec.marks, sec.k)
                    font: pc.roles.fSmall; color: pc.roles.ink3; lineHeight: pc.roles.r.smallLeading
                }
            }
        }
    }
    }
}
