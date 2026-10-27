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

// THE SESSION DIAGNOSTICS PANEL'S BODY: its data and view state, the frame on surfaces that have no
// stage card round them, and the TYPESET REGISTER that draws everything else (design system §15).
//
// IT TAKES A `source` AND OWNS NOTHING. Every string, count and tick state is read off one object
// shaped like SessionDiagnosticsModel; the wiring that produces one (the session directory, the shot
// processor, review) lives next door in PpSessionDiagnosticsPanel. So this file still loads offscreen
// with a fixture, and there is one answer to "where does this number come from": `source`.
//
// THE REGISTER IS CHOSEN, NOT THE LAYOUT (§15.6). A register is how much the panel says:
//   1. forceRegister ("Replay" | "Analyse"), set by tests only;
//   2. surface "cast": ALWAYS PpDiagReplay (D4), whatever mode the main stage is in;
//   3. SessionMode: Analyse reads in depth (PpDiagAnalyse); Capture and Replay glance (PpDiagReplay),
//      except that an open detail is always read in PpDiagAnalyse: a condition clicked in Replay opens
//      in place, and Back returns to the glance, with the stage still in Replay.
// The register is handed `body: root` and reads and drives everything through this API: the view is
// `detailOpen ? "detail" : _tab`, and interactive, cardFilter and pulseCue come straight through. It
// lays itself out by the size class of the width it is given and NEVER scales its type: there is no
// fit factor here any more (house rule 2). The cast's viewing-distance zoom scales the whole surface
// instead (the register's own cast zoom and PpSessionDiagnosticsWindow.contentScale).
//
// THIS API IS THE INTEGRATION SEAM (the panel's navigation save and restore, PpModeStage's card, the
// cast window, the tests and every probe reach in): source, readout, tab, _tab, cardFilter,
// watchingExpanded, interactive, framed, cardAside, readingShot, reviewing, detailOpen,
// detailConditionId, detail, cards, header, chains, pulseCue; _openDetail, _closeDetail,
// _setDetailMeasure, _declareFocus, _declareMiss, pulseAfterShot; screenRequested, shotRequested;
// objectName "sdBody". Change none of it without changing every caller.

import QtQuick
import PinPointStudio

