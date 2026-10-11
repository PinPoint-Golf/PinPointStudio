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

// QUESTIONS — Analyse only: at most three sharper questions a view can end with, each computed from the
// data on the panel and answered IN PLACE through the brush (light the shots or conditions that answer
// it; everything else recedes). Shown only when the answer exists. Co-occurrence, never cause.
// Build the list with forCondition(card, readout, source) (or your own [{ id, text, answer, shots |
// conditions, step }]); asking one applies it to the brush and emits asked(q); asking it again clears.
// `step` (shot ids) lets the register offer "step through them" via body.shotRequested.
// Print: the answers become footnotes (medium "print" draws `answer` sentences, no affordances).
// Round 4: forCondition(card, readout, source, opts) — opts.only: ids that "what fired with it" may name
// (the rows on screen); opts.subject: true phrases each question and answer about the card by name
// ("Over the top: which shots were clean?"). Every question also carries `parts`, its answer as DATA:
//   clean { shots, firedCount } · together { list: [{ id, name, n, of }] } · worse { shots, trendText }
//   · bookends { worst, best }  — so print can word the answers without recomputing them.
Column {
    id: qs
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    readonly property bool isPrint: medium === "print"
    property var questions: []
    property string label: "Ask"
    property bool applyToBrush: true
    property bool interactive: !isPrint
    property bool atom: true
    readonly property var keyKinds: []
    signal asked(var q)
    signal stepRequested(var shotIds)

    // ── candidates, computed from the live shapes ──
    function cleanShots(card) { return ((card && card.ticks) || []).filter(t => t.state === "clean").map(t => t.shotId) }
    function firedShots(ticks) { return (ticks || []).filter(t => t.state === "fired").map(t => t.shotId) }
    function firedTogether(readout, id, max) {
        const cs = (readout && readout.conditions) || [], me = roles.byId(cs, id)
        if (!me) return []
        const mine = firedShots(me.ticks)
        if (!mine.length) return []
        const out = []
        cs.forEach(c => {
            if (c.id === id) return
            const n = firedShots(c.ticks).filter(s => mine.indexOf(s) >= 0).length
            if (n > 0) out.push({ id: c.id, name: c.name, n: n, of: mine.length })
        })
        out.sort((a, b) => b.n - a.n || a.name.localeCompare(b.name))
        return out.slice(0, max || 3)
    }
    function thirds(card) {
        const ids = ((card && card.ticks) || []).map(t => t.shotId), k = Math.floor(ids.length / 3)
        return k > 0 ? ids.slice(0, k).concat(ids.slice(ids.length - k)) : []
    }
    function list(xs) { return xs.length <= 1 ? xs.join("") : xs.slice(0, -1).join(", ") + " and " + xs[xs.length - 1] }
    function forCondition(card, readout, source, opts) {
        if (!card) return []
        const o = opts || {}, subj = !!o.subject, nm = card.name || ""
        const ask = t => subj ? nm + ": " + roles.lower1(t) : t
        const out = []
        const clean = cleanShots(card), firedN = firedShots(card.ticks).length
        if (clean.length > 0 && clean.length < (card.ticks || []).length)
            out.push({ id: "clean", text: ask("Which shots were clean?"), shots: clean, step: clean,
                       parts: { shots: clean, firedCount: firedN },
                       answer: subj ? roles.cap1(nm) + " was clean on shot" + (clean.length > 1 ? "s " : " ") + list(clean) + " and fired on the other " + firedN + "."
                                    : "Clean on shot" + (clean.length > 1 ? "s " : " ") + list(clean) + "." })
        let tog = firedTogether(readout, card.id, 40)
        if (o.only) tog = tog.filter(t => o.only.indexOf(t.id) >= 0)
        tog = tog.slice(0, 3)
        if (tog.length && tog[0].n >= 2)
            out.push({ id: "together", text: ask("What fired with it?"), conditions: [card.id].concat(tog.map(t => t.id)),
                       parts: { list: tog },
                       answer: subj ? "On the same shots: " + tog.map(t => t.name + " (" + t.n + " of its " + t.of + ")").join(", ") + ". They occur together; that is not to say one causes another."
                                    : tog.map(t => t.name + " fired together on " + t.n + " of its " + t.of + " shots").join("; ") + "." })
        if (card.trendKnown && card.trend === "worsening")
            out.push({ id: "worse", text: ask("Is it getting worse?"), shots: thirds(card),
                       parts: { shots: thirds(card), trendText: card.trendText || "" },
                       answer: "The first and last thirds of the run: " + roles.lower1(card.trendText || "") + "." })
        else {
            const b = roles.byId((source && source.bookends) || [], card.id)
            if (b) out.push({ id: "bookends", text: ask("Worst against best?"), shots: [b.worstShotId, b.bestShotId], step: [b.worstShotId, b.bestShotId],
                              parts: { worst: b.worstShotId, best: b.bestShotId },
                              answer: "Worst on shot " + b.worstShotId + ", best on shot " + b.bestShotId + "." })
        }
        return out.slice(0, 3)
    }
    function ask(q) {
        const on = !(brush && brush.question === q.id)
        if (applyToBrush && brush) {
            if (!on) brush.lightShots([], "")
            else if (q.conditions) brush.lightConditions(q.conditions, q.id)
            else brush.lightShots(q.shots || [], q.id)
        }
        asked(on ? q : null)
    }

    objectName: "sdQuestions"
    visible: (questions || []).length > 0
    spacing: roles.u(4)
    Text { visible: !qs.isPrint; text: qs.label; font: qs.roles.fLabel; color: qs.roles.ink3 }
    Repeater {
        model: qs.roles ? (qs.questions || []) : []
        delegate: Column {
            id: qd
            objectName: "sdQuestion"
            required property var modelData
            readonly property string questionId: modelData.id
            readonly property bool on: qs.brush && qs.brush.question === modelData.id
            width: qs.width
            spacing: qs.roles.u(2)
            Row {
                visible: !qs.isPrint
                spacing: qs.roles.u(10)
                Text {
                    text: qd.modelData.text; font: qd.on ? qs.roles.fSmallM : qs.roles.fSmallL; color: qd.on ? qs.roles.ink : qs.roles.link
                    MouseArea { enabled: qs.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: qs.ask(qd.modelData) }
                }
                Text {
                    visible: qd.on && (qd.modelData.step || []).length > 0
                    text: "step through them"; font: qs.roles.fSmallL; color: qs.roles.link
                    MouseArea { enabled: qs.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: qs.stepRequested(qd.modelData.step) }
                }
                Text {
                    visible: qd.on
                    text: "clear"; font: qs.roles.fSmallL; color: qs.roles.ink3
                    MouseArea { enabled: qs.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: qs.ask(qd.modelData) }
                }
            }
            // the answer: in place on screen (reserved space appears only when asked), a footnote on paper
            Text {
                objectName: "sdAnswer"
                readonly property string questionId: qd.modelData.id
                visible: qs.isPrint || qd.on
                width: Math.min(qs.width, qs.roles.flows.measure * qs.roles.em)
                wrapMode: Text.WordWrap
                text: qd.modelData.answer || ""; font: qs.isPrint ? qs.roles.fMicro : qs.roles.fSmallI; color: qs.roles.ink2
            }
        }
    }
}
