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
// LmSessionModel decides every number: the statistics by the effective club and without the shots left
// out, the corridor grades, the enclosing ellipses, the club evidence. The body draws its `graphics` map
// and its bands; what the golfer does on the panel comes back here as intents — focus a shot (SessionMode,
// the carousel's own path), leave a shot out, fix a club label, undo the evidence's relabel — and goes to
// the model, which persists it with the shot.
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

    readonly property var sessionShots: sessionReviewController.activeShots
    readonly property bool reviewing:   sessionReviewController.reviewActive

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

    LmSessionModel {
        id: board
        shotModel:     root.sessionShots
        focusedShotId: SessionMode.focusedShotId
        // The two live-capture gates, and they answer for the LIVE session only — see emptyText(). A
        // saved session's readings are on disk whatever the device is doing now.
        reviewing:     root.reviewing
        connected:     launchMonitor.configured
        deviceName:    launchMonitor.deviceName
        // The SAME setting MetricCatalog is bound to wherever it is hosted, so a corridor graded here
        // carries the colour it carries on every other metric surface.
        gradePolicy:   appSettings.diagnosticsGradePolicy
        // Publishes the handedness convention (graphics.leftHanded): the values stay in the device's
        // convention and the body mirrors the art and the words.
        leftHanded:    athleteController.currentHandedness === "Left"
    }

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

    // The board's bands (the reading index and the ledger read them): one row per band of readings.
    property int _bandsRev: 0
    Instantiator {
        id: bandInst
        model: board
        delegate: QtObject {
            required property string band
            required property int count
            required property var tiles
        }
        onObjectAdded: root._bandsRev++
        onObjectRemoved: root._bandsRev++
    }
    readonly property var bands: {
        if (root._bandsRev < 0 || !board.graphics) return []
        const out = []
        for (let i = 0; i < bandInst.count; ++i) {
            const o = bandInst.objectAt(i)
            if (o) out.push({ band: o.band, count: o.count, tiles: o.tiles || [] })
        }
        return out
    }

    // A shot asked for from a figure: focus it the way the carousel does. Analyse stays Analyse (promote
    // the swing and reload, without the detour through Replay that enterReplay() would take).
    function _focusShot(shotId) {
        for (const s of root.shotList) {
            if (s.shotId !== shotId) continue
            if (SessionMode.mode === SessionMode.analyse) {
                SessionMode.focusedShotId   = s.shotId
                SessionMode.focusedSwingDir = s.swingDir
                SessionMode.enterAnalyse()
            } else {
                SessionMode.enterReplay(s.shotId, s.swingDir)
            }
            return true
        }
        return false
    }
    // The model's actions. Guarded, so a build whose model predates them is a quiet no-op, never an error.
    function _exclude(shotId, excluded) { if (typeof board.setShotExcluded === "function") board.setShotExcluded(shotId, excluded) }
    function _club(shotId, club)        { if (typeof board.setShotClub === "function") board.setShotClub(shotId, club) }
    function _undoMismatch()            { if (typeof board.undoMismatch === "function") board.undoMismatch() }

    // ── empty and degraded states ────────────────────────────────────────────
    // One quiet line, so the card keeps its shape. Which line is LmSessionModel's decision: "no
    // monitor", "not saving", "nothing yet" and "nothing for this club" are four different problems.
    PpCardNote {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        visible: board.emptyText !== "" && root.medium !== "print"
        text: board.emptyText
    }

    PpLmGraphicsBody {
        id: lmBody
        anchors.fill: parent
        visible: board.emptyText === "" || root.medium === "print"
        g: board.graphics
        bands: root.bands
        shotList: root.shotList
        scopeText: board.scopeText
        emptyText: board.emptyText
        liveDeviceName: board.deviceName
        reviewing: root.reviewing
        focusedShotId: SessionMode.focusedShotId
        onShotRequested: (shotId) => root._focusShot(shotId)
        onExcludeRequested: (shotId, excluded) => root._exclude(shotId, excluded)
        onClubRequested: (shotId, club) => root._club(shotId, club)
        onUndoMismatchRequested: root._undoMismatch()
    }
}
