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

#pragma once

// THE VARIABILITY GEOMETRY PROVIDERS (docs/design/variability-mark/spec.md §4.3 – §4.7, §6, §11): the
// shapes PpVariabilityMark and PpVariabilityFunnel draw, as plain data. The QML never computes
// geometry; it asks a provider and turns design units into pixels.
//   - CanonicalVariabilityGeometry: the graded variants (§4), one shape per (variant, form, grade).
//   - DataVariabilityGeometry: per-instance data (§6): the spread funnel and the two typicality
//     variants. Further down; its own notes are there.
// Both live in this one file pair: the typicality shapes ride in VariabilityShape (so the §11 seam
// answers for them), and the QML singleton serves both.
//
// UNITS. Every position, radius, half-angle and half-width is in DESIGN UNITS (§4.3: Icon 24 × 24,
// Cell 80 × 24; screen coordinates, y down). The mark multiplies them by k = size / 24. Stroke widths
// are the exception: they are PIXELS, constant at every size (§4.2), and are never multiplied by k.
//
// SIZE-DEPENDENT VALUES. §4 gives a few values that change at small sizes (a dot's radius at size ≤ 12,
// a stroke's width or a wedge's alpha at size ≤ 16). The provider stays grade-only by carrying both:
// the element has `smallMaxSize` (px; 0 when §4 gives no small variant) and the mark picks
// `size <= smallMaxSize ? xSmall : x`. When there is no small variant, xSmall == x, so that one test
// is always safe to write.
//
// ANGLES are degrees, 0° along +x, positive CLOCKWISE on screen (y down): Qt's PathAngleArc convention,
// so `startAngle` / `sweepAngle` bind straight through. 270° points up.
//
// PAINT ORDER, bottom to top, for every variant: ring, arc, line, ticks (the neutral elements); then
// ellipse, wedges (in list order, widest first), traces, needle, dots, pivot.
//
// GRADE 0 (§4.7) carries the neutral element alone: the ring or target line (Dispersion), the
// baseline (Overlay), the arc, or the ruler with its ticks (Needle). Grades outside 0–5 clamp; the
// mark is the one that warns (§4.1), so the provider is silent.

#include <QJsonObject>
#include <QJSValue>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace pinpoint::variability {

// The variants and forms, in PpVariabilityMark's `enum Variant` / `enum Form` order, so a QML enum
// value IS one of these.
enum class MarkVariant { Dispersion = 0, Overlay = 1, Needle = 2, Typicality = 3, TypicalityTrace = 4 };
enum class MarkForm    { Icon = 0, Cell = 1 };

inline constexpr int kMarkVariantCount = 5;
inline constexpr int kMarkFormCount    = 2;
inline constexpr int kMarkGradeMax     = 5;

// Which colour token an element takes (§3, §4, §6). The QML maps it, then applies `alpha`:
//   NeutralStrong → colorBorderStrong   NeutralMid → colorBorderMid
//   Tone          → the mark's tone (by grade, or overridden; the typicality variants default to
//                   colorAccent, §4.1), or for the funnel the CURRENT session's grade tone (§6.1)
//   Text → colorText   Text2 → colorText2   Text3 → colorText3   Surface → colorSurface   Good → colorGood
// The canonical §4 shapes use only the first three; the rest are the §6 companions'.
enum class GeometryRole { NeutralStrong, NeutralMid, Tone, Text, Text2, Text3, Surface, Good };

// DASHES. Every dashed element in §4 – §6 is a 1 px stroke (the funnel's centre line and break ticks,
// the typicality outer zone, the trace's mean). The spec gives no pattern; this is the one QML binds:
// `dashPattern: [kDashOn, kDashOff]`, in units of the stroke width (Qt's ShapePath convention), so
// 2 px on, 2 px off at 1 px. Every data object carries it as `dashPattern` too.
inline constexpr double kDashOn  = 2;
inline constexpr double kDashOff = 2;

