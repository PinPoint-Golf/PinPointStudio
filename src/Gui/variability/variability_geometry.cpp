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

#include "variability_geometry.h"

#include "../../Metrics/variability/variability_constants.h"   // kTrendMinSessions, kFunnelMaxSessions, kTraceGridPoints

#include <QByteArray>
#include <QJSEngine>
#include <QJsonArray>
#include <QQmlEngine>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::variability {

namespace {

// ── The §4 tables, spelled once ─────────────────────────────────────────────────────────────────────
// Design geometry, not calibration: none of these is a §8.10 constant. Every per-grade array is
// indexed by g = grade − 1 (§4.3).

constexpr double kPi = 3.14159265358979323846;

// Strokes, px (§4.2: constant at every size).
constexpr double kHairline = 1.0;   // every neutral element: ring, target line, baselines, arc, ruler, ticks

// Alphas of the tone (§4.4 – §4.6).
constexpr double kDotAlpha     = 0.85;
constexpr double kEllipseAlpha = 0.12;

// §4.4 Dispersion Icon.
constexpr double kDispIconCx = 12, kDispIconCy = 12, kDispIconRingR = 10.5;
constexpr std::array<double, 3> kDispIconAngles = {-30, 90, 210};
constexpr std::array<double, 5> kDispIconSpread = {7.2, 5.4, 3.6, 1.8, 0};
constexpr double kDispIconDotR = 2.6, kDispIconDotRSmall = 3.0, kDispIconDotSmallMax = 12;

// §4.4 Dispersion Cell.
constexpr double kDispCellCx = 40, kDispCellCy = 12;
constexpr double kDispCellLineY1 = 2, kDispCellLineY2 = 22;
constexpr double kDispCellEllipsePadX = 4, kDispCellEllipsePadY = 3.5;
constexpr double kDispCellDotR = 2.2;
constexpr std::array<double, 5> kDispCellSx = {30, 21, 13, 6.5, 1.8};
constexpr std::array<double, 5> kDispCellSy = {7.5, 5.5, 3.8, 2.2, 0.6};
constexpr std::array<double, 7> kDispCellOx = {-0.95, -0.55, -0.15, 0.1, 0.45, 0.75, 1};
constexpr std::array<double, 7> kDispCellOy = {0.3, -0.9, 0.7, -0.2, 1, -0.6, 0.1};

// §4.5 Overlay Icon.
constexpr double kOverIconBaseY = 21.5, kOverIconX0 = 1, kOverIconX1 = 23, kOverIconStep = 0.5;
constexpr double kOverIconC = 12, kOverIconWidthScale = 3.8, kOverIconH = 15;
constexpr std::array<double, 3> kOverIconShift = {-1, 0.2, 1};
constexpr std::array<double, 3> kOverIconHk    = {-1, 1, 0.1};
constexpr std::array<double, 5> kOverIconSp    = {3.2, 2.2, 1.3, 0.6, 0};
constexpr std::array<double, 5> kOverIconHv    = {0.3, 0.2, 0.12, 0.05, 0};
constexpr double kOverIconWidth = 1.5, kOverIconWidthSmall = 1.25, kOverIconSmallMax = 16;
constexpr double kOverIconAlpha = 0.70;

// §4.5 Overlay Cell.
constexpr double kOverCellBaseY = 21.5, kOverCellX0 = 2, kOverCellX1 = 78, kOverCellStep = 1;
constexpr double kOverCellC = 40, kOverCellWidthScale = 9, kOverCellH = 17;
constexpr double kOverCellBumpH = 1.5, kOverCellBumpC = 14, kOverCellBumpWidth = 8;
constexpr std::array<double, 5> kOverCellTshift = {-1, 0.55, 0.1, 1, -0.6};
constexpr std::array<double, 5> kOverCellTh     = {-1, 0.6, 0.05, -0.5, 1};
constexpr std::array<double, 5> kOverCellTsp    = {9, 6.2, 3.8, 1.8, 0.4};
constexpr std::array<double, 5> kOverCellThv    = {0.3, 0.21, 0.13, 0.06, 0.015};
constexpr double kOverCellWidth = 1.3;
constexpr double kOverCellAlpha = 0.60;

// §4.6 Needle, both forms: the three wedges' share of the table value and their alphas.
constexpr std::array<double, 3> kWedgeShare      = {1, 0.62, 0.3};
constexpr std::array<double, 3> kWedgeAlpha      = {0.12, 0.16, 0.24};
constexpr std::array<double, 3> kWedgeAlphaSmall = {0.14, 0.18, 0.26};   // Icon only, at size ≤ 16 px

// §4.6 Needle Icon.
constexpr double kNeedleIconPx = 12, kNeedleIconPy = 20, kNeedleIconArcR = 10;
constexpr double kNeedleIconWedgeR = 11, kNeedleIconTipY = 8.5;
constexpr std::array<double, 5> kNeedleIconA = {34, 22, 13, 6, 0};   // degrees
constexpr double kNeedleIconSmallMax = 16;
constexpr double kNeedleIconWidth = 1.5, kNeedleIconWidthSmall = 1.25;
constexpr double kNeedleIconPivotR = 1.8, kNeedleIconPivotRSmall = 2.2;
constexpr double kMaxChord = 1.0;   // design units: the sector's arc is sampled at chords no longer than this
constexpr double kUp = 270;         // degrees: straight up on screen

// §4.6 Needle Cell.
constexpr double kRulerY = 5, kRulerX0 = 4, kRulerX1 = 76;
constexpr double kTickX0 = 8, kTickX1 = 72, kTickStep = 8;
constexpr double kMajorTickY1 = 2, kMinorTickY1 = 3.5;
constexpr std::array<double, 5> kMajorTickX = {8, 24, 40, 56, 72};
constexpr double kNeedleCellPx = 40, kNeedleCellPy = 21, kNeedleCellTipY = 4;
constexpr std::array<double, 5> kNeedleCellW = {24, 15, 9, 4, 0};
constexpr double kNeedleCellWidth = 1.5, kNeedleCellPivotR = 2;

// ── The §6 companions ───────────────────────────────────────────────────────────────────────────────
// Layout, not calibration: none of these is a §8.10 constant (the session minimum is
// kTrendMinSessions, the 12-session cap kFunnelMaxSessions, both from variability_constants.h).
// Values the spec does not give are marked "spec silent" and were reported to Mark.

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// §6.1 the spread funnel, PIXELS.
constexpr double kFunnelPad      = 6;      // end padding
constexpr double kFunnelInset    = 2;      // the half-width's H/2 − 2
constexpr double kFunnelRhoScale = 1.25;   // clamp(ρ / 1.25, 0.08, 1)
constexpr double kFunnelMinFrac  = 0.08, kFunnelMaxFrac = 1;
constexpr double kFunnelFillAlpha = 0.16, kFunnelEdgeAlpha = 0.55, kFunnelEdgeWidth = 1;
constexpr double kFunnelMarkerR  = 1.6, kFunnelCurrentR = 2.2;
constexpr double kFunnelBreakGap = 4;
// Spec silent: "a short dashed tick". It spans ± half the largest half-width the band can take
// (H/2 − 2), so it is short against the funnel's height at any size: ±7 px at the compact 32.
constexpr double kFunnelTickFrac = 0.5;

// §6.2 Your group, Cell.
constexpr double kTypCellCx = 40, kTypCellCy = 12, kTypCellSd = 14;   // 1 SD = 14 units
constexpr double kTypCellW = 80;
constexpr double kTypOuterRx = 28, kTypOuterRy = 9;                    // ±2 SD, dashed
constexpr double kTypInnerRx = 14, kTypInnerRy = 5;                    // ±1 SD
constexpr double kTypInnerAlpha = 0.07;                                // colorText at 7 %
constexpr double kGhostR = 1.5, kGhostAlpha = 0.70, kGhostJitter = 4;  // ±4 units of jitter
constexpr int    kGhostMax = 12;
constexpr double kRingR = 3, kRingWidth = 1.75, kCentreR = 1.1;
constexpr double kClampZ = 2.6, kClampXLo = 4, kClampXHi = 76;
// Spec silent: the "outward chevron". An open ‹ or ›, 2 units deep and 4 tall, on the ring's centre
// line just INBOARD of it (the ring at x 4 leaves no room outboard), its tip pointing out of the
// cell: tip x 9 / 71, arms x 11 / 69 at y 10 and 14. 1.5 px, tone, round cap and join. (First cut was
// a unit further in, tip 10 / 70; review: the right one then touched the outer zone's dashes.)
constexpr double kChevronTipLo = 9, kChevronArmLo = 11;               // mirrored about x 40 for the right
constexpr double kChevronHalfH = 2;
constexpr double kChevronWidth = 1.5;

// §6.2 Your group, Icon.
constexpr double kTypIconC = 12, kTypIconOuterR = 10.5, kTypIconInnerR = 5.25, kTypIconSd = 5.25;
constexpr double kTypIconW = 24;
constexpr double kTypIconRingR = 2.6, kTypIconClampZ = 2.2;
// Spec silent: the Icon ring's stroke and fill. The Cell ring's: 1.75 px tone, colorSurface fill.
constexpr double kTypIconRingWidth = kRingWidth;

// §6.2 showCorridor. Spec silent: where "beneath" is and the end ticks. The line at y 23.25, so a
// 1.5 px stroke at k = 1 ends exactly on the cell's bottom edge (24), with 0.75-unit ticks up toward
// the visual; the same in every form. Clear of the Cell's outer zone (bottom y 21) and the Icon's
// (22.5); the trace's ticks stop 0.25 units short of its 1 px baseline.
constexpr double kBracketY = 23.25, kBracketTick = 0.75, kBracketWidth = 1.5;

// §6.2 Trace on envelope (Cell).
constexpr double kTraceBaseY = 21.5, kTraceX0 = 2, kTraceX1 = 78;
// Spec silent: the vertical scale. The Overlay Cell's peak height h (§4.5) above the baseline, and the
// room below it to the cell's bottom edge (24 − 21.5); VariabilityTraceShape says how both are used.
constexpr double kTracePeak = 17, kTraceBelow = 24 - kTraceBaseY;
constexpr std::size_t kGridN = std::size_t(kTraceGridPoints);           // 101
constexpr double kEnvelopeAlpha = 0.09;                                // colorText at 9 %
constexpr double kTraceMeanWidth = 1, kTraceSwingWidth = 1.75;

// ── Builders ────────────────────────────────────────────────────────────────────────────────────────

VariabilityLine neutralLine(GeometryRole role, double x1, double y1, double x2, double y2)
{
    VariabilityLine l;
    l.role = role;
    l.x1 = x1; l.y1 = y1; l.x2 = x2; l.y2 = y2;
    l.width = l.widthSmall = kHairline;
    return l;
}

VariabilityDot toneDot(double x, double y, double r, double rSmall, double smallMaxSize, double alpha)
{
    VariabilityDot d;
    d.role = GeometryRole::Tone;
    d.x = x; d.y = y;
    d.r = r; d.rSmall = rSmall; d.smallMaxSize = smallMaxSize;
    d.alpha = alpha;
    return d;
}

VariabilityLine toneNeedle(double x1, double y1, double x2, double y2, double width, double widthSmall,
                           double smallMaxSize)
{
    VariabilityLine l;
    l.role = GeometryRole::Tone;
    l.x1 = x1; l.y1 = y1; l.x2 = x2; l.y2 = y2;
    l.width = width; l.widthSmall = widthSmall; l.smallMaxSize = smallMaxSize;
    l.alpha = 1;           // full tone (§4.6)
    l.roundCap = true;
    return l;
}

// §4.4 Icon: the ring, then three dots at spread[g] from the centre.
void buildDispersionIcon(VariabilityShape &s)
{
    VariabilityArc ring;
    ring.role = GeometryRole::NeutralStrong;
    ring.cx = kDispIconCx; ring.cy = kDispIconCy; ring.r = kDispIconRingR;
    ring.startAngle = 0; ring.sweepAngle = 360;
    ring.width = ring.widthSmall = kHairline;
    s.ring = ring;
    if (s.grade < 1)
        return;
    const double d = kDispIconSpread[s.grade - 1];
    for (const double a : kDispIconAngles) {
        const double t = a * kPi / 180;
        s.dots.push_back(toneDot(kDispIconCx + d * std::cos(t), kDispIconCy + d * std::sin(t), kDispIconDotR,
                                 kDispIconDotRSmall, kDispIconDotSmallMax, kDotAlpha));
    }
}

// §4.4 Cell: the target line, the group ellipse, seven dots.
void buildDispersionCell(VariabilityShape &s)
{
    s.line = neutralLine(GeometryRole::NeutralMid, kDispCellCx, kDispCellLineY1, kDispCellCx, kDispCellLineY2);
    if (s.grade < 1)
        return;
    const int g = s.grade - 1;
    VariabilityEllipse e;
    e.role = GeometryRole::Tone;
    e.cx = kDispCellCx; e.cy = kDispCellCy;
    e.rx = kDispCellSx[g] + kDispCellEllipsePadX;
    e.ry = kDispCellSy[g] + kDispCellEllipsePadY;
    e.alpha = kEllipseAlpha;
    s.ellipse = e;
    for (std::size_t i = 0; i < kDispCellOx.size(); ++i)
        s.dots.push_back(toneDot(kDispCellCx + kDispCellSx[g] * kDispCellOx[i],
                                 kDispCellCy + kDispCellSy[g] * kDispCellOy[i], kDispCellDotR, kDispCellDotR, 0,
                                 kDotAlpha));
}

// §4.5 Icon: the baseline, three traces sampled at x = 1…23 in steps of 0.5.
void buildOverlayIcon(VariabilityShape &s)
{
    // §4.5: 1 px, colorBorderStrong.
    s.line = neutralLine(GeometryRole::NeutralStrong, kOverIconX0, kOverIconBaseY, kOverIconX1, kOverIconBaseY);
    if (s.grade < 1)
        return;
    const int g = s.grade - 1;
    const int samples = int(std::lround((kOverIconX1 - kOverIconX0) / kOverIconStep)) + 1;   // 45
    for (std::size_t k = 0; k < kOverIconShift.size(); ++k) {
        VariabilityPolyline t;
        t.centre = kOverIconC + kOverIconSp[g] * kOverIconShift[k];
        t.height = kOverIconH * (1 + kOverIconHv[g] * kOverIconHk[k]);
        t.points.reserve(samples);
        for (int n = 0; n < samples; ++n) {
            const double x = kOverIconX0 + n * kOverIconStep;   // by index, so the samples never drift
            const double e = (x - t.centre) / kOverIconWidthScale;
            t.points.emplace_back(x, kOverIconBaseY - t.height * std::exp(-e * e));
        }
        t.width = kOverIconWidth; t.widthSmall = kOverIconWidthSmall; t.smallMaxSize = kOverIconSmallMax;
        t.alpha = kOverIconAlpha;
        s.traces.push_back(std::move(t));
    }
}

// §4.5 Cell: the baseline, five traces sampled at x = 2…78 in steps of 1.
void buildOverlayCell(VariabilityShape &s)
{
    // §4.5 names no colour for the Cell's baseline; the Icon's colorBorderStrong, as the first-cut
    // mark and the S2 spike decided.
    s.line = neutralLine(GeometryRole::NeutralStrong, kOverCellX0, kOverCellBaseY, kOverCellX1, kOverCellBaseY);
    if (s.grade < 1)
        return;
    const int g = s.grade - 1;
    const int samples = int(std::lround((kOverCellX1 - kOverCellX0) / kOverCellStep)) + 1;   // 77
    for (std::size_t j = 0; j < kOverCellTshift.size(); ++j) {
        VariabilityPolyline t;
        t.centre = kOverCellC + kOverCellTsp[g] * kOverCellTshift[j];
        t.height = kOverCellH * (1 + kOverCellThv[g] * kOverCellTh[j]);
        t.points.reserve(samples);
        for (int n = 0; n < samples; ++n) {
            const double x = kOverCellX0 + n * kOverCellStep;
            const double e = (x - t.centre) / kOverCellWidthScale;
            const double b = (x - kOverCellBumpC) / kOverCellBumpWidth;
            t.points.emplace_back(x, kOverCellBaseY - t.height * std::exp(-e * e) - kOverCellBumpH * std::exp(-b * b));
        }
        t.width = t.widthSmall = kOverCellWidth;
        t.alpha = kOverCellAlpha;
        s.traces.push_back(std::move(t));
    }
}

// A sector on (px, py), radius r, pointing up, half-angle `half` degrees: the pivot, the arc at chords
// ≤ kMaxChord, the pivot again.
std::vector<QPointF> sectorPolygon(double px, double py, double r, double half)
{
    const double sweep = 2 * half * kPi / 180;                                  // radians
    const int chords = std::max(1, int(std::ceil(r * sweep / kMaxChord)));      // chord < arc ≤ kMaxChord
    std::vector<QPointF> p;
    p.reserve(chords + 3);
    p.emplace_back(px, py);
    const double a0 = (kUp - half) * kPi / 180;
    for (int i = 0; i <= chords; ++i) {
        const double a = a0 + sweep * i / chords;
        p.emplace_back(px + r * std::cos(a), py + r * std::sin(a));
    }
    p.emplace_back(px, py);
    return p;
}

// §4.6 Icon: the gauge arc, three sectors, the needle, the pivot.
void buildNeedleIcon(VariabilityShape &s)
{
    VariabilityArc arc;
    arc.role = GeometryRole::NeutralStrong;
    arc.cx = kNeedleIconPx; arc.cy = kNeedleIconPy; arc.r = kNeedleIconArcR;
    arc.startAngle = 180; arc.sweepAngle = 180;   // (2, 20) over the top to (22, 20)
    arc.width = arc.widthSmall = kHairline;
    s.arc = arc;
    if (s.grade < 1)
        return;
    const double a = kNeedleIconA[s.grade - 1];
    for (std::size_t i = 0; i < kWedgeShare.size(); ++i) {
        VariabilityWedge w;
        w.spread = kWedgeShare[i] * a;
        w.points = sectorPolygon(kNeedleIconPx, kNeedleIconPy, kNeedleIconWedgeR, w.spread);
        w.alpha = kWedgeAlpha[i]; w.alphaSmall = kWedgeAlphaSmall[i]; w.smallMaxSize = kNeedleIconSmallMax;
        s.wedges.push_back(std::move(w));
    }
    s.needle = toneNeedle(kNeedleIconPx, kNeedleIconPy, kNeedleIconPx, kNeedleIconTipY, kNeedleIconWidth,
                          kNeedleIconWidthSmall, kNeedleIconSmallMax);
    // §4.6 names no colour for the pivot; it is the needle's hub, so it takes the needle's full tone.
    s.pivot = toneDot(kNeedleIconPx, kNeedleIconPy, kNeedleIconPivotR, kNeedleIconPivotRSmall, kNeedleIconSmallMax, 1);
}

// §4.6 Cell: the ruler and its ticks, three triangles, the needle, the pivot.
void buildNeedleCell(VariabilityShape &s)
{
    // §4.6 gives the ruler and ticks no width; 1 px, as every other neutral element in §4.
    s.line = neutralLine(GeometryRole::NeutralMid, kRulerX0, kRulerY, kRulerX1, kRulerY);
    const int tickCount = int(std::lround((kTickX1 - kTickX0) / kTickStep)) + 1;   // 9
    for (int i = 0; i < tickCount; ++i) {
        const double x = kTickX0 + i * kTickStep;
        const bool major = std::find(kMajorTickX.begin(), kMajorTickX.end(), x) != kMajorTickX.end();
        s.ticks.push_back(neutralLine(GeometryRole::NeutralStrong, x, major ? kMajorTickY1 : kMinorTickY1, x, kRulerY));
    }
    if (s.grade < 1)
        return;
    const double w0 = kNeedleCellW[s.grade - 1];
    for (std::size_t i = 0; i < kWedgeShare.size(); ++i) {
        VariabilityWedge w;
        w.spread = kWedgeShare[i] * w0;
        w.points = {{kNeedleCellPx, kNeedleCellPy}, {kNeedleCellPx - w.spread, kRulerY},
                    {kNeedleCellPx + w.spread, kRulerY}, {kNeedleCellPx, kNeedleCellPy}};
        // §4.6 gives the Cell no small-size alphas.
        w.alpha = w.alphaSmall = kWedgeAlpha[i];
        s.wedges.push_back(std::move(w));
    }
    // §4.6 gives the Cell needle 1.5 px and no small width; its cap, like its tone, is the Icon's.
    s.needle = toneNeedle(kNeedleCellPx, kNeedleCellTipY, kNeedleCellPx, kNeedleCellPy, kNeedleCellWidth,
                          kNeedleCellWidth, 0);
    s.pivot = toneDot(kNeedleCellPx, kNeedleCellPy, kNeedleCellPivotR, kNeedleCellPivotR, 0, 1);
}

// ── JSON ────────────────────────────────────────────────────────────────────────────────────────────

QJsonArray flat(const std::vector<QPointF> &points)
{
    QJsonArray a;
    for (const QPointF &p : points) {
        a.append(p.x());
        a.append(p.y());
    }
    return a;
}

QJsonObject toJson(const VariabilityArc &a)
{
    return {{"role", geometryRoleToken(a.role)}, {"cx", a.cx}, {"cy", a.cy}, {"r", a.r},
            {"startAngle", a.startAngle}, {"sweepAngle", a.sweepAngle}, {"width", a.width},
            {"widthSmall", a.widthSmall}, {"smallMaxSize", a.smallMaxSize}, {"alpha", a.alpha}};
}

QJsonObject toJson(const VariabilityLine &l)
{
    return {{"role", geometryRoleToken(l.role)}, {"x1", l.x1}, {"y1", l.y1}, {"x2", l.x2}, {"y2", l.y2},
            {"width", l.width}, {"widthSmall", l.widthSmall}, {"smallMaxSize", l.smallMaxSize},
            {"alpha", l.alpha}, {"roundCap", l.roundCap}, {"dashed", l.dashed}};
}

QJsonObject toJson(const VariabilityDot &d)
{
    return {{"role", geometryRoleToken(d.role)}, {"x", d.x}, {"y", d.y}, {"r", d.r}, {"rSmall", d.rSmall},
            {"smallMaxSize", d.smallMaxSize}, {"alpha", d.alpha}};
}

QJsonObject toJson(const VariabilityEllipse &e)
{
    return {{"role", geometryRoleToken(e.role)}, {"cx", e.cx}, {"cy", e.cy}, {"rx", e.rx}, {"ry", e.ry},
            {"alpha", e.alpha}};
}

QJsonObject toJson(const VariabilityWedge &w)
{
    return {{"role", geometryRoleToken(w.role)}, {"points", flat(w.points)}, {"spread", w.spread},
            {"alpha", w.alpha}, {"alphaSmall", w.alphaSmall}, {"smallMaxSize", w.smallMaxSize}};
}

QJsonObject toJson(const VariabilityPolyline &t)
{
    return {{"role", geometryRoleToken(t.role)}, {"points", flat(t.points)}, {"width", t.width},
            {"widthSmall", t.widthSmall}, {"smallMaxSize", t.smallMaxSize}, {"alpha", t.alpha},
            {"roundCap", t.roundCap}, {"centre", t.centre}, {"height", t.height}, {"dashed", t.dashed}};
}

// A finite number, else null: JSON has no ±∞ or NaN, and a JS consumer should see "no number", not a
// silently converted one.
QJsonValue num(double v)
{
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
}

QJsonArray dashPattern()
{
    return {kDashOn, kDashOff};
}

QJsonObject toJson(const VariabilityZone &z)
{
    return {{"role", geometryRoleToken(z.role)}, {"cx", z.cx}, {"cy", z.cy}, {"rx", z.rx}, {"ry", z.ry},
            {"filled", z.filled}, {"width", z.width}, {"dashed", z.dashed}, {"alpha", z.alpha}};
}

QJsonObject toJson(const VariabilityRing &r)
{
    return {{"cx", r.cx}, {"cy", r.cy}, {"r", r.r}, {"strokeRole", geometryRoleToken(r.strokeRole)},
            {"width", r.width}, {"fillRole", geometryRoleToken(r.fillRole)}, {"alpha", r.alpha}};
}

QJsonObject toJson(const VariabilityGhost &g)
{
    return {{"swingId", g.swingId}, {"z", num(g.z)}, {"clamped", g.clamped}, {"x", g.dot.x}, {"y", g.dot.y},
            {"r", g.dot.r}, {"rSmall", g.dot.rSmall}, {"smallMaxSize", g.dot.smallMaxSize},
            {"role", geometryRoleToken(g.dot.role)}, {"alpha", g.dot.alpha}};
}

QJsonObject toJson(const VariabilityBracket &b)
{
    return {{"role", geometryRoleToken(b.role)}, {"lo", b.lo}, {"hi", b.hi}, {"x1", b.x1}, {"x2", b.x2},
            {"y", b.y}, {"tick", b.tick}, {"loClamped", b.loClamped}, {"hiClamped", b.hiClamped},
            {"points", flat(b.points)}, {"width", b.width}, {"alpha", b.alpha}};
}

QJsonObject toJson(const VariabilityArea &a)
{
    return {{"role", geometryRoleToken(a.role)}, {"points", flat(a.points)}, {"alpha", a.alpha}};
}

template <typename T>
QJsonValue optionalJson(const std::optional<T> &v)
{
    return v ? QJsonValue(toJson(*v)) : QJsonValue(QJsonValue::Null);
}

template <typename T>
QJsonArray listJson(const std::vector<T> &v)
{
    QJsonArray a;
    for (const T &e : v)
        a.append(toJson(e));
    return a;
}

QPolygonF scaled(const std::vector<QPointF> &points, qreal k)
{
    QPolygonF p;
    p.reserve(qsizetype(points.size()));
    for (const QPointF &q : points)
        p.append(q * k);
    return p;
}

// ── §6 builders ─────────────────────────────────────────────────────────────────────────────────────

bool hasFunnelReading(double rho)
{
    return std::isfinite(rho) && rho >= 0;
}

VariabilityZone zone(GeometryRole role, double cx, double cy, double rx, double ry, bool filled, double alpha)
{
    VariabilityZone z;
    z.role = role;
    z.cx = cx; z.cy = cy; z.rx = rx; z.ry = ry;
    z.filled = filled;
    z.width  = kHairline;
    z.dashed = !filled;   // §6.2: the stroked zone is the dashed outer one; the filled one has no stroke
    z.alpha  = alpha;
    return z;
}

// The corridor bracket for a mapping x = x0 + v × perUnit, clamped to [xMin, xMax].
std::optional<VariabilityBracket> bracket(const std::optional<VariabilityCorridor> &c, double x0, double perUnit,
                                          double xMin, double xMax)
{
    if (!c || !std::isfinite(c->lo) || !std::isfinite(c->hi))
        return std::nullopt;
    VariabilityBracket b;
    b.lo = std::min(c->lo, c->hi);
    b.hi = std::max(c->lo, c->hi);
    const double x1 = x0 + b.lo * perUnit, x2 = x0 + b.hi * perUnit;
    b.loClamped = x1 < xMin || x1 > xMax;
    b.hiClamped = x2 < xMin || x2 > xMax;
    b.x1 = std::clamp(x1, xMin, xMax);
    b.x2 = std::clamp(x2, xMin, xMax);
    b.y = kBracketY;
    b.tick = kBracketTick;
    if (!b.loClamped)
        b.points.emplace_back(b.x1, b.y - b.tick);
    b.points.emplace_back(b.x1, b.y);
    b.points.emplace_back(b.x2, b.y);
    if (!b.hiClamped)
        b.points.emplace_back(b.x2, b.y - b.tick);
    b.width = kBracketWidth;
    return b;
}

} // namespace

