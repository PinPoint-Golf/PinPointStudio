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

// PpTopoBackground
// ----------------
// Theme-reactive animated topographic-contour background.
//
// The three colour stops are plain `color` properties. Bind them to Theme
// tokens at the call site (e.g. Theme.gradientWarm / gradientWarmLit /
// gradientCool). Those tokens recompute off Theme.themeIndex, so switching the
// active aesthetic in Settings repaints the shader automatically — no extra
// wiring needed.
//
// Animation pauses when the item is hidden or the app is backgrounded, and
// respects the `animated` master switch — gate it on appSettings.reduceMotion
// at the call site (animated: !appSettings.reduceMotion).
//
// Pointer interaction (optional)
// ------------------------------
// This item paints behind screen content, so it cannot receive pointer events
// itself when something (e.g. a full-bleed Flickable) sits on top. Drive it
// from passive handlers at the SCREEN ROOT instead:
//
//   PpTopoBackground {
//       id: topo
//       anchors.fill: parent
//       colorLow:  Theme.gradientWarm
//       colorMid:  Theme.gradientWarmLit
//       colorHigh: Theme.gradientCool
//       animated:  !appSettings.reduceMotion
//       hoverPoint: hh.hovered
//           ? Qt.point(hh.point.position.x / width, hh.point.position.y / height)
//           : Qt.point(-1, -1)
//   }
//   HoverHandler { id: hh; enabled: topo.animated }        // passive: never blocks
//   TapHandler {
//       enabled: topo.animated
//       gesturePolicy: TapHandler.DragThreshold            // passive grab: buttons still work
//       onTapped: (ep) => topo.ripple(ep.position.x / topo.width,
//                                     ep.position.y / topo.height)
//   }
//
// Hover lifts the terrain under the cursor; tap/click sends a decaying ripple.
// On touch there is no hover (hh.hovered stays false) — taps still ripple.

