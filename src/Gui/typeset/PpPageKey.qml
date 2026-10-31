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

// PAGEKEY — C's key: each entry a drawn mark and its word, flowed. On screen one key per view, above its
// figures (never repeated per figure); in print the PpPager draws one key per page in the footer holding
// only the marks that page uses (every block exposes `keyKinds`).
// kinds: any of fired clean ring hover median fault pass corridor bar tick stub (gap = stub) strip
//        box fit chance dots (the Charts panel's: a session box, a least-squares line clear of / inside
//        chance, raw samples)
//        n-fired n-clean n-root n-ghost n-outcome
//        l-movedTogether l-conditionallyDependent l-coherent l-presentTogether l-unanchored
// or set: "shot" | "session" | "watch" | "figure" | "paths" | "pathsShort". extraWords overrides a kind's word
// for one instance (a panel's own vocabulary); nothing else changes.
// Round 4: a page holding a ledger strip AND a barcode keys the strip ONCE ("strip" absorbs bar/tick/stub);
// the not-measured mark is a stub, never a gap (D5); the five link grades key with their own strokes (D6).
Flow {
    id: pk
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property var readout: null
    property string set: ""
    property var kinds: []
    property bool atom: true
    property bool keepNext: true
    readonly property var keyKinds: []
    objectName: "sdPageKey"

    readonly property string shotLabel: readout ? roles.shotOfText(readout) : "this shot"
    // A panel's own words for a kind, merged over the defaults (e.g. Charts: a hollow mark is "a shot past the
    // whiskers", not "clean"). Empty by default, so a key that sets nothing reads exactly as before.
    property var extraWords: ({})
    readonly property var words: Object.assign({}, _words, extraWords || {})
    readonly property var _words: ({
        "fired": "fired", "clean": "clean", "ring": shotLabel, "hover": "the shot under the pointer",
        "median": "median of the last 5", "fault": "fault line", "pass": "pass edge", "corridor": "inside the corridor",
        "bar": "fired", "tick": "clean", "gap": "stub: not measured", "stub": "stub: not measured",
        "strip": "shots in the order struck; a bar fired, a tick clean, a stub not measured",
        "box": "the session: middle half, median, whiskers", "fit": "least squares, clears chance",
        "chance": "least squares, inside chance", "dots": "raw samples",
        "n-fired": "fired on this shot", "n-clean": "measured, clean", "n-root": "screened root, needs a physical screen",
        "n-ghost": "no reading on this shot", "n-outcome": "outcome, authored",
        "l-movedTogether": "moved together", "l-conditionallyDependent": "conditionally dependent",
        "l-coherent": "coherent", "l-presentTogether": "present together", "l-unanchored": "unanchored"
    })
    readonly property var sets: ({
        shot: ["fired", "clean", "corridor", "fault"],
        session: ["fired", "clean", "ring", "median", "fault", "corridor"],
        watch: ["bar", "tick", "stub"],
        figure: ["fired", "clean", "ring", "median", "fault", "pass", "corridor"],
        paths: ["n-fired", "n-clean", "n-root", "n-ghost", "n-outcome", "l-movedTogether", "l-conditionallyDependent", "l-coherent", "l-presentTogether", "l-unanchored"],
        pathsShort: ["n-fired", "n-clean", "n-root", "n-ghost", "n-outcome", "l-movedTogether", "l-conditionallyDependent", "l-coherent", "l-presentTogether", "l-unanchored"]
    })
    readonly property var list: {
        if (!roles) return []
        let ks = (kinds && kinds.length ? kinds : (sets[set] || [])).filter(k => words[k] !== undefined)
        if (ks.indexOf("strip") >= 0) ks = ks.filter(k => k !== "bar" && k !== "tick" && k !== "gap" && k !== "stub")
        if (ks.indexOf("gap") >= 0 && ks.indexOf("stub") >= 0) ks = ks.filter(k => k !== "gap")
        return ks.filter((k, i) => ks.indexOf(k) === i)
    }

    spacing: roles ? roles.u(14) : 0
    Repeater {
        model: pk.roles ? (pk.list) : []
        delegate: Row {
            id: ki
            required property var modelData
            readonly property string kind: modelData
            spacing: pk.roles.u(5)
            Item {
                id: sw
                width: pk.roles.u(ki.kind.indexOf("l-") === 0 || ki.kind === "box" || ki.kind === "fit" || ki.kind === "chance" ? 22
                                   : ki.kind === "median" || ki.kind === "fault" || ki.kind === "pass" ? 16
                                   : ki.kind === "strip" ? 30 : 10)
                height: pk.roles.u(12)
                anchors.verticalCenter: parent.verticalCenter
                PpMark { visible: ki.kind === "fired"; roles: pk.roles; fired: true; cx: sw.width / 2; cy: sw.height / 2 }
                PpMark { visible: ki.kind === "clean"; roles: pk.roles; fired: false; cx: sw.width / 2; cy: sw.height / 2 }
                Rectangle { visible: ki.kind === "ring" || ki.kind === "hover"; readonly property real d: pk.roles.ringD * 0.8
                            x: (sw.width - d) / 2; y: (sw.height - d) / 2; width: d; height: d; radius: pk.roles.square ? 0 : d / 2
                            color: "transparent"; border.width: ki.kind === "ring" ? 1.25 : 1; border.color: ki.kind === "ring" ? pk.roles.ink : pk.roles.ink3 }
                Rectangle { visible: ki.kind === "median"; width: parent.width; height: 1.2; color: pk.roles.ink2; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { visible: ki.kind === "fault"; width: parent.width; height: Math.max(1, pk.roles.hw); color: pk.roles.fault; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { visible: ki.kind === "corridor"; anchors.fill: parent; color: Qt.alpha(pk.roles.ink, 0.07) }
                Rectangle { visible: ki.kind === "bar"; anchors.centerIn: parent; width: Math.max(3, pk.roles.u(4)); height: parent.height; radius: pk.roles.r.barRadius; color: pk.roles.warn }
                Rectangle { visible: ki.kind === "tick"; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                            width: 1; height: parent.height * 0.6; color: pk.roles.ink3 }
                Rectangle { visible: ki.kind === "gap" || ki.kind === "stub"; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                            width: Math.max(3, pk.roles.u(4)); height: pk.roles.stubH(parent.height); color: pk.roles.ink3 }
                // a box in miniature: whisker rule, the middle half, the median
                Rectangle { visible: ki.kind === "box"; anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: pk.roles.hw; color: pk.roles.ink2 }
                Rectangle { visible: ki.kind === "box"; x: parent.width * 0.25; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width * 0.5; height: pk.roles.u(8); color: Qt.alpha(pk.roles.ink3, 0.10); border.width: pk.roles.hw; border.color: pk.roles.ink2 }
                Rectangle { visible: ki.kind === "box"; x: parent.width * 0.45; anchors.verticalCenter: parent.verticalCenter
                            width: 1.5; height: pk.roles.u(10); color: pk.roles.ink }
                Rectangle { visible: ki.kind === "fit"; anchors.verticalCenter: parent.verticalCenter; width: parent.width
                            height: pk.roles.u(pk.roles.r.ch.fitLineW); color: pk.roles.ink2 }
                Row {
                    visible: ki.kind === "dots"; anchors.centerIn: parent; spacing: 2
                    Repeater { model: 3; Rectangle { width: 2; height: 2; color: Qt.alpha(pk.roles.ink3, 0.6) } }
                }
                PpShotStrip { visible: ki.kind === "strip"; roles: pk.roles; anchors.verticalCenter: parent.verticalCenter; interactive: false
                            pitch: sw.width / 4; barH: sw.height * 0.8
                            ticks: [{ state: "fired", shotId: -9 }, { state: "clean", shotId: -9 }, { state: "na", shotId: -9 }, { state: "fired", shotId: -9 }] }
                Item {
                    visible: ki.kind.indexOf("n-") === 0
                    anchors.centerIn: parent; width: pk.roles.u(9); height: width
                    readonly property string nk: ki.kind.slice(2)
                    PpMark { visible: parent.nk === "fired"; roles: pk.roles; fired: true; cx: parent.width / 2; cy: parent.height / 2 }
                    Rectangle { visible: parent.nk === "clean"; anchors.centerIn: parent; width: pk.roles.u(7); height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: pk.roles.ink2 }
                    Rectangle { visible: parent.nk === "ghost"; anchors.centerIn: parent; width: pk.roles.u(5); height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: Qt.alpha(pk.roles.ink3, 0.7) }
                    Rectangle { visible: parent.nk === "root"; anchors.centerIn: parent; width: pk.roles.u(7); height: width; color: "transparent"; border.width: 1; border.color: pk.roles.ink3 }
                    Rectangle { visible: parent.nk === "outcome"; anchors.centerIn: parent; width: pk.roles.u(6); height: width; rotation: 45; color: "transparent"; border.width: 1; border.color: pk.roles.ink3; antialiasing: true }
                }
                Canvas {
                    visible: ki.kind === "pass" || ki.kind === "chance" || ki.kind.indexOf("l-") === 0
                    anchors.fill: parent
                    readonly property var deps: [ki.kind, pk.roles.ink2, pk.roles.ink3, pk.roles.passEdge, width]
                    onDepsChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        const R = pk.roles, y = Math.round(height / 2) + 0.5
                        if (ki.kind === "pass" || ki.kind === "chance") {
                            const chance = ki.kind === "chance"
                            ctx.strokeStyle = chance ? R.ink3 : R.passEdge; ctx.lineWidth = chance ? R.u(R.r.ch.fitLineW) : 1
                            const pat = chance ? R.r.ch.chanceDash.map(d => R.u(d)) : R.r.passDash; let x = 0, on = true
                            ctx.beginPath()
                            while (x < width) { const st = pat[on ? 0 : 1]; if (on) { ctx.moveTo(x, y); ctx.lineTo(Math.min(width, x + st), y) } x += st; on = !on }
                            ctx.stroke(); return
                        }
                        // a link grade: the same stroke, weight, opacity and (only where it carries one) arrowhead
                        const st = R.linkStyle({ grade: ki.kind.slice(2) })
                        const col = Qt.alpha(st.color, st.opacity)
                        const head = st.arrow ? R.u(5) + st.width : 0
                        ctx.strokeStyle = col; ctx.lineWidth = st.width; ctx.lineCap = st.stroke === "dotted" ? "round" : "butt"
                        const d = R.dashFor(st.stroke), pat = d ? (st.stroke === "dotted" ? [0.1, d[1] + 0.6] : d) : null
                        ctx.beginPath()
                        if (!pat) { ctx.moveTo(0, y); ctx.lineTo(width - head, y) }
                        else { let x = 0, on = true; while (x < width - head) { const s2 = pat[on ? 0 : 1]; if (on) { ctx.moveTo(x, y); ctx.lineTo(Math.min(width - head, x + s2), y) } x += s2; on = !on } }
                        ctx.stroke()
                        if (head > 0) {
                            const hw = R.u(3) + st.width * 0.5
                            ctx.fillStyle = col; ctx.beginPath(); ctx.moveTo(width, y); ctx.lineTo(width - head, y - hw); ctx.lineTo(width - head, y + hw); ctx.closePath(); ctx.fill()
                        }
                    }
                }
            }
            Text { text: pk.words[ki.kind] || ""; font: pk.roles.fLabel; color: pk.roles.ink3; anchors.verticalCenter: parent.verticalCenter }
        }
    }
}
