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

// Resolves the session-toolbar View state (which panels are visible + how the
// centre stage packs them) per SessionController::Type, persisting to appSettings.
// Two orthogonal concerns: VISIBILITY (panel on/off) and ARRANGEMENT (stage
// packing). A named preset sets both; toggling either marks the preset "Custom".
//
// Panels split into two homes:
//   stagePanels — arranged inside PpModeStage per `arrangement`
//   railPanels  — full-width rails the screen shows/hides via isPanelOn()
//
// Storage shape (per type, key = String(typeInt)):
//   viewPanelsByType[k]      → QStringList of enabled panel keys
//   viewArrangementByType[k] → "tabs" | "split" | "stage"
//   viewPresetByType[k]      → preset name, or "Custom"
//
// The resolver functions read the notifying appSettings.view*ByType properties,
// so QML binding dependency-tracking re-evaluates visible:/arrangement: bindings
// whenever the maps change — no extra change signal needed.

pragma Singleton
import QtQuick
import PinPointStudio

QtObject {
    id: vl

    readonly property var panelKeys:  ["camera", "timeline", "charts", "dashboard", "table", "carousel"]
    readonly property var stagePanels: ["camera", "charts", "dashboard", "table"]   // arranged
    readonly property var railPanels:  ["timeline", "carousel"]                     // full-width

    // preset name → { panels: [enabled keys], arrangement }
    readonly property var presets: ({
        "Capture": { panels: ["camera", "carousel"],                            arrangement: "stage" },
        "Review":  { panels: ["camera", "carousel", "charts"],                  arrangement: "split" },
        "Analyse": { panels: ["camera", "charts", "table", "carousel", "timeline"], arrangement: "split" },
        "All":     { panels: ["camera", "timeline", "charts", "dashboard", "table", "carousel"], arrangement: "split" }
    })
    readonly property var presetOrder: ["Capture", "Review", "Analyse", "All"]

    function defaultPreset(type) {
        switch (type) {
            case SessionController.Wrist: return "Capture"
            case SessionController.Coach: return "Review"
            default:                      return "Analyse"
        }
    }

    function _key(type) { return String(type) }

    // ── resolution (read) ───────────────────────────────────────────────────
    function presetFor(type) {
        var m = appSettings.viewPresetByType, k = _key(type)
        return (m && m[k] !== undefined) ? m[k] : defaultPreset(type)
    }

    // QStringList of enabled keys; falls back to the active preset's set.
    function enabledKeysFor(type) {
        var m = appSettings.viewPanelsByType, k = _key(type)
        if (m && m[k] !== undefined) return m[k]
        var p = presets[presetFor(type)] || presets["Analyse"]
        return p.panels
    }

    function isPanelOn(type, key) {
        return enabledKeysFor(type).indexOf(key) >= 0
    }

    function arrangementFor(type) {
        var m = appSettings.viewArrangementByType, k = _key(type)
        if (m && m[k] !== undefined) return m[k]
        var p = presets[presetFor(type)] || presets["Analyse"]
        return p.arrangement
    }

    // ── mutation (write) ────────────────────────────────────────────────────
    function applyPreset(type, name) {
        var p = presets[name]; if (!p) return
        var k = _key(type)
        var mp = _clone(appSettings.viewPanelsByType);      mp[k] = p.panels.slice()
        var ma = _clone(appSettings.viewArrangementByType); ma[k] = p.arrangement
        var ms = _clone(appSettings.viewPresetByType);      ms[k] = name
        appSettings.viewPanelsByType      = mp
        appSettings.viewArrangementByType = ma
        appSettings.viewPresetByType      = ms
    }

    function setPanel(type, key, on) {
        var k = _key(type)
        var keys = enabledKeysFor(type).slice()
        var i = keys.indexOf(key)
        if (on && i < 0) keys.push(key)
        else if (!on && i >= 0) keys.splice(i, 1)
        var mp = _clone(appSettings.viewPanelsByType); mp[k] = keys
        appSettings.viewPanelsByType = mp
        _reconcilePreset(type)
    }

    function setArrangement(type, name) {
        var k = _key(type)
        var ma = _clone(appSettings.viewArrangementByType); ma[k] = name
        appSettings.viewArrangementByType = ma
        _reconcilePreset(type)
    }

    // If the resolved state matches a named preset, show that name; else "Custom".
    function _reconcilePreset(type) {
        var match = ""
        for (var i = 0; i < presetOrder.length; ++i)
            if (_matchesPreset(type, presetOrder[i])) { match = presetOrder[i]; break }
        var k = _key(type)
        var ms = _clone(appSettings.viewPresetByType)
        ms[k] = match !== "" ? match : "Custom"
        appSettings.viewPresetByType = ms
    }

    function _matchesPreset(type, name) {
        var p = presets[name]; if (!p) return false
        if (arrangementFor(type) !== p.arrangement) return false
        var cur = enabledKeysFor(type)
        if (cur.length !== p.panels.length) return false
        for (var i = 0; i < p.panels.length; ++i)
            if (cur.indexOf(p.panels[i]) < 0) return false
        return true
    }

    function _clone(m) {
        var out = {}
        if (m) for (var key in m) out[key] = m[key]
        return out
    }
}
