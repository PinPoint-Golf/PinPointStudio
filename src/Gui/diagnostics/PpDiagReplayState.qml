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

// REPLAYSTATE — what the session IS right now, read once for both registers (PpDiagReplay and PpDiagAnalyse
// each make one). The old body branched on the model's stage, the review badge,
// the readout and `interactive`; the round-3 registers branched only on the readout. This restores the
// old panel's states (INVENTORY §1) without the registers asking the stage separately.
//   tense        "none" (no source) · "live" · "closing" · "review"
//   shot         what This shot reads: the picked readout, or (D3) in a live session with nothing picked
//                the NEWEST swing, built from source.afterShotDelta + source.thisShot (a partial readout:
//                fired conditions only, the clean / not-measured counts as numbers; `partial: true`).
//   empty*       the old panel's words for a This shot with nothing to read (F19), by tense.
//   quiet        bandwidth cadence held the newest swing back (S8, D2).
//   label        "shot N of M" from the ORDINAL (shotIndex + 1), never the shot id (D7).
QtObject {
    id: st
    property var source: null
    property var header: null
    property var readout: null
    property var cards: []
    property bool interactive: true

    readonly property string stage: source ? String(source.stage || "") : ""
    readonly property bool reviewing: !!header && String(header.reviewBadge || "") !== ""
    readonly property bool closing: !reviewing && (stage === "closing" || !!(header && header.closed))
    readonly property bool cold: stage === "cold"
    readonly property bool forming: stage === "forming"
    readonly property string tense: !source ? "none" : reviewing ? "review" : closing ? "closing" : "live"
    readonly property bool live: tense === "live"
    readonly property int sessionShots: source ? (source.shotCount || (header ? header.shotCount || 0 : 0)) : 0

    readonly property var delta: source && source.afterShotDelta && source.afterShotDelta.shotId !== undefined ? source.afterShotDelta : null
    readonly property var newest: (!readout && live && delta && sessionShots > 0) ? buildNewest() : null
    readonly property var shot: readout || newest
    readonly property bool partial: !!shot && !!shot.partial
    readonly property bool isNewest: !!shot && shot.shotIndex === sessionShots - 1
    readonly property bool quiet: live && !!source && source.quiet === true && isNewest
    readonly property string label: shot ? "shot " + (shot.shotIndex + 1) + " of " + shot.shotCount : ""
    readonly property string club: shot && shot.club ? String(shot.club).toLowerCase() : ""

    // F19: the three tenses of an empty This shot (the old sdShotEmpty words), and the live one
    readonly property string emptyTitle: tense === "live" ? "No shot yet" : ""
    readonly property string emptyLine: tense === "review" ? "Pick a shot in the carousel to read it here."
                                      : tense === "closing" ? "The session has closed — pick a shot in the carousel to read it here."
                                      : tense === "live" ? "the next swing reads here" : ""

    // the declared miss (F66): invited in Cold and Forming while live; reported once declared, at any stage
    readonly property string declaredMiss: source ? String(source.declaredMiss || "") : ""
    readonly property string declaredMissName: source ? String(source.declaredMissName || "") : ""
    readonly property bool missInvited: interactive && declaredMiss === "" && (cold || forming) && !reviewing && !!source
    readonly property bool missShown: missInvited || declaredMiss !== ""
    readonly property string missText: declaredMiss !== "" ? "Miss: " + (declaredMissName || declaredMiss) : "Declare miss"
    function missCandidates() {
        if (source && typeof source.missCandidates === "function") return source.missCandidates() || []
        return []
    }

    // the focus contract (S25): read back off the model, never remembered here
    readonly property string focusId: source ? String(source.focusConditionId || "") : ""
    function focusLine(name, state) {
        return name + " — " + (state === "fired" ? "fired here" : state === "clean" ? "clean here" : "not measured here")
    }

    // the newest swing publishes no per-shot run for a watched condition: Watching lists names and counts
    readonly property var watchSpec: [{ key: "name", tier: 1, kind: "name", w: 0, head: "Condition" },
                                      { key: "frac", tier: 1, kind: "figs", w: 3, head: "Fired", align: "r" }]
    function byId(list, id) { for (let i = 0; i < (list || []).length; ++i) if (list[i].id === id) return list[i]; return null }
    // D3: the newest swing when nothing is picked live. thisShot carries the fired chips (id, name, tier,
    // strength*) and the delta the counts; a pattern's card adds its corridor and run. The value printed is
    // the out-by figure (the dot plot's own x), the one number published for EVERY fired chip.
    function buildNewest() {
        const d = delta, cs = cards || [], ws = (source && source.watching) || []
        const conds = ((source && source.thisShot) || []).filter(t => t.kind === "fired").map(t => {
            const card = byId(cs, t.id), w = byId(ws, t.id)
            return { id: t.id, name: t.name, stateKind: "fired", tierTag: t.tier || "watching",
                     strengthExcess: t.strengthExcess, strengthKnown: t.strengthKnown !== false,
                     valueText: t.strengthKnown !== false && t.strengthExcess !== undefined ? Number(t.strengthExcess).toFixed(1) + "×" : "-",
                     corridorText: card ? card.corridorText : "", recurrence: card ? card.recurrence : (w ? w.recurrence : ""),
                     ticks: card ? card.ticks : [], firingsAfterText: "" }
        })
        return { shotId: d.shotId, shotIndex: d.shotIndex, shotCount: sessionShots, club: "", conditions: conds,
                 firedCount: d.firedCount || conds.length, cleanCount: d.cleanCount || 0,
                 notAssessableCount: d.notAssessableCount || 0, measurableSetCount: (d.firedCount || 0) + (d.cleanCount || 0),
                 headline: d.headline || "", note: d.note || "", partial: true }
    }
}
