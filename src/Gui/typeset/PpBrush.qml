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

// BRUSH — the shared focus state. ONE instance per register, passed to every block as `brush`.
// A thing that is hovered or picked is shown ONCE in every block that contains it.
//   picked shot   — the carousel's shot: the ink ring (points) or the pick column (strips, barcode). Stays.
//   hovered shot  — the same form, lighter: a hairline ring / a fainter column. Follows the pointer.
//   highlight*    — the answer to a question: everything NOT highlighted recedes to roles.dim.
// Blocks WRITE hoveredShot / hoveredCondition themselves on hover (so brushing needs no wiring);
// clicks are only signals, which the register routes to the real body.
QtObject {
    id: br
    property int pickedShot: -1             // bind to readout.shotId
    property int hoveredShot: -1
    property string pickedCondition: ""     // the open condition (detail.id), drawn strong in lists
    property string hoveredCondition: ""
    property var highlightShots: []         // shot ids
    property var highlightConditions: []    // condition ids
    property string question: ""            // the active question's id, if any (PpQuestions block)
    property var cards: []                  // the session cards: teaches the brush each shot's swingDir

    readonly property var shotDirs: {
        const out = {}
        const cs = cards || []
        for (let i = 0; i < cs.length; ++i) {
            const run = (cs[i].spread && cs[i].spread.run) || []
            for (let j = 0; j < run.length; ++j) if (run[j].swingDir && !out[run[j].shotId]) out[run[j].shotId] = run[j].swingDir
        }
        return out
    }
    function dirOf(shotId) { return shotDirs[shotId] || "" }

    readonly property bool shotFilter: (highlightShots || []).length > 0
    readonly property bool conditionFilter: (highlightConditions || []).length > 0
    function isPicked(id) { return id === pickedShot }
    function isHovered(id) { return id === hoveredShot && id !== pickedShot }
    function shotLit(id) { return !shotFilter || highlightShots.indexOf(id) >= 0 }
    function conditionLit(id) { return !conditionFilter || highlightConditions.indexOf(id) >= 0 }
    function shotOpacity(id, dim) { return shotLit(id) ? 1 : (dim === undefined ? 0.28 : dim) }
    function conditionOpacity(id, dim) { return conditionLit(id) ? 1 : (dim === undefined ? 0.28 : dim) }

    function hoverShot(id, on) { if (on) hoveredShot = id; else if (hoveredShot === id) hoveredShot = -1 }
    function hoverCondition(id, on) { if (on) hoveredCondition = id; else if (hoveredCondition === id) hoveredCondition = "" }
    function lightShots(ids, q) { highlightShots = ids || []; highlightConditions = []; question = q || "" }
    function lightConditions(ids, q) { highlightConditions = ids || []; highlightShots = []; question = q || "" }
    function clear() { highlightShots = []; highlightConditions = []; question = ""; hoveredShot = -1; hoveredCondition = "" }

    // ── PULSE (round 4, D1): after a SURFACED shot, the rows/marks whose state changed get ONE soft ink
    // wash that fades over Theme.durationSlow; the focused condition leads (order = stagger). The register
    // calls pulse(ids, focusId) from body.pulseCue; every block with condition rows draws a PpPulseWash.
    // Nothing under reduceMotion. A new token restarts the wash even for the same ids.
    property var pulseIds: []                // condition ids, focus first
    property int pulseToken: 0
    property int pulseStagger: 70            // ms between rows, in pulse order
    property bool pulseHold: false           // renders / demo states only: hold the wash at full instead of fading
    function pulse(ids, focusId) {
        const list = (ids || []).slice()
        if (focusId) { const i = list.indexOf(focusId); if (i > 0) { list.splice(i, 1); list.unshift(focusId) } else if (i < 0) list.unshift(focusId) }
        pulseIds = list
        pulseToken = pulseToken + 1
    }
    function pulseOrder(id) { return (pulseIds || []).indexOf(id) }
}