// A ring or arc, stroked, never filled: the Dispersion Icon's ring (sweep 360) and the Needle Icon's
// gauge (the upper half-circle, startAngle 180, sweep 180).
struct VariabilityArc {
    GeometryRole role = GeometryRole::NeutralStrong;
    double cx = 0, cy = 0, r = 0;
    double startAngle = 0, sweepAngle = 360;
    double width = 1, widthSmall = 1, smallMaxSize = 0;   // px
    double alpha = 1;
};

// A filled dot, never stroked: the group dots, the Needle's pivot.
struct VariabilityDot {
    GeometryRole role = GeometryRole::Tone;
    double x = 0, y = 0;
    double r = 0, rSmall = 0, smallMaxSize = 0;           // rSmall applies at size ≤ smallMaxSize px
    double alpha = 1;
};

// A filled ellipse, never stroked: the Dispersion Cell's group ellipse.
struct VariabilityEllipse {
    GeometryRole role = GeometryRole::Tone;
    double cx = 0, cy = 0, rx = 0, ry = 0;
    double alpha = 1;
};

// A straight stroke: the target line, the baselines, the ruler and its ticks, the needle.
struct VariabilityLine {
    GeometryRole role = GeometryRole::NeutralStrong;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    double width = 1, widthSmall = 1, smallMaxSize = 0;   // px
    double alpha = 1;
    bool   roundCap = false;                              // round for the needle; flat for neutrals
    bool   dashed   = false;                              // §6 only: kDashOn / kDashOff
};

// An Overlay trace: a stroked polyline, round cap and join (§4.5). `centre` and `height` are the c and
// h of the §4.5 formula that generated `points`; descriptive only, the mark draws `points`.
struct VariabilityPolyline {
    GeometryRole role = GeometryRole::Tone;
    std::vector<QPointF> points;
    double width = 1, widthSmall = 1, smallMaxSize = 0;   // px
    double alpha = 1;
    bool   roundCap = true;                               // and round join
    double centre = 0, height = 0;
    bool   dashed   = false;                              // §6 only: kDashOn / kDashOff
};

// A Needle wedge (§4.6): one of the three stacked translucent shapes, filled, never stroked.
//
// REPRESENTATION: a CLOSED POLYGON for both forms, `points` = apex first and apex again last. The Cell
// wedge is a triangle by definition. The Icon wedge is a sector on the pivot (12, 20), radius 11,
// pointing up: the polygon is the pivot, then the arc from 270° − halfAngle to 270° + halfAngle
// sampled at equal steps with every chord ≤ 1 design unit (≤ 14 chords at a = 34°), then the pivot.
// Why a polygon and not an arc description:
//   - one QML path for every wedge in both forms (a filled PathPolyline), with no per-form branch;
//   - a degenerate wedge (grade 5, half-angle or half-width 0) is still a well-formed polygon of zero
//     area, rather than a zero-sweep arc the renderer has to special-case;
//   - the error is invisible: a 1-unit chord on radius 11 bows in by r(1 − cos(θ/2)) ≈ 0.011 units,
//     0.02 px at a 40 px icon, so CurveRenderer's exact arcs would buy nothing.
// `spread` is the §4.6 table value the polygon was built from: the half-angle in degrees (Icon) or the
// half-width in design units (Cell). A wedge with spread 0 has zero area and must draw nothing; the
// mark skips it (fill transparent) so antialiasing cannot leave a hairline at grade 5.
struct VariabilityWedge {
    GeometryRole role = GeometryRole::Tone;
    std::vector<QPointF> points;
    double spread = 0;
    double alpha = 1, alphaSmall = 1, smallMaxSize = 0;   // alphaSmall applies at size ≤ smallMaxSize px
};

// ── §6.2 typicality shapes (design units, as §4) ──────────────────────────────────────────────────────

// One zone of the Your-group visual: an ellipse (Cell) or a circle (Icon, rx == ry). EITHER filled (the
// inner "most swings" zone, ±1 SD) OR stroked (the outer "nearly all" zone, ±2 SD, dashed), never both.
struct VariabilityZone {
    GeometryRole role = GeometryRole::NeutralStrong;
    double cx = 0, cy = 0, rx = 0, ry = 0;
    bool   filled = false;
    double width  = 1;        // px; stroked zones only
    bool   dashed = false;    // stroked zones only
    double alpha  = 1;
};

