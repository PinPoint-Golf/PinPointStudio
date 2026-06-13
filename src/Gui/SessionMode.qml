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

// Session-screen MODE — the layout/activity axis (Capture | Review | Analyse).
// Effectively global: only one session screen is visible at a time, so a single
// singleton holds the active mode and the focused swing that Review/Analyse show.
//
// Mode is ORTHOGONAL to the data-source axis (Live vs Loaded, owned by
// SessionReviewController.reviewActive). Mode never stops live capture; clicking
// a card from a live session enters Review with capture still running. The two
// compose — do not fuse them.
//
// Enum values are plain ints (a QML singleton can't host a Q_ENUM cleanly):
//   capture = 0, review = 1, analyse = 2. Compare with `=== SessionMode.review`.
//
// shotReplay (a context property) is reachable from this singleton's root context.

pragma Singleton
import QtQuick
import PinPointStudio

QtObject {
    id: sm

    readonly property int capture: 0
    readonly property int review:  1
    readonly property int analyse: 2

    property int    mode: capture

    // The swing currently promoted onto the stage (Review/Analyse). -1 / "" = none.
    property int    focusedShotId:   -1
    property string focusedSwingDir: ""

    // Capture shows live tiles and never the disk replay, so free it here. The
    // focused swing is kept so re-entering Review/Analyse re-shows it.
    function enterCapture() { mode = capture; shotReplay.stop() }

    // Analyse shows the focused swing's charts/table (its analysisDetail), so the
    // replay must stay loaded — ensure it is.
    function enterAnalyse() { mode = analyse; _loadFocused() }

    // Show Review for the currently-focused swing (the mode-switch path). With no
    // focused swing the stage shows its empty-state — Review is never blocked.
    function showReview() {
        mode = review
        _loadFocused()
    }

    // Focus a swing and review it (the carousel-click path). Re-clicking the same
    // card in Review just keeps it on the stage (no reload — see _loadFocused).
    function enterReview(shotId, swingDir) {
        focusedShotId   = shotId
        focusedSwingDir = swingDir
        showReview()
    }

    // Leave any session screen: back to Capture, drop the focused swing, tear down
    // the disk replay. Mirrors the existing shotReplay.stop() site in Main.qml.
    function clear() {
        focusedShotId   = -1
        focusedSwingDir = ""
        mode = capture
        shotReplay.stop()
    }

    // Load the focused swing onto the stage, unless it is already the loaded one
    // (so toggling Review↔Analyse, or re-clicking the focused card, never restarts
    // playback). One replay surface now (the session stage) — no target arg.
    function _loadFocused() {
        if (focusedShotId >= 0 && focusedSwingDir !== ""
                && shotReplay.shotId !== focusedShotId)
            shotReplay.start(focusedShotId, focusedSwingDir, 0.25)
    }
}
