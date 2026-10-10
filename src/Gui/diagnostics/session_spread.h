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

#include "../../Analysis/diagnostic_ledger.h"
#include "../../Diagnostics/norm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// HOW FAR, NOT ONLY WHETHER — the arithmetic behind the corridor strip, the value-by-shot run and
// the across-sessions columns on the session diagnostics panel.
//
// THE TICK RUN SAYS WHETHER EACH SHOT WAS OUTSIDE THE CORRIDOR AND NEVER HOW FAR, and on the
// session that prompted this (2026-10-07, over the top, 46 of 50 fired) that hid the whole story:
// the readings had fallen from a median of 22 % of the hand rise on 4 July to 13 %, and every one
// of those 46 ticks was the same red. This file is what puts the distance back, in the measure's
// own units, against the bands the app actually grades with.
//
// PURE AND HEADER-ONLY, for the reason dag_layout.h and corridor_plot.h are: the axis, the bands,
// the stacking and the clipping are the only things about these pictures that CAN be tested, and
// geometry worked out inside QML bindings is geometry nothing can assert. The model calls these and
// publishes the result; the QML positions what it is given (PpSessionDiagnosticsBody's header).
//
// ── What the pictures may and may not claim ────────────────────────────────────────────────────
//
// THE BANDS ARE THE ONES THAT GRADE. Every edge comes from bandEdgesOf() and deviationEdges() —
// the two definitions grade() itself applies, monitor-band precedence included — so a dot drawn
// in the Watch band is a reading that graded Watch. A second derivation is how a surface draws a
// corridor the app does not use; norm.h documents that as a bug that already happened once.
//
// AN OPEN TAIL IS NEUTRAL, NOT GOOD. On a ceiling only the high tail grades and everything below
// the aspiration point is "not graded", which is a different statement from "Ideal". corridor_plot
// paints that side Ideal because its reader is an AUTHOR setting the corridor; this one's reader is
// the golfer, who would read green as praise for a direction the norm deliberately says nothing
// about. So the open side is its own band, `open`, and the panel paints it as background.
//
// THE CURVE IS THE NORM'S, NEVER THE GOLFER'S. It is the split normal the norm claims, peak-
// normalised, drawn over the GRADED side only on a one-sided corridor — the open tail has no
// tolerance to draw. Nothing in this file fits a distribution to the shots: a Gaussian fitted to
// one golfer's session would be a second corridor wearing the first one's clothes, and the whole
// point of the strip is the distance between what the norm claims and where the swings landed.
//
// ONE SHOT MUST NOT CRUSH THE SCALE. The axis spans the bands and the shots, EXCEPT a shot so far
// out that including it would squeeze the rest into a few pixels; that one is drawn at the edge as
// a marker carrying its value (`clipped` ±1). The fence is generous — three spreads past the
// quartiles — so a clipped dot is a genuine outlier and never a shot that was merely bad.