// This swing: a ring stroked in the tone and FILLED with `fillRole`, so it covers what lies beneath.
struct VariabilityRing {
    double cx = 0, cy = 0, r = 0;
    GeometryRole strokeRole = GeometryRole::Tone;
    double width = 1.75;                                  // px
    GeometryRole fillRole = GeometryRole::Surface;        // opaque
    double alpha = 1;                                     // of the stroke
};

// One recent swing behind this one (a ghost dot). `dot.y` is the deterministic jitter (ghostJitter()),
// which carries no meaning (§6.2).
struct VariabilityGhost {
    QString swingId;
    double  z = 0;
    bool    clamped = false;   // x held at the ring's off-scale stops (x 4 / 76) because |z| > 2.6
    VariabilityDot dot;
};

// The corridor bracket (§6.2 `showCorridor`): ONE open polyline, `points` =
//   (x1, y − tick), (x1, y), (x2, y), (x2, y − tick)
// a ⌴ opening up toward the visual. An end clamped to the visual's edge has NO tick (a tick there
// would claim an end that is not on the scale), so `points` has 2, 3 or 4 points.
struct VariabilityBracket {
    GeometryRole role = GeometryRole::Good;
    double lo = 0, hi = 0;                    // as given (lo ≤ hi after a swap): z, or normalised time
    double x1 = 0, x2 = 0, y = 0, tick = 0;   // design units
    bool   loClamped = false, hiClamped = false;
    std::vector<QPointF> points;
    double width = 1.5;                       // px
    double alpha = 1;
};

// A filled closed polygon: the trace's envelope. `points` repeats the first point last.
struct VariabilityArea {
    GeometryRole role = GeometryRole::Text;
    std::vector<QPointF> points;
    double alpha = 1;
};

// The `Typicality` variant (§6.2 "Your group"), one form.
//   Cell: outer zone, inner zone, ghosts, ring, centre dot, chevron (when clamped), corridor.
//   Icon: outer zone, inner zone, ring, corridor. §6.2 gives the Icon no ghosts, centre dot or chevron.
// PAINT ORDER, bottom to top: corridor, outer, inner, ghosts, ring, centre, chevron.
struct VariabilityTypicalityShape {
    MarkForm form = MarkForm::Cell;
    double   z = 0;              // as given; ±∞ (a zero-spread window, §8.7) clamps like any |z| > 2.6
    bool     hasSwing = false;   // false when z is NaN: no ring, centre or chevron
    bool     clamped  = false;   // the ring is held at the edge (Cell |z| > 2.6; Icon |z| > 1.62, R25)
    int      side     = 0;       // −1 left, +1 right, when clamped
    VariabilityZone outer, inner;
    std::vector<VariabilityGhost>      ghosts;    // Cell only; the input order (oldest first), at most 12
    std::optional<VariabilityRing>     ring;
    std::optional<VariabilityDot>      centre;    // Cell only
    std::optional<VariabilityPolyline> chevron;   // Cell only, when clamped
    std::optional<VariabilityBracket>  corridor;  // when a corridor was given
};

// The `TypicalityTrace` variant (§6.2 "Trace on envelope"), Cell only.
//
// X: the 101 grid points map onto x 2 … 78 (as the Overlay Cell's traces span), x_i = 2 + 76·i/100.
// Y: ONE linear scale, zero on the baseline (y 21.5), positive up. §6.2 gives no vertical scale. The
// largest positive value among the envelope (mean ± SD) and this swing maps to 17 units above the
// baseline (the Overlay Cell's peak height h, §4.5); a negative value maps BELOW the baseline on the
// same scale, never folded or offset. Only 2.5 units lie below the baseline (the cell's bottom edge
// is y 24), so when the largest negative excursion would fall past it the scale shrinks until it fits:
//     valueScale = min(17 / maxPositive, 2.5 / maxNegative)          (units per unit of value)
// For a curve that stays ≥ 0 this is exactly "the largest |value| → 17". A curve that is mostly
// negative is drawn honestly small; the spec has no layout for one (reported to Mark).
// PAINT ORDER, bottom to top: corridor, baseline, envelope, mean, swing.
struct VariabilityTraceShape {
    bool valid = false;   // false: the inputs were not three whole 101-point curves (SD ≥ 0); then
                          // only the baseline is set
    VariabilityLine baseline;
    std::optional<VariabilityArea>     envelope;   // mean + SD left → right, then mean − SD back
    std::optional<VariabilityPolyline> mean;       // 1 px dashed colorText3, flat cap
    std::optional<VariabilityPolyline> swing;      // 1.75 px tone, round cap and join
    std::optional<VariabilityBracket>  corridor;
    double valueScale = 0;      // design units per unit of value
    double zeroY      = 21.5;   // the baseline: where value 0 sits
    double maxPositive = 0, maxNegative = 0;   // the extremes the scale was taken from (≥ 0 each)
};

