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

// The session screens' VIEW state, effectively global: only one session screen is visible at a time. The mode,
// the selected shot, the replay and their transitions are app.state's (src/App/app_state.h; refactor step 4-a);
// what stays here is the readers' place in the panels a mode switch rebuilds, the trash notice, whether the session
// screen is on show, and folder comparison.

pragma Singleton
import QtQuick
import PinPointStudio

QtObject {
    id: sm

    // (The "last panel up wins" sessionDiagnostics seam and its claimant are gone, refactor 3.7-e: a screen reaches the
    // model a diagnostics panel shows for its session through diagnosticsAdapters.shownFor(app.sessionAt(folder), folder).)

    // Where the reader was in the stage's session diagnostics panel — tab, card filter,
    // Watching fold, open characteristic, and each list's scroll — kept HERE because the panel
    // does not survive a mode switch: PpModeStage rebuilds its panels on every Capture↔Replay
    // flip, i.e. twice per auto-replayed shot. The panel saves it as it changes and puts it back
    // on build (PpSessionDiagnosticsPanel). Stamped with the session folder it was read in, so
    // a different session starts from the defaults. Not persisted. null = nothing saved.
    property var    diagnosticsNav: null

    // Where the reader was in the stage's Charts panel — the open profile row and each view's scroll — kept
    // HERE for the same reason as diagnosticsNav: PpModeStage rebuilds its panels on every Capture↔Replay
    // flip. PpChartsPanel saves it as it changes and puts it back on build: { sessionDir, openId, scroll }, where
    // scroll is { "<register>/<list objectName>": contentY } (PpReaderPlace: per register, the reader's own scroll).
    // Stamped with the session folder (a different session starts at the defaults). Not persisted: the
    // reader's CHOICES (view, preset, window, against, facet …) persist in AppSettings chartPrefs instead.
    property var    chartsNav: null

    // A swing's folder went to the trash (the carousel's delete, #56). The session panels forget
    // it on this — the coaching ledger and the charts — so neither goes on counting a swing that
    // is not there. A signal here rather than on the carousel's model because the panels are not
    // the carousel's to reach, and this singleton is in every screen and every harness.
    signal swingTrashed(string swingDir)

    // Whether the session screen is the one on show. Every screen stays instantiated in the shell's
    // StackLayout (the session screen sits at index AppScreen.Session), so the session screen and the
    // panels on it gate their costly work on this: a hidden host must not take the replay's one video
    // sink (PpCameraTiles), re-parse swing.json on every reload (PpDataViewer), re-lay-out the transit
    // timeline (ScreenWrist) or feed its presenters (PpChartsPanel, PpTrustPanel). Guarded because a
    // test engine provides navController only when it asks for it.
    readonly property bool sessionScreenActive:
        (typeof app !== "undefined" && !!app && !!app.navController) && app.navController !== null
        && app.navController.currentIndex === AppScreen.Session

    // A session or swing folder as one spelling, so two can be compared: backslashes to slashes (a
    // Windows path), runs of slashes to one (a library path ending in a slash gives "…/lib//athlete/…"
    // from the processor and "…/lib/athlete/…" from the carousel), no trailing slash. "" for none.
    function folderKey(p) { return String(p || "").replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "") }
    function sameFolder(a, b) { return folderKey(a) === folderKey(b) }
}