bool VariabilityShape::isEmpty() const
{
    return !ring && !arc && !line && ticks.empty() && !ellipse && wedges.empty() && traces.empty() && !needle
        && dots.empty() && !pivot && !typicality && !typicalityTrace;
}

int clampGrade(int grade)
{
    return std::clamp(grade, 0, kMarkGradeMax);
}

QString geometryRoleToken(GeometryRole role)
{
    switch (role) {
    case GeometryRole::NeutralStrong: return QStringLiteral("neutralStrong");
    case GeometryRole::NeutralMid:    return QStringLiteral("neutralMid");
    case GeometryRole::Tone:          return QStringLiteral("tone");
    case GeometryRole::Text:          return QStringLiteral("text");
    case GeometryRole::Text2:         return QStringLiteral("text2");
    case GeometryRole::Text3:         return QStringLiteral("text3");
    case GeometryRole::Surface:       return QStringLiteral("surface");
    case GeometryRole::Good:          return QStringLiteral("good");
    }
    return QStringLiteral("tone");
}

CanonicalVariabilityGeometry::CanonicalVariabilityGeometry()
{
    for (int v = 0; v < kMarkVariantCount; ++v) {
        for (int f = 0; f < kMarkFormCount; ++f) {
            for (int g = 0; g <= kMarkGradeMax; ++g) {
                VariabilityShape &s = m_shapes[v][f][g];
                s.variant = MarkVariant(v);
                s.form    = MarkForm(f);
                s.grade   = g;
                const bool cell = s.form == MarkForm::Cell;
                switch (s.variant) {
                case MarkVariant::Dispersion: cell ? buildDispersionCell(s) : buildDispersionIcon(s); break;
                case MarkVariant::Overlay:    cell ? buildOverlayCell(s)    : buildOverlayIcon(s);    break;
                case MarkVariant::Needle:     cell ? buildNeedleCell(s)     : buildNeedleIcon(s);     break;
                // Data-only (§6.2): DataVariabilityGeometry's, from Session 3.
                case MarkVariant::Typicality:
                case MarkVariant::TypicalityTrace:
                    break;
                }
            }
        }
    }
}