// Everything one mark draws. Absent single elements are nullopt; absent lists are empty.
struct VariabilityShape {
    MarkVariant variant = MarkVariant::Dispersion;
    MarkForm    form    = MarkForm::Icon;
    int         grade   = 0;                             // clamped 0–5

    std::optional<VariabilityArc>     ring;      // Dispersion Icon
    std::optional<VariabilityArc>     arc;       // Needle Icon: the gauge
    std::optional<VariabilityLine>    line;      // Dispersion Cell target line, Overlay baselines, Needle Cell ruler
    std::vector<VariabilityLine>      ticks;     // Needle Cell, left to right
    std::optional<VariabilityEllipse> ellipse;   // Dispersion Cell
    std::vector<VariabilityWedge>     wedges;    // Needle: a, 0.62a, 0.3a (widest first)
    std::vector<VariabilityPolyline>  traces;    // Overlay
    std::optional<VariabilityLine>    needle;    // Needle
    std::vector<VariabilityDot>       dots;      // Dispersion
    std::optional<VariabilityDot>     pivot;     // Needle

    // The data variants (§6.2), from DataVariabilityGeometry only; the canonical provider leaves them
    // unset. Their own structs, not the slots above: §6.2's elements (dashed and stroked zones, a
    // filled ring, a bracket) have no faithful home in the §4 slots.
    std::optional<VariabilityTypicalityShape> typicality;        // Typicality
    std::optional<VariabilityTraceShape>      typicalityTrace;   // TypicalityTrace

    // True when the shape draws nothing at all (an unknown variant or form, or a variant the
    // provider does not cover).
    bool isEmpty() const;
};

// The geometry seam (spec §11). Two providers, one interface:
//   - CanonicalVariabilityGeometry (here): GRADE-ONLY. The §4 tables for Dispersion, Overlay and
//     Needle, built once, cached, shared process-wide.
//   - DataVariabilityGeometry (below): per-instance data for the typicality variants (§6.2). It is
//     constructed over ONE instance's data and answers the same call; whatever per-instance input it
//     needs goes to its constructor, not to this interface, so a surface holding an
//     IVariabilityGeometry draws either without knowing which. The funnel (§6.1) is not a mark
//     variant, so this interface cannot ask for it: it is a DataVariabilityGeometry builder only.
//
// The canonical provider returns an EMPTY shape for the typicality variants: they are data-only.
// (In QML, shape() of an unknown variant or form is one shared empty object with variant and form -1.)
class IVariabilityGeometry {
public:
    virtual ~IVariabilityGeometry() = default;

    // The geometry of one mark. `grade` clamps to 0–5. An unknown variant or form gives an empty
    // shape. The reference stays valid for the provider's lifetime.
    virtual const VariabilityShape &shape(MarkVariant variant, MarkForm form, int grade) const = 0;
};

// The §4 tables. Every shape is computed in the constructor and never changes after it, so a const
// instance is safe to read from any thread.
class CanonicalVariabilityGeometry final : public IVariabilityGeometry {
public:
    CanonicalVariabilityGeometry();

    const VariabilityShape &shape(MarkVariant variant, MarkForm form, int grade) const override;

private:
    std::array<std::array<std::array<VariabilityShape, kMarkGradeMax + 1>, kMarkFormCount>,
               kMarkVariantCount> m_shapes;
    VariabilityShape m_empty;
};

