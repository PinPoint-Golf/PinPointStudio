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

// Resolves the session-stage layout (which panels are visible + how the centre
// stage packs them) per session MODE (Capture | Replay | Analyse), persisting to
// appSettings.viewLayoutByMode. Two orthogonal concerns: VISIBILITY (panel on/off)
// and ARRANGEMENT (stage packing). There are no named presets — every edit just
// updates that mode's layout in place.
//
// Panels split into two homes:
//   stage panels — camera/charts/dashboard/table, arranged inside PpModeStage
//   rail panels  — timeline/carousel, full-width rails the screen shows/hides
//
// Storage shape (per mode, key = String(modeInt)):
//   viewLayoutByMode[k] = { panels: QStringList, arrangement: "tabs"|"split"|"stage" }
//
// The resolver functions read the notifying appSettings.viewLayoutByMode property,
// so QML binding dependency-tracking re-evaluates visible:/arrangement: bindings
// whenever the map changes — no extra change signal needed.

pragma Singleton
import QtQuick
import PinPointStudio

QtObject {
    id: vl

    // mode → sensible default layout (used until the user edits a mode's layout).
    // overlays: the analysis overlay (pose skeleton + club shaft + ball) drawn on
    // the camera panel — on by default in every mode.
    function defaultLayout(mode) {
        switch (mode) {
            case SessionMode.replay:
                return { panels: ["camera", "charts", "timeline", "carousel"], arrangement: "split", overlays: true }
            case SessionMode.analyse:
                // Analyse plays the loaded shot's video (same disk source as Replay)
                // alongside the deeper-analysis panels — camera included so playback
                // follows the Replay↔Analyse toggle.
                return { panels: ["camera", "charts", "table", "timeline", "carousel"], arrangement: "split", overlays: true }
            default: // capture
                return { panels: ["camera", "carousel"], arrangement: "stage", overlays: true }
        }
    }

    function _key(mode) { return String(mode) }

    function _layout(mode) {
        var m = appSettings.viewLayoutByMode, k = _key(mode)
        return (m && m[k] !== undefined) ? m[k] : defaultLayout(mode)
    }

    // ── resolution (read) ───────────────────────────────────────────────────
    function enabledKeysFor(mode) {
        return _layout(mode).panels || []
    }

    function isPanelOn(mode, key) {
        return enabledKeysFor(mode).indexOf(key) >= 0
    }

    function arrangementFor(mode) {
        return _layout(mode).arrangement || "split"
    }

    // Analysis-overlay visibility for a mode (pose skeleton + club shaft + ball).
    function overlaysOn(mode) {
        var l = _layout(mode)
        return l.overlays !== undefined ? l.overlays : true
    }

    // ── mutation (write) ────────────────────────────────────────────────────
    function setPanel(mode, key, on) {
        var keys = enabledKeysFor(mode).slice()
        var i = keys.indexOf(key)
        if (on && i < 0) keys.push(key)
        else if (!on && i >= 0) keys.splice(i, 1)
        _write(mode, keys, arrangementFor(mode))
    }

    function setArrangement(mode, name) {
        _write(mode, enabledKeysFor(mode).slice(), name)
    }

    function setOverlays(mode, on) {
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), on)
    }

    // overlays is optional — omitted by setPanel/setArrangement, so preserve the
    // mode's current value (default true) rather than dropping it on those edits.
    function _write(mode, panels, arrangement, overlays) {
        var m = _clone(appSettings.viewLayoutByMode)
        var cur = m[_key(mode)]
        var ov = (overlays !== undefined) ? overlays
               : (cur && cur.overlays !== undefined) ? cur.overlays : true
        m[_key(mode)] = { panels: panels, arrangement: arrangement, overlays: ov }
        appSettings.viewLayoutByMode = m
    }

    function _clone(m) {
        var out = {}
        if (m) for (var key in m) out[key] = m[key]
        return out
    }
}
