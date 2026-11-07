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

// THE SPREAD FUNNEL (docs/design/variability-mark/spec.md §6.1): a group's spread drawn as a band along
// time, one session after another, oldest on the left. It narrows into a funnel as the golfer tightens
// and opens out as they loosen; the words beside it say the current grade and the trend (§7.3), so
// the band is never colour alone.
//
// SIZES. Compact (`wide: false`, tables and work-on rows): the graphic is 120 × variabilityFunnelH and
// its label sits BESIDE it, a gap of 10 away, as a Cell badge's word does (§5). Wide (`wide: true`, the
// Trend tab): the graphic fills the item's width at the same height and the label sits BENEATH it, a
// gap of 6 away. Never narrower than the provider's minimum (96 px): a narrower item is laid out at 96.
//
// GEOMETRY comes from the provider only: VariabilityGeometry.funnel(sessions, widthPx, heightPx)
// (src/Gui/variability/variability_geometry.h), in PIXELS, so there is no k and the item is never
// scaled; strokes keep their pixel widths. Paint order, bottom to top: the dashed centre line, one band
// per unbroken run, the dashed break ticks, the session markers. With fewer than 3 sessions in the
// latest unbroken run (`trendAfter3`) the provider gives no band and no tick; the centre line and the
// markers stay, and the label is the grade's badge plus "Trend after 3 sessions" in colorText3.
//
// COLOUR. The provider names a role per element; this file maps it to a token (_roleColor) and
// multiplies the element's alpha in. The band FILL is a wash (§3): on screen its alpha is also
// multiplied by Theme.variabilityWashAlpha (1 light, 3 dark), clamped to 1 (_washAlpha); the band's
// edge, the lines and the markers keep theirs. "tone" is the CURRENT session's grade tone (§3), for
// every band, old runs included (§6.1). Colour changes ease over Theme.durationSlow (0 under reduce
// motion); the geometry never animates: a new session list is a new shape at once.
//
// WORDS (§7). The label is the grade word (ConsistencyVocabulary, in the tone, colorText3 at grade 0)
// then " · " and the trend label (TrendVocabulary, arrow outside the translatable string) in
// colorText2: "Repeatable · ↑ Tightening". The technical layer adds the caller's basis string
// ("ρ 0.62 → 0.38 over 6 sessions", the grader's TrendResult::basisString) in data-sm colorText2. QML
// never formats ρ. Text tokens are the badge's: body face at fontSzBody2, data face at fontSzDataSm.
// MEASUREMENT-LIMITED (R25, as the badge, §8.6): with `measurementLimited` on the golfer layer the word is
// "Checking" (MeasurementVocabulary's grade 0) and the tone is grade 0's (colorText3); the trend word
// still follows it. The technical layer keeps the grade. The current grade itself never changes.
// THE CURRENT SESSION is the last one drawn (R25): a trailing session with no ρ never sets the tone or
// the word (the provider's currentGrade).
// REUSE (R25): a host that reuses delegates calls snapTone() from TableView.onReused / ListView.onReused,
// so a reused row shows its own tone at once.
//
// PRINT (§9.5.4), as PpVariabilityMark and PpVariabilityBadge do it. `medium` is "screen" or "print",
// PpTypeset's vocabulary, and defaults to the optional `roles` (a PpTypeset). In print:
//   - every translucent FILL (the band fill) has its alpha multiplied by
//     Theme.variabilityPrintAlphaScale, clamped to 1 (never roles.tintK, never variabilityWashAlpha);
//     strokes keep their alpha;
//   - surface → roles.paper (with roles);
//   - without `roles` the gaps are sp(), never gap(), so a printed funnel never follows Density.
//
// TYPESET HOSTS (R17, decisions.md §2). With `roles`, on screen and on paper alike, the host's sizes,
// spaces, type and inks replace Theme's: the compact width and the height are roles.u(), the gaps
// roles.g(), the words in roles.fBody and the basis in roles.fFig; the tones are roles.good /
// attention / warn / ink3, colorBorderStrong → roles.baseline and colorBorderMid → roles.grid (each at
// its opacity), text → ink, text2 → ink2, text3 → ink3, good → good. On screen the band keeps
// variabilityWashAlpha. Without `roles` the funnel reads Theme.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: funnel

    // ── Properties ───────────────────────────────────────────────────────────────────────────────
    // [{ rho, grade, breakBefore }, …] oldest first, the current session last: passed to the provider
    // as is. A session with no finite ρ is skipped there; only the latest 12 are drawn.
    property var    sessions: []
    // The trend direction token (§8.8, the grader's): "tightening" | "loosening" | "holding", or
    // "insufficient" / "" for none.
    property string trend: ""
    // The measure's name for the accessible name ("Pelvis rotation at top"). Empty: the domain word.
    property string measureName: ""
    // Technical layer: the grader's trend basis string, pre-built. Shown as given; empty shows nothing.
    property string basisText: ""
    // "golfer" | "technical" (§7.5), the badge's name for it: Item.layer is FINAL.
    property string contentLayer: "golfer"
    // false: compact, 120 wide, the label beside. true: fills the item's width, the label beneath.
    property bool   wide: false
    // The measure's own mark (§6.1's "show the badge" is the measure's badge): Dispersion for a scalar
    // measure, Overlay for a time series. Anything else warns and draws Dispersion.
    property int    variant: PpVariabilityMark.Dispersion
    // R25: the current session's reading is measurement-limited (its observed spread is no larger than
    // its propagated σ, or its source's Needle is Unsteady or Noisy): on the golfer layer the word is
    // "Checking", as the badge's. The presenter passes the current session's flag.
    property bool   measurementLimited: false
    property var    roles: null                                // a PpTypeset, optional
    property string medium: roles ? roles.medium : "screen"   // "screen" | "print"

    // ── What it shows, for hosts and tests ───────────────────────────────────────────────────────
    // From _facts, not _geom: they never depend on the width, so a wide funnel sized by its label
    // (implicitWidth) cannot loop through its own geometry. currentGrade is the last DRAWN session's.
    readonly property int    currentGrade: _facts ? _facts.currentGrade : 0
    // R25, as the badge's: the golfer layer is showing "Checking" for a measurement-limited grade.
    readonly property bool   substituted: measurementLimited && contentLayer === "golfer"
    readonly property int    shownGrade: substituted ? 0 : currentGrade
    // The word shown: the grade's, or "Checking" when substituted.
    readonly property string word: substituted ? MeasurementVocabulary.word(0) : ConsistencyVocabulary.word(currentGrade)
    readonly property bool   trendAfter3: _facts ? _facts.trendAfter3 : true
    // Sessions in the latest unbroken run: the "over N sessions" of the accessible name.
    readonly property int    sessionCount: _facts ? _facts.latestRun : 0
    readonly property color  tone: _toneOf(shownGrade)
    // The label as shown, in one string: "Repeatable · ↑ Tightening", or, with too few sessions, the
    // badge's word and "Trend after 3 sessions". The basis string is not part of it.
    readonly property string label: trendAfter3 ? word + waitText.text
                                                : wordText.text + (trendText.visible ? trendText.text : "")
    // The minimum width, the provider's own (§6.1: 96). Read once from an empty shape, so it never
    // depends on this item's width.
    readonly property real   minimumWidth: VariabilityGeometry.funnel([], 0, 0).minWidth

    // ── Internals ────────────────────────────────────────────────────────────────────────────────
    readonly property bool _print: medium === "print"
    readonly property int  _variant: variant === PpVariabilityMark.Overlay ? PpVariabilityMark.Overlay : PpVariabilityMark.Dispersion
    property bool _completed: false
    onVariantChanged: if (_completed) _checkVariant()
    Component.onCompleted: { _completed = true; _checkVariant() }
    function _checkVariant() {
        if (variant !== PpVariabilityMark.Dispersion && variant !== PpVariabilityMark.Overlay)
            console.warn("PpVariabilityFunnel: variant " + variant + " is not Dispersion or Overlay; drawn as Dispersion")
    }
    // With the host's roles (R17), on screen as on paper: its inks, sizes, spaces and type, not Theme's.
    readonly property bool _ts: roles !== null && roles !== undefined
    readonly property bool _technical: contentLayer === "technical"
    readonly property real _graphicH: _size(Theme.variabilityFunnelH)
    // Compact: 120 (§6.1; no token). Wide: the item's width; the provider lifts anything under 96.
    readonly property real _requestedW: wide ? width : _size(120)
    // One shape per instance and input (the provider never caches §6 shapes).
    // `sessions` as a plain JS array of plain objects. A list a host or createObject() hands a `var`
    // property is stored as a QVariantList, which reads back as a sequence wrapper the provider's
    // QJSValue::isArray() refuses (it would draw no sessions at all); copying it here takes any
    // array-like list. Values are passed through untouched: the provider judges them.
    readonly property var  _sessionList: {
        const s = sessions, out = []
        if (s && s.length !== undefined)
            for (let i = 0; i < s.length; ++i) {
                const e = s[i]
                out.push(e ? { rho: e.rho, grade: e.grade, breakBefore: e.breakBefore } : {})
            }
        return out
    }
    readonly property var  _geom: VariabilityGeometry.funnel(_sessionList, _requestedW, _graphicH)
    // The same sessions at the minimum width: the shape's width-free facts (current grade, the latest
    // run, trendAfter3) for the words. The provider's own answers, asked a second time.
    readonly property var  _facts: VariabilityGeometry.funnel(_sessionList, 0, _graphicH)

    // A size: roles.u() with roles, else Theme.sp().
    function _size(n) { return _ts ? roles.u(n) : Theme.sp(n) }
    // A space: roles.g() with roles (gap() on screen, u() on paper); without roles gap() on screen and
    // sp() on paper, so Density never reaches paper.
    function _space(n) { return _ts ? roles.g(n) : _print ? Theme.sp(n) : Theme.gap(n) }

    // §3: the tone of a grade (grade 0 has none: colorText3), in the host's inks with roles.
    function _toneOf(g) {
        if (_ts)
            return g >= 4 ? roles.good : g === 3 ? roles.attention : g >= 1 ? roles.warn : roles.ink3
        return g >= 4 ? Theme.colorGood : g === 3 ? Theme.colorAttention : g >= 1 ? Theme.colorWarn : Theme.colorText3
    }
    // The inks the words and neutral elements use: Theme's, or with roles the host's (the neutrals are
    // its chart furniture, each at its opacity).
    readonly property color _neutralStrong: _ts ? Qt.alpha(roles.baseline, roles.baseline.a * roles.baselineOpacity) : Theme.colorBorderStrong
    readonly property color _neutralMid: _ts ? Qt.alpha(roles.grid, roles.grid.a * roles.gridOpacity) : Theme.colorBorderMid
    readonly property color _text:     _ts ? roles.ink   : Theme.colorText
    readonly property color _text2:    _ts ? roles.ink2  : Theme.colorText2
    readonly property color _text3:    _ts ? roles.ink3  : Theme.colorText3
    readonly property color _surface:  _ts && _print ? roles.paper : Theme.colorSurface   // a knock-out
    readonly property color _good:     _ts ? roles.good  : Theme.colorGood
    // The tone as drawn: colour changes ease over durationSlow; the geometry never animates.
    property color _ink: tone
    Behavior on _ink { enabled: !funnel._toneSnap; ColorAnimation { duration: Theme.durationSlow } }
    // R25: a reused delegate's funnel shows its own tone at once (see REUSE above).
    property bool _toneSnap: false
    function snapTone() {
        tooFewBadge.snapTone()
        _toneSnap = true
        _ink = Qt.binding(function () { return funnel.tone })
        _toneSnap = false
    }

    // A provider role's token (variability_geometry.h, GeometryRole).
    function _roleColor(role) {
        switch (role) {
        case "tone":          return funnel._ink
        case "neutralStrong": return funnel._neutralStrong
        case "neutralMid":    return funnel._neutralMid
        case "text":          return funnel._text
        case "text2":         return funnel._text2
        case "text3":         return funnel._text3
        case "surface":       return funnel._surface
        case "good":          return funnel._good
        }
        console.warn("PpVariabilityFunnel: unknown geometry role", role, "; drawn in colorBorderStrong")
        return funnel._neutralStrong
    }
    // An element's colour: its role's token with its alpha MULTIPLIED in, so a translucent neutral
    // token keeps its own translucency (the mark's rule).
    function _inkOf(role, alpha) {
        const c = _roleColor(role)
        return Qt.rgba(c.r, c.g, c.b, c.a * alpha)
    }
    // A FILL's alpha: in print a translucent fill is lifted by variabilityPrintAlphaScale, at most 1.
    function _fillAlpha(a) { return _print && a < 1 ? Math.min(1, a * Theme.variabilityPrintAlphaScale) : a }
    // A WASH's alpha (the band fill only; spec §3): on screen × Theme.variabilityWashAlpha (1 light,
    // 3 dark), at most 1; in print the fill rule above alone. The band's edge and the markers are not washes.
    function _washAlpha(a) { return _print ? _fillAlpha(a) : Math.min(1, a * Theme.variabilityWashAlpha) }

    implicitWidth: wide ? Math.max(minimumWidth, labelRow.implicitWidth) : layout.implicitWidth
    implicitHeight: layout.implicitHeight

    // "Pelvis rotation at top: Repeatable, tightening over 6 sessions".
    Accessible.role: Accessible.Graphic
    Accessible.name: {
        const measure = measureName !== "" ? measureName : ConsistencyVocabulary.domain
        const word = funnel.word
        if (trendAfter3)
            //: PpVariabilityFunnel: the accessible name while too few sessions have been graded for a trend. %1 the measure ("Pelvis rotation at top"), %2 the grade word ("Repeatable"), %3 "Trend after 3 sessions"
            return qsTr("%1: %2. %3").arg(measure).arg(word).arg(TrendVocabulary.tooFewSessions)
        const t = TrendVocabulary.word(trend)
        if (t === "")
            //: PpVariabilityFunnel: the accessible name with no trend to show. %1 the measure ("Pelvis rotation at top"), %2 the grade word ("Repeatable")
            return qsTr("%1: %2").arg(measure).arg(word)
        //: PpVariabilityFunnel: the accessible name. %1 the measure ("Pelvis rotation at top"), %2 the grade word ("Repeatable"), %3 the trend word in lower case ("tightening"), %n the sessions in the trend's run
        return qsTr("%1: %2, %3 over %n sessions", "", sessionCount).arg(measure).arg(word).arg(t.toLocaleLowerCase())
    }

    Grid {
        id: layout
        // Compact: the graphic, then its label beside it (a Cell badge's gap of 10). Wide: the label
        // beneath (the Icon badge's gap of 6).
        columns: funnel.wide ? 1 : 2
        columnSpacing: funnel._space(10)
        rowSpacing: funnel._space(6)
        verticalItemAlignment: Grid.AlignVCenter

        Item {
            id: graphic
            objectName: "funnelGraphic"
            width: funnel._geom ? funnel._geom.width : 0
            height: funnel._geom ? funnel._geom.height : 0

            // The centre line at H/2, 1 px dashed, colorBorderStrong.
            Shape {
                objectName: "funnelShape"   // the renderer a probe reports
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                antialiasing: true
                ShapePath {
                    id: centre
                    objectName: "funnelCentre"
                    readonly property var e: funnel._geom ? funnel._geom.centre : null
                    strokeWidth: e ? e.width : 0
                    strokeColor: e ? funnel._inkOf(e.role, e.alpha) : "transparent"
                    strokeStyle: e && e.dashed ? ShapePath.DashLine : ShapePath.SolidLine
                    dashPattern: funnel._geom ? funnel._geom.dashPattern : [2, 2]
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    startX: e ? e.x1 : 0
                    startY: e ? e.y1 : 0
                    PathLine { x: centre.e ? centre.e.x2 : 0; y: centre.e ? centre.e.y2 : 0 }
                }
            }

            // One band per unbroken run: filled in the current tone at 16 % (× variabilityWashAlpha on
            // screen, _washAlpha), edged at 55 %, 1 px. An int
            // model, so a new shape of the same run count rebinds the paths rather than rebuilding them.
            Repeater {
                model: funnel._geom ? funnel._geom.bands.length : 0
                Shape {
                    id: bandShape
                    required property int index
                    readonly property var b: funnel._geom && index < funnel._geom.bands.length ? funnel._geom.bands[index] : null
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    antialiasing: true
                    ShapePath {
                        objectName: "funnelBand"
                        fillColor: bandShape.b ? funnel._inkOf(bandShape.b.fillRole, funnel._washAlpha(bandShape.b.fillAlpha)) : "transparent"
                        strokeWidth: bandShape.b ? bandShape.b.edgeWidth : 0
                        strokeColor: bandShape.b ? funnel._inkOf(bandShape.b.edgeRole, bandShape.b.edgeAlpha) : "transparent"
                        joinStyle: ShapePath.RoundJoin
                        PathPolyline { path: bandShape.b ? VariabilityGeometry.polygon(bandShape.b.points, 1) : [] }
                    }
                }
            }

            // The break ticks: one dashed path, colorBorderStrong, 1 px (they share a role and a width).
            Shape {
                anchors.fill: parent
                visible: funnel._geom ? funnel._geom.breaks.length > 0 : false
                preferredRendererType: Shape.CurveRenderer
                antialiasing: true
                ShapePath {
                    id: ticks
                    objectName: "funnelTicks"
                    readonly property var list: funnel._geom ? funnel._geom.breaks : []
                    readonly property var e: list.length ? list[0].tick : null
                    strokeWidth: e ? e.width : 0
                    strokeColor: e ? funnel._inkOf(e.role, e.alpha) : "transparent"
                    strokeStyle: e && e.dashed ? ShapePath.DashLine : ShapePath.SolidLine
                    dashPattern: funnel._geom ? funnel._geom.dashPattern : [2, 2]
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    PathMultiline {
                        paths: {
                            const out = []
                            for (const b of ticks.list)
                                out.push([Qt.point(b.tick.x1, b.tick.y1), Qt.point(b.tick.x2, b.tick.y2)])
                            return out
                        }
                    }
                }
            }

            // The session markers on the centre line: r 1.6 in colorText2; the current one r 2.2 in the tone.
            Repeater {
                model: funnel._geom ? funnel._geom.sessions.length : 0
                Shape {
                    id: markerShape
                    required property int index
                    readonly property var m: funnel._geom && index < funnel._geom.sessions.length
                                             ? funnel._geom.sessions[index].marker : null
                    readonly property bool current: funnel._geom && index < funnel._geom.sessions.length
                                                    ? funnel._geom.sessions[index].current : false
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    antialiasing: true
                    ShapePath {
                        objectName: markerShape.current ? "funnelCurrentMarker" : "funnelMarker"
                        strokeWidth: -1
                        strokeColor: "transparent"
                        fillColor: markerShape.m ? funnel._inkOf(markerShape.m.role, funnel._fillAlpha(markerShape.m.alpha)) : "transparent"
                        PathAngleArc {
                            centerX: markerShape.m ? markerShape.m.x : 0
                            centerY: markerShape.m ? markerShape.m.y : 0
                            radiusX: markerShape.m ? markerShape.m.r : 0
                            radiusY: markerShape.m ? markerShape.m.r : 0
                            startAngle: 0; sweepAngle: 360
                        }
                    }
                }
            }
        }

        Row {
            id: labelRow
            objectName: "funnelLabel"
            spacing: 0

            // Too few sessions for a trend (§6.1): the grade's badge stands in for the word.
            PpVariabilityBadge {
                id: tooFewBadge
                objectName: "funnelBadge"
                visible: funnel.trendAfter3
                anchors.verticalCenter: parent.verticalCenter
                // R25: the funnel's own accessible name already says the grade, so its mark is not read.
                accessibleIgnored: true
                grade: funnel.currentGrade
                measurementLimited: funnel.measurementLimited   // the badge substitutes "Checking" itself
                variant: funnel._variant
                contentLayer: funnel.contentLayer
                roles: funnel.roles
                medium: funnel.medium
            }
            PpCaption {
                id: wordText
                objectName: "funnelWord"
                visible: !funnel.trendAfter3
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideNone
                Accessible.ignored: true   // the funnel's accessible name says it
                text: funnel.word
                color: funnel._ink   // the tone; grade 0's (and "Checking"'s) is colorText3 (§3, §4.7)
                // Body 12 (PpCaption's own face); with roles, the host's body role.
                font.family:    funnel._ts ? funnel.roles.fBody.family : Theme.fontBody
                font.pixelSize: funnel._ts ? funnel.roles.fBody.pixelSize : Theme.fontSzBody2
                font.weight:    funnel._ts ? funnel.roles.fBody.weight : Theme.fontBodyWeight
            }
            PpCaption {
                id: trendText
                objectName: "funnelTrend"
                readonly property string trendLabel: TrendVocabulary.label(funnel.trend)
                visible: !funnel.trendAfter3 && trendLabel !== ""
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideNone
                Accessible.ignored: true   // the funnel's accessible name says it
                // The separator is punctuation, not a word: outside the translatable strings, as the
                // badge's and the arrow glyphs are (§7).
                text: " · " + trendLabel
                color: funnel._text2
                // Body 12 (PpCaption's own face); with roles, the host's body role.
                font.family:    funnel._ts ? funnel.roles.fBody.family : Theme.fontBody
                font.pixelSize: funnel._ts ? funnel.roles.fBody.pixelSize : Theme.fontSzBody2
                font.weight:    funnel._ts ? funnel.roles.fBody.weight : Theme.fontBodyWeight
            }
            PpCaption {
                id: waitText
                objectName: "funnelTrendAfter3"
                visible: funnel.trendAfter3
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideNone
                Accessible.ignored: true   // the funnel's accessible name says it
                text: " · " + TrendVocabulary.tooFewSessions
                color: funnel._text3   // colorText3 (§6.1), or the page's ink3
                // Body 12 (PpCaption's own face); with roles, the host's body role.
                font.family:    funnel._ts ? funnel.roles.fBody.family : Theme.fontBody
                font.pixelSize: funnel._ts ? funnel.roles.fBody.pixelSize : Theme.fontSzBody2
                font.weight:    funnel._ts ? funnel.roles.fBody.weight : Theme.fontBodyWeight
            }
            // The technical layer's basis string, in the data face (the badge's basis: no text component
            // sets data-sm, so this is the badge's own Text, token for token); with roles, the host's
            // figure role.
            Text {
                objectName: "funnelBasis"
                visible: funnel._technical && funnel.basisText !== ""
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
                text: visible ? " · " + funnel.basisText : ""
                font: funnel._ts ? funnel.roles.fFig
                                 : Qt.font({ family: Theme.fontData, pixelSize: Theme.fontSzDataSm, letterSpacing: Theme.trackingData })
                color: funnel._text2
            }
        }
    }
}
