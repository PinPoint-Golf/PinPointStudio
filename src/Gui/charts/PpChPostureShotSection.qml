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

// PPCHPOSTURESHOTSECTION — THE POSTURE SECTION OF THIS SHOT (docs/design/posture_chart_design.md §5.1–5.3, 5.6;
// built the way PpChWristShotSection is). "Where did my body go, position by position, against good players?"
// PpChartsShotView draws it under its traces only when this shot carries a posture reading AND a posture metric
// is among the curves on show (body.postureShotShown) — in practice the Posture preset; a section of the page,
// a fold after Summary that remembers its state ("posture"), with the pattern line as its state when closed.
//   Analyse: the head (the largest NAMED departure, else the W-D6 words), the standfirst at the playhead's
//     position, the pattern line; the quiet controls — Pattern (Auto → resolved · Level · Squat and push) and
//     Against (address · previous · reference ★ · session median), folded to one summary line below medium; the
//     profile (PpChPostureProfile, a column seeks); "How far can I trust this?" under it, closed (said once);
//     "At address" (PpChPostureAddress: the rig-sensitive address readings, band, boxes and ⚠ — §7.4.2);
//     what moved at the top and at impact (PpChPostureMoved); Findings | Working well (PpChPostureLedger), in
//     two columns when the width allows.
//   The picture strip (PpChPosturePicture, stage 5): the frames at address, the top and impact with the frozen coach
//     lines and the bands at impact, after what moved; a press on a frame seeks there.
//   Replay (the glance at the mat, and the cast): the head, the pattern line, the profile and the picture strip; no
//     controls, no ledgers, no trust fold, and the frames do not seek.
// Every word and number is ChartsPosturePresenter's (body.p*, each bound ONCE on the body); the choices go to the
// body (body.choose: posturePattern, postureAgainst — persisted per screen × mode by the panel).
// objectNames: chPostureSection, chPostureHead, chPosturePattern, chPostureControls, chPostureFold, chPosturePicture (+ the blocks').
// The shell (the fold head, its folded height, seek) is PpChSection's; the controls PpChSectionControls.
PpChSection {
    id: ps
    objectName: "chPostureSection"
    key: "posture"; title: "Posture"
    foldState: sh.patternLine || ""
    has: !!roles && !!sh && sh.available === true
    mainHeight: mainCol.height
    readonly property var pc: body ? body.pChoices : ({})
    readonly property var sh: body ? body.pShot : ({})
    readonly property var prof: has ? (sh.profile || ({})) : ({})
    readonly property bool wide: has && ty.atLeast(width, "medium")

    readonly property var sets: [
        ps.autoChoice("pattern", "Pattern", pc.pattern, pc.patterns, pc.patternAfter),
        ps.againstChoice(pc.against, pc.againstOptions)
    ]
    function picked(set, opt) {
        if (!body) return
        if (set === "pattern") body.choose("posturePattern", Number(opt))
        else if (set === "against") body.choose("postureAgainst", opt)
    }

    Column {
        id: mainCol
        width: ps.width
        spacing: ps.has ? ps.ty.g(14) : 0

        // ── the head, the standfirst at the playhead's position, the pattern line ──
        Column {
            objectName: "chPostureHead"
            width: ps.width
            spacing: ps.has ? ps.ty.g(6) : 0
            PpChText { roles: ps.ty; sentence: ps.has ? ps.sh.head : null; font: ps.has ? ps.ty.fStand : Qt.font({}); tone: ps.has ? ps.ty.ink : "black" }
            PpChText { visible: !ps.replay && present; roles: ps.ty; sentence: ps.has && ps.body ? ps.body.pStandfirst : null; font: ps.has ? ps.ty.fBody : Qt.font({}) }
        }
        Flow {
            objectName: "chPosturePattern"
            readonly property string line: ps.sh.patternLine || ""
            visible: ps.has && line !== ""
            width: ps.width
            spacing: ps.has ? ps.ty.g(10) : 0
            Text { text: "Pattern"; font: ps.has ? ps.ty.fKicker : Qt.font({}); color: ps.has ? ps.ty.ink3 : "grey" }
            Text { width: Math.min(implicitWidth, ps.width); wrapMode: Text.WordWrap
                   text: parent.line; font: ps.has ? ps.ty.fBody : Qt.font({}); color: ps.has ? ps.ty.ink : "black" }
        }

        // ── the quiet controls (Analyse); below medium folded to the one-line summary ──
        PpChSectionControls {
            objectName: "chPostureControls"
            width: ps.width
            section: ps; foldName: "chPostureFold"; key: "postureControls"
            summary: ps.pc.summary || ""; wide: ps.wide
            sets: ps.sets
            onPicked: (set, opt) => ps.picked(set, opt)
        }

        // ── the profile ──
        Column {
            width: ps.width
            spacing: ps.has ? ps.ty.g(8) : 0
            PpSectionHead { width: parent.width; roles: ps.ty; paged: false; pad: 0
                            title: "Positions"; sub: ps.prof.sub || ""; subFit: "top"; subWrap: false }
            Text { visible: !!ps.prof.ghostRefusal; width: parent.width; wrapMode: Text.WordWrap
                   text: ps.prof.ghostRefusal || ""; font: ps.has ? ps.ty.fSmall : Qt.font({}); color: ps.has ? ps.ty.ink3 : "grey" }
            PpChPostureProfile {
                width: parent.width
                roles: ps.ty; brush: ps.brush; interactive: ps.interactive
                ticker: ps.body ? ps.body.relationsP : null
                rows: ps.prof.rows || []
                positions: ps.prof.positions || []
                selected: ps.body ? ps.body.pSelected : -1
                mode: "shot"
                onColumnActivated: (i) => { const p = (ps.prof.positions || [])[i]; if (p) ps.seek(p.seekUs) }
            }
            Text { visible: !ps.replay && text !== ""; width: ps.has ? ps.ty.measureW(parent.width) : 0; wrapMode: Text.WordWrap
                   text: ps.prof.caption || ""; font: ps.has ? ps.ty.fSmall : Qt.font({}); color: ps.has ? ps.ty.ink3 : "grey" }
            // why a reading is held back, said once, under the plot it explains (closed)
            PpChPostureTrust { visible: !ps.replay && !ps.cast && (lines || []).length > 0; width: parent.width; roles: ps.ty
                               lines: ps.has && ps.sh.trust ? (ps.sh.trust.lines || []) : [] }
        }

        // ── at address: the rig-sensitive readings, each shown with its band, its boxes and its ⚠ (§7.4.2) ──
        PpChPostureAddress { visible: ps.has && !ps.replay && rows.length > 0; width: ps.width; roles: ps.ty; brush: ps.brush
                             interactive: ps.interactive && !ps.cast; mode: "shot"
                             block: ps.has ? (ps.sh.address || null) : null }

        PpChPostureMoved { visible: ps.has && !ps.replay && parts.length > 0; width: ps.width; roles: ps.ty; interactive: ps.interactive
                           moved: ps.has ? (ps.sh.whatMoved || ({})) : ({})
                           onSeek: (us) => ps.seek(us) }

        // ── the picture strip: address, the top and impact (§5.4); at the glance too, with no affordance ──
        PpChPosturePicture {
            visible: ps.has && on
            width: ps.width
            roles: ps.ty
            picture: ps.has ? (ps.sh.picture || null) : null
            interactive: ps.interactive && !ps.cast
            glance: ps.replay
            onSeek: (us) => ps.seek(us)
        }

        // ── findings | working well ──
        Item {
            id: ledgers
            visible: ps.has && !ps.replay
            width: ps.width
            readonly property bool two: ps.has && ps.ty.columns(ledgers.width, 30, ps.ty.u(28)) >= 2
            readonly property real colW: two ? (ledgers.width - ps.ty.u(28)) / 2 : ledgers.width
            height: !visible ? 0 : two ? Math.max(fl.height, sl.height) : fl.height + ps.ty.u(18) + sl.height
            PpChPostureLedger { id: fl; width: ledgers.colW; roles: ps.ty; title: "Findings"; items: ps.sh.findings || []
                                interactive: ps.interactive; canOpen: !!ps.body && ps.body.canOpenCondition
                                emptyText: ps.sh.findingsEmpty || ""
                                onSeek: (us) => ps.seek(us); onOpenCondition: (id) => { if (ps.body) ps.body.openCondition(id) } }
            PpChPostureLedger { id: sl; x: ledgers.two ? ledgers.colW + ps.ty.u(28) : 0; y: ledgers.two ? 0 : fl.height + ps.ty.u(18)
                                width: ledgers.colW; roles: ps.ty; title: "Working well"; strengths: true; items: ps.sh.workingWell || []
                                interactive: ps.interactive; canOpen: !!ps.body && ps.body.canOpenCondition
                                emptyText: ps.sh.workingWellEmpty || ""
                                onSeek: (us) => ps.seek(us); onOpenCondition: (id) => { if (ps.body) ps.body.openCondition(id) } }
        }
    }
}
