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
.pragma library

// Home's per-athlete view state (.pragma library, imported relatively by HmHomeTabs and
// HmSwingAreas): appSettings.homeViewByAthlete, a map of athlete uuid → entry, each pane keeping its
// own keys in the one entry —
//   tab            "coaching" | "swing" | "matchups"    HmHomeTabs
//   expanded       [group…]                             HmSwingAreas
//   expandedSeen   bool                                 HmSwingAreas
//
// A QVariantMap property read is a deep copy, so every read takes the map ONCE, and every write
// modifies a copy of the entry and writes the whole map back, keeping the entry's other keys and
// every other athlete's. With no map on the settings object (a harness), or no athlete, there is
// no store: reads give the default and writes do nothing.

function has(settings, uuid) {
    return settings !== null && settings !== undefined
           && settings.homeViewByAthlete !== undefined && uuid !== undefined && uuid !== ""
}

// The athlete's entry as stored, or null when there is none.
function entry(settings, uuid) {
    if (!has(settings, uuid)) return null
    const map = settings.homeViewByAthlete || {}            // read ONCE
    const e = map[uuid]
    return e ? e : null
}

// One key of the athlete's entry, or dflt when it is not stored.
function read(settings, uuid, key, dflt) {
    const e = entry(settings, uuid)
    return e && e[key] !== undefined ? e[key] : dflt
}

// The map with the athlete's entry changed by `fields`, written back whole.
function _put(settings, uuid, map, fields) {
    const next  = Object.assign({}, map)
    const entry = Object.assign({}, next[uuid] || {})
    Object.assign(entry, fields)
    next[uuid] = entry
    settings.homeViewByAthlete = next
}

// Set several keys of the athlete's entry in one write. False when there is no store.
function writeFields(settings, uuid, fields) {
    if (!has(settings, uuid)) return false
    _put(settings, uuid, settings.homeViewByAthlete || {}, fields)     // read ONCE
    return true
}

// Set one key of the athlete's entry. Writes nothing when the stored value is already this one
// (compared with ===, so an array is always written). False when nothing was written.
function write(settings, uuid, key, value) {
    if (!has(settings, uuid)) return false
    const map = settings.homeViewByAthlete || {}            // read ONCE
    const e = map[uuid]
    if (e && e[key] === value) return false
    const fields = {}
    fields[key] = value
    _put(settings, uuid, map, fields)
    return true
}