// The process-wide canonical provider: built on first call (a function-local static, so the build is
// thread-safe), the same instance for every caller after that.
std::shared_ptr<const IVariabilityGeometry> makeCanonicalVariabilityGeometry();

// ── DataVariabilityGeometry (§6, §11) ─────────────────────────────────────────────────────────────────
//
// Per-instance geometry: fed ONE instance's data, never cached across instances (two marks with the
// same data build two shapes; the data is the instance). Two kinds of use:
//   - as an IVariabilityGeometry, constructed over one Your-group input (both forms) or one trace
//     input (Cell): shape() answers for that variant and returns the empty shape for anything else;
//   - through the static builders, which the QML singleton calls per instance.
//
// UNITS. The typicality shapes are in DESIGN UNITS (Cell 80 × 24, Icon 24 × 24; × k = size / 24, as
// §4); strokes are px. The FUNNEL is in PIXELS: §6.1 is specified in px (6 px padding, 4 px gap,
// r 1.6) against the funnel's own width and height, so there is no k and nothing to scale.

// §6.1 minimum funnel width, px. Narrower requests are laid out at this width (`width` in the result).
inline constexpr double kFunnelMinWidthPx = 96;

// One session of a funnel, oldest first; the current session is the last.
struct VariabilityFunnelSession {
    double rho   = 0;       // the session's ρ (§8); not finite or negative = no reading (skipped)
    int    grade = 0;       // 0–5, clamped
    bool   breakBefore = false;   // a change of context, basis or scale since the previous session
};

// One drawn session.
struct VariabilityFunnelPoint {
    int    input = 0;          // index into the input list
    double rho = 0, halfWidth = 0, x = 0;   // halfWidth and x in px
    int    grade = 0;
    int    run = 0;            // which unbroken run, 0 = the oldest drawn
    bool   current = false;    // the current session: the last one drawn (R25)
    VariabilityDot marker;     // on the centre line: Text2 r 1.6, or Tone r 2.2 for the current one
};

// One unbroken run's band: a CLOSED polygon in px (the upper edge left → right, the lower edge back,
// the first point again), straight segments. A run that meets a break on its right ends 2 px short of
// the break, at the session's own half-width; one that starts after a break starts 2 px past it. So
// the sessions stay evenly spaced and the gap between the two bands is exactly 4 px (§6.1).
struct VariabilityFunnelBand {
    std::vector<QPointF> points;
    int    first = 0, last = 0;                 // indices into VariabilityFunnelShape::sessions
    GeometryRole fillRole = GeometryRole::Tone;
    double fillAlpha = 0.16;
    GeometryRole edgeRole = GeometryRole::Tone;
    double edgeAlpha = 0.55;
    double edgeWidth = 1;                       // px
};

// One break: the 4 px gap and the dashed tick in its middle.
struct VariabilityFunnelBreak {
    int    before = 0;              // index (into sessions) of the first session after the break
    double gapX1 = 0, gapX2 = 0;    // the gap, px; gapX2 − gapX1 = 4
    VariabilityLine tick;           // vertical, dashed, colorBorderStrong, 1 px
};

// §6.1, in px. PAINT ORDER, bottom to top: centre line, bands, break ticks, markers.
struct VariabilityFunnelShape {
    double width = 0, height = 0;   // px; width ≥ kFunnelMinWidthPx
    double requestedWidth = 0;
    int    currentGrade = 0;        // the last DRAWN session's grade, clamped (0 when none is drawn): the
                                    // band tone and the word (§6.1). A trailing session with no reading
                                    // never sets it (R25).
    bool   currentHasReading = false;   // the input's last session has a reading (is the one drawn current)
    bool   trendAfter3 = true;      // < TREND_MIN_SESSIONS (3) in the latest unbroken run: no bands, no
                                    // ticks; the QML shows the badge plus "Trend after 3 sessions"
    int    latestRun = 0;           // sessions in the latest unbroken run
    int    rolledOff = 0;           // older sessions with a reading not drawn (only 12 are)
    VariabilityLine centre;         // y = H/2, x 0 … W, dashed, colorBorderStrong, 1 px
    std::vector<VariabilityFunnelPoint>  sessions;   // at most 12, oldest first
    std::vector<VariabilityFunnelBand>   bands;      // one per unbroken run, oldest first
    std::vector<VariabilityFunnelBreak>  breaks;
};