Item {
    id: root

    // Anything with SessionDiagnosticsModel's read surface. Null draws the empty chrome.
    property var source: null

    // SessionDiagnosticsModel::shotReadout(selectedShotId), for the shot the carousel has focused —
    // null when nothing is selected. shotReadout() is an INVOKABLE, so it cannot be bound: the panel
    // calls it and re-calls it when the selection or the surface changes. A test hands a fixture in.
    property var readout: null

    // LEGACY, kept as a no-op: the panel's navigation still saves and restores it, and the Watching
    // view no longer folds (the barcode is the whole view).
    property bool watchingExpanded: false

    // ── the card filter: a VIEW state, saved with the navigation ─────────────
    // The model publishes the two facts (reachesBall, rootHere); this decides which the reader looks
    // through. Nothing is withdrawn: a filtered-out pattern keeps its rank. PpDiagAnalyse offers it.
    property string cardFilter: "all"     // "all" | "ball" | "roots"
    readonly property var _visibleCards: {
        const cs = cards || []
        if (cardFilter === "ball")  return cs.filter(function (c) { return c.reachesBall === true })
        if (cardFilter === "roots") return cs.filter(function (c) { return c.rootHere === true })
        return cs
    }

    // Off on the auto-closing cast, where every affordance is a trap: the surface is a glance and it is
    // about to vanish under the pointer (PpSessionDiagnosticsWindow.interactive).
    property bool interactive: true

    // Where the panel is drawn: "panel" (the stage, or anywhere in the app) or "cast" (the second-screen
    // window). The cast always reads the Replay register (D4) and draws its own running head, so the
    // body's frame stands down there.
    property string surface: "panel"

    // Tests only: pin the register ("Replay" | "Analyse") regardless of SessionMode.
    property string forceRegister: ""
    // Renders only: a named interaction state the register shows statically (§15.7). Always "" in the app.
    property string demoState: ""

    // ── the views ────────────────────────────────────────────────────────────
    // The user's choice is `tab`, and every tab can always be pressed: a tab with nothing to show says
    // why. `_tab` differs only on the auto-closing cast, where nobody can press a tab and the shot is
    // what the cast is for.
    property string tab: "session"           // "shot" | "session" | "watching"
    readonly property var _tabDefs: [
        { key: "shot",     label: Theme.caps(qsTr("This shot")) },
        { key: "session",  label: Theme.caps(qsTr("Session")) },
        { key: "watching", label: Theme.caps(qsTr("Watching")) }
    ]
    readonly property bool _shotAvailable: readingShot || (!isClosing && !reviewing)
    readonly property string _tab: interactive ? tab
                                 : (_shotAvailable ? "shot" : "session")
    // PICKING A SHOT OFF THE CAROUSEL IS ASKING ABOUT IT. Only on the edge into shot-reading, never on
    // every re-fetch of the readout, so a reader who goes back to Session is not dragged back.
    onReadingShotChanged: if (readingShot) tab = "shot"
    // What the register shows: the open detail over the tab (an open detail loads PpDiagAnalyse; the cast's
    // PpDiagReplay, and the one held under the detail, read "detail" as the reader's tab, D9).
    readonly property string view: detailOpen ? "detail" : _tab

    readonly property int _shotFiredCount: {
        const xs = readingShot ? (readout.conditions || [])
                                 : (source ? (source.thisShot || []) : [])
        let n = 0
        for (let i = 0; i < xs.length; ++i)
            if ((readingShot ? xs[i].stateKind : xs[i].kind) === "fired") ++n
        return n
    }
    readonly property int _watchingCount: (source && source.watching) ? source.watching.length : 0

    // ── the two declarations the golfer can make (design §A6) ────────────────
    // BOTH GO STRAIGHT TO THE MODEL AND NEITHER IS MIRRORED HERE; the read-back is the model's.
    // GUARDED ON THE FUNCTION, not on the source: a test fixture that wants to assert the tap arrived
    // supplies a spy of the same name; one that does not gets a no-op rather than a TypeError.
    function _declareFocus(conditionId, on) {
        if (!source) return
        if (on && typeof source.declareFocus === "function")
            source.declareFocus(conditionId)
        else if (!on && typeof source.clearFocus === "function")
            source.clearFocus()
        else
            return
        _writeFocusBack(on ? conditionId : "",
                        on ? Math.max(0, Number(source.focusFromShot) || 0) : 0)
    }

    // IN A LIVE SESSION THE FOCUS IS SessionController's, and this is where a declaration made here
    // reaches it — and through it the other surfaces (a rebuilt stage panel, the cast), which adopt
    // it (PpSessionDiagnosticsPanel._adoptFocus). Set by the panel only while live; null in review,
    // on a finished session and in the tests that do not supply one. Kept as an input so the body
    // still touches no singleton.
    //
    // THE SPLIT FIRST, THEN THE ID: a surface adopting on the id must find the split already there.
    // Each written only on a real difference, so the adoption it triggers comes back a no-op.
    property var focusController: null
    function _writeFocusBack(id, fromShot) {
        const c = focusController
        if (!c) return
        if (c.focusFromShot !== fromShot) c.focusFromShot = fromShot
        if ((c.focusConditionId || "") !== id) c.focusConditionId = id
    }
    // The register's inline picker closes itself on a pick.
    function _declareMiss(missId) {
        if (source && typeof source.declareMiss === "function")
            source.declareMiss(missId)
    }
    function _missCandidates() {
        if (source && typeof source.missCandidates === "function")
            return source.missCandidates() || []
        return []
    }

    // ── the condition detail ─────────────────────────────────────────────────
    // Opening re-targets the one detail page; Back returns to the tab. The model holds which detail is
    // open (so it survives a stage rebuild); every register keeps each view alive, so Back restores.
    function _openDetail(conditionId) {
        if (!conditionId) return
        if (source && typeof source.openDetail === "function")
            source.openDetail(conditionId)
    }
    function _closeDetail() {
        if (source && typeof source.closeDetail === "function")
            source.closeDetail()
    }
    function _setDetailMeasure(measureId) {
        if (source && typeof source.setDetailMeasure === "function")
            source.setDetailMeasure(measureId)
    }

    // READ BACK OFF THE MODEL, never remembered here.
    readonly property string detailConditionId:
        source ? (source.detailConditionId || "") : ""
    readonly property var detail: source ? source.detail : null
    readonly property bool detailOpen: detailConditionId !== "" && !!detail && !!detail.header

    // ── the after-shot pulse (§B3, §B8; D1) ──────────────────────────────────
    // ONE CUE OBJECT, PUBLISHED DOWNWARD: { token, ids, focusId }. `ids` are condition IDS, in the
    // panel's drawing order (the session's patterns first, then the rest), the focused one first; a
    // token so the same ids firing twice in a row are still two events. The register's PpBrush pulses
    // them (PpPulseWash on every row).
    //
    // afterShotDelta.fired is a list of the model's CHIP MAPS ({ id, name, kind, tier, … }), not ids;
    // a fixture may hand plain ids. Both are read as ids here (R round 4, note 14: the old code keyed a
    // map on the chip object itself, so the drawing order was lost and the cue carried chips).
    property var pulseCue: null
    property int _pulseToken: 0

    function pulseAfterShot() {
        const d = source ? source.afterShotDelta : null
        const fired = ((d && d.fired) ? d.fired : [])
            .map(function (x) { return x && typeof x === "object" ? (x.id || "") : String(x || "") })
            .filter(function (id) { return id !== "" })
        if (fired.length === 0) { root.pulseCue = null; return }

        const wanted = {}
        for (let i = 0; i < fired.length; ++i) wanted[fired[i]] = true

        // ORDER IS THE PANEL'S DRAWING ORDER, never the delta's: the patterns as the session ranks
        // them, then anything only a detail draws, then whatever fired that neither draws.
        const ids = []
        function take(id) {
            if (id && wanted[id] && ids.indexOf(id) < 0) ids.push(id)
        }
        const cs = root.cards || []
        for (let i = 0; i < cs.length; ++i) take(cs[i].id)
        const chs = root.chains || []
        for (let i = 0; i < chs.length; ++i) {
            const ns = chs[i].nodes || []
            for (let j = 0; j < ns.length; ++j) take(ns[j].id)
        }
        for (let i = 0; i < fired.length; ++i) take(fired[i])

        // THE FOCUSED CONDITION LEADS the stagger.
        const focusId = d.focusId || ""
        const at = ids.indexOf(focusId)
        if (at > 0) { ids.splice(at, 1); ids.unshift(focusId) }

        root._pulseToken += 1
        root.pulseCue = { token: root._pulseToken, ids: ids, focusId: focusId }
    }

    // A screen was asked for, from a screened root. `screenRef` is the model's when it recommended that
    // screen and empty when it did not; `conditionId` always names what the screen would settle. THIS
    // PANEL RUNS NO SCREEN: the host routes it.
    signal screenRequested(string screenRef, string conditionId)

    // A shot was picked (a strip cell, a mark, a ruler column, a bookend). THIS BODY SELECTS NOTHING:
    // the carousel owns the selection, so the request goes up to the panel. By swing folder, the one
    // identity the carousel and the ledger share.
    signal shotRequested(string swingDir, int shotId)

    objectName: "sdBody"
    clip: true

    // ── the source, read once ────────────────────────────────────────────────
    readonly property var header:       source ? source.headerInfo    : null
    readonly property string stage:     source ? (source.stage || "") : ""
    readonly property var cards:        source ? source.cards         : []
    readonly property var expectations: source ? source.expectations  : []
    readonly property var bookends:     source ? source.bookends      : []
    readonly property var chains:       source ? source.chains        : []
    readonly property var driver:       source ? source.driver        : null

    readonly property bool isCold:      stage === "cold"
    readonly property bool isForming:   stage === "forming"
    readonly property bool isEstablished: stage === "established"
    readonly property bool isClosing:   stage === "closing"

    // The declared miss: invited while the answer can still shape the session (Cold, Forming), reported
    // once declared. The register draws the chip and its inline picker (PpDiagReplayState).
    readonly property string declaredMiss:     source ? (source.declaredMiss || "")     : ""
    readonly property string declaredMissName: source ? (source.declaredMissName || "") : ""
    readonly property bool _missInvited:
        interactive && declaredMiss === "" && (isCold || isForming) && !reviewing

    // ── review (13a, brief §6) ───────────────────────────────────────────────
    // READ OFF THE PUBLISHED SURFACE: the badge is non-empty exactly when the model is reviewing.
    readonly property bool reviewing:
        !!header && (header.reviewBadge || "") !== ""
    // A SHOT IS BEING READ once the carousel has focused one: in review any pick, live any swing but
    // the newest (the newest is the after-shot read, with its delta and its quiet state).
    readonly property bool readingShot:
        !!readout && !!readout.conditions && readout.conditions.length > 0
        && (reviewing || readout.shotIndex !== readout.shotCount - 1)

    // ── the frame ────────────────────────────────────────────────────────────
    // THE STAGE OWNS THE FRAME. On the stage PpModeStage puts this panel in its card and switches
    // `framed` off, so the body draws no surface, border or title — the card's heading carries the title
    // and `cardAside` what the old header chips said. ON THE CAST the register draws the running head
    // (the shot and the view), so the frame stands down there too (S30). Anywhere else a framed body
    // draws the card itself, in the QUIET tone every stage card takes (§15.11: colour means "outside
    // its corridor" and nothing else, so a frame never speaks for what is inside it).
    property bool framed: true
    readonly property bool _drawFrame: framed && surface !== "cast"

    readonly property color tone: Theme.colorText3

    // Narrow enough that the frame's title abbreviates rather than elides (12c).
    readonly property bool compact: width < Math.round(640 * Theme.fontScale)

    // What the header used to say in chips, as the card heading's aside: the newest swing's label
    // live (never while an older one is being read), the stage, and the tense the panel is read in.
    readonly property string _shotLabelText:
        (!reviewing && !readingShot && !detailOpen && header) ? (header.shotLabel || "") : ""
    readonly property string _stageWord:  header ? (header.stageLabel || "") : ""
    readonly property string _reviewWord: header ? (header.reviewBadge || "") : ""
    readonly property string cardAside:
        Theme.caps([_shotLabelText, _unshout(_stageWord), _unshout(_reviewWord)]
                   .filter(function (s) { return s !== "" }).join(" · "))
    // The model spells the stage and the review badge in capitals; lower-case a shouted word so
    // Theme.caps gets a sentence-case source.
    function _unshout(s) {
        return s.replace(/\b[A-Z]{2,}\b/g, function (w) { return w.toLowerCase() })
    }

    readonly property int _framePad: Theme.gap(14)

    PpCardShell {
        anchors.fill: parent
        visible: root._drawFrame
        tone: root.tone
    }

    // The framed card's heading: the panel's name, the aside on the right.
    Item {
        id: frameHead
        visible: root._drawFrame
        x: root._framePad; y: root._framePad
        width: parent.width - 2 * root._framePad
        height: visible ? frameTitle.implicitHeight : 0

        PpMicro {
            id: frameTitle
            objectName: "sdTitle"
            text: root.compact ? Theme.caps(qsTr("Session diag.")) : Theme.caps(qsTr("Session diagnostics"))
            color: root.tone
        }
        Row {
            anchors.right: parent.right
            anchors.top: parent.top

            PpMicro {
                objectName: "sdShotLabel"
                visible: text !== ""
                text: Theme.caps(root._shotLabelText)
                font.letterSpacing: Theme.trackingData
            }
            PpMicro {
                objectName: "sdStageChip"
                visible: root._stageWord !== ""
                text: (root._shotLabelText !== "" ? " · " : "") + Theme.caps(root._unshout(root._stageWord))
                font.letterSpacing: Theme.trackingData
            }
            PpMicro {
                objectName: "sdReviewBadge"
                visible: root._reviewWord !== ""
                text: (root._shotLabelText !== "" || root._stageWord !== "" ? " · " : "")
                      + Theme.caps(root._unshout(root._reviewWord))
                font.letterSpacing: Theme.trackingData
            }
        }
    }

    // ── the register ─────────────────────────────────────────────────────────
    // An open detail is read in the Analyse register whatever the stage's mode: a condition clicked in
    // Replay opens in place, and Back returns to the glance, without the stage leaving Replay. The glance
    // is HELD while the detail is up (hidden, renamed, never torn down), so Back finds its tab, scroll and
    // hover where they were.
    readonly property string _baseRegister: forceRegister !== "" ? forceRegister
                                          : surface === "cast" ? "Replay"
                                          : SessionMode.mode === SessionMode.analyse ? "Analyse" : "Replay"
    readonly property string register: _baseRegister === "Replay" && detailOpen && forceRegister === ""
                                       && surface !== "cast" ? "Analyse" : _baseRegister
    readonly property Item registerItem: register === "Analyse" ? analyseLoader.item : replayLoader.item

    Loader {
        id: replayLoader
        x: root._drawFrame ? root._framePad : 0
        y: root._drawFrame ? frameHead.y + frameHead.height + Theme.gap(10) : 0
        width: root.width - 2 * x
        height: root.height - y - (root._drawFrame ? root._framePad : 0)
        active: root._baseRegister === "Replay"
        visible: root.register === "Replay"
        sourceComponent: replayC
    }
    Loader {
        id: analyseLoader
        x: replayLoader.x
        y: replayLoader.y
        width: replayLoader.width
        height: replayLoader.height
        active: root.register === "Analyse"
        sourceComponent: analyseC
    }
    Component {
        id: replayC
        PpDiagReplay {
            // Held under an open detail, it must not answer for the register the reader sees.
            objectName: root.register === "Replay" ? "sdRegister" : "sdRegisterHeld"
            body: root
            view: root.view
            readout: root.readout
            cards: root.cards || []
            source: root.source
            header: root.header
            detail: root.detail
            surface: root.surface
            interactive: root.interactive
            cardFilter: root.cardFilter
            pulseCue: root.pulseCue
            demoState: root.demoState
        }
    }
    Component {
        id: analyseC
        PpDiagAnalyse {
            body: root
            view: root.view
            readout: root.readout
            cards: root.cards || []
            source: root.source
            header: root.header
            detail: root.detail
            surface: root.surface
            interactive: root.interactive
            cardFilter: root.cardFilter
            pulseCue: root.pulseCue
            demoState: root.demoState
        }
    }
}