const VariabilityShape &CanonicalVariabilityGeometry::shape(MarkVariant variant, MarkForm form, int grade) const
{
    const int v = int(variant), f = int(form);
    if (v < 0 || v >= kMarkVariantCount || f < 0 || f >= kMarkFormCount)
        return m_empty;
    return m_shapes[v][f][clampGrade(grade)];
}

std::shared_ptr<const IVariabilityGeometry> makeCanonicalVariabilityGeometry()
{
    static const std::shared_ptr<const IVariabilityGeometry> shared =
        std::make_shared<const CanonicalVariabilityGeometry>();
    return shared;
}

QJsonObject toJson(const VariabilityShape &s)
{
    return {{"variant", int(s.variant)},
            {"form", int(s.form)},
            {"grade", s.grade},
            {"ring", optionalJson(s.ring)},
            {"arc", optionalJson(s.arc)},
            {"line", optionalJson(s.line)},
            {"ticks", listJson(s.ticks)},
            {"ellipse", optionalJson(s.ellipse)},
            {"wedges", listJson(s.wedges)},
            {"traces", listJson(s.traces)},
            {"needle", optionalJson(s.needle)},
            {"dots", listJson(s.dots)},
            {"pivot", optionalJson(s.pivot)},
            {"typicality", s.typicality ? QJsonValue(toJson(*s.typicality)) : QJsonValue(QJsonValue::Null)},
            {"typicalityTrace",
             s.typicalityTrace ? QJsonValue(toJson(*s.typicalityTrace)) : QJsonValue(QJsonValue::Null)}};
}