// A recent swing for the Your-group Cell.
struct VariabilityGhostInput {
    QString swingId;
    double  z = 0;
};

// A corridor for `showCorridor`: in z units (Your group), or in normalised time 0 … 1 over the trace's
// window (TypicalityTrace; see VariabilityTraceShape). lo > hi is swapped; a non-finite end is no
// corridor.
struct VariabilityCorridor {
    double lo = 0, hi = 0;
};

// The ghost-dot jitter (§6.2): deterministic from the swing id, carrying no meaning.
//   hash   = FNV-1a 32-bit over the id's UTF-8 bytes (offset basis 2166136261, prime 16777619),
//            finished with murmur3's fmix32 avalanche (h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13;
//            h *= 0xc2b2ae35; h ^= h >> 16), so ids differing only in their last character still
//            spread; a fixed function of the bytes, never qHash (seeded per process)
//   jitter = (hash / 2³² × 2 − 1) × 4, in [−4, 4) design units; the dot sits at y 12 + jitter.
quint32 ghostJitterHash(const QString &swingId);
double  ghostJitter(const QString &swingId);

class DataVariabilityGeometry final : public IVariabilityGeometry {
public:
    // A Your-group instance: shape(Typicality, Icon | Cell, ·) answers; the grade is ignored (§4.1).
    DataVariabilityGeometry(double z, const std::vector<VariabilityGhostInput> &ghosts,
                            const std::optional<VariabilityCorridor> &corridor = std::nullopt);
    // A trace instance: shape(TypicalityTrace, Cell, ·) answers. No Icon form (§6.2: icon contexts use
    // the Your-group icon with the trace's RMS z, i.e. the other constructor).
    DataVariabilityGeometry(const std::vector<double> &mean, const std::vector<double> &sd,
                            const std::vector<double> &swing,
                            const std::optional<VariabilityCorridor> &corridor = std::nullopt);

    const VariabilityShape &shape(MarkVariant variant, MarkForm form, int grade) const override;

    // §6.1. `sessions` oldest first, the current one last. Sessions with no reading are skipped (a
    // skipped session's break carries to the next one), then the latest 12 are kept. Positions: n = 1
    // sits at W − 6 (where the current session always sits); otherwise x_i = 6 + i·(W − 12)/(n − 1).
    static VariabilityFunnelShape funnel(const std::vector<VariabilityFunnelSession> &sessions,
                                         double widthPx, double heightPx);

    // §6.2 Your group, one form. `ghosts` oldest first; the latest 12 are kept (Icon: none drawn).
    static VariabilityTypicalityShape typicality(MarkForm form, double z,
                                                 const std::vector<VariabilityGhostInput> &ghosts,
                                                 const std::optional<VariabilityCorridor> &corridor);

    // §6.2 Trace on envelope. Each curve on the kTraceGridPoints (101) grid.
    static VariabilityTraceShape typicalityTrace(const std::vector<double> &mean, const std::vector<double> &sd,
                                                 const std::vector<double> &swing,
                                                 const std::optional<VariabilityCorridor> &corridor);

private:
    MarkVariant      m_variant;
    VariabilityShape m_icon, m_cell, m_empty;
};

// 0–5, as every reader clamps.
int clampGrade(int grade);

// The role's token, as QML reads it: "neutralStrong" | "neutralMid" | "tone".
QString geometryRoleToken(GeometryRole role);

