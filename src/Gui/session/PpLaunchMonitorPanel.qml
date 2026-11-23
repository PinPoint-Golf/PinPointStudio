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

// The launch monitor stage panel: every reading the device produced this session, drawn in Foresight
// FSX's grammar inside the typeset themes (docs/design/lm_panel_fsx.md). This shot and Session are its
// two quiet tabs; PpLmGraphicsBody draws both, and this file only connects it.
//
// THE SESSION ON SCREEN, not the live one: sessionReviewController.activeShots is the list the carousel
// shows, so opening a past session boards THAT session's readings.
//
// THE BOARD IS THE SESSION ON SCREEN'S, NOT THIS FILE'S (refactor 6.1b): sessionReviewController.lmBoard, the one
// presenter beside the shots it boards (activeShots), held while this panel is up. It decides every number, through
// the one launch monitor reduction (lmBoardOf): the statistics by the effective club and without the shots left out,
// the corridor grades, the enclosing ellipses, the club evidence. The body draws its `graphics` map and its typed
// bands; what the golfer does on the panel comes back here as intents — focus a shot (SessionMode, the carousel's own
// path), leave a shot out, fix a club label, undo the evidence's relabel — and goes to the board, which persists it
// with the shot. This file binds what only a view knows: the focused shot, the live connector, the policy, the hand.
//
// Off by default (ViewLayout.defaultLayout omits it): a golfer with no monitor is never shown an empty
// board they did not ask for. View → Launch monitor turns it on.

import QtQuick
import PinPointStudio

// THE STAGE FRAMES IT. PpModeStage puts the panel in its LAUNCH MONITOR card, so the panel draws no
// surface, border or title of its own.
Item {
    id: root

    // Set by PpModeStage only for the muted placeholder path. Present so the two are interchangeable in
    // the arranger's Loaders.
    property string title: qsTr("Launch monitor")

    clip: true

    readonly property var sessionShots: app.sessionReviewController.activeShots
    // app.state's selected shot as this list's row id (ShotListModel.idOfShot, by ShotRef), re-found as rows change.
    readonly property int _focusedId: {
        void root.sessionShots.activeCount
        return root.sessionShots.idOfShot(app.state.selection.shotRef, app.state.selection.swingDir)
    }
    readonly property bool reviewing:   app.sessionReviewController.reviewActive

    // The body's view and registers, reachable from the panel (the stage, a print or cast host, the
    // render harness). Cast and print set `interactive: false`: they take no input and show no controls.
    property alias tab: lmBody.tab
    property alias interactive: lmBody.interactive
    property alias medium: lmBody.medium
    property alias surface: lmBody.surface
    property alias printOrientation: lmBody.printOrientation
    property alias printPage: lmBody.printPage
    readonly property int printPageCount: lmBody.printPageCount
    readonly property Item graphicsBody: lmBody

    // The board of the shots on screen. Its list and its register (reviewing) are its owner's: activeShots, and
    // whether that is a loaded session. The rest it is asked here.
    readonly property var board: app.sessionReviewController.lmBoard
    Binding { target: root.board; property: "focusedShotId"; value: root._focusedId; restoreMode: Binding.RestoreNone }
    // The live-capture gate, and it answers for the LIVE session only — see emptyText(). A saved session's readings
    // are on disk whatever the device is doing now.
    Binding { target: root.board; property: "connected"; value: launchMonitor.configured; restoreMode: Binding.RestoreNone }
    Binding { target: root.board; property: "deviceName"; value: launchMonitor.deviceName; restoreMode: Binding.RestoreNone }
    // The SAME setting every other metric surface grades by (Session diagnostics, the Charts presenters), so a
    // corridor graded here carries the colour it carries everywhere else.
    Binding { target: root.board; property: "gradePolicy"; value: appSettings.diagnosticsGradePolicy; restoreMode: Binding.RestoreNone }
    // Publishes the handedness convention (graphics.leftHanded): the values stay in the device's convention and the
    // body mirrors the art and the words.
    Binding { target: root.board; property: "leftHanded"; value: app.athleteController.currentHandedness === "Left"; restoreMode: Binding.RestoreNone }
    // Held while this panel is up, after the asks above are set, so the board's first reduction is asked as the panel
    // asks it. It lets go when the panel is destroyed.
    Component.onCompleted: app.sessionReviewController.holdLmBoard(root)

    // The session's shots as recorded: the swing folder SessionMode needs to focus one, and the body's
    // heading until the model's graphics.shots is published.
    property int _shotsRev: 0
    Instantiator {
        id: shotInst
        model: root.sessionShots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string club
            required property string swingDir
        }
        onObjectAdded: root._shotsRev++
        onObjectRemoved: root._shotsRev++
    }
    readonly property var shotList: {
        if (root._shotsRev < 0) return []
        const out = []
        for (let i = 0; i < shotInst.count; ++i) {
            const o = shotInst.objectAt(i)
            if (o) out.push({ shotId: o.shotId, ordinal: o.ordinal, club: o.club, swingDir: o.swingDir })
        }
        return out
    }

    // The board's bands (the reading index and the ledger read them): one per band of readings, typed (LmBand: band,
    // count, tiles), as the plain list the body walks.
    readonly property var bands: Array.from(root.board.bands, b => ({ band: b.band, count: b.count, tiles: Array.from(b.tiles) }))

    // A shot asked for from a figure: focus it the way the carousel does. Analyse stays Analyse (promote
    // the swing and reload, without the detour through Replay that enterReplay() would take).
    function _focusShot(shotId) {
        for (const s of root.shotList) {
            if (s.shotId !== shotId) continue
            app.state.pickShot(root.sessionShots.shotRefOf(s.shotId), s.swingDir)
            return true
        }
        return false
    }
    // The model's actions. Guarded, so a build whose model predates them is a quiet no-op, never an error.
    function _exclude(shotId, excluded) { if (typeof root.board.setShotExcluded === "function") root.board.setShotExcluded(shotId, excluded) }
    function _club(shotId, club)        { if (typeof root.board.setShotClub === "function") root.board.setShotClub(shotId, club) }
    function _undoMismatch()            { if (typeof root.board.undoMismatch === "function") root.board.undoMismatch() }

    // ── empty and degraded states ────────────────────────────────────────────
    // One quiet line, so the card keeps its shape. Which line is LmSessionModel's decision: "no
    // monitor", "not saving", "nothing yet" and "nothing for this club" are four different problems.
    PpCardNote {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        visible: root.board.emptyText !== "" && root.medium !== "print"
        text: root.board.emptyText
    }

    PpLmGraphicsBody {
        id: lmBody
        anchors.fill: parent
        visible: root.board.emptyText === "" || root.medium === "print"
        g: root.board.graphics
        bands: root.bands
        shotList: root.shotList
        scopeText: root.board.scopeText
        emptyText: root.board.emptyText
        shotEmptyText: root.board.shotEmptyText
        liveDeviceName: root.board.deviceName
        reviewing: root.reviewing
        focusedShotId: root._focusedId
        onShotRequested: (shotId) => root._focusShot(shotId)
        onExcludeRequested: (shotId, excluded) => root._exclude(shotId, excluded)
        onClubRequested: (shotId, club) => root._club(shotId, club)
        onUndoMismatchRequested: root._undoMismatch()
    }
}