// ── DataVariabilityGeometry ─────────────────────────────────────────────────────────────────────────

quint32 ghostJitterHash(const QString &swingId)
{
    // FNV-1a, 32-bit: a fixed function of the bytes, the same on every run, build and platform.
    quint32 h = 2166136261u;
    const QByteArray bytes = swingId.toUtf8();
    for (const char c : bytes) {
        h ^= quint32(quint8(c));
        h *= 16777619u;
    }
    // murmur3's fmix32 avalanche. Plain FNV-1a barely moves its TOP bits when only the last byte
    // changes, and the jitter reads the top bits: sequential ids ("swing_0038" … "swing_0049") all
    // landed within 0.4 units, one row. The finaliser spreads every input bit over all 32.
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

double ghostJitter(const QString &swingId)
{
    return (double(ghostJitterHash(swingId)) / 4294967296.0 * 2 - 1) * kGhostJitter;   // [−4, 4)
}

DataVariabilityGeometry::DataVariabilityGeometry(double z, const std::vector<VariabilityGhostInput> &ghosts,
                                                 const std::optional<VariabilityCorridor> &corridor)
    : m_variant(MarkVariant::Typicality)
{
    m_icon.variant = m_cell.variant = MarkVariant::Typicality;
    m_icon.form = MarkForm::Icon;
    m_cell.form = MarkForm::Cell;
    m_icon.typicality = typicality(MarkForm::Icon, z, ghosts, corridor);
    m_cell.typicality = typicality(MarkForm::Cell, z, ghosts, corridor);
}

DataVariabilityGeometry::DataVariabilityGeometry(const std::vector<double> &mean, const std::vector<double> &sd,
                                                 const std::vector<double> &swing,
                                                 const std::optional<VariabilityCorridor> &corridor)
    : m_variant(MarkVariant::TypicalityTrace)
{
    m_cell.variant = MarkVariant::TypicalityTrace;
    m_cell.form = MarkForm::Cell;
    m_cell.typicalityTrace = typicalityTrace(mean, sd, swing, corridor);
}

const VariabilityShape &DataVariabilityGeometry::shape(MarkVariant variant, MarkForm form, int grade) const
{
    Q_UNUSED(grade);   // §4.1: the typicality variants ignore it
    if (variant != m_variant)
        return m_empty;
    if (form == MarkForm::Cell)
        return m_cell;
    if (form == MarkForm::Icon && m_variant == MarkVariant::Typicality)
        return m_icon;
    return m_empty;    // TypicalityTrace has no Icon (§6.2); unknown forms
}

VariabilityFunnelShape DataVariabilityGeometry::funnel(const std::vector<VariabilityFunnelSession> &in,
                                                       double widthPx, double heightPx)
{
    VariabilityFunnelShape f;
    f.requestedWidth = widthPx;
    f.width  = std::isfinite(widthPx) ? std::max(widthPx, kFunnelMinWidthPx) : kFunnelMinWidthPx;
    f.height = std::isfinite(heightPx) ? std::max(heightPx, 0.0) : 0.0;
    const double W = f.width, H = f.height, cy = H / 2;

    f.centre = neutralLine(GeometryRole::NeutralStrong, 0, cy, W, cy);
    f.centre.dashed = true;
    if (!in.empty()) {
        f.currentGrade = clampGrade(in.back().grade);
        f.currentHasReading = hasFunnelReading(in.back().rho);
    }

    // The sessions with a reading. A skipped session's break carries to the next kept one: the change
    // still lies between the two readings either side of it.
    struct Kept { int input; bool breakBefore; };
    std::vector<Kept> kept;
    bool pendingBreak = false;
    for (int i = 0; i < int(in.size()); ++i) {
        const VariabilityFunnelSession &s = in[std::size_t(i)];
        pendingBreak = pendingBreak || s.breakBefore;
        if (!hasFunnelReading(s.rho))
            continue;
        kept.push_back({i, pendingBreak && !kept.empty()});
        pendingBreak = false;
    }
    // At most 12; older ones roll off (§6.1), and a break before the first drawn one means nothing.
    if (int(kept.size()) > kFunnelMaxSessions) {
        f.rolledOff = int(kept.size()) - kFunnelMaxSessions;
        kept.erase(kept.begin(), kept.begin() + f.rolledOff);
        kept.front().breakBefore = false;
    }
    const int n = int(kept.size());
    if (n == 0)
        return f;

    const double halfMax = std::max(0.0, H / 2 - kFunnelInset);
    const double step = n > 1 ? (W - 2 * kFunnelPad) / (n - 1) : 0;
    int run = 0;
    for (int i = 0; i < n; ++i) {
        if (kept[std::size_t(i)].breakBefore)
            ++run;
        const VariabilityFunnelSession &s = in[std::size_t(kept[std::size_t(i)].input)];
        VariabilityFunnelPoint p;
        p.input = kept[std::size_t(i)].input;
        p.rho = s.rho;
        p.grade = clampGrade(s.grade);
        p.x = n > 1 ? kFunnelPad + i * step : W - kFunnelPad;
        p.halfWidth = std::clamp(s.rho / kFunnelRhoScale, kFunnelMinFrac, kFunnelMaxFrac) * halfMax;
        p.run = run;
        p.current = p.input == int(in.size()) - 1;
        p.marker.role = p.current ? GeometryRole::Tone : GeometryRole::Text2;
        p.marker.x = p.x;
        p.marker.y = cy;
        p.marker.r = p.marker.rSmall = p.current ? kFunnelCurrentR : kFunnelMarkerR;
        p.marker.alpha = 1;
        f.sessions.push_back(p);
    }

    int start = n - 1;
    while (start > 0 && !kept[std::size_t(start)].breakBefore)
        --start;
    f.latestRun = n - start;
    f.trendAfter3 = f.latestRun < kTrendMinSessions;
    if (f.trendAfter3)
        return f;   // no band at all (§6.1)

    const auto &P = f.sessions;
    const double tickHalf = kFunnelTickFrac * halfMax;
    for (int a = 0; a < n;) {
        int b = a;
        while (b + 1 < n && !kept[std::size_t(b + 1)].breakBefore)
            ++b;
        const auto &pa = P[std::size_t(a)];
        const auto &pb = P[std::size_t(b)];
        // A break between two sessions sits midway; each band stops 2 px short of it.
        const double left  = a > 0     ? (P[std::size_t(a - 1)].x + pa.x) / 2 + kFunnelBreakGap / 2 : pa.x;
        const double right = b < n - 1 ? (pb.x + P[std::size_t(b + 1)].x) / 2 - kFunnelBreakGap / 2 : pb.x;

        VariabilityFunnelBand band;
        band.first = a;
        band.last  = b;
        band.fillAlpha = kFunnelFillAlpha;
        band.edgeAlpha = kFunnelEdgeAlpha;
        band.edgeWidth = kFunnelEdgeWidth;
        auto &pts = band.points;
        if (left < pa.x)
            pts.emplace_back(left, cy - pa.halfWidth);
        for (int i = a; i <= b; ++i)
            pts.emplace_back(P[std::size_t(i)].x, cy - P[std::size_t(i)].halfWidth);
        if (right > pb.x) {
            pts.emplace_back(right, cy - pb.halfWidth);
            pts.emplace_back(right, cy + pb.halfWidth);
        }
        for (int i = b; i >= a; --i)
            pts.emplace_back(P[std::size_t(i)].x, cy + P[std::size_t(i)].halfWidth);
        if (left < pa.x)
            pts.emplace_back(left, cy + pa.halfWidth);
        pts.push_back(pts.front());
        f.bands.push_back(std::move(band));

        if (b < n - 1) {
            VariabilityFunnelBreak br;
            br.before = b + 1;
            const double mid = (pb.x + P[std::size_t(b + 1)].x) / 2;
            br.gapX1 = mid - kFunnelBreakGap / 2;
            br.gapX2 = mid + kFunnelBreakGap / 2;
            br.tick = neutralLine(GeometryRole::NeutralStrong, mid, cy - tickHalf, mid, cy + tickHalf);
            br.tick.dashed = true;
            f.breaks.push_back(br);
        }
        a = b + 1;
    }
    return f;
}

VariabilityTypicalityShape DataVariabilityGeometry::typicality(MarkForm form, double z,
                                                               const std::vector<VariabilityGhostInput> &ghosts,
                                                               const std::optional<VariabilityCorridor> &corridor)
{
    VariabilityTypicalityShape t;
    t.form = form == MarkForm::Icon ? MarkForm::Icon : MarkForm::Cell;
    t.z = z;
    const bool cell = t.form == MarkForm::Cell;

    if (cell) {
        t.outer = zone(GeometryRole::NeutralStrong, kTypCellCx, kTypCellCy, kTypOuterRx, kTypOuterRy, false, 1);
        t.inner = zone(GeometryRole::Text, kTypCellCx, kTypCellCy, kTypInnerRx, kTypInnerRy, true, kTypInnerAlpha);

        // Ghosts: the latest 12 with a z. |z| > 2.6 is held at the ring's stops, flagged, never dropped.
        std::vector<const VariabilityGhostInput *> kept;
        for (const auto &g : ghosts)
            if (!std::isnan(g.z))
                kept.push_back(&g);
        if (int(kept.size()) > kGhostMax)
            kept.erase(kept.begin(), kept.end() - kGhostMax);
        for (const VariabilityGhostInput *g : kept) {
            VariabilityGhost gh;
            gh.swingId = g->swingId;
            gh.z = g->z;
            gh.clamped = std::abs(g->z) > kClampZ;
            gh.dot.role = GeometryRole::Text3;
            gh.dot.x = gh.clamped ? (g->z < 0 ? kClampXLo : kClampXHi) : kTypCellCx + g->z * kTypCellSd;
            gh.dot.y = kTypCellCy + ghostJitter(g->swingId);
            gh.dot.r = gh.dot.rSmall = kGhostR;
            gh.dot.smallMaxSize = 0;   // §6.2 gives no small variant; explicit, so QML never picks on undefined
            gh.dot.alpha = kGhostAlpha;
            t.ghosts.push_back(gh);
        }
        t.corridor = bracket(corridor, kTypCellCx, kTypCellSd, 0, kTypCellW);
    } else {
        t.outer = zone(GeometryRole::NeutralStrong, kTypIconC, kTypIconC, kTypIconOuterR, kTypIconOuterR, false, 1);
        t.inner = zone(GeometryRole::Text, kTypIconC, kTypIconC, kTypIconInnerR, kTypIconInnerR, true,
                       kTypInnerAlpha);
        t.corridor = bracket(corridor, kTypIconC, kTypIconSd, 0, kTypIconW);
    }

    if (std::isnan(z))
        return t;   // no swing to place
    t.hasSwing = true;
    VariabilityRing ring;
    if (cell) {
        t.clamped = std::abs(z) > kClampZ;
        ring.cx = t.clamped ? (z < 0 ? kClampXLo : kClampXHi) : kTypCellCx + z * kTypCellSd;
        ring.cy = kTypCellCy;
        ring.r = kRingR;
        ring.width = kRingWidth;
    } else {
        t.clamped = std::abs(z) > kTypIconClampZ;
        ring.cx = kTypIconC + std::clamp(z, -kTypIconClampZ, kTypIconClampZ) * kTypIconSd;
        ring.cy = kTypIconC;
        ring.r = kTypIconRingR;
        ring.width = kTypIconRingWidth;
    }
    t.side = t.clamped ? (z < 0 ? -1 : 1) : 0;
    t.ring = ring;
    if (!cell)
        return t;

    VariabilityDot centre;
    centre.role = GeometryRole::Tone;
    centre.x = ring.cx;
    centre.y = ring.cy;
    centre.r = centre.rSmall = kCentreR;
    centre.alpha = 1;
    t.centre = centre;

    if (t.clamped) {
        // The left chevron, or its mirror about the cell's centre.
        const auto mx = [&](double x) { return t.side < 0 ? x : 2 * kTypCellCx - x; };
        VariabilityPolyline c;
        c.role = GeometryRole::Tone;
        c.points = {{mx(kChevronArmLo), kTypCellCy - kChevronHalfH},
                    {mx(kChevronTipLo), kTypCellCy},
                    {mx(kChevronArmLo), kTypCellCy + kChevronHalfH}};
        c.width = c.widthSmall = kChevronWidth;
        c.alpha = 1;
        c.roundCap = true;
        t.chevron = std::move(c);
    }
    return t;
}

VariabilityTraceShape DataVariabilityGeometry::typicalityTrace(const std::vector<double> &mean,
                                                               const std::vector<double> &sd,
                                                               const std::vector<double> &swing,
                                                               const std::optional<VariabilityCorridor> &corridor)
{
    VariabilityTraceShape t;
    // §6.2 gives the baseline only its y; it spans the traces' x and takes the Overlay baseline's
    // colorBorderStrong, 1 px (§4.5).
    t.baseline = neutralLine(GeometryRole::NeutralStrong, kTraceX0, kTraceBaseY, kTraceX1, kTraceBaseY);
    t.zeroY = kTraceBaseY;
    t.corridor = bracket(corridor, kTraceX0, kTraceX1 - kTraceX0, kTraceX0, kTraceX1);

    constexpr std::size_t N = kGridN;
    const auto whole = [](const std::vector<double> &v) {
        return v.size() == kGridN && std::all_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); });
    };
    if (!whole(mean) || !whole(sd) || !whole(swing)
        || std::any_of(sd.begin(), sd.end(), [](double x) { return x < 0; }))
        return t;
    t.valid = true;

    double maxPos = 0, maxNeg = 0;
    const auto see = [&](double v) {
        maxPos = std::max(maxPos, v);
        maxNeg = std::max(maxNeg, -v);
    };
    for (std::size_t i = 0; i < N; ++i) {
        see(mean[i] + sd[i]);
        see(mean[i] - sd[i]);
        see(swing[i]);
    }
    double k = std::numeric_limits<double>::infinity();
    if (maxPos > 0) k = std::min(k, kTracePeak / maxPos);
    if (maxNeg > 0) k = std::min(k, kTraceBelow / maxNeg);
    if (!std::isfinite(k)) k = 0;   // every value 0: flat on the baseline
    t.valueScale = k;
    t.maxPositive = maxPos;
    t.maxNegative = maxNeg;

    const auto X = [](std::size_t i) { return kTraceX0 + (kTraceX1 - kTraceX0) * double(i) / double(kGridN - 1); };
    const auto Y = [k](double v) { return kTraceBaseY - v * k; };

    VariabilityArea env;
    env.role = GeometryRole::Text;
    env.alpha = kEnvelopeAlpha;
    env.points.reserve(2 * N + 1);
    for (std::size_t i = 0; i < N; ++i)
        env.points.emplace_back(X(i), Y(mean[i] + sd[i]));
    for (std::size_t i = N; i-- > 0;)
        env.points.emplace_back(X(i), Y(mean[i] - sd[i]));
    env.points.push_back(env.points.front());
    t.envelope = std::move(env);

    VariabilityPolyline m;
    m.role = GeometryRole::Text3;
    m.width = m.widthSmall = kTraceMeanWidth;
    m.alpha = 1;
    m.roundCap = false;   // §6.2 gives the mean no cap; a dashed line takes the flat one
    m.dashed = true;
    VariabilityPolyline w;
    w.role = GeometryRole::Tone;
    w.width = w.widthSmall = kTraceSwingWidth;
    w.alpha = 1;
    w.roundCap = true;    // and round join (§6.2)
    m.points.reserve(N);
    w.points.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        m.points.emplace_back(X(i), Y(mean[i]));
        w.points.emplace_back(X(i), Y(swing[i]));
    }
    t.mean = std::move(m);
    t.swing = std::move(w);
    return t;
}