Item {
    id: root

    // --- Palette (bind to Theme tokens at the call site) ---
    property color colorLow:  "#E6AC54"     // warm / low elevation
    property color colorMid:  "#5FA8A0"     // mid
    property color colorHigh: "#4A7BA6"     // cool / high elevation
    property color backgroundColor: "transparent"
    property bool  fillBackground: false    // false = overlay; true = paint bg

    // --- Look & motion (subtle by default; override per call site if needed) ---
    property real levels: 9.0         // number of contour bands (6–16 sensible)
    property real lineWidth: 1.6      // line thickness in screen px (AA)
    property real noiseScale: 3.2     // spatial frequency of the terrain
    property real intensity: 0.2      // line opacity 0..1 (kept low = calm)
    property real speed: 0.85         // animation rate multiplier (slow drift)
    property bool animated: true      // master on/off (gate on reduced-motion)

    // --- Interaction tuning (UV / screen-fraction units) ---
    property real hoverLift:     0.28 // height of the lift under the cursor
    property real hoverRadius:   0.18 // falloff radius of the lift
    property real rippleRadius:  1.05 // how far a ripple travels
    property real rippleWidth:   0.06 // ring thickness
    property real rippleAmp:     0.36 // ring height
    property real rippleDuration: 1.6 // seconds from tap to fully faded

    // Accent tint on the ripple ring's contour lines. Bind accentColor to
    // Theme.colorAccent at the call site; rippleTint scales the accent mix.
    property color accentColor:  colorHigh
    property real  rippleTint:   1.0

    // --- Occasional light sweep ("sparkle") ---
    // Every so often a soft band of light drifts across the terrain, flaring
    // the contour lines it crosses toward `flashColor` — brightest where the
    // lines crowd together (convergence ridges). Autonomous; no interaction
    // needed. Rides the same FrameAnimation, so it pauses with everything else
    // and honours the `animated` reduced-motion switch. Set `sparkle: false`
    // to disable just the sweep while keeping the drift/interaction.
    property bool  sparkle:       true                          // master on/off
    property color flashColor:    Qt.rgba(1.0, 0.97, 0.9, 1.0)  // warm near-white glint
    property real  flashWidth:    0.12                          // band half-width (aspect-UV)
    property real  flashStrength: 1.0                           // peak strength 0..1
    property real  flashDuration: 2.2                           // seconds for one sweep
    property real  flashInterval: 7.0                           // min gap between sweeps (s)
    property real  flashJitter:   6.0                           // random extra gap (s)

    // --- Interaction input (set from handlers at the screen root) ---
    // Normalised 0..1 cursor position; (-1,-1) = no hover.
    property point hoverPoint: Qt.point(-1, -1)

    // Spawn a ripple at normalised (nx, ny). Call from a TapHandler.
    function ripple(nx, ny) {
        if (!animated) return
        _ripples.push({ x: nx, y: ny, t0: frame.elapsedTime })
        if (_ripples.length > 3) _ripples.shift()
    }

    // --- Internal eased interaction state ---
    property real _hx: 0.5
    property real _hy: 0.5
    property real _h:  0.0
    property var  _ripples: []

    // --- Internal light-sweep state ---
    property real _flashT0:   -1.0    // sweep start time; <0 = idle
    property real _flashNext:  3.0    // elapsedTime at which the next sweep begins
    property real _fdx:        1.0    // sweep direction (aspect-UV, unit)
    property real _fdy:        0.35

    function _tick() {
        var active = hoverPoint.x >= 0.0
        var tx = active ? hoverPoint.x : _hx
        var ty = active ? hoverPoint.y : _hy
        _hx += (tx - _hx) * 0.18
        _hy += (ty - _hy) * 0.18
        _h  += ((active ? 1.0 : 0.0) - _h) * 0.10
        fx.mouse = Qt.vector2d(_hx, _hy)
        fx.hover = _h

        var now = frame.elapsedTime
        var dur = rippleDuration
        _ripples = _ripples.filter(function (r) { return (now - r.t0) < dur })
        var s0 = Qt.vector3d(0, 0, -1), s1 = Qt.vector3d(0, 0, -1), s2 = Qt.vector3d(0, 0, -1)
        if (_ripples.length > 0) { var r0 = _ripples[0]; s0 = Qt.vector3d(r0.x, r0.y, (now - r0.t0) / dur) }
        if (_ripples.length > 1) { var r1 = _ripples[1]; s1 = Qt.vector3d(r1.x, r1.y, (now - r1.t0) / dur) }
        if (_ripples.length > 2) { var r2 = _ripples[2]; s2 = Qt.vector3d(r2.x, r2.y, (now - r2.t0) / dur) }
        fx.ripple0 = s0; fx.ripple1 = s1; fx.ripple2 = s2

        // --- Occasional light sweep ---
        // Idle until `now` reaches _flashNext, then march a gaussian wavefront
        // (fx.flash.z) across the whole field along a fresh direction, fading in
        // and out over flashDuration, then reschedule the next one with jitter.
        if (sparkle) {
            if (_flashT0 < 0.0 && now >= _flashNext) {
                var ang = (Math.random() - 0.5) * 1.2       // ~±34° off horizontal
                if (Math.random() < 0.5) ang += Math.PI     // sweep either way
                _fdx = Math.cos(ang)
                _fdy = Math.sin(ang)
                _flashT0 = now
            }
            if (_flashT0 >= 0.0) {
                var fp = (now - _flashT0) / Math.max(flashDuration, 0.001)
                if (fp >= 1.0) {
                    _flashT0 = -1.0
                    _flashNext = now + flashInterval + Math.random() * flashJitter
                    fx.flash = Qt.vector4d(_fdx, _fdy, 0.0, 0.0)
                } else {
                    // Project the four screen corners onto the sweep direction to
                    // get the wavefront's travel span (aspect-corrected UV space).
                    var aspect = width / Math.max(height, 1)
                    var d1 = _fdx * aspect, d2 = _fdy, d3 = _fdx * aspect + _fdy
                    var sMin = Math.min(0.0, Math.min(d1, Math.min(d2, d3)))
                    var sMax = Math.max(0.0, Math.max(d1, Math.max(d2, d3)))
                    var margin = flashWidth * 2.5
                    var sPos = (sMin - margin) + (sMax - sMin + 2.0 * margin) * fp
                    var env = Math.sin(fp * Math.PI)         // 0 -> 1 -> 0 envelope
                    fx.flash = Qt.vector4d(_fdx, _fdy, sPos, env * flashStrength)
                }
            }
        } else if (fx.flash.w !== 0.0) {
            _flashT0 = -1.0
            fx.flash = Qt.vector4d(_fdx, _fdy, 0.0, 0.0)
        }
    }

    ShaderEffect {
        id: fx
        anchors.fill: parent
        blending: true

        // Uniforms — names must match the UBO members in the shaders.
        property real time: frame.elapsedTime * root.speed * 0.07
        property size resolution: Qt.size(width, height)
        property color colorLow: root.colorLow
        property color colorMid: root.colorMid
        property color colorHigh: root.colorHigh
        property color backgroundColor: root.backgroundColor
        property real levels: root.levels
        property real lineWidth: root.lineWidth
        property real noiseScale: root.noiseScale
        property real intensity: root.intensity
        property real fillBackground: root.fillBackground ? 1.0 : 0.0

        // Interaction uniforms — `mouse`, `hover` and the ripples are written
        // imperatively each frame by _tick(); the tuning ones are static.
        property vector2d mouse: Qt.vector2d(0.5, 0.5)
        property real hover: 0.0
        property vector3d ripple0: Qt.vector3d(0, 0, -1)
        property vector3d ripple1: Qt.vector3d(0, 0, -1)
        property vector3d ripple2: Qt.vector3d(0, 0, -1)
        property real hoverLift:    root.hoverLift
        property real hoverRadius:  root.hoverRadius
        property real rippleRadius: root.rippleRadius
        property real rippleWidth:  root.rippleWidth
        property real rippleAmp:    root.rippleAmp
        property color accentColor: root.accentColor
        property real rippleTint:   root.rippleTint

        // Light-sweep uniforms — `flash` is written imperatively each frame by
        // _tick() (xy dir, z wavefront pos, w strength); the rest are static.
        property vector4d flash:    Qt.vector4d(1.0, 0.35, 0.0, 0.0)
        property color flashColor:  root.flashColor
        property real flashWidth:   root.flashWidth

        // Compiled by qt6_add_shaders (PREFIX "/shaders", no BASE) — the qrc
        // path is PREFIX + the source path relative to the repo root + ".qsb".
        vertexShader:   "qrc:/shaders/src/Shaders/topo.vert.qsb"
        fragmentShader: "qrc:/shaders/src/Shaders/topo.frag.qsb"
    }

    // FrameAnimation (Qt 6.4+) drives `time` in sync with the render loop and
    // pauses cleanly when not running, so backgrounding stops all GPU work.
    // onTriggered fires once per rendered frame — we use it to ease the
    // interaction state and advance ripples.
    FrameAnimation {
        id: frame
        running: root.animated
                 && root.visible
                 && Qt.application.state === Qt.ApplicationActive
        onTriggered: root._tick()
    }
}
