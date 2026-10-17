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

// PPCHWRISTSHOTSECTION — THE WRIST SECTION OF THIS SHOT (docs/design/wrist_motion_into_charts.md §4, as Mark
// re-surfaced it on 16 Oct: the standard curves first, the domain read added when it is meaningful). "At each
// checkpoint, where was the wrist against its band, and what did it cost?" PpChartsShotView draws it under its
// traces only when this shot carries a wrist reading AND a wrist metric is among the curves on show
// (body.wristShotShown); it is a section of the page, never a page of its own.
//   Analyse: the section head "Wrist"; the wrist head (the first wrist finding; with none, the out-of-band
//     checkpoints said, W-D6), the standfirst at the playhead's checkpoint, the wrist score with its sum (a
//     figure at the right at medium, one line under the head below); the quiet controls — Bands (Auto →
//     resolved), Against (address · previous · reference ★ · session median), Curves behind, and "★ set this
//     swing as your reference" — folded to one summary line below medium (decision rule 5); the resemblance
//     line; the checkpoint profile (PpChWristProfile, a column seeks); Findings | Working well
//     (PpChWristLedger) and what moves with the lead wrist at impact (PpChMovesWith), in a right rail at wide.
//   Replay (the glance at the mat, and the cast): the head, the standfirst, the score line and the profile;
//     no controls, no ledgers.
// Every word and number is ChartsWristPresenter's (body.w*, each bound ONCE on the body); the choices go to the
// body (body.choose: wristAgainst, wristCurves, wristArchetype — persisted per screen × mode by the panel). The
// ★ writes appSettings.wristReferenceSwingDir (cross-session; the traces' Against "reference ★" reads it too).
// objectNames: chWristSection, chWristHead, chWristScore, chWristControls, chWristFold, chWristRefLink,
// chWristMoves (+ the blocks' own).
Item {
    id: ws
    objectName: "chWristSection"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property string reg: "Analyse"
    property bool interactive: true
    property bool cast: false
    readonly property bool replay: reg === "Replay"
    readonly property var ty: roles
    readonly property var wc: body ? body.wChoices : ({})
    readonly property var sh: body ? body.wShot : ({})
    readonly property bool has: !!roles && !!sh && sh.available === true
    readonly property var score: has ? (sh.score || ({})) : ({})
    readonly property var prof: has ? (sh.profile || ({})) : ({})
    readonly property bool rail: has && !replay && ty.atLeast(width, "wide")
    readonly property real railW: has ? Math.min(ty.u(600), width * 0.3) : 0
    readonly property real mainW: rail ? width - railW - ty.u(48) : width
    readonly property bool wide: has && ty.atLeast(mainW, "medium")
    property bool controlsOpen: false
    property string openId: ""
    readonly property string fid: openId !== "" ? openId : ((sh.findings || []).length ? sh.findings[0].id : "")
    height: has ? secHead.height + (folded ? 0 : ty.u(14) + (rail ? Math.max(mainCol.height, railCol.height) : mainCol.height)) : 0
    // folds like every section after the Charts' Summary (Mark, 17 Oct 2026), saved with the others
    readonly property bool folded: !!body && !!body.collapsed && body.collapsed.wrist === true

    function seekTo(us) { if (us >= 0 && typeof shotReplay !== "undefined" && shotReplay) shotReplay.seekToUs(us) }
    // WHY, ON DEMAND (trust stage 6 session 2): a gate's words where a value is missing open its reading in the
    // Trust panel, else in place under the profile (PpTrustInlineFor)
    property string trustSubject: ""
    function openTrust(subject) {
        if (!subject) return
        if (TrustFocus.open(subject, sh.swingDir || "")) return
        trustSubject = trustSubject === subject ? "" : subject
    }
    readonly property string _dir: sh ? (sh.swingDir || "") : ""
    on_DirChanged: trustSubject = ""
    function esc(t) { return String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    // the caption, and the DOFs not measured on this swing, each a gate refused naming its words as a link
    readonly property bool missingLinks: (prof.missingParts || []).some(function (m) { return !!m.subject })
    readonly property string missingRich: {
        const parts = prof.missingParts || []
        let t = esc(prof.caption || "")
        if (parts.length)
            t += " Not measured on this swing: " + parts.map(function (m) {
                return esc(m.name) + (m.subject ? " (<a href=\"" + esc(m.subject) + "\">" + esc(m.words) + " ›</a>)" : "") }).join(", ") + "."
        if (prof.missingTail) t += " " + esc(prof.missingTail)
        return t
    }
    function setReference() {
        const ref = wc.reference || {}
        if (!interactive || !ref.canSet) return
        appSettings.wristReferenceSwingDir = ref.isThisShot ? "" : (sh.swingDir || "")
    }
    readonly property var sets: [
        { key: "archetype", label: "Bands", current: String(wc.archetype !== undefined ? wc.archetype : -1),
          options: (wc.archetypes || []).map(function (a) {
              return { key: String(a.id), label: a.id === -1 && a.current && wc.archetypeAfter ? a.label + " " + wc.archetypeAfter : a.label } }) },
        { key: "against", label: "Against", current: wc.against || "address",
          options: (wc.againstOptions || []).map(function (a) { return { key: a.id, label: a.label, enabled: a.enabled } }) },
        { key: "curves", label: "Curves behind", current: wc.curvesBehind ? "on" : "off",
          options: (wc.curves || []).map(function (c) { return { key: c.id, label: c.label } }) }
    ]
    function picked(set, opt) {
        if (!body) return
        if (set === "archetype") body.choose("wristArchetype", Number(opt))
        else if (set === "against") body.choose("wristAgainst", opt)
        else if (set === "curves") body.choose("wristCurves", opt === "on")
    }

    // the section head across the whole page width, the main column and the rail under it
    PpChFoldHead { id: secHead; visible: ws.has; width: ws.width; roles: ws.ty; key: "wrist"; title: "Wrist"
                   interactive: ws.interactive; collapsed: ws.folded
                   state_: ws.folded && ws.score && ws.score.scoreText ? "score " + ws.score.scoreText : ""
                   onToggled: if (ws.body) ws.body.toggleSection("wrist") }
    Column {
        id: mainCol
        visible: ws.has && !ws.folded
        y: secHead.height + (ws.has ? ws.ty.u(14) : 0)
        width: ws.mainW
        spacing: ws.has ? ws.ty.u(14) : 0
        // ── the head, the standfirst at the playhead's checkpoint, the score beside at medium+ ──
        Item {
            id: headItem
            objectName: "chWristHead"
            width: ws.mainW
            height: Math.max(headCol.height, scoreCol.visible ? scoreCol.height : 0)
            Column {
                id: headCol
                width: scoreCol.visible ? ws.mainW - scoreCol.width - ws.ty.u(32) : ws.mainW
                spacing: ws.ty.u(6)
                PpChText { roles: ws.ty; sentence: ws.has ? ws.sh.head : null; font: ws.ty.fStand; tone: ws.ty.ink }
                PpChText { roles: ws.ty; sentence: ws.has && ws.body ? ws.body.wStandfirst : null; font: ws.ty.fBody }
                // the score as one line below medium, and always at the glance
                Text { visible: !scoreCol.visible && !!ws.score.line; width: ws.ty.measureW(parent.width); wrapMode: Text.WordWrap
                       text: ws.score.line || ""; font: ws.ty.fBody; color: ws.ty.ink }
            }
            Column {
                id: scoreCol
                objectName: "chWristScore"
                visible: ws.wide && !ws.replay
                anchors.right: parent.right
                width: ws.ty.u(230)
                spacing: ws.ty.u(3)
                Text { text: "Wrist score"; font: ws.ty.fKicker; color: ws.ty.ink3 }
                Row {
                    spacing: ws.ty.u(6)
                    // no score because a gate refused the wrist: the gate's words, quiet, opening the reason
                    Text { id: big
                           readonly property bool why: !!ws.score.trust
                           objectName: why ? "chWristScoreWhy:" + ws.score.trust : ""
                           text: why ? (ws.score.gateWords || "") + " ›" : (ws.score.scoreText || "-")
                           font: why ? ws.ty.fBody : ws.ty.fFigL; color: why ? ws.ty.link : ws.ty.ink
                           MouseArea { enabled: big.why && ws.interactive; anchors.fill: parent; anchors.margins: -2
                                       cursorShape: Qt.PointingHandCursor; onClicked: ws.openTrust(ws.score.trust) } }
                    Text { visible: !big.why; anchors.baseline: big.baseline; text: "/ " + (ws.score.base !== undefined ? ws.score.base : 100); font: ws.ty.fFigS; color: ws.ty.ink3 }
                    Text { anchors.baseline: big.baseline; text: ws.score.band || ""; font: ws.ty.fBody; color: ws.ty.ink2 }
                }
                Text { visible: !!ws.score.sumText; text: ws.score.sumText || ""; font: ws.ty.fFigS; color: ws.ty.ink2 }
                Repeater {
                    model: ws.has ? (ws.score.contributions || []) : []
                    delegate: Text {
                        required property var modelData
                        width: scoreCol.width; wrapMode: Text.WordWrap
                        text: (modelData.penaltyText || "") + "  " + (modelData.label || ""); font: ws.ty.fSmall; color: ws.ty.ink3
                    }
                }
            }
        }

        // ── the quiet controls (Analyse); below medium folded to the one-line summary (rule 5) ──
        Column {
            objectName: "chWristControls"
            visible: !ws.replay && !ws.cast
            width: ws.mainW
            spacing: ws.ty.u(4)
            PpChFoldHead {
                objectName: "chWristFold"
                visible: !ws.wide
                width: parent.width; roles: ws.ty; key: "wristControls"; title: "Controls"; interactive: ws.interactive
                collapsed: !ws.controlsOpen
                state_: ws.wc.summary || ""
                onToggled: ws.controlsOpen = !ws.controlsOpen
            }
            PpChChoices {
                visible: ws.wide || ws.controlsOpen
                width: parent.width; roles: ws.ty; interactive: ws.interactive
                sets: ws.sets
                onPicked: (set, opt) => ws.picked(set, opt)
            }
            Text {
                objectName: "chWristRefLink"
                readonly property var ref: ws.wc.reference || ({})
                visible: (ws.wide || ws.controlsOpen) && ref.canSet === true
                text: ref.linkText || ""
                font: ref.isThisShot ? ws.ty.fSmallM : (ws.interactive ? ws.ty.fSmallL : ws.ty.fSmall)
                color: ref.isThisShot ? ws.ty.ink : (ws.interactive ? ws.ty.link : ws.ty.ink3)
                MouseArea { anchors.fill: parent; enabled: ws.interactive; cursorShape: Qt.PointingHandCursor; onClicked: ws.setReference() }
            }
            PpChText { visible: (ws.wide || ws.controlsOpen) && present; roles: ws.ty; sentence: ws.has && ws.sh.style ? ws.sh.style.line : null; measure: false }
        }

        // ── the checkpoint profile ──
        Column {
            width: ws.mainW
            spacing: ws.ty.u(8)
            Flow {
                width: parent.width; spacing: ws.ty.u(10)
                Text { text: "Checkpoints"; font: ws.ty.fSection; color: ws.ty.ink }
                Text { visible: text !== ""; text: ws.prof.sub || ""; font: ws.ty.fSmall; color: ws.ty.ink3 }
            }
            Text { visible: !!ws.prof.ghostRefusal; width: parent.width; wrapMode: Text.WordWrap
                   text: ws.prof.ghostRefusal || ""; font: ws.ty.fSmall; color: ws.ty.ink3 }
            PpChWristProfile {
                width: parent.width
                roles: ws.ty; brush: ws.brush; interactive: ws.interactive
                ticker: ws.body ? ws.body.relationsP : null
                rows: ws.prof.rows || []
                positions: ws.prof.positions || []
                selected: ws.body ? ws.body.wSelected : -1
                mode: "shot"
                onColumnActivated: (i) => { const p = (ws.prof.positions || [])[i]; if (p) ws.seekTo(p.seekUs) }
                onTrustClicked: (subject) => ws.openTrust(subject)
            }
            Text { visible: !ws.replay; width: ws.ty.measureW(parent.width); wrapMode: Text.WordWrap
                   textFormat: ws.missingLinks ? Text.StyledText : Text.PlainText
                   linkColor: ws.ty.link
                   text: ws.missingLinks ? ws.missingRich : (ws.prof.caption || "") + (ws.prof.missingText ? " " + ws.prof.missingText : "")
                   font: ws.ty.fSmall; color: ws.ty.ink3
                   onLinkActivated: (link) => { if (ws.interactive) ws.openTrust(link) }
                   HoverHandler { enabled: ws.missingLinks; cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor } }
            PpTrustInlineFor { width: parent.width; roles: ws.ty; subject: ws.trustSubject; swingDir: ws._dir
                               onClosed: ws.trustSubject = "" }
        }
        Loader { active: !ws.rail && ws.has && !ws.replay; visible: active; width: ws.mainW; sourceComponent: ledgersC }
        Loader { active: !ws.rail && ws.has && !ws.replay; visible: active; width: ws.mainW; sourceComponent: movesC }
    }
    Column {
        id: railCol
        visible: ws.rail && !ws.folded
        x: ws.mainW + (ws.has ? ws.ty.u(48) : 0)
        y: mainCol.y
        width: ws.railW
        spacing: ws.has ? ws.ty.u(26) : 0
        Loader { active: ws.rail; width: ws.railW; sourceComponent: ledgersC }
        Loader { active: ws.rail; width: ws.railW; sourceComponent: movesC }
    }

    // ── findings | working well ──
    Component { id: ledgersC
    Item {
        id: ledgers
        readonly property bool two: ws.ty.columns(ledgers.width, 30, ws.ty.u(28)) >= 2
        readonly property real colW: two ? (ledgers.width - ws.ty.u(28)) / 2 : ledgers.width
        height: two ? Math.max(fl.height, sl.height) : fl.height + ws.ty.u(18) + sl.height
        PpChWristLedger { id: fl; width: ledgers.colW; roles: ws.ty; title: "Findings"; items: ws.sh.findings || []
                          openId: ws.fid; interactive: ws.interactive
                          emptyText: ws.sh.findingsEmpty || ""
                          onSeek: (us) => ws.seekTo(us); onToggled: (id) => ws.openId = (ws.fid === id ? "-" : id) }
        PpChWristLedger { id: sl; x: ledgers.two ? ledgers.colW + ws.ty.u(28) : 0; y: ledgers.two ? 0 : fl.height + ws.ty.u(18)
                          width: ledgers.colW; roles: ws.ty; title: "Working well"; strengths: true; items: ws.sh.strengths || []
                          openId: ws.openId; interactive: ws.interactive
                          emptyText: ws.sh.strengthsEmpty || ""
                          onSeek: (us) => ws.seekTo(us); onToggled: (id) => ws.openId = (ws.openId === id ? "-" : id) }
    }
    }

    // ── what moves with the lead wrist at impact (Relations' own ledger, launch monitor included) ──
    Component { id: movesC
    Column {
        id: moves
        objectName: "chWristMoves"
        readonly property var mw: ws.body ? ws.body.wMoves : ({})
        spacing: ws.ty.u(5)
        PpSectionHead { width: parent.width; roles: ws.ty; title: moves.mw && moves.mw.head && moves.mw.head.text ? moves.mw.head.text : "What moves with the lead wrist at impact"
                        sub: moves.mw ? (moves.mw.sub || "") : "" }
        PpChText { visible: present && !(moves.mw && moves.mw.available); roles: ws.ty; sentence: moves.mw ? moves.mw.refusal : null; tone: ws.ty.ink3 }
        PpChMovesWith {
            visible: !!moves.mw && moves.mw.available === true
            width: parent.width; roles: ws.ty; interactive: ws.interactive
            rows: moves.mw && moves.mw.available ? (moves.mw.rows || []) : []
            onRowActivated: (id) => { if (ws.body) { ws.body.chooseUp(moves.mw.relationsUpId); ws.body.choose("acrossId", id); ws.body.choose("tab", "relations") } }
        }
        PpChText { visible: present && !!moves.mw && moves.mw.available === true; roles: ws.ty
                   sentence: moves.mw && moves.mw.chance ? moves.mw.chance.line : null; tone: ws.ty.ink3 }
        PpChText { visible: present && !!moves.mw && moves.mw.available === true; roles: ws.ty
                   sentence: moves.mw ? moves.mw.note : null; tone: ws.ty.ink3 }
        Text { visible: ws.interactive && !!moves.mw && !!moves.mw.relationsUpId
               text: "Open in Relations"; font: ws.ty.fSmallL; color: ws.ty.link
               MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                           onClicked: if (ws.body) { ws.body.chooseUp(moves.mw.relationsUpId); ws.body.choose("tab", "relations") } } }
    }
    }
}