QJsonObject toJson(const VariabilityTypicalityShape &t)
{
    QJsonArray ghosts;
    for (const auto &g : t.ghosts)
        ghosts.append(toJson(g));
    return {{"kind", QStringLiteral("typicality")},
            {"form", int(t.form)},
            {"z", num(t.z)},
            {"hasSwing", t.hasSwing},
            {"clamped", t.clamped},
            {"side", t.side},
            {"dashPattern", dashPattern()},
            {"outer", toJson(t.outer)},
            {"inner", toJson(t.inner)},
            {"ghosts", ghosts},
            {"ring", optionalJson(t.ring)},
            {"centre", optionalJson(t.centre)},
            {"chevron", optionalJson(t.chevron)},
            {"corridor", optionalJson(t.corridor)}};
}

QJsonObject toJson(const VariabilityTraceShape &t)
{
    return {{"kind", QStringLiteral("typicalityTrace")},
            {"form", int(MarkForm::Cell)},
            {"valid", t.valid},
            {"dashPattern", dashPattern()},
            {"baseline", toJson(t.baseline)},
            {"envelope", optionalJson(t.envelope)},
            {"mean", optionalJson(t.mean)},
            {"swing", optionalJson(t.swing)},
            {"corridor", optionalJson(t.corridor)},
            {"valueScale", t.valueScale},
            {"zeroY", t.zeroY},
            {"maxPositive", t.maxPositive},
            {"maxNegative", t.maxNegative}};
}

