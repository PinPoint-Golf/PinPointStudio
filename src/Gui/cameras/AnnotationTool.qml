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

// Shared state for the analyse-view telestrator (draw circles / lines / hollow
// squares over replay video). ONE toolbar drives the active tool + colour here;
// each PpAnnotationLayer (one per camera tile) reads them and owns its own marks.
//
// Marks are EPHEMERAL — held only in this in-memory store, keyed per swing+tile,
// and wiped wholesale the moment the focused swing changes (new swing OR deselect).
// That is the "cleared when a swing is deselected" contract; nothing is persisted
// to disk. The store survives tile churn WITHIN a swing (Replay↔Analyse toggles
// recreate the layers) because it lives above the layers, here.

pragma Singleton
import QtQuick
import PinPointStudio

QtObject {
    id: at

    // ── Shared tool state (the one toolbar writes these) ───────────────────────
    property string tool: "select"            // select | circle | line | rect
    property color  strokeColor: Theme.colorAccent

    // ── Per-swing, per-tile mark store ────────────────────────────────────────
    // key = "<swingDir>#<streamIndex>" → array of shape objects
    //   { id, type:"circle"|"line"|"rect", x1, y1, x2, y2 (normalized), color }
    // Returns the LIVE stored array so a layer's in-place edits reflect here.
    property var _store: ({})

    function marksFor(key) {
        if (!key) return []
        if (!_store[key]) _store[key] = []
        return _store[key]
    }

    // ── Single active selection across all tiles ──────────────────────────────
    // Selecting in one tile clears any selection in the others, so Delete and the
    // colour swatches act on exactly one mark regardless of which tile owns it.
    property QtObject selectionOwner: null
    readonly property bool hasSelection: selectionOwner !== null
    signal selectionClaimed(var owner)

    function claimSelection(owner) { selectionOwner = owner; selectionClaimed(owner) }
    function releaseSelection(owner) { if (selectionOwner === owner) selectionOwner = null }
    function deleteSelected()  { if (selectionOwner) selectionOwner.deleteSelectedShape() }
    function recolorSelected(c) { if (selectionOwner) selectionOwner.recolorSelectedShape(c) }

    // ── Clear-all (toolbar button) ────────────────────────────────────────────
    signal cleared()
    function clearAll() {
        _store = ({})
        selectionOwner = null
        cleared()
    }

    // ── Lifecycle: wipe everything when the focused swing changes ─────────────
    // Fires on both a new swing and a deselect (focusedSwingDir → ""). Layers
    // reload from the now-empty store via cleared().
    property string _focusedSwing: SessionMode.focusedSwingDir
    on_FocusedSwingChanged: clearAll()
}
