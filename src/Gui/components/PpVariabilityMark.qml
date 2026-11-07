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

// THE VARIABILITY MARK (docs/design/variability-mark/spec.md §4): how much something varies, or how
// far a reading can be trusted, as a grade drawn in geometry: 5 the least variable (or steadiest), 1
// the most, 0 no grade. The mark never draws text; its word goes beside it (PpVariabilityBadge, §5)
// and into the accessible name.
//
// VARIANTS. Dispersion (§4.4, scalar measures) and Overlay (§4.5, time series) speak the Consistency
// words; Needle (§4.6) speaks the Measurement words and is for measurement quality only (§9.4). Each
// has an Icon (square) and a Cell (80 : 24) form.
//
// TYPICALITY (§6.2): this swing compared with the golfer's usual, never graded (§8.7), always in the
// accent (never a status colour). Both variants draw from per-instance `values`, handed in by the
// caller as the C++ grader computed them; QML never computes a z or a band:
//   Typicality       { z, ghosts: [{ swingId, z }] oldest first, corridor: { loZ, hiZ } | null, band }
//                    Icon and Cell.
//   TypicalityTrace  { mean: [101], sd: [101], swing: [101], corridor: { loT, hiT } | null, band }
//                    CELL ONLY (§6.2 "No icon form"): asked for an Icon, it warns once and draws the
//                    Cell (an icon context uses Typicality with the trace's RMS z instead).
// `band` is "usual" | "aLittleOff" | "unusual" and chooses the word (TypicalityVocabulary). The corridor
// is passed to the provider only with `showCorridor`, and is then the one colorGood element: it keeps
// "usual" and "good" visibly separate (§6.2). Their shapes come from the provider's per-instance calls
// (VariabilityGeometry.typicality / typicalityTrace), rebuilt only when this mark's inputs change.
//
// GEOMETRY comes from the VariabilityGeometry provider (src/Gui/variability/variability_geometry.h),
// in design units; this file holds no geometry tables. It is turned into PIXELS here (k = size / 24)
// and the item is never scaled, so every stroke keeps the provider's pixel width at every size (§4.2,
// §11). A value with a small-size variant (a dot's radius, a stroke's width, a wedge's alpha) is
// chosen by `size <= smallMaxSize`. Drawn with Qt Quick Shapes under CurveRenderer only (§11; the S2
// spike settled it, docs/design/variability-mark/spike/README.md).
//
// WASHES (§3). The translucent washes (the Dispersion Cell's group ellipse, the Needle's wedges, the
// typicality inner zone, the trace's envelope) multiply their alpha on screen by
// Theme.variabilityWashAlpha (1 light, 3 dark), clamped to 1, in _washAlpha(). The dots, the ghosts,
// the dashed outer zone and every stroke keep the provider's alpha. The provider holds the base alphas.
// The Needle's innermost wedge is then lowered, if it must be, so the three never composite past
// Theme.variabilityWedgeCompositeMax (R25: at ×3 in dark themes they reached 91 % of the tone and the
// full-tone needle was lost in them); _wedgeAlpha().
//
// COLOUR. The tone is the grade's (§3) unless overridden; neutral elements are colorBorderStrong or
// colorBorderMid as the provider names them, and the typicality elements take the provider's other
// roles: text → colorText, text2 → colorText2, text3 → colorText3, surface → colorSurface (the ring's
// opaque fill), good → colorGood (the corridor bracket only). Dashed elements use the provider's
// dashPattern. Colour changes ease over Theme.durationSlow (0 under
// reduce motion); the geometry never animates between grades (§4.2). A host that REUSES delegates
// (reuseItems) calls snapTone() from its TableView.onReused / ListView.onReused, so a reused row shows
// its own tone at once instead of fading from the last row's (R25); a live grade change on the same
// item still eases.
//
// PRINT (§9.5.4). `medium` is "screen" or "print", PpTypeset's vocabulary, and defaults to the
// optional `roles` (a PpTypeset) as the typeset blocks' does. In print:
//   - every translucent FILL (the dots, the group ellipse, the Needle's wedges, the inner zone, the
//     ghost dots, the trace's envelope) has its alpha multiplied
//     by Theme.variabilityPrintAlphaScale, clamped to 1 (never roles.tintK, never variabilityWashAlpha);
//     strokes keep their alpha;
//   - surface → roles.paper (with roles), so a printed page never depends on a screen setting.
//
// TYPESET HOSTS (R17, decisions.md §2). With `roles`, on screen and on paper alike, the host's inks
// and sizes replace Theme's: the default size is roles.u(), the tones are roles.good / attention /
// warn / ink3 / accent, the neutral elements are the host's chart furniture (colorBorderStrong →
// roles.baseline, colorBorderMid → roles.grid, each at its opacity), and text → roles.ink, text2 →
// ink2, text3 → ink3, good → good. On screen the ring's knock-out stays colorSurface and the washes
// keep variabilityWashAlpha. Without `roles` the mark reads Theme, as it always has.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: mark

    enum Variant { Dispersion, Overlay, Needle, Typicality, TypicalityTrace }
    enum Form { Icon, Cell }

    // ── The §4.1 properties ──────────────────────────────────────────────────────────────────────
    // 0–5; out of range is clamped, with a console warning. Ignored by the typicality variants.
    property int  grade: 0
    property int  variant: PpVariabilityMark.Dispersion
    property int  form: PpVariabilityMark.Icon
    // The Icon's edge, or the Cell's height (Cell width = height × 80 / 24).
    property real size: _ts ? roles.u(Theme.variabilityIconSize) : Theme.sp(Theme.variabilityIconSize)
    // From the grade (§3); the typicality variants wear the accent. Overridable.
    property color tone: _toneOf(_grade)
    // The words: Consistency for Dispersion and Overlay, Measurement for the Needle, Typicality for the
    // typicality variants.
    property QtObject vocabulary: _typicality ? TypicalityVocabulary
                                : variant === PpVariabilityMark.Needle ? MeasurementVocabulary
                                : ConsistencyVocabulary
    // The word, from the vocabulary: the grade's, or for the typicality variants the band's
    // (values.band, §7.4).
    property string label: _typicality ? vocabulary.word(_band) : vocabulary.word(_grade)
    // The typicality variants' per-instance values (§6.2; the shapes are in the header above). (`values`,
    // not `data`: that is Item's own child list.)
    property var  values: null
    // Typicality only: draws the corridor bracket, the one colorGood element (§6.2).
    property bool showCorridor: false

    // ── Print (§9.5.4) ───────────────────────────────────────────────────────────────────────────
    property var    roles: null                                // a PpTypeset, optional
    property string medium: roles ? roles.medium : "screen"   // "screen" | "print"

    readonly property int  _grade: Math.max(0, Math.min(5, grade))
    readonly property bool _typicality: variant === PpVariabilityMark.Typicality
                                        || variant === PpVariabilityMark.TypicalityTrace
    // The form as DRAWN: TypicalityTrace has no Icon (§6.2), so it is always a Cell (and warns when
    // asked for an Icon, _checkForm()).
    readonly property bool _cell: form === PpVariabilityMark.Cell || variant === PpVariabilityMark.TypicalityTrace
    readonly property real _k: size / 24
    // The band the caller computed (C++, the grader's typicality); "" until there is one.
    readonly property string _band: values && typeof values.band === "string" ? values.band : ""
    readonly property bool _print: medium === "print"
    // With a host's roles (a typeset panel on screen, or a page on paper): its inks and sizes, not
    // Theme's (R17). The screen-only rules (the wash multiplier) and the paper-only ones (the print
    // alpha scale, the paper knock-out) still follow `medium`.
    readonly property bool _ts: roles !== null && roles !== undefined

    implicitWidth: _cell ? size * 80 / 24 : size
    implicitHeight: size

    // §4.2: "<domain>: <word> (<grade> of 5)", spelled by the vocabulary, with `label` as the word (§4.1).
    // Typicality is never graded, so its name has no grade: "Typicality: Your usual".
    Accessible.role: Accessible.Graphic
    Accessible.name: _typicality ? vocabulary.accessibleName(_band, label) : vocabulary.accessibleName(_grade, label)

    property bool _completed: false
    onGradeChanged: if (_completed) _checkGrade()
    onFormChanged: if (_completed) _checkForm()
    onVariantChanged: if (_completed) { _checkGrade(); _checkForm() }
    Component.onCompleted: { _completed = true; _checkGrade(); _checkForm() }
    function _checkGrade() {
        if (!_typicality && (grade < 0 || grade > 5))
            // The clamp spelled again, not `_grade`: onGradeChanged can run before that binding updates.
            console.warn("PpVariabilityMark: grade " + grade + " is outside 0–5; clamped to " + Math.max(0, Math.min(5, grade)))
    }
    // §6.2: "Trace on envelope … Cell only. No icon form." An Icon request is a caller's mistake (an
    // icon context uses Typicality with the trace's RMS z); it is said once, and the Cell is drawn.
    function _checkForm() {
        if (variant === PpVariabilityMark.TypicalityTrace && form === PpVariabilityMark.Icon)
            console.warn("PpVariabilityMark: TypicalityTrace has no Icon form (spec §6.2); drawn as a Cell. "
                         + "In an icon context use Typicality with the trace's RMS z")
    }

    // §3: the tone of a grade, in the host's inks when it passes roles. Typicality is never graded and
    // wears the accent.
    function _toneOf(g) {
        if (_ts)
            return _typicality ? roles.accent : g >= 4 ? roles.good : g === 3 ? roles.attention : g >= 1 ? roles.warn : roles.ink3
        if (_typicality)
            return Theme.colorAccent
        return g >= 4 ? Theme.colorGood : g === 3 ? Theme.colorAttention : g >= 1 ? Theme.colorWarn : Theme.colorText3
    }

    // The tone as drawn: colour changes ease over durationSlow (0 under reduceMotion); the geometry
    // never animates (§4.2).
    property color _ink: tone
    Behavior on _ink { enabled: !mark._toneSnap; ColorAnimation { duration: Theme.durationSlow } }
    // R25: a reused delegate shows its new tone at once. A delegate's model properties are updated
    // BEFORE its view's `reused` signal, so by then the ease has begun; rebinding with the Behavior off
    // stops it and writes the tone. The reuse host calls this from TableView.onReused or
    // ListView.onReused (the attached signals reach only a delegate's root item, never a mark inside it).
    property bool _toneSnap: false
    function snapTone() {
        _toneSnap = true
        _ink = Qt.binding(function () { return mark.tone })
        _toneSnap = false
    }

    // ── Geometry: the provider's shape for (variant, form, grade), in design units ───────────────
    // One cached JS object per (variant, form, clamped grade), shared by every mark of that grade.
    readonly property var _geom: VariabilityGeometry.shape(variant, form, _grade)

    // ── Geometry: the typicality variants' per-instance shapes (§6.2), in design units ───────────
    // Built afresh by the provider whenever this mark's values, form or showCorridor change (never
    // cached: each instance's data is its own); null for the graded variants. The corridor goes to the
    // provider only with showCorridor, so without it there is no bracket at all.
    // Every list is copied into a plain JS array first: a list a host or createObject() hands a `var`
    // property is stored as a QVariantList, whose sequence wrapper the provider's isArray() refuses
    // (it would draw no ghosts and no trace). Values pass through untouched; the provider judges them.
    function _list(a) {
        const out = []
        if (a && a.length !== undefined)
            for (let i = 0; i < a.length; ++i) out.push(a[i])
        return out
    }
    readonly property var _typGeom: {
        if (!_typicality)
            return null
        const v = values || ({})
        const c = v.corridor
        if (variant === PpVariabilityMark.Typicality) {
            const ghosts = _list(v.ghosts).map(g => g ? { swingId: g.swingId, z: g.z } : {})
            return VariabilityGeometry.typicality(_cell ? PpVariabilityMark.Cell : PpVariabilityMark.Icon,
                                                  typeof v.z === "number" ? v.z : NaN, ghosts,
                                                  showCorridor && c ? { loZ: c.loZ, hiZ: c.hiZ } : null)
        }
        return VariabilityGeometry.typicalityTrace(_list(v.mean), _list(v.sd), _list(v.swing),
                                                   showCorridor && c ? { loT: c.loT, hiT: c.hiT } : null)
    }

    // A value with a small-size variant: the small one at size ≤ smallMaxSize px (0 = none). An element
    // with no small variant (the §6 ones) has no smallMaxSize, and takes the big one.
    function _pick(e, big, small) { return mark.size <= e.smallMaxSize ? small : big }
    // A provider role's token (variability_geometry.h, GeometryRole): Theme's, or with roles
    // the host's.
    function _roleColor(role) {
        switch (role) {
        case "tone":          return mark._ink
        case "neutralStrong": return mark._neutralStrong
        case "neutralMid":    return mark._neutralMid
        case "text":          return mark._text
        case "text2":         return mark._text2
        case "text3":         return mark._text3
        case "surface":       return mark._surface
        case "good":          return mark._good
        }
        console.warn("PpVariabilityMark: unknown geometry role", role, "; drawn in colorBorderStrong")
        return mark._neutralStrong
    }
    // An element's colour: its role's token with its alpha MULTIPLIED in, so a neutral token keeps
    // its own translucency (the borders are translucent ink) and a tone at 85 % stays 85 % of the tone.
    function _inkOf(role, alpha) {
        const c = _roleColor(role)
        return Qt.rgba(c.r, c.g, c.b, c.a * alpha)
    }
    // The neutral inks: Theme's borders, or with roles the host's chart furniture.
    readonly property color _neutralStrong: _ts ? Qt.alpha(roles.baseline, roles.baseline.a * roles.baselineOpacity) : Theme.colorBorderStrong
    readonly property color _neutralMid: _ts ? Qt.alpha(roles.grid, roles.grid.a * roles.gridOpacity) : Theme.colorBorderMid
    // The typicality elements' inks (§6.2): Theme's, or with roles the host's.
    readonly property color _text:    _ts ? roles.ink   : Theme.colorText
    readonly property color _text2:   _ts ? roles.ink2  : Theme.colorText2
    readonly property color _text3:   _ts ? roles.ink3  : Theme.colorText3
    // The ring's knock-out: the page's paper in print; on screen the panel surface, roles or not.
    readonly property color _surface: _ts && _print ? roles.paper : Theme.colorSurface
    readonly property color _good:    _ts ? roles.good  : Theme.colorGood
    // A FILL's alpha: in print a translucent fill is lifted by variabilityPrintAlphaScale, at most 1.
    function _fillAlpha(a) { return _print && a < 1 ? Math.min(1, a * Theme.variabilityPrintAlphaScale) : a }
    // A WASH's alpha (the group ellipse, the Needle's wedges, the inner zone, the envelope; spec §3): on
    // screen × Theme.variabilityWashAlpha (1 light, 3 dark), at most 1; in print the fill rule above
    // alone, so the paper never follows the screen's theme. Dots, ghosts and strokes are not washes.
    function _washAlpha(a) { return _print ? _fillAlpha(a) : Math.min(1, a * Theme.variabilityWashAlpha) }
    // A Needle wedge's alpha: its wash. The innermost (the last, painted over the wider two) is lowered,
    // never raised, so where all three overlap they composite to at most variabilityWedgeCompositeMax
    // of the tone and the full-tone needle reads over them (R25). Light themes and paper never reach it.
    function _wedgeAlpha(i) {
        const ws = _geom.wedges
        const a = _washAlpha(_pick(ws[i], ws[i].alpha, ws[i].alphaSmall))
        if (i !== ws.length - 1)
            return a
        let under = 1   // what the wider wedges leave of the ground: Π (1 − αⱼ)
        for (let j = 0; j < i; ++j)
            if (ws[j].spread > 0)
                under *= 1 - _washAlpha(_pick(ws[j], ws[j].alpha, ws[j].alphaSmall))
        const fit = under > 0 ? 1 - (1 - Theme.variabilityWedgeCompositeMax) / under : 0
        return Math.max(0, Math.min(a, fit))
    }

    // ── Drawing: one Shape per variant and form, chosen once (a table column keeps both) ─────────
    // Paint order, bottom to top (the provider's): ring, arc, line, ticks; ellipse, wedges, traces,
    // needle, dots, pivot.

    // A stroked circle or arc (the Dispersion Icon's ring, the Needle Icon's gauge). Never filled.
    component Arc: ShapePath {
        id: arcPath
        required property var mk
        property var e: null
        objectName: "variabilityArc"
        strokeWidth: e ? mk._pick(e, e.width, e.widthSmall) : 0
        strokeColor: e ? mk._inkOf(e.role, e.alpha) : "transparent"
        fillColor: "transparent"
        capStyle: ShapePath.FlatCap
        PathAngleArc {
            centerX: arcPath.e ? arcPath.e.cx * arcPath.mk._k : 0
            centerY: arcPath.e ? arcPath.e.cy * arcPath.mk._k : 0
            radiusX: arcPath.e ? arcPath.e.r * arcPath.mk._k : 0
            radiusY: arcPath.e ? arcPath.e.r * arcPath.mk._k : 0
            startAngle: arcPath.e ? arcPath.e.startAngle : 0
            sweepAngle: arcPath.e ? arcPath.e.sweepAngle : 0
        }
    }
    // A straight stroke: the target line, a baseline, the ruler, the needle (round cap).
    component Rule: ShapePath {
        id: rule
        required property var mk
        property var e: null
        objectName: "variabilityLine"
        strokeWidth: e ? mk._pick(e, e.width, e.widthSmall) : 0
        strokeColor: e ? mk._inkOf(e.role, e.alpha) : "transparent"
        fillColor: "transparent"
        capStyle: e && e.roundCap ? ShapePath.RoundCap : ShapePath.FlatCap
        startX: e ? e.x1 * mk._k : 0
        startY: e ? e.y1 * mk._k : 0
        PathLine { x: rule.e ? rule.e.x2 * rule.mk._k : 0; y: rule.e ? rule.e.y2 * rule.mk._k : 0 }
    }
    // The Needle Cell's ticks, as one path: they share a role and a width.
    component Ticks: ShapePath {
        id: ticks
        required property var mk
        readonly property var list: mk._geom.ticks
        readonly property var e: list.length ? list[0] : null
        objectName: "variabilityTicks"
        strokeWidth: e ? mk._pick(e, e.width, e.widthSmall) : 0
        strokeColor: e ? mk._inkOf(e.role, e.alpha) : "transparent"
        fillColor: "transparent"
        capStyle: ShapePath.FlatCap
        PathMultiline {
            paths: {
                const k = ticks.mk._k, out = []
                for (const t of ticks.list)
                    out.push([Qt.point(t.x1 * k, t.y1 * k), Qt.point(t.x2 * k, t.y2 * k)])
                return out
            }
        }
    }
    // A filled dot, never stroked: a group dot, the Needle's pivot. An absent one draws nothing.
    component Dot: ShapePath {
        id: dot
        required property var mk
        property var e: null
        objectName: "variabilityDot"
        readonly property real r: e ? mk._pick(e, e.r, e.rSmall) * mk._k : 0
        strokeWidth: -1
        strokeColor: "transparent"
        fillColor: e ? mk._inkOf(e.role, mk._fillAlpha(e.alpha)) : "transparent"
        PathAngleArc {
            centerX: dot.e ? dot.e.x * dot.mk._k : 0; centerY: dot.e ? dot.e.y * dot.mk._k : 0
            radiusX: dot.r; radiusY: dot.r
            startAngle: 0; sweepAngle: 360
        }
    }
    // The Dispersion Cell's group ellipse: filled, never stroked.
    component Ellipse: ShapePath {
        id: ell
        required property var mk
        readonly property var e: mk._geom.ellipse
        objectName: "variabilityEllipse"
        strokeWidth: -1
        strokeColor: "transparent"
        fillColor: e ? mk._inkOf(e.role, mk._washAlpha(e.alpha)) : "transparent"
        PathAngleArc {
            centerX: ell.e ? ell.e.cx * ell.mk._k : 0; centerY: ell.e ? ell.e.cy * ell.mk._k : 0
            radiusX: ell.e ? ell.e.rx * ell.mk._k : 0; radiusY: ell.e ? ell.e.ry * ell.mk._k : 0
            startAngle: 0; sweepAngle: 360
        }
    }
    // One Overlay trace: a polyline, round cap and join, in the tone at the form's alpha.
    component Trace: ShapePath {
        id: trace
        required property var mk
        required property int i
        readonly property var e: i < mk._geom.traces.length ? mk._geom.traces[i] : null
        objectName: "variabilityTrace"
        strokeWidth: e ? mk._pick(e, e.width, e.widthSmall) : 0
        strokeColor: e ? mk._inkOf(e.role, e.alpha) : "transparent"
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        PathPolyline {
            path: trace.e ? VariabilityGeometry.tracePath(trace.mk.variant, trace.mk.form, trace.mk._grade,
                                                          trace.i, trace.mk._k)
                          : []
        }
    }
    // One Needle wedge: a closed polygon, filled, never stroked. A wedge of spread 0 (grade 5) has no
    // area and is skipped, so antialiasing cannot leave a hairline.
    component Wedge: ShapePath {
        id: wedge
        required property var mk
        required property int i
        readonly property var e: i < mk._geom.wedges.length && mk._geom.wedges[i].spread > 0 ? mk._geom.wedges[i] : null
        objectName: "variabilityWedge"
        strokeWidth: -1
        strokeColor: "transparent"
        fillColor: e ? mk._inkOf(e.role, mk._wedgeAlpha(wedge.i)) : "transparent"
        PathPolyline {
            path: wedge.e ? VariabilityGeometry.wedgePath(wedge.mk.variant, wedge.mk.form, wedge.mk._grade,
                                                          wedge.i, wedge.mk._k)
                          : []
        }
    }

    // ── The §6.2 elements (typicality), from _typGeom ────────────────────────────────────────────
    // A zone of the Your-group visual: an ellipse (Cell) or circle (Icon), EITHER filled (the inner
    // "most swings" zone) OR stroked and dashed (the outer "nearly all" zone), as the provider says.
    component Zone: ShapePath {
        id: zone
        required property var mk
        property var e: null
        objectName: "variabilityZone"
        strokeWidth: e && !e.filled ? e.width : -1
        strokeColor: e && !e.filled ? mk._inkOf(e.role, e.alpha) : "transparent"
        strokeStyle: e && e.dashed ? ShapePath.DashLine : ShapePath.SolidLine
        dashPattern: mk._typGeom ? mk._typGeom.dashPattern : [2, 2]
        fillColor: e && e.filled ? mk._inkOf(e.role, mk._washAlpha(e.alpha)) : "transparent"
        capStyle: ShapePath.FlatCap
        PathAngleArc {
            centerX: zone.e ? zone.e.cx * zone.mk._k : 0; centerY: zone.e ? zone.e.cy * zone.mk._k : 0
            radiusX: zone.e ? zone.e.rx * zone.mk._k : 0; radiusY: zone.e ? zone.e.ry * zone.mk._k : 0
            startAngle: 0; sweepAngle: 360
        }
    }
    // This swing: a ring stroked in the tone (the accent) and FILLED opaque with its fill role
    // (colorSurface; the paper in print), so it covers the zones and ghosts beneath it.
    component Ring: ShapePath {
        id: ring
        required property var mk
        readonly property var e: mk._typGeom ? mk._typGeom.ring : null
        objectName: "variabilityRing"
        strokeWidth: e ? e.width : 0
        strokeColor: e ? mk._inkOf(e.strokeRole, e.alpha) : "transparent"
        fillColor: e ? mk._inkOf(e.fillRole, 1) : "transparent"
        PathAngleArc {
            centerX: ring.e ? ring.e.cx * ring.mk._k : 0; centerY: ring.e ? ring.e.cy * ring.mk._k : 0
            radiusX: ring.e ? ring.e.r * ring.mk._k : 0; radiusY: ring.e ? ring.e.r * ring.mk._k : 0
            startAngle: 0; sweepAngle: 360
        }
    }
    // A stroked open polyline from flat points: the chevron, the corridor bracket, the trace's mean
    // (dashed, flat cap) and this swing's trace (round cap and join). An absent one draws nothing.
    component Polyline: ShapePath {
        id: poly
        required property var mk
        property var e: null
        strokeWidth: e ? e.width : 0
        strokeColor: e ? mk._inkOf(e.role, e.alpha) : "transparent"
        strokeStyle: e && e.dashed ? ShapePath.DashLine : ShapePath.SolidLine
        dashPattern: mk._typGeom ? mk._typGeom.dashPattern : [2, 2]
        fillColor: "transparent"
        capStyle: e && e.roundCap ? ShapePath.RoundCap : ShapePath.FlatCap
        joinStyle: e && e.roundCap ? ShapePath.RoundJoin : ShapePath.MiterJoin
        PathPolyline { path: poly.e ? VariabilityGeometry.polygon(poly.e.points, poly.mk._k) : [] }
    }
    // The trace's envelope: a closed polygon, filled, never stroked.
    component Area: ShapePath {
        id: area
        required property var mk
        readonly property var e: mk._typGeom ? mk._typGeom.envelope : null
        objectName: "variabilityEnvelope"
        strokeWidth: -1
        strokeColor: "transparent"
        fillColor: e ? mk._inkOf(e.role, mk._washAlpha(e.alpha)) : "transparent"
        PathPolyline { path: area.e ? VariabilityGeometry.polygon(area.e.points, area.mk._k) : [] }
    }

    Loader {
        id: shapeLoader
        objectName: "variabilityShapeLoader"
        anchors.fill: parent
        sourceComponent: mark.variant === PpVariabilityMark.Dispersion ? (mark._cell ? dispersionCell : dispersionIcon)
                       : mark.variant === PpVariabilityMark.Overlay    ? (mark._cell ? overlayCell : overlayIcon)
                       : mark.variant === PpVariabilityMark.Needle     ? (mark._cell ? needleCell : needleIcon)
                       : mark.variant === PpVariabilityMark.Typicality ? (mark._cell ? typicalityCell : typicalityIcon)
                       : mark.variant === PpVariabilityMark.TypicalityTrace ? traceCell   // Cell only (§6.2)
                       : null
    }
    // §4.4 Icon: ring, three dots.
    Component {
        id: dispersionIcon
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Arc { mk: mark; e: mark._geom.ring }
            Dot { mk: mark; e: mark._geom.dots[0] ?? null }
            Dot { mk: mark; e: mark._geom.dots[1] ?? null }
            Dot { mk: mark; e: mark._geom.dots[2] ?? null }
        }
    }
    // §4.4 Cell: target line, group ellipse, seven dots.
    Component {
        id: dispersionCell
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Rule { mk: mark; e: mark._geom.line }
            Ellipse { mk: mark }
            Dot { mk: mark; e: mark._geom.dots[0] ?? null }
            Dot { mk: mark; e: mark._geom.dots[1] ?? null }
            Dot { mk: mark; e: mark._geom.dots[2] ?? null }
            Dot { mk: mark; e: mark._geom.dots[3] ?? null }
            Dot { mk: mark; e: mark._geom.dots[4] ?? null }
            Dot { mk: mark; e: mark._geom.dots[5] ?? null }
            Dot { mk: mark; e: mark._geom.dots[6] ?? null }
        }
    }
    // §4.5 Icon: baseline, three traces.
    Component {
        id: overlayIcon
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Rule { mk: mark; e: mark._geom.line }
            Trace { mk: mark; i: 0 }
            Trace { mk: mark; i: 1 }
            Trace { mk: mark; i: 2 }
        }
    }
    // §4.5 Cell: baseline, five traces.
    Component {
        id: overlayCell
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Rule { mk: mark; e: mark._geom.line }
            Trace { mk: mark; i: 0 }
            Trace { mk: mark; i: 1 }
            Trace { mk: mark; i: 2 }
            Trace { mk: mark; i: 3 }
            Trace { mk: mark; i: 4 }
        }
    }
    // §4.6 Icon: gauge arc, three sectors (widest first), needle, pivot.
    Component {
        id: needleIcon
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Arc { mk: mark; e: mark._geom.arc }
            Wedge { mk: mark; i: 0 }
            Wedge { mk: mark; i: 1 }
            Wedge { mk: mark; i: 2 }
            Rule { mk: mark; e: mark._geom.needle; objectName: "variabilityNeedle" }
            Dot { mk: mark; e: mark._geom.pivot; objectName: "variabilityPivot" }
        }
    }
    // §4.6 Cell: ruler and ticks, three triangles (widest first), needle, pivot.
    Component {
        id: needleCell
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Rule { mk: mark; e: mark._geom.line }
            Ticks { mk: mark }
            Wedge { mk: mark; i: 0 }
            Wedge { mk: mark; i: 1 }
            Wedge { mk: mark; i: 2 }
            Rule { mk: mark; e: mark._geom.needle; objectName: "variabilityNeedle" }
            Dot { mk: mark; e: mark._geom.pivot; objectName: "variabilityPivot" }
        }
    }
    // §6.2 Your group, Cell: corridor, outer zone, inner zone, up to 12 ghosts, ring, centre dot,
    // chevron (the provider's paint order). The ghost slots are fixed, as the Dispersion Cell's dots:
    // an absent ghost draws nothing.
    Component {
        id: typicalityCell
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Polyline { mk: mark; e: mark._typGeom ? mark._typGeom.corridor : null; objectName: "variabilityCorridor" }
            Zone { mk: mark; e: mark._typGeom ? mark._typGeom.outer : null; objectName: "variabilityZoneOuter" }
            Zone { mk: mark; e: mark._typGeom ? mark._typGeom.inner : null; objectName: "variabilityZoneInner" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[0] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[1] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[2] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[3] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[4] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[5] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[6] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[7] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[8] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[9] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[10] ?? null : null; objectName: "variabilityGhost" }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.ghosts[11] ?? null : null; objectName: "variabilityGhost" }
            Ring { mk: mark }
            Dot { mk: mark; e: mark._typGeom ? mark._typGeom.centre : null; objectName: "variabilityCentre" }
            Polyline { mk: mark; e: mark._typGeom ? mark._typGeom.chevron : null; objectName: "variabilityChevron" }
        }
    }
    // §6.2 Your group, Icon: corridor, outer circle, inner disc, ring (no ghosts, centre or chevron).
    Component {
        id: typicalityIcon
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Polyline { mk: mark; e: mark._typGeom ? mark._typGeom.corridor : null; objectName: "variabilityCorridor" }
            Zone { mk: mark; e: mark._typGeom ? mark._typGeom.outer : null; objectName: "variabilityZoneOuter" }
            Zone { mk: mark; e: mark._typGeom ? mark._typGeom.inner : null; objectName: "variabilityZoneInner" }
            Ring { mk: mark }
        }
    }
    // §6.2 Trace on envelope (Cell only): corridor, baseline, envelope, mean, this swing.
    Component {
        id: traceCell
        Shape {
            objectName: "variabilityShape"
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true
            Polyline { mk: mark; e: mark._typGeom ? mark._typGeom.corridor : null; objectName: "variabilityCorridor" }
            Rule { mk: mark; e: mark._typGeom ? mark._typGeom.baseline : null }
            Area { mk: mark }
            Polyline { mk: mark; e: mark._typGeom ? mark._typGeom.mean : null; objectName: "variabilityMean" }
            Polyline { mk: mark; e: mark._typGeom ? mark._typGeom.swing : null; objectName: "variabilitySwing" }
        }
    }
}