QJsonObject toJson(const VariabilityFunnelShape &f)
{
    QJsonArray sessions, bands, breaks;
    for (const auto &p : f.sessions)
        sessions.append(QJsonObject{{"input", p.input}, {"rho", p.rho}, {"grade", p.grade},
                                    {"halfWidth", p.halfWidth}, {"x", p.x}, {"run", p.run},
                                    {"current", p.current}, {"marker", toJson(p.marker)}});
    for (const auto &b : f.bands)
        bands.append(QJsonObject{{"points", flat(b.points)}, {"first", b.first}, {"last", b.last},
                                 {"fillRole", geometryRoleToken(b.fillRole)}, {"fillAlpha", b.fillAlpha},
                                 {"edgeRole", geometryRoleToken(b.edgeRole)}, {"edgeAlpha", b.edgeAlpha},
                                 {"edgeWidth", b.edgeWidth}});
    for (const auto &b : f.breaks)
        breaks.append(QJsonObject{{"before", b.before}, {"gapX1", b.gapX1}, {"gapX2", b.gapX2},
                                  {"tick", toJson(b.tick)}});
    return {{"kind", QStringLiteral("funnel")},
            {"width", f.width},
            {"height", f.height},
            {"requestedWidth", num(f.requestedWidth)},
            {"minWidth", kFunnelMinWidthPx},
            {"currentGrade", f.currentGrade},
            {"currentHasReading", f.currentHasReading},
            {"trendAfter3", f.trendAfter3},
            {"latestRun", f.latestRun},
            {"rolledOff", f.rolledOff},
            {"dashPattern", dashPattern()},
            {"centre", toJson(f.centre)},
            {"sessions", sessions},
            {"bands", bands},
            {"breaks", breaks}};
}

