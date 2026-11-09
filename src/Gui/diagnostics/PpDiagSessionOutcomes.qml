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

// DIAGSESSIONOUTCOMES — the Session view's "what happened" block, one layout for Replay, Analyse and Print
// (shot_outcomes_design.md §6.3): the shot log (PpShotRuler with outcome words), the shot mix, the carry against
// the usual, and the reasons table. The model's words, positioned; no reason colours, no tooltips.
// In print every part is a pager unit (the ruler and the notes atoms, the table a printGroup list).
// A reason row opens its condition; the unexplained row offers "Flag for review" instead, for its shots.
// A reason set from Home (sessionReviewController.reasonFilter) is shown by the brush highlight: the other shots recede.
Column {
    id: so
    objectName: "sdSessionOutcomes"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    readonly property bool isPrint: medium === "print"
    property var source: null
    property bool interactive: !isPrint
    property bool conditionsInteractive: interactive  // false on the cast: strips still move the carousel, rows never open
    signal conditionActivated(string id)
    signal shotActivated(string swingDir, int shotId)

    readonly property var log: (source && source.shotLog) || []
    readonly property var mix: (source && source.shotMix) || ({})
    readonly property var carryToday: (source && source.carryToday) || []
    readonly property var reasons: (source && source.reasons) || []
    readonly property var clubMix: (source && source.clubMix) || []
    function mixTone(kind) { return kind === "good" ? roles.good : kind === "main" ? roles.warn : roles.attention }
    readonly property real mixClubW: roles.u(70)
    readonly property real mixBarW: Math.min(roles.u(150), width * 0.3)

    visible: log.length > 0
    spacing: roles.g(6)

    // ✓ means clean; an intended or clean shape keeps its name and adds ✓; a corrected cell ends in * (explained below)
    function cellLines(s) {
        const lines = (s.lines || []).slice()
        if (s.clean) {
            if (lines.length) lines[lines.length - 1] += " ✓"
            else lines.push("✓")
        } else if (!lines.length) {
            lines.push("-")
        }
        if (s.corrected) lines[lines.length - 1] += "*"
        return lines
    }
    readonly property var words: log.map(s => ({ shotId: s.shotId, lines: cellLines(s), quiet: !!s.clean && !s.corrected }))

    readonly property string correctionsLine: String(mix.correctionsLine || "")
    readonly property string fromFlightLine: String(mix.fromFlightLine || "")
    readonly property string mixText: [ (mix.line ? "Mix: " + mix.line : ""), (correctionsLine !== "" ? correctionsLine + " (cells marked *)" : ""), fromFlightLine ]
                                      .filter(s => s !== "").join("  ·  ")
    // no launch monitor at all: one line here, in place of "no reading" under every shot
    readonly property bool noReadings: log.length > 0 && log.every(s => !!s.noReading)

    // ── the reason filter set from Home ──
    readonly property var reasonController: typeof sessionReviewController !== "undefined" ? sessionReviewController : null
    readonly property string reasonFilter: reasonController ? String(reasonController.reasonFilter || "") : ""
    readonly property var filterReason: reasonFilter !== "" ? reasons.find(r => r.conditionId === reasonFilter) || null : null
    readonly property var filterShots: filterReason ? firedShots(filterReason) : []
    function firedShots(r) { return (r.ticks || []).filter(t => t.state === "fired").map(t => t.shotId) }
    Binding {
        target: so.brush
        property: "highlightShots"
        when: so.brush !== null && !so.isPrint && so.visible && so.filterShots.length > 0
        value: so.filterShots
    }

    // ── the reasons ──
    readonly property var reasonSpec: [
        { key: "name",     tier: 1, kind: "name",  w: 0,   head: "" },
        { key: "strip",    tier: 2, kind: "strip", w: 0,   head: "Shots", fo: 1 },
        { key: "shots",    tier: 1, kind: "figs",  w: 2.6, head: "Shots", align: "r" },
        { key: "produced", tier: 3, kind: "small", w: 13,  head: "What it produced", fo: 2, fold: "" },
        { key: "cost",     tier: 1, kind: "figs",  w: 4.6, head: "Cost", align: "r" },
        { key: "trend",    tier: 2, kind: "small", w: 7,   head: "Trend", fo: 3, fold: "" }
    ]
    // Flagged is remembered for these exact shots: a new unexplained shot offers the flag again
    property string flaggedKey: ""
    function flagKey(r) { return String((source && source.sessionDir) || "") + "|" + firedShots(r).join(",") }
    function reasonCell(r, key) {
        switch (key) {
        case "name":     return { t: r.name, c: r.flaggable ? so.roles.ink2 : so.roles.ink }
        case "strip":    return { ticks: r.ticks || [] }
        case "shots":    return { t: String(r.shots) }
        case "produced": return { t: (r.produced || []).map(p => p.count > 1 ? p.phrase + " ×" + p.count : p.phrase).join(" · ") || "-" }
        case "cost":     return { t: r.costText || "-" }
        case "trend":
            if (r.flaggable && so.conditionsInteractive) {
                if (so.flaggedKey !== "" && so.flaggedKey === so.flagKey(r)) return { t: "Flagged", f: so.roles.fSmall, c: so.roles.ink3 }
                return { t: "Flag for review", f: so.roles.fSmallL, c: so.roles.link }
            }
            return { t: r.trendWords || "-" }
        }
        return {}
    }
    function flagUnexplained() {
        const r = reasons.find(x => x.flaggable)
        if (!r || !so.conditionsInteractive || !source || typeof source.flagForReview !== "function") return
        if (so.flaggedKey === so.flagKey(r)) return      // the tuning log is append-only: once per set of shots
        firedShots(r).forEach(id => source.flagForReview(id))
        flaggedKey = flagKey(r)
    }

    PpSectionHead {
        width: so.width; roles: so.roles; paged: so.isPrint; lead: 6; pad: 2
        title: "Shot log · what happened"
    }
    PpStandfirst {
        objectName: "sdReasonFilterNote"
        visible: !so.isPrint && so.reasonFilter !== ""
        width: so.width; roles: so.roles; brush: so.brush; interactive: so.interactive
        spacing: so.roles.g(2)
        kicker: so.filterReason ? "Shots where " + so.filterReason.name + " was the reason"
                                : "That reason didn't explain any shots in this session"
        chips: [{ key: "clear", label: "Show all shots", on: false }]
        onChipActivated: if (so.reasonController) so.reasonController.reasonFilter = ""
    }
    PpNotes {
        objectName: "sdNoReadings"
        visible: so.noReadings
        width: so.width; roles: so.roles; small: true; tone: 2
        text: "No launch monitor readings in this session, so ball flight and strike can't be judged. The swing findings still stand."
    }
    PpShotRuler {
        id: shotLog
        objectName: "sdShotLog"
        visible: !so.noReadings
        roles: so.roles; brush: so.brush; interactive: so.interactive
        outcomeWords: so.words
        pitch: so.roles.u(38)
        labelW: 0
        shotsLabel: ""
        countsLabel: ""
        availableWidth: so.width
        onShotActivated: (d, s) => so.shotActivated(d, s)
    }
    PpNotes {
        objectName: "sdShotMix"
        visible: so.mixText !== "" && !so.noReadings
        width: so.width; roles: so.roles; small: true; tone: 2
        text: so.roles.esc(so.mixText)
    }
    // each club's shots: a band bar, with the words beside it because colour is never the only channel
    Column {
        objectName: "sdClubMix"
        visible: so.clubMix.length > 0 && !so.noReadings
        width: so.width
        spacing: so.roles.g(3)
        Repeater {
            model: so.clubMix
            delegate: Row {
                id: mixRow
                required property var modelData
                width: so.width
                spacing: so.roles.g(8)
                Text {
                    width: so.mixClubW
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: mixRow.modelData.club || ""
                    font: so.roles.fSmall; color: so.roles.ink2
                }
                PpBandBar {
                    objectName: "sdClubMixBar:" + (mixRow.modelData.clubId || "")
                    width: so.mixBarW
                    height: so.roles.u(6)
                    anchors.verticalCenter: parent.verticalCenter
                    segments: (mixRow.modelData.parts || []).map(p => ({ count: p.count, tone: so.mixTone(p.kind) }))
                }
                Text {
                    width: Math.max(0, so.width - so.mixClubW - so.mixBarW - 2 * mixRow.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.WordWrap
                    text: mixRow.modelData.line || ""
                    font: so.roles.fSmall; color: so.roles.ink2
                }
            }
        }
    }
    PpNotes {
        objectName: "sdCarryToday"
        visible: so.carryToday.length > 0
        width: so.width; roles: so.roles; small: true; tone: 2
        text: so.carryToday.map(c => so.roles.esc(c.line)).join("<br>")
    }
    PpLedgerTable {
        objectName: "sdReasonsTable"
        visible: so.reasons.length > 0
        width: so.width; roles: so.roles; brush: so.brush; medium: so.medium; interactive: so.interactive
        kind: "reasons"
        printGroup: "reasons"
        title: "Reasons for the bad shots"
        rows: so.reasons
        spec: so.reasonSpec
        getter: (r, key) => so.reasonCell(r, key)
        extra: r => String(r.tierText || "")
        onConditionActivated: id => { if (id !== "") so.conditionActivated(id); else if (so.conditionsInteractive) so.flagUnexplained() }
        onShotActivated: (d, s) => so.shotActivated(d, s)
    }
}
