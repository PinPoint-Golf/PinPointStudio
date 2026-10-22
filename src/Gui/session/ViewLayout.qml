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
// appSettings.viewLayoutByMode. Three orthogonal concerns: VISIBILITY (panel
// on/off), ARRANGEMENT (stage packing), and MOTION (the per-element overlay
// drawn on the camera panel — pose skeleton + club shaft + ball). There are no
// named panel/arrangement presets — every panel/arrangement edit just updates
// that mode's layout in place. Motion DOES have named presets (see below).
//
// Panels split into two homes:
//   stage panels — camera/charts/sessionDiagnostics/launchMonitor/table/swing3d/markup, arranged
//                  inside PpModeStage
//   rail panels  — timeline/carousel, full-width rails the screen shows/hides
//
// A panel key is legal by being in PpModeStage's `_defs` and PpViewPanel's catalogue;
// nothing here validates one, and a stored key no host wires is simply not shown. A RETIRED key
// is the exception: it is read, and rewritten once, as its successor (_retiredKeys below). What
// defaultLayout() OMITS is therefore the real switch: "launchMonitor" is deliberately
// absent from every default, because a golfer with no monitor must never be shown an
// empty board they did not ask for. They turn it on in View.
//
// Storage shape (per mode, key = String(modeInt)):
//   viewLayoutByMode[k] = {
//       panels:      QStringList,
//       arrangement: "tabs"|"split"|"stage",
//       motion: {
//           on:          bool,                         // master overlay switch
//           preset:      string,                       // preset id, or "custom"
//           modes:       { arms, spine, shoulders, hips, legs, shaft, shaftGrip,
//                          ball, hands, plane, synth:
//                          "off"|"frame"|"fan"|"trace" },   // hands (WB4) default off;
//                                                            // plane (3-D panel only) off|frame;
//                                                            // synth (the 3-D synthetic shaft, DTL
//                                                            // tile) off|frame — auto_annotations_design.md
//           traceTarget: string                        // optional (see below)
//       },
//       coachLineSets: { posture: bool, plane: bool,  // the View menu's static address marks, in three
//                        headHips: bool },             // independent sets (auto_annotations_design.md §1c);
//                                                       // Replay and Analyse only — Capture never draws them
//       coachLines: bool                               // LEGACY single toggle, still written as "any set on"
//                                                       // for an older build. Read only when coachLineSets
//                                                       // is absent: true ⇒ Head & hips + Plane (what the
//                                                       // old toggle drew), Posture off; false/absent ⇒ none
//       sizes: {                                       // optional — the user's dragged panel sizes,
//           split: { <panelKey>: weight },             // per ARRANGEMENT (setSizes). Relative weights,
//           stage: { main: fraction,                   // mean 1 over the panels active when written;
//                    side: { <panelKey>: weight } }    // `main` = the dominant panel's share of the
//       }                                              // row (gap excluded). An absent arrangement, key
//                                                       // or `main` takes PpModeStage's default (even;
//                                                       // the dominant card and side column 62 : 30).
//           traceTarget: string                        // optional — only present
//                                                       // when the preset/edit
//                                                       // overrides an element's
//                                                       // default trace anchor
//       }
//   }
//
// Element default trace anchors (owned by the renderer, noted here for
// context): arms → leadWrist, spine → neckMid, shoulders → leadShoulder,
// hips → pelvisMid, legs → leadAnkle, shaft → clubhead, shaftGrip → grip,
// ball → none.
//
// Back-compat: older stores wrote `overlays: bool` instead of `motion`. Reads
// migrate that losslessly forever (see _rawMotion); the first subsequent write
// of that mode replaces `overlays` with a full `motion` object — the old key
// is never written back. `overlaysOn()`/`setOverlays()` remain as thin shims
// over `motionOn()`/`setMotionOn()` so pre-existing callers keep working.
//
// Capture hard guard: in Capture mode every element except "ball" always
// resolves to "off", regardless of what's stored — Capture never renders body
// or shaft motion. Enforced once, in motionFor() (elementMode() inherits it by
// calling motionFor()).
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
    function defaultLayout(mode) {
        switch (mode) {
            case SessionMode.replay:
                return { panels: ["camera", "charts", "timeline", "carousel"], arrangement: "split", motion: _defaultMotion(mode) }
            case SessionMode.analyse:
                // Analyse plays the loaded shot's video (same disk source as Replay)
                // alongside the deeper-analysis panels — camera included so playback
                // follows the Replay↔Analyse toggle.
                return { panels: ["camera", "charts", "table", "timeline", "carousel"], arrangement: "split", motion: _defaultMotion(mode) }
            default: // capture
                return { panels: ["camera", "carousel"], arrangement: "stage", motion: _defaultMotion(mode) }
        }
    }

    function _key(mode) { return String(mode) }

    function _layout(mode) {
        var m = appSettings.viewLayoutByMode, k = _key(mode)
        return (m && m[k] !== undefined) ? _migrateLayout(m[k]).layout : defaultLayout(mode)
    }

    // ── retired stage keys ──────────────────────────────────────────────────
    // A saved layout naming a stage panel that no longer exists is read as its successor:
    //   wristMotion → charts   (the Wrist motion panel, retired 15 Oct 2026: its checkpoint profile,
    //                           findings and score are the wrist sections of Charts' This shot and Session —
    //                           wrist_motion_into_charts.md §4)
    // in `panels`, `sizes.split` and `sizes.stage.side`, de-duplicated: when the layout already has the
    // successor, the retired key is dropped (and so is its dragged weight; the successor keeps its own).
    // Every read goes through it, and the stored map is rewritten ONCE, on load (_migrateStored), so the
    // old key does not linger in the INI. Pure: never mutates the stored entry.
    readonly property var _retiredKeys: ({ wristMotion: "charts" })
    function _successor(key) { return _retiredKeys[key] !== undefined ? _retiredKeys[key] : key }
    // A weights map ({ <panelKey>: weight }) with retired keys renamed; a real key's own weight wins.
    function _migrateWeights(w) {
        var out = {}, changed = false, k
        for (k in w) if (_retiredKeys[k] === undefined) out[k] = w[k]
        for (k in w) {
            if (_retiredKeys[k] === undefined) continue
            changed = true
            var to = _successor(k)
            if (out[to] === undefined) out[to] = w[k]
        }
        return { weights: changed ? out : w, changed: changed }
    }
    // { layout, changed }: the entry with every retired key replaced (the same object when nothing changed).
    function _migrateLayout(l) {
        if (!l || typeof l !== "object") return { layout: l, changed: false }
        var changed = false
        var ps = l.panels || [], np = []
        for (var i = 0; i < ps.length; i++) {
            var key = _successor(ps[i])
            if (key !== ps[i]) changed = true
            if (np.indexOf(key) < 0) np.push(key)
            else changed = true
        }
        var sz = l.sizes, nsz = sz
        if (sz && typeof sz === "object") {
            nsz = _cloneSizes(sz)
            var szChanged = false
            if (nsz.split && typeof nsz.split === "object") {
                var sp = _migrateWeights(nsz.split)
                if (sp.changed) { nsz.split = sp.weights; szChanged = true }
            }
            if (nsz.stage && typeof nsz.stage === "object" && nsz.stage.side && typeof nsz.stage.side === "object") {
                var sd = _migrateWeights(nsz.stage.side)
                if (sd.changed) { nsz.stage.side = sd.weights; szChanged = true }
            }
            if (szChanged) changed = true
            else nsz = sz
        }
        if (!changed) return { layout: l, changed: false }
        var out = _clone(l)
        if (l.panels !== undefined) out.panels = np
        if (sz !== undefined) out.sizes = nsz
        return { layout: out, changed: true }
    }
    // Rewrites the stored map once if any mode's entry names a retired key. Called on load.
    function _migrateStored() {
        var m = appSettings.viewLayoutByMode
        if (!m) return false
        var out = _clone(m), any = false
        for (var k in m) {
            var r = _migrateLayout(m[k])
            if (r.changed) { out[k] = r.layout; any = true }
        }
        if (any) appSettings.viewLayoutByMode = out
        return any
    }
    Component.onCompleted: _migrateStored()

    // ── motion preset catalogue ─────────────────────────────────────────────
    // Global, read-only. Every entry lists all element keys explicitly (shaftGrip
    // = the club's GRIP-end trace, companion to shaft = the CLUBHEAD-end trace;
    // plane = the fused downswing plane, drawn by the 3-D swing panel only —
    // docs/design/swing_3d_annotations_design.md §5b — and ignored by the tiles).
    readonly property var _presets: [
        { id: "clean", label: "Clean", hint: "frame · body + club",
          modes: { arms: "frame", spine: "frame", shoulders: "frame", hips: "frame", legs: "off", shaft: "frame", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "off" } },
        { id: "ballOnly", label: "Ball only", hint: "frame · ball",
          modes: { arms: "off", spine: "off", shoulders: "off", hips: "off", legs: "off", shaft: "off", shaftGrip: "off", ball: "frame", hands: "off", plane: "off", synth: "off" } },
        { id: "clubLeadArm", label: "Club + lead arm", hint: "fan · club + lead arm",
          modes: { arms: "fan", spine: "off", shoulders: "off", hips: "off", legs: "off", shaft: "fan", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "off" } },
        { id: "clubTrack", label: "Club track", hint: "trace · club grip + head",
          modes: { arms: "off", spine: "off", shoulders: "off", hips: "off", legs: "off", shaft: "trace", shaftGrip: "trace", ball: "off", hands: "off", plane: "frame", synth: "off" } },
        // The hand path: the club's butt end (shaftGrip) beside the lead wrist (the
        // arms trace's default anchor) — two measurements of one path, so a gap
        // between them is the tracker disagreeing, not the hands. Made for DTL,
        // where the hands coming over the top show as the downswing loop outside
        // the backswing.
        { id: "traceHands", label: "Trace hands", hint: "trace · club butt + lead wrist",
          modes: { arms: "trace", spine: "off", shoulders: "off", hips: "off", legs: "off", shaft: "off", shaftGrip: "trace", ball: "off", hands: "off", plane: "off", synth: "off" } },
        { id: "core", label: "Core", hint: "frame · spine + hips + shoulders",
          modes: { arms: "off", spine: "frame", shoulders: "frame", hips: "frame", legs: "off", shaft: "off", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "off" } },
        { id: "tracePelvis", label: "Trace pelvis", hint: "trace · pelvis",
          modes: { arms: "off", spine: "off", shoulders: "off", hips: "trace", legs: "off", shaft: "off", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "off" } },
        { id: "traceHead", label: "Trace head", hint: "trace · head",
          modes: { arms: "off", spine: "off", shoulders: "trace", hips: "off", legs: "off", shaft: "off", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "off" },
          traceTarget: "head" },
        { id: "traceLeadSh", label: "Trace lead shoulder", hint: "trace · lead shoulder",
          modes: { arms: "off", spine: "off", shoulders: "trace", hips: "off", legs: "off", shaft: "off", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "off" } },
        // The measured club with its 3-D synthesis dashed under it (dtl_shaft_synth3d.h; the
        // DTL tile only), nothing else, so the two can be compared frame by frame.
        { id: "syntheticClub", label: "Synthetic club", hint: "frame · club + its 3-D synthesis (DTL)",
          modes: { arms: "off", spine: "off", shoulders: "off", hips: "off", legs: "off", shaft: "frame", shaftGrip: "off", ball: "off", hands: "off", plane: "off", synth: "frame" } },
    ]

    // Fresh copies only — callers must not be able to mutate the catalogue
    // through the returned reference.
    function presetCatalog() {
        return _presets.map(function (p) {
            var copy = { id: p.id, label: p.label, hint: p.hint, modes: _cloneModes(p.modes) }
            if (p.traceTarget !== undefined) copy.traceTarget = p.traceTarget
            return copy
        })
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

    // Master on/off switch for the motion overlay (pose skeleton + club shaft + ball).
    function motionOn(mode) {
        return motionFor(mode).on
    }

    // Selected preset id for a mode ("custom" once the user hand-edits an element).
    function motionPreset(mode) {
        return motionFor(mode).preset
    }

    // The whole motion object for a mode: migrated, default-filled (never an
    // undefined field; modes always has all 7 keys), and Capture-hard-guarded —
    // in Capture every element except "ball" is forced "off" regardless of
    // stored state.
    function motionFor(mode) {
        var mo = _rawMotion(mode)
        if (mode !== SessionMode.capture) return mo

        var keys = ["arms", "spine", "shoulders", "hips", "legs", "shaft", "shaftGrip", "ball", "hands", "plane", "synth"]
        var modes = {}
        for (var i = 0; i < keys.length; i++)
            modes[keys[i]] = (keys[i] === "ball") ? mo.modes[keys[i]] : "off"
        var out = { on: mo.on, preset: mo.preset, modes: modes }
        if (mo.traceTarget !== undefined) out.traceTarget = mo.traceTarget
        return out
    }

    // Per-element render mode ("off"|"frame"|"fan"|"trace"); inherits the
    // Capture guard via motionFor().
    function elementMode(mode, key) {
        return motionFor(mode).modes[key]
    }

    // The active trace anchor override, or "" when the element/preset uses its
    // own default anchor (the renderer owns the per-element defaults).
    function motionTraceTarget(mode) {
        var mo = motionFor(mode)
        return mo.traceTarget !== undefined ? mo.traceTarget : ""
    }

    // The coach's static marks (auto_annotations_design.md): a VIEW property, not a motion
    // element — they are reference lines, available whenever a swing is being looked at.
    // Three independent sets (§1c), each mark tagged with its set by AutoAnnotations.js:
    //   posture  — spine (tailbone → back of the neck), shoulder plane, trail leg
    //   plane    — the address shaft plane and the Hogan line
    //   headHips — face-on head circle and hip edges; the DTL butt line
    // Off by default; never in Capture.
    readonly property var coachSetKeys: ["posture", "plane", "headHips"]

    // The stored sets of a layout entry, migrating the old single `coachLines` bool: true read
    // as Head & hips + Plane (exactly what that toggle drew before Posture existed), Posture off.
    function _coachSetsOf(l) {
        var cs = l ? l.coachLineSets : undefined
        if (cs && typeof cs === "object")
            return { posture: cs.posture === true, plane: cs.plane === true, headHips: cs.headHips === true }
        var old = !!l && l.coachLines === true
        return { posture: false, plane: old, headHips: old }
    }

    // { posture, plane, headHips } for a mode — all false in Capture.
    function coachSetsFor(mode) {
        if (mode === SessionMode.capture) return { posture: false, plane: false, headHips: false }
        return _coachSetsOf(_layout(mode))
    }

    function coachSetOn(mode, set) {
        return coachSetsFor(mode)[set] === true
    }

    // Any set on — the tile's gate (and what `coachLines` meant before the sets).
    function coachLinesOn(mode) {
        var cs = coachSetsFor(mode)
        return cs.posture || cs.plane || cs.headHips
    }

    // The dragged panel sizes for a mode (see the storage shape above): a fresh object, {} when
    // none are stored. PpModeStage reads it once per change and falls back to its defaults.
    function sizesFor(mode) {
        var s = _layout(mode).sizes
        return (s && typeof s === "object") ? _cloneSizes(s) : {}
    }

    // Shim — pre-existing callers (PpViewPanel.qml, PpCameraTiles.qml, Main.qml)
    // keep working unchanged.
    function overlaysOn(mode) {
        return motionOn(mode)
    }

    // True when the CURRENT athlete is right-handed, i.e. the lead side is the
    // left side (kp5/7/9). Mirrors the ArmVizView.qml handedness precedent.
    function leadIsLeft() {
        return athleteController.currentHandedness !== "Left"
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

    // One set on or off; the others keep their stored (or migrated) state.
    function setCoachSet(mode, set, on) {
        if (coachSetKeys.indexOf(set) < 0) return
        var cs = _coachSetsOf(_layout(mode))
        cs[set] = on === true
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), undefined, cs)
    }

    // Shim for the old single toggle: every set on, or every set off.
    function setCoachLines(mode, on) {
        var v = on === true
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), undefined,
               { posture: v, plane: v, headHips: v })
    }

    function setMotionOn(mode, on) {
        var mo = _rawMotion(mode)
        mo.on = on
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), mo)
    }

    // Applies a named preset wholesale (on stays as-is; preset + modes + any
    // traceTarget come from the catalogue entry). Unknown id → no-op.
    function applyPreset(mode, id) {
        var p = _presetById(id)
        if (!p) return

        var mo = _rawMotion(mode)
        var out = { on: mo.on, preset: id, modes: _cloneModes(p.modes) }
        if (p.traceTarget !== undefined) out.traceTarget = p.traceTarget
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), out)
    }

    // Hand-edits one element's mode. Flips preset to "custom" and drops any
    // traceTarget — a custom edit reverts every element to its default anchor.
    function setElementMode(mode, key, m) {
        var mo = _rawMotion(mode)
        var modes = _cloneModes(mo.modes)
        modes[key] = m
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), { on: mo.on, preset: "custom", modes: modes })
    }

    // Shim — pre-existing callers keep working unchanged.
    function setOverlays(mode, on) {
        setMotionOn(mode, on)
    }

    // One arrangement's dragged sizes ("split" | "stage"), replacing what that arrangement had;
    // null/undefined clears it, so the arrangement takes its default again. The other
    // arrangement's sizes are kept. PpModeStage calls this once, on RELEASE — never mid-drag.
    function setSizes(mode, arrangement, sizes) {
        var all = sizesFor(mode)
        if (sizes === null || sizes === undefined) delete all[arrangement]
        else all[arrangement] = sizes
        _write(mode, enabledKeysFor(mode).slice(), arrangementFor(mode), undefined, undefined, all)
    }

    // motion, coachSets and sizes are optional — omitted by setPanel/setArrangement and kin, so
    // preserve the mode's current value (migrating an old bool `overlays` entry into a
    // full `motion` object at that moment; the old key is not written back). The entry starts
    // from the stored one, so any field this version does not name survives a write too.
    function _write(mode, panels, arrangement, motion, coachSets, sizes) {
        var m = _clone(appSettings.viewLayoutByMode)
        var l = _layout(mode)
        var mo = (motion !== undefined) ? motion : _rawMotion(mode)
        var cs = (coachSets !== undefined) ? coachSets : _coachSetsOf(l)
        var sz = (sizes !== undefined) ? sizes : l.sizes
        var e = _clone(m[_key(mode)])
        delete e.overlays
        e.panels = panels
        e.arrangement = arrangement
        e.motion = mo
        e.coachLineSets = { posture: cs.posture === true, plane: cs.plane === true, headHips: cs.headHips === true }
        e.coachLines = e.coachLineSets.posture || e.coachLineSets.plane || e.coachLineSets.headHips   // legacy readers
        if (sz && typeof sz === "object" && Object.keys(sz).length > 0) e.sizes = sz
        else delete e.sizes
        m[_key(mode)] = e
        appSettings.viewLayoutByMode = m
    }

    // A deep copy of a sizes object (two levels of maps under it, numbers at the leaves).
    function _cloneSizes(s) {
        var out = {}
        if (!s) return out
        for (var a in s) {
            var v = s[a]
            if (v && typeof v === "object") {
                var o = {}
                for (var k in v) {
                    var x = v[k]
                    if (x && typeof x === "object") { var oo = {}; for (var kk in x) oo[kk] = x[kk]; o[k] = oo }
                    else o[k] = x
                }
                out[a] = o
            } else out[a] = v
        }
        return out
    }

    function _clone(m) {
        var out = {}
        if (m) for (var key in m) out[key] = m[key]
        return out
    }

    // ── motion internals ────────────────────────────────────────────────────
    // This mode's motion object, migrated from an old bool `overlays` entry
    // (forever) and default-filled against its own preset. Never mutates the
    // stored map.
    function _rawMotion(mode) {
        var l = _layout(mode)
        if (l.motion !== undefined) return _fillMotion(mode, l.motion)
        if (l.overlays !== undefined) return _defaultMotion(mode, l.overlays)
        return _defaultMotion(mode) // no motion, no legacy overlays key: today's default is on
    }

    // The default motion object for a mode: the given on-flag (default true),
    // the mode's default preset id, and that preset's modes/traceTarget.
    function _defaultMotion(mode, on) {
        var id = _modeDefaultPresetId(mode)
        var p = _presetById(id)
        var out = { on: (on !== undefined) ? on : true, preset: id, modes: _cloneModes(p.modes) }
        if (p.traceTarget !== undefined) out.traceTarget = p.traceTarget
        return out
    }

    function _modeDefaultPresetId(mode) {
        return (mode === SessionMode.capture) ? "ballOnly" : "clean"
    }

    function _presetById(id) {
        for (var i = 0; i < _presets.length; i++)
            if (_presets[i].id === id) return _presets[i]
        return undefined
    }

    // Default-fills a possibly-partial motion object (e.g. a future version's
    // write missing a field this version doesn't know about yet) so callers
    // never see an undefined field. Gaps are filled from the object's own
    // preset when it resolves, else the mode's default preset. Never mutates
    // the input.
    function _fillMotion(mode, motion) {
        var presetId = (motion.preset !== undefined) ? motion.preset : _modeDefaultPresetId(mode)
        var p = _presetById(presetId) || _presetById(_modeDefaultPresetId(mode))
        var out = {
            on: (motion.on !== undefined) ? motion.on : true,
            preset: presetId,
            modes: _fillModes(motion.modes, p.modes)
        }
        var tt = (motion.traceTarget !== undefined) ? motion.traceTarget : p.traceTarget
        if (tt !== undefined) out.traceTarget = tt
        return out
    }

    function _fillModes(modes, fallback) {
        var keys = ["arms", "spine", "shoulders", "hips", "legs", "shaft", "shaftGrip", "ball", "hands", "plane", "synth"]
        var out = {}
        for (var i = 0; i < keys.length; i++) {
            var key = keys[i]
            out[key] = (modes && modes[key] !== undefined) ? modes[key] : fallback[key]
        }
        return out
    }

    function _cloneModes(modes) {
        var out = {}
        for (var key in modes) out[key] = modes[key]
        return out
    }
}