// ── The QML singleton ───────────────────────────────────────────────────────────────────────────────

VariabilityGeometryQml::VariabilityGeometryQml(QJSEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine), m_geometry(makeCanonicalVariabilityGeometry())
{
}

VariabilityGeometryQml *VariabilityGeometryQml::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    return new VariabilityGeometryQml(jsEngine);
}

QJSValue VariabilityGeometryQml::shape(int variant, int form, int grade)
{
    const bool known = variant >= 0 && variant < kMarkVariantCount && form >= 0 && form < kMarkFormCount;
    // Unknown variants and forms share ONE empty object, so a stray int cannot grow the cache.
    const int key = known ? (variant * kMarkFormCount + form) * (kMarkGradeMax + 1) + clampGrade(grade) : -1;
    if (const auto it = m_cache.constFind(key); it != m_cache.constEnd())
        return *it;
    if (!m_engine)
        return QJSValue();
    const VariabilityShape &s = m_geometry->shape(MarkVariant(known ? variant : 0), MarkForm(known ? form : 0),
                                                  grade);
    QJsonObject json = toJson(known ? s : VariabilityShape{});
    if (!known) {
        json[QStringLiteral("variant")] = -1;
        json[QStringLiteral("form")]    = -1;
    }
    QJSValue js = m_engine->toScriptValue(json);
    m_cache.insert(key, js);
    return js;
}