// One shape as the JSON object QML is handed. Keys mirror PpVariabilityMark's first-cut
// `_canonical()` (ring, line, ellipse, dots, traces) and add arc, ticks, wedges, needle and pivot.
// Absent single elements are null; absent lists are []. Points are FLAT arrays, design units:
//   { variant, form, grade,
//     ring:    null | { role, cx, cy, r, startAngle, sweepAngle, width, widthSmall, smallMaxSize, alpha },
//     arc:     null | (as ring),
//     line:    null | { role, x1, y1, x2, y2, width, widthSmall, smallMaxSize, alpha, roundCap },
//     ticks:   [ (as line) ],
//     ellipse: null | { role, cx, cy, rx, ry, alpha },
//     wedges:  [ { role, points: [x0, y0, x1, y1, …], spread, alpha, alphaSmall, smallMaxSize } ],
//     traces:  [ { role, points: [x0, y0, …], width, widthSmall, smallMaxSize, alpha, roundCap,
//                  centre, height } ],
//     needle:  null | (as line),
//     dots:    [ { role, x, y, r, rSmall, smallMaxSize, alpha } ],
//     pivot:   null | (as a dot),
//     typicality:      null | (toJson(VariabilityTypicalityShape), below),
//     typicalityTrace: null | (toJson(VariabilityTraceShape), below) }
// Lines and traces also carry `dashed` (always false in the canonical shapes).
QJsonObject toJson(const VariabilityShape &shape);

// The §6 shapes as the JSON objects QML is handed. Points are FLAT arrays [x0, y0, x1, y1, …]; roles
// are geometryRoleToken() strings. Element objects are those of toJson(VariabilityShape) above, plus:
//   line      += dashed
//   trace     += dashed                              (a polyline: the chevron, the trace's mean and swing)
//   zone      = { role, cx, cy, rx, ry, filled, width, dashed, alpha }
//   ringFill  = { cx, cy, r, strokeRole, width, fillRole, alpha }      (this swing's ring)
//   ghost     = { swingId, z, clamped, x, y, r, rSmall, smallMaxSize (0: no small variant), role, alpha }
//   bracket   = { role, lo, hi, x1, x2, y, tick, loClamped, hiClamped, points, width, alpha }
//   area      = { role, points, alpha }
//
// toJson(VariabilityTypicalityShape), design units:
//   { kind: "typicality", form, z, hasSwing, clamped, side, dashPattern: [2, 2],
//     outer: zone, inner: zone, ghosts: [ghost], ring: null | ringFill, centre: null | dot,
//     chevron: null | trace, corridor: null | bracket }
// toJson(VariabilityTraceShape), design units:
//   { kind: "typicalityTrace", form: 1, valid, dashPattern: [2, 2], baseline: line,
//     envelope: null | area, mean: null | trace, swing: null | trace, corridor: null | bracket,
//     valueScale, zeroY, maxPositive, maxNegative }
// toJson(VariabilityFunnelShape), PIXELS:
//   { kind: "funnel", width, height, requestedWidth, minWidth: 96, currentGrade, currentHasReading,
//     trendAfter3, latestRun, rolledOff, dashPattern: [2, 2], centre: line,
//     sessions: [ { input, rho, grade, halfWidth, x, run, current, marker: dot } ],
//     bands:    [ { points, first, last, fillRole, fillAlpha, edgeRole, edgeAlpha, edgeWidth } ],
//     breaks:   [ { before, gapX1, gapX2, tick: line } ] }
// toJson(VariabilityShape) carries the first two under `typicality` / `typicalityTrace` (null for
// the canonical shapes).
QJsonObject toJson(const VariabilityTypicalityShape &shape);
QJsonObject toJson(const VariabilityTraceShape &shape);
QJsonObject toJson(const VariabilityFunnelShape &shape);

