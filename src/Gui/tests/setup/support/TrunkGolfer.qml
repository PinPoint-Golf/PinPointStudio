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

// TrunkGolfer — a scripted golfer for the trunk routine's turn steps (TrunkRoutine.qml; design
// §16.2, 26 Oct). The turn steps are started and ended by the sensors, not a timer, so a test that
// wants a turn step to finish must TURN the fake sensors: when the routine ARMS a turn step, this
// plays a turn on the two FakeImuInstances (FakeImuInstance.play) — the chest out and back, the
// hips half as far, the gyro high while moving and 0 at the end — in the direction the step asks
// (a right-hander turns away about −Z; checkTrunkTurn's sign).
//
// It never clicks and never touches the routine: it only moves the sensors, as a golfer would.
//   enabled: false        no golfer (the step waits, armed)
//   scripts: [frames, …]  one movement per armed turn, oldest first; then `defaultTurn`
//   frames:  [{ chest, hips, gyro, ms }] — chest / hips the turn (°) from the armed heading, + = the
//            way the step asks (hips default to chest / 2); NaN = no reading; held `ms` real ms
import QtQuick

QtObject {
    id: g

    property var  routine: null
    property bool enabled: true
    property var  scripts: []
    // Out to 90°, back to 3°, still: ~0.35 s at the tests' pace (tick 20 ms), each frame ≥ 2 ticks.
    readonly property var defaultTurn: [
        { chest: 0,  gyro: 0,  ms: 20 },
        { chest: 15, gyro: 60, ms: 40 },
        { chest: 45, gyro: 80, ms: 40 },
        { chest: 75, gyro: 80, ms: 40 },
        { chest: 90, gyro: 40, ms: 60 },
        { chest: 60, gyro: 80, ms: 40 },
        { chest: 30, gyro: 80, ms: 40 },
        { chest: 8,  gyro: 40, ms: 40 },
        { chest: 3,  gyro: 0,  ms: 0 }]
    // Every turn played: { stage, t }.
    property var log: []
    property int turns: 0

    readonly property var _pel: routine ? routine.pelvisImu : null
    readonly property var _tho: routine ? routine.thoraxImu : null

    property Connections _armed: Connections {
        target:  g.routine
        enabled: g.enabled && g.routine !== null
        function onSubstageChanged() {
            if (g.routine.substage === "armed") g.turn()
        }
    }

    function reset() { enabled = true; scripts = []; log = []; turns = 0 }

    // One movement now, from where the sensors are.
    function turn(frames) {
        if (!routine) return
        if (frames === undefined) {
            frames = scripts.length > 0 ? scripts[0] : defaultTurn
            if (scripts.length > 0) scripts = scripts.slice(1)
        }
        var away = routine.stage === "turnAway"
        var sign = (away ? -1 : 1) * (routine.rightHanded ? 1 : -1)
        turns += 1
        log.push({ stage: routine.stage, t: Date.now() })
        var lane = function(base, key) {
            return frames.map(function(f) {
                var o = { ms: f.ms }
                if (f.gyro !== undefined) o.gyro = f.gyro
                var v = f[key]
                if (v === undefined && key === "hips" && f.chest !== undefined) v = f.chest / 2
                if (v !== undefined) o.heading = isFinite(v) ? base + sign * v : v
                return o
            })
        }
        if (_tho) _tho.play(lane(_tho.headingDeg, "chest"))
        if (_pel) _pel.play(lane(_pel.headingDeg, "hips"))
    }
}