QPolygonF VariabilityGeometryQml::tracePath(int variant, int form, int grade, int index, qreal k) const
{
    const VariabilityShape &s = m_geometry->shape(MarkVariant(variant), MarkForm(form), grade);
    if (index < 0 || std::size_t(index) >= s.traces.size())
        return {};
    return scaled(s.traces[std::size_t(index)].points, k);
}

QPolygonF VariabilityGeometryQml::wedgePath(int variant, int form, int grade, int index, qreal k) const
{
    const VariabilityShape &s = m_geometry->shape(MarkVariant(variant), MarkForm(form), grade);
    if (index < 0 || std::size_t(index) >= s.wedges.size())
        return {};
    return scaled(s.wedges[std::size_t(index)].points, k);
}

namespace {

// The most elements read from one array-like input: a guard against a stray object whose `length` is
// huge, far above anything a funnel (12), a ghost list (12) or a trace (101) ever passes.
constexpr int kMaxJsElements = 1 << 16;

// The C++ value a JS object wraps, if it wraps one: a QVariantList or other C++ value handed to QML
// arrives as a wrapper object, not a JS Array. (RetainJSObjects leaves a genuine JS object as a QJSValue
// variant rather than converting it, and QJSValue::isVariant() is deprecated since Qt 6.9.)
QVariant wrappedVariant(const QJSValue &v)
{
    if (!v.isObject())
        return {};
    QVariant var = v.toVariant(QJSValue::RetainJSObjects);
    if (var.metaType() == QMetaType::fromType<QJSValue>())
        return {};   // a plain JS object
    return var;
}

// A number, else `fallback` (missing, null, a string, a bool: no number). A value that arrived wrapping
// a C++ number counts.
double jsToNumber(const QJSValue &v, double fallback = kNaN)
{
    if (v.isNumber())
        return v.toNumber();
    if (const QVariant var = wrappedVariant(v); var.isValid()) {
        switch (var.metaType().id()) {
        case QMetaType::Double: case QMetaType::Float: case QMetaType::Int: case QMetaType::UInt:
        case QMetaType::LongLong: case QMetaType::ULongLong: case QMetaType::Short: case QMetaType::UShort:
            return var.toDouble();
        default:
            break;
        }
    }
    return fallback;
}

double jsNumber(const QJSValue &object, const char *key, double fallback = kNaN)
{
    return jsToNumber(object.property(QString::fromLatin1(key)), fallback);
}

// The elements of ANY array-like input, in order, each as a JS value. The one reader every
// per-instance entry point uses, because what QML hands over depends on where the list came from:
//   - a JS Array (a literal, Array.from, a JS-built list): isArray();
//   - a list held in a QML `var` property that came from C++ or createObject (a QVariantList): a
//     sequence wrapper (numeric `length`, read in place) or a wrapped QVariant (read through
//     toList()), for both of which isArray() is FALSE;
//   - any object with a numeric `length` and indexed elements (an array-like).
// Anything else (null, undefined, a number, a string, a plain object) has no elements.
std::vector<QJSValue> jsElements(QJSEngine *engine, const QJSValue &list)
{
    std::vector<QJSValue> out;
    const auto byIndex = [&](double length) {
        const int n = std::isfinite(length) ? int(std::clamp(length, 0.0, double(kMaxJsElements))) : 0;
        out.reserve(std::size_t(n));
        for (int i = 0; i < n; ++i)
            out.push_back(list.property(quint32(i)));
    };
    if (list.isArray()) {
        byIndex(list.property(QStringLiteral("length")).toNumber());
        return out;
    }
    if (list.isObject()) {
        const QJSValue length = list.property(QStringLiteral("length"));
        if (length.isNumber()) {   // a sequence wrapper or any array-like: read it in place
            byIndex(length.toNumber());
            return out;
        }
    }
    if (const QVariant var = wrappedVariant(list); var.isValid()) {
        if (var.metaType().id() == QMetaType::QVariantList || var.metaType().id() == QMetaType::QStringList
            || (var.metaType().id() != QMetaType::QVariantMap && var.canConvert<QVariantList>())) {
            const QVariantList items = var.toList();
            const int n = std::min(int(items.size()), kMaxJsElements);
            out.reserve(std::size_t(n));
            for (int i = 0; i < n; ++i)
                out.push_back(engine ? engine->toScriptValue(items[i]) : QJSValue());
            return out;
        }
    }
    return out;
}

// An array-like of numbers; a non-number element is NaN (which every builder treats as "no value").
std::vector<double> jsNumbers(QJSEngine *engine, const QJSValue &list)
{
    std::vector<double> out;
    for (const QJSValue &v : jsElements(engine, list))
        out.push_back(jsToNumber(v));
    return out;
}

std::optional<VariabilityCorridor> jsCorridor(const QJSValue &c, const char *lo, const char *hi)
{
    if (!c.isObject())   // null, undefined, a number: no corridor
        return std::nullopt;
    const VariabilityCorridor r{jsNumber(c, lo), jsNumber(c, hi)};
    if (!std::isfinite(r.lo) || !std::isfinite(r.hi))
        return std::nullopt;
    return r;
}

} // namespace

QJSValue VariabilityGeometryQml::funnel(const QJSValue &sessions, qreal widthPx, qreal heightPx) const
{
    if (!m_engine)
        return QJSValue();
    std::vector<VariabilityFunnelSession> in;
    for (const QJSValue &s : jsElements(m_engine, sessions)) {
        VariabilityFunnelSession p;
        p.rho = jsNumber(s, "rho");
        const double g = jsNumber(s, "grade", 0);
        p.grade = std::isfinite(g) ? int(g) : 0;
        p.breakBefore = s.property(QStringLiteral("breakBefore")).toBool();
        in.push_back(p);
    }
    return m_engine->toScriptValue(toJson(DataVariabilityGeometry::funnel(in, widthPx, heightPx)));
}

QJSValue VariabilityGeometryQml::typicality(int form, qreal z, const QJSValue &ghosts, const QJSValue &corridor) const
{
    if (!m_engine)
        return QJSValue();
    std::vector<VariabilityGhostInput> in;
    for (const QJSValue &g : jsElements(m_engine, ghosts)) {
        const QJSValue id = g.property(QStringLiteral("swingId"));
        in.push_back({id.isUndefined() || id.isNull() ? QString() : id.toString(), jsNumber(g, "z")});
    }
    const MarkForm f = form == int(MarkForm::Icon) ? MarkForm::Icon : MarkForm::Cell;
    return m_engine->toScriptValue(
        toJson(DataVariabilityGeometry::typicality(f, z, in, jsCorridor(corridor, "loZ", "hiZ"))));
}

QJSValue VariabilityGeometryQml::typicalityTrace(const QJSValue &mean, const QJSValue &sd, const QJSValue &swing,
                                                 const QJSValue &corridor) const
{
    if (!m_engine)
        return QJSValue();
    return m_engine->toScriptValue(toJson(DataVariabilityGeometry::typicalityTrace(
        jsNumbers(m_engine, mean), jsNumbers(m_engine, sd), jsNumbers(m_engine, swing),
        jsCorridor(corridor, "loT", "hiT"))));
}

QPolygonF VariabilityGeometryQml::polygon(const QList<qreal> &points, qreal k) const
{
    QPolygonF p;
    p.reserve(points.size() / 2);
    for (qsizetype i = 0; i + 1 < points.size(); i += 2)
        p.append(QPointF(points[i] * k, points[i + 1] * k));
    return p;
}

} // namespace pinpoint::variability