namespace pinpoint::analysis {

// The corridor in the measure's own units, every edge the bands are drawn from. On an OPEN side
// every edge collapses onto mu — the same convention NormBandEdges uses, for the same reason: a
// sentinel crossing into QML becomes a corridor at the origin.
struct SpreadCorridor {
    bool   known    = false;     // false: nothing to grade against — no bands, no curve, no fault line
    bool   fromNorm = false;     // false: reconstructed from a row's stored Ideal band (no norm resolved)
    CorridorShape shape = CorridorShape::Unknown;
    bool   lowOpen  = false;     // this tail does not grade (a ceiling)
    bool   highOpen = false;     // this tail does not grade (a floor)
    double mu = 0.0, sigmaLo = 0.0, sigmaHi = 0.0;
    double idealLo  = 0.0, idealHi  = 0.0;   // policy.idealMaxZ
    double signalLo = 0.0, signalHi = 0.0;   // where the signal fires: Good ends, Watch begins
    double faultLo  = 0.0, faultHi  = 0.0;   // where Action begins — the authored fault line
};

// Ideal ≤ signal ≤ fault on each graded side, whatever an explicit monitor band says. A monitor
// bound inside the Good edge is legal content (it caps Watch), and without this the bands would
// overlap and one would be painted over another.
inline void orderSpreadEdges(SpreadCorridor &c)
{
    c.faultHi  = std::max(c.faultHi, c.mu);
    c.signalHi = std::clamp(c.signalHi, c.mu, c.faultHi);
    c.idealHi  = std::clamp(c.idealHi,  c.mu, c.signalHi);
    c.faultLo  = std::min(c.faultLo, c.mu);
    c.signalLo = std::clamp(c.signalLo, c.faultLo, c.mu);
    c.idealLo  = std::clamp(c.idealLo,  c.signalLo, c.mu);
}

// From the norm the model resolved — the normal case, and the only one that knows about explicit
// monitor bands and asymmetric tolerances.
inline SpreadCorridor spreadCorridorFromNorm(const Norm &n, Shape shape, const GradePolicy &policy)
{
    SpreadCorridor c;
    c.known    = true;
    c.fromNorm = true;
    c.shape    = shape == Shape::Ceiling ? CorridorShape::Ceiling
               : shape == Shape::Floor   ? CorridorShape::Floor
                                         : CorridorShape::TwoSided;
    c.mu       = n.mu;
    c.sigmaLo  = n.sigmaLo;
    c.sigmaHi  = n.sigmaHi;

    const NormBandEdges e = bandEdgesOf(n, shape, policy, /*marginOverride*/ -1.0);
    c.lowOpen  = e.lowOpen;
    c.highOpen = e.highOpen;
    c.idealLo  = e.idealLo;
    c.idealHi  = e.idealHi;
    c.faultLo  = e.watchLo;
    c.faultHi  = e.watchHi;

    double dLo = 0.0, dHi = 0.0;
    deviationEdges(n, shape, policy, dLo, dHi);
    // ±infinity on an open tail — nothing there deviates. Collapsed onto mu like every other edge
    // on that side, never carried as a sentinel.
    c.signalLo = std::isfinite(dLo) ? dLo : n.mu;
    c.signalHi = std::isfinite(dHi) ? dHi : n.mu;
    if (c.lowOpen)  c.idealLo = c.signalLo = c.faultLo = n.mu;
    if (c.highOpen) c.idealHi = c.signalHi = c.faultHi = n.mu;

    if (!(c.sigmaLo > 0.0) && !(c.sigmaHi > 0.0)) c.known = false;   // a norm that admits only mu
    orderSpreadEdges(c);
    return c;
}

// WHEN NO NORM RESOLVES, from what the row stored. The row's corridor IS the policy's Ideal band
// (MeasureEvidence: greenLo/greenHi off bandEdgesOf), so the tolerance is recoverable by dividing
// the Ideal width by idealMaxZ:
//   twoSided  lo, hi are mu ∓ idealMaxZ·σ   → mu = (lo+hi)/2, σ = (hi−lo)/(2·idealMaxZ)
//   ceiling   lo IS mu, hi = mu + idealMaxZ·σ → σ = (hi−lo)/idealMaxZ, the low side open
//   floor     hi IS mu, lo = mu − idealMaxZ·σ → the mirror
// What it cannot recover is an asymmetric two-sided norm (the midpoint is not mu then) or an
// explicit monitor band. Both are why the norm comes first; this is the honest second best, and
// `fromNorm` false says which one was drawn.
inline SpreadCorridor spreadCorridorFromRow(const ConditionRow &r, const GradePolicy &policy)
{
    SpreadCorridor c;
    const double w  = r.corridorHi - r.corridorLo;
    const double iz = policy.idealMaxZ > 0.0 ? policy.idealMaxZ : 1.0;
    if (!(w > 0.0) || !std::isfinite(w)) return c;

    switch (r.corridorShape) {
    case CorridorShape::TwoSided:
        c.mu = 0.5 * (r.corridorLo + r.corridorHi);
        c.sigmaLo = c.sigmaHi = 0.5 * w / iz;
        break;
    case CorridorShape::Ceiling:
        c.mu = r.corridorLo;
        c.sigmaLo = c.sigmaHi = w / iz;
        c.lowOpen = true;
        break;
    case CorridorShape::Floor:
        c.mu = r.corridorHi;
        c.sigmaLo = c.sigmaHi = w / iz;
        c.highOpen = true;
        break;
    case CorridorShape::Unknown:
    case CorridorShape::None:
        return c;
    }
    c.known = true;
    c.shape = r.corridorShape;
    c.idealLo  = c.lowOpen  ? c.mu : c.mu - policy.idealMaxZ * c.sigmaLo;
    c.idealHi  = c.highOpen ? c.mu : c.mu + policy.idealMaxZ * c.sigmaHi;
    c.signalLo = c.lowOpen  ? c.mu : c.mu - policy.goodMaxZ  * c.sigmaLo;
    c.signalHi = c.highOpen ? c.mu : c.mu + policy.goodMaxZ  * c.sigmaHi;
    c.faultLo  = c.lowOpen  ? c.mu : c.mu - policy.watchMaxZ * c.sigmaLo;
    c.faultHi  = c.highOpen ? c.mu : c.mu + policy.watchMaxZ * c.sigmaHi;
    orderSpreadEdges(c);
    return c;
}

// The norm's own curve, peak-normalised: two half-normals meeting at mu. The same two-piece normal
// corridor_plot.h's splitNormalPeakNormalised() draws for the corridor editor — restated here
// rather than linked, because the model is compiled into four tool and test targets that do not
// carry corridor_plot.cpp, and the formula is the whole of it.
inline double spreadCurveDensity(double x, double mu, double sigmaLo, double sigmaHi)
{
    const double sigma = (x < mu) ? sigmaLo : sigmaHi;
    if (!(sigma > 0.0)) return 0.0;
    const double z = (x - mu) / sigma;
    return std::exp(-0.5 * z * z);
}

// A sorted sample's quantile, linear between order statistics (type 7, R's default). Empty → NaN.
inline double spreadQuantile(std::vector<double> sorted, double q)
{
    if (sorted.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(sorted.begin(), sorted.end());
    const double h  = (double(sorted.size()) - 1.0) * std::clamp(q, 0.0, 1.0);
    const size_t lo = size_t(std::floor(h));
    const size_t hi = std::min(sorted.size() - 1, lo + 1);
    return sorted[lo] + (h - double(lo)) * (sorted[hi] - sorted[lo]);
}

// One graded region along the axis. `grade` is the band's word — ideal | good | watch | action, or
// `open` for the side that does not grade — so the QML colours by meaning and never by position.
struct SpreadBand {
    QString grade;
    double  lo = 0.0;
    double  hi = 0.0;
};

struct SpreadAxis {
    double lo = 0.0;
    double hi = 1.0;
    int    decimals = 1;

    // Where `v` sits along the axis, 0..1, and whether it had to be pinned to an end to get there.
    double fraction(double v, int *clipped = nullptr) const
    {
        int c = 0;
        if (v < lo) { v = lo; c = -1; }
        if (v > hi) { v = hi; c = +1; }
        if (clipped) *clipped = c;
        const double span = hi - lo;
        return span > 0.0 ? (v - lo) / span : 0.5;
    }
};

// HOW MANY DECIMALS THE STRIP QUOTES, off the axis it is drawn on and nothing else. Whole units
// once the axis spans twenty of them — "13 %", not "12.5 %", is what a golfer reads off a picture
// thirty units wide — one decimal down to two units, two below that. NEVER off σ: a reading is
// not quantised by its uncertainty (feedback, 3 Oct 2026; displayStep was removed for it).
inline int spreadDecimals(double span)
{
    if (!(span > 0.0)) return 1;
    if (span >= 20.0) return 0;
    if (span >= 2.0)  return 1;
    return 2;
}

// The axis: the bands' extent and every reading inside the fence, padded a little each side.
//
// THE BANDS' EXTENT. A two-sided corridor out to a quarter of the graded width past each fault
// line, so Action is a visible band rather than a hairline; a one-sided one the same past its
// fault line, and NOTHING into the open side — that side reaches only as far as the shots that
// landed there, and the padding alone shows where the aspiration point is. A fixed reach into a
// side nobody's shots were on was a quarter of the strip spent on nothing (and on a ceiling at 0,
// on readings the measure cannot take).
//
// THE FENCE. Three spreads past the quartiles, the spread being the interquartile range or the
// norm's own σ, whichever is larger — so a tight session cannot make its own ordinary misses look
// like outliers, and a session with no corridor still gets a sensible scale.
inline SpreadAxis spreadAxisFor(const SpreadCorridor &c, const std::vector<double> &values)
{
    double lo =  std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    double sigma = 0.0;
    // The open side's reach for the DECIMALS only: half the graded width, as the axis itself once
    // reached. Trimming empty axis off the drawing must not turn "13 %" into "13.0 %".
    double openReach = 0.0;

    if (c.known) {
        const double wLo = c.lowOpen  ? 0.0 : c.mu - c.faultLo;
        const double wHi = c.highOpen ? 0.0 : c.faultHi - c.mu;
        lo = c.lowOpen  ? c.mu : c.faultLo - 0.25 * wLo;
        hi = c.highOpen ? c.mu : c.faultHi + 0.25 * wHi;
        sigma = std::max(c.sigmaLo, c.sigmaHi);
        openReach = (c.lowOpen || c.highOpen) ? 0.5 * std::max(wLo, wHi) : 0.0;
    }

    std::vector<double> finite;
    finite.reserve(values.size());
    for (double v : values) if (std::isfinite(v)) finite.push_back(v);

    if (!finite.empty()) {
        const double q1 = spreadQuantile(finite, 0.25);
        const double q3 = spreadQuantile(finite, 0.75);
        const double spread = std::max({ q3 - q1, sigma, 1e-9 });
        const double fenceLo = q1 - 3.0 * spread;
        const double fenceHi = q3 + 3.0 * spread;
        for (double v : finite) {
            if (v < fenceLo || v > fenceHi) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }

    if (!std::isfinite(lo) || !std::isfinite(hi)) { lo = 0.0; hi = 1.0; }
    if (!(hi > lo)) {
        const double half = std::max(std::fabs(lo) * 0.1, 1.0);
        lo -= half;
        hi += half;
    }
    const double pad = 0.04 * (hi - lo);
    SpreadAxis a;
    a.lo = lo - pad;
    a.hi = hi + pad;
    const double wordLo = c.known && c.lowOpen  ? std::min(lo, c.mu - openReach) : lo;
    const double wordHi = c.known && c.highOpen ? std::max(hi, c.mu + openReach) : hi;
    a.decimals = spreadDecimals((1.0 + 2.0 * 0.04) * (wordHi - wordLo));
    return a;
}

// The bands, left to right, clipped to the axis. Empty when there is no corridor: an ungraded
// measure gets dots and a median and nothing pretending to be a judgement.
inline std::vector<SpreadBand> spreadBands(const SpreadCorridor &c, const SpreadAxis &a)
{
    std::vector<SpreadBand> out;
    if (!c.known) return out;
    auto add = [&](const char *g, double from, double to) {
        from = std::max(from, a.lo);
        to   = std::min(to,   a.hi);
        if (to > from) out.push_back(SpreadBand{ QString::fromLatin1(g), from, to });
    };
    if (c.lowOpen) {
        add("open", a.lo, c.mu);
    } else {
        add("action", a.lo,       c.faultLo);
        add("watch",  c.faultLo,  c.signalLo);
        add("good",   c.signalLo, c.idealLo);
        add("ideal",  c.idealLo,  c.mu);
    }
    if (c.highOpen) {
        add("open", c.mu, a.hi);
    } else {
        add("ideal",  c.mu,       c.idealHi);
        add("good",   c.idealHi,  c.signalHi);
        add("watch",  c.signalHi, c.faultHi);
        add("action", c.faultHi,  a.hi);
    }
    // The two halves of a two-sided Ideal band meet at mu; one region, not two.
    for (size_t i = 1; i < out.size(); ) {
        if (out[i].grade == out[i - 1].grade && std::fabs(out[i].lo - out[i - 1].hi) < 1e-12) {
            out[i - 1].hi = out[i].hi;
            out.erase(out.begin() + long(i));
        } else {
            ++i;
        }
    }
    return out;
}

// The norm's curve as (value, density) pairs across the axis — the graded side only on a
// one-sided corridor, which is what makes it a HALF curve there rather than a bell the norm does
// not claim. Empty without a corridor.
inline std::vector<std::pair<double, double>> spreadCurve(const SpreadCorridor &c, const SpreadAxis &a,
                                                          int steps = 96)
{
    std::vector<std::pair<double, double>> out;
    if (!c.known || steps < 2) return out;
    const double span = a.hi - a.lo;
    for (int i = 0; i <= steps; ++i) {
        double v = a.lo + span * double(i) / double(steps);
        if (c.lowOpen  && v < c.mu) continue;
        if (c.highOpen && v > c.mu) continue;
        out.emplace_back(v, spreadCurveDensity(v, c.mu, c.sigmaLo, c.sigmaHi));
    }
    // A half curve starts exactly AT mu, not at the first sample past it, so its peak is drawn.
    if (c.lowOpen && (out.empty() || out.front().first > c.mu) && c.mu >= a.lo && c.mu <= a.hi)
        out.insert(out.begin(), { c.mu, 1.0 });
    if (c.highOpen && (out.empty() || out.back().first < c.mu) && c.mu >= a.lo && c.mu <= a.hi)
        out.emplace_back(c.mu, 1.0);
    return out;
}

// A SMALL BEESWARM, in value space. The axis is cut into `bins` columns and each dot takes the next
// free row in its column, in shot order — so a session's first swing in a column sits on the line
// and later ones stack off it. The QML alternates rows above and below the line and chooses the
// pitch to fit its height; what it never has to do is decide which dots collide.
inline std::vector<int> spreadStacks(const std::vector<double> &fractions, int bins = 40)
{
    std::vector<int> out(fractions.size(), 0);
    if (bins < 1) return out;
    std::vector<int> used(size_t(bins), 0);
    for (size_t i = 0; i < fractions.size(); ++i) {
        const int b = std::clamp(int(std::floor(fractions[i] * bins)), 0, bins - 1);
        out[i] = used[size_t(b)]++;
    }
    return out;
}

// THE ROLLING MEDIAN for the run chart — TRAILING, over the last `window` ASSESSABLE shots up to
// and including each one, so the line at shot 30 is what the golfer had been doing as of shot 30
// and never borrows from swings not yet struck (the live panel draws it as they arrive). A point
// is emitted only once at least half the window (rounded up) is in hand: a median of one swing
// is that swing, and drawing it as a trend would be the single-shot claim the ledger refuses.
//
// `values[i]` is NaN for a shot that was not assessable; it contributes nothing and gets no
// point, and the line carries on across it rather than dropping to a gap or to zero.
inline std::vector<double> trailingMedians(const std::vector<double> &values, int window)
{
    std::vector<double> out(values.size(), std::numeric_limits<double>::quiet_NaN());
    if (window < 1) window = 1;
    const size_t need = size_t((window + 1) / 2);
    std::vector<double> recent;
    for (size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) continue;
        recent.push_back(values[i]);
        if (recent.size() > size_t(window)) recent.erase(recent.begin());
        if (recent.size() >= need) out[i] = spreadQuantile(recent, 0.5);
    }
    return out;
}

// ── Saying it in words ──────────────────────────────────────────────────────────────────────
//
// ONE FORMATTER for every number these surfaces quote, so the strip's fault label, the hover
// readout, the card's corridor sentence and the work-ons line cannot print the same edge two
// ways. The true minus; a value that rounds to zero printed as 0, never "−0"; a space before a
// worded unit ("13 % hand rise", "92 mph") and none before a degree sign ("13°"); and "-" where
// there is no number (the panel's NA marker). Decimals come from the CALLER — an axis's span, or
// a corridor's — and never from σ.
inline QString spreadNumber(double v, int decimals, bool trimZeros = false)
{
    if (!std::isfinite(v)) return QStringLiteral("-");
    const double scale = std::pow(10.0, decimals);
    double r = std::round(v * scale) / scale;
    if (r == 0.0) r = 0.0;
    QString n = QString::number(std::fabs(r), 'f', std::max(0, decimals));
    if (trimZeros && n.contains(QLatin1Char('.'))) {
        while (n.endsWith(QLatin1Char('0'))) n.chop(1);
        if (n.endsWith(QLatin1Char('.'))) n.chop(1);
    }
    return r < 0.0 ? QString(QChar(0x2212)) + n : n;
}

inline QString spreadWithUnit(const QString &number, const QString &unit)
{
    if (unit.isEmpty() || number == QLatin1String("-")) return number;
    if (unit.startsWith(QChar(0x00B0))) return number + unit;
    return number + QLatin1Char(' ') + unit;
}

// THE CORRIDOR AS A SENTENCE, stating the two edges the strip draws: where the PASS band ends (the
// signal edge — inside it the condition does not fire) and where the FAULT line is (Action). It
// replaces "pass 0.0 to 4.3", which quoted the policy's Ideal band — mu to mu + σ — and so put a
// number on the card that appears nowhere on the strip under it and is not where anything fires.
//
//   ceiling   "pass up to 8.7 · fault at 13 % hand rise"
//   floor     "pass from 1.3 up · fault at 1.2"
//   twoSided  "pass 5 to 25 · fault outside 0 to 30 mm"
//
// The unit once, at the end, where it reads for both numbers. Decimals off the corridor's own span
// (spreadDecimals) with trailing zeros dropped, so 13/3·2 reads 8.7 and 13 reads 13, not 13.0.
inline QString spreadCorridorWords(const SpreadCorridor &c, const QString &unit)
{
    if (!c.known) return QStringLiteral("no corridor authored");
    const double span = std::max(c.faultHi - c.faultLo, 1e-9);
    const int d = spreadDecimals(span);
    auto n = [&](double v) { return spreadNumber(v, d, /*trimZeros*/ true); };
    if (c.lowOpen)
        return spreadWithUnit(QStringLiteral("pass up to %1 · fault at %2").arg(n(c.signalHi), n(c.faultHi)), unit);
    if (c.highOpen)
        return spreadWithUnit(QStringLiteral("pass from %1 up · fault at %2").arg(n(c.signalLo), n(c.faultLo)), unit);
    return spreadWithUnit(QStringLiteral("pass %1 to %2 · fault outside %3 to %4")
                              .arg(n(c.signalLo), n(c.signalHi), n(c.faultLo), n(c.faultHi)), unit);
}

// WHERE ONE READING SITS against the corridor, in words, for the hover readout. Past the fault
// line by how much; past the pass band and how far short of the fault line; or inside the pass
// band. The side that does not grade is inside the pass band — that is what not grading means.
//
// THE DISTANCE CARRIES NO UNIT. The readout already prints the reading with its unit one slot to
// the left ("28 % hand rise · 15 past the fault line"); a second "% hand rise" on the distance
// doubled the line's length and said nothing the first one had not.
inline QString spreadPositionWords(const SpreadCorridor &c, double v, int decimals)
{
    if (!std::isfinite(v)) return QStringLiteral("-");
    if (!c.known) return QStringLiteral("no corridor to read it against");
    auto amount = [&](double x) { return spreadNumber(x, decimals); };
    // A distance that ROUNDS to nothing at the readout's precision is "at the fault line": "0 past
    // the fault line" (13.04 against 13, quoted in whole units) reads as a contradiction, and
    // "0 short of" as a reading that cannot be both past the pass band and on the line.
    auto zero = [&](double x) { return std::round(x * std::pow(10.0, decimals)) == 0.0; };
    const QString atLine = QStringLiteral("at the fault line");
    if (!c.highOpen && v > c.faultHi)
        return zero(v - c.faultHi) ? atLine : QStringLiteral("%1 past the fault line").arg(amount(v - c.faultHi));
    if (!c.lowOpen  && v < c.faultLo)
        return zero(c.faultLo - v) ? atLine : QStringLiteral("%1 past the fault line").arg(amount(c.faultLo - v));
    if (!c.highOpen && v > c.signalHi)
        return zero(c.faultHi - v) ? atLine
             : QStringLiteral("past the pass band, %1 short of the fault line").arg(amount(c.faultHi - v));
    if (!c.lowOpen && v < c.signalLo)
        return zero(v - c.faultLo) ? atLine
             : QStringLiteral("past the pass band, %1 short of the fault line").arg(amount(v - c.faultLo));
    return QStringLiteral("inside the pass band");
}

// ROUND TICKS for a value axis — the run's and the across-sessions chart's Y labels. A 1-2-5 step
// chosen to land about `target` ticks inside the axis, every tick a multiple of the step so the
// labels read 0 5 10 15, never 2.7 7.9 13.1. Values only; the caller formats them on the axis's
// own decimals (spreadDecimals), never on σ.
inline std::vector<double> spreadTickValues(const SpreadAxis &a, int target = 5)
{
    std::vector<double> out;
    const double span = a.hi - a.lo;
    if (!(span > 0.0) || target < 1) return out;
    const double raw  = span / double(target);
    const double mag  = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double step = (norm < 1.5 ? 1.0 : norm < 3.5 ? 2.0 : norm < 7.5 ? 5.0 : 10.0) * mag;
    for (double v = std::ceil(a.lo / step) * step; v <= a.hi + step * 1e-9; v += step)
        out.push_back(std::fabs(v) < step * 1e-9 ? 0.0 : v);
    return out;
}

} // namespace pinpoint::analysis