// `VariabilityGeometry` in QML: the canonical provider, per engine, and the per-instance builders.
//
//   shape(variant, form, grade)   the toJson() object above as a plain JS object, built ONCE per
//                                 (variant, form, clamped grade) and cached here, so every mark of a
//                                 grade shares one object (−1 and 0 share one; so do 7 and 5).
//                                 Treat it as read-only. Built through QJsonObject, never a
//                                 QVariantMap (src/Gui/shot/qml_payload.h says why).
//
//   tracePath(variant, form, grade, index, k)
//   wedgePath(variant, form, grade, index, k)
//                                 one trace's or wedge's points, already × k, as a QPolygonF.
//
// WHY POINTS ARE FLAT ARRAYS, AND WHY THE TWO PATH CALLS. PathPolyline.path takes a QPolygonF, a
// QList<QPointF> or a JS array of Qt.point(); it does NOT take [x, y, …] or [{x, y}, …] (each element
// must convert to a QPointF). Every route from the cached object therefore needs the mark to scale and
// convert, and the flat array is the cheapest one to do it from: one JS array of numbers per trace, no
// object per point (`for (i = 0; i < p.length; i += 2) out.push(Qt.point(p[i] * k, p[i + 1] * k))`).
// The CHEAPEST feed of all is the path call: `path: VariabilityGeometry.tracePath(v, f, g, i, k)`
// hands PathPolyline a QPolygonF it takes as is, with no JS object per point and no JS loop (77
// points for a Cell trace, scaled in C++ per call; nothing is cached, nothing needs to be).
//
// THE PER-INSTANCE CALLS (§6): built afresh on every call, NEVER cached (each instance's data is its
// own; a binding re-evaluates only when that instance's inputs change). Each returns the toJson()
// object above as a plain JS object, built through QJsonObject; undefined with no engine.
// Every list argument is ARRAY-LIKE, read element by element: a JS Array, a list held in a QML `var`
// property that came from C++ or createObject (a QVariantList, for which isArray() is false), or any
// object with a numeric `length`. Anything else is an empty list.
//
//   funnel(sessions, widthPx, heightPx)
//       sessions: [ { rho: number, grade: int, breakBefore: bool }, … ] oldest first, current last.
//       A missing or non-numeric rho is no reading. Result in PIXELS.
//   typicality(form, z, ghosts, corridor)
//       form: 0 Icon | 1 Cell (any other value is Cell); z: number (±Infinity allowed, NaN = no swing);
//       ghosts: [ { swingId: string (a number is stringified), z: number }, … ] oldest first, or
//       null; corridor: { loZ, hiZ } or null/undefined. Result in design units.
//   typicalityTrace(mean, sd, swing, corridor)
//       mean, sd, swing: 101 numbers each; corridor: { loT, hiT } in normalised time 0 … 1 over the
//       window, or null/undefined. Result in design units (Cell).
//   polygon(points, k)
//       a flat [x0, y0, …] array (any `points` above) × k, as a QPolygonF for PathPolyline.path:
//       the C++ loop that replaces the JS one (`path: VariabilityGeometry.polygon(o.swing.points, k)`).
//       It reads the one array it is given and recomputes nothing. For the funnel (px) k = 1.
class VariabilityGeometryQml : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(VariabilityGeometry)
    QML_SINGLETON

public:
    // `engine` is the engine whose JS objects this singleton builds; the QML engine passes itself.
    explicit VariabilityGeometryQml(QJSEngine *engine, QObject *parent = nullptr);

    // QML's singleton factory: one instance per engine, holding that engine.
    static VariabilityGeometryQml *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    Q_INVOKABLE QJSValue  shape(int variant, int form, int grade);
    Q_INVOKABLE QPolygonF tracePath(int variant, int form, int grade, int index, qreal k) const;
    Q_INVOKABLE QPolygonF wedgePath(int variant, int form, int grade, int index, qreal k) const;

    Q_INVOKABLE QJSValue  funnel(const QJSValue &sessions, qreal widthPx, qreal heightPx) const;
    Q_INVOKABLE QJSValue  typicality(int form, qreal z, const QJSValue &ghosts, const QJSValue &corridor) const;
    Q_INVOKABLE QJSValue  typicalityTrace(const QJSValue &mean, const QJSValue &sd, const QJSValue &swing,
                                          const QJSValue &corridor) const;
    Q_INVOKABLE QPolygonF polygon(const QList<qreal> &points, qreal k) const;

private:
    QJSEngine *m_engine = nullptr;
    std::shared_ptr<const IVariabilityGeometry> m_geometry;
    QHash<int, QJSValue> m_cache;   // key: (variant × 2 + form) × 6 + grade; or -1 for "unknown"
};

} // namespace pinpoint::variability
