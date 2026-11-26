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

// The launch monitor panel's two INFERRED reads — the nine-window flight shape and
// strike quality. Pure, header-only, no Qt-GUI. Sibling of lm_session_reductions.h and
// lm_flight_path.h, same rule: QML positions and paints, C++ decides the numbers.
//
// THESE ARE CLAIMS THE DEVICE DID NOT MAKE. Everything else on this panel is a reading
// passed through; these two are conclusions drawn from readings, and the whole panel's
// credibility rests on the difference being visible. Two things enforce it:
//
//   1. The UI labels both INFERRED. That is the panel's job, not this header's.
//   2. EVERY READ CARRIES ITS EVIDENCE. `evidence` is the audit trail — the measured
//      values the classification came from, in words. A reader who disagrees with
//      "Pull-fade" can see it was 1.3° left with the face 1.6° open to the path and
//      decide for themselves. It is formatted HERE, beside the thresholds that produced
//      it, so a future export cannot quote a different justification from the card.
//
// A MISSING INPUT YIELDS NO READ. Not "Straight", not "flushed" — nothing. The absence
// of a measurement and a measurement of zero are different facts (the same rule that
// governs which tiles exist at all, lm_session_reductions.h rule 3), and a default read
// would be the panel inventing the one kind of number it exists to avoid inventing.
//
// Unit-tested standalone in src/Analysis/tests/lm_inferred_reads_test.cpp.

#include "lm_session_reductions.h"
#include "../../Diagnostics/derived/handedness_gloss.h"   // the one handedness gloss (refactor 6.3b)

#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pinpoint::analysis {

// Local to this header so it stays independent of lm_flight_path.h, which owns the
// aerodynamics and has no business being pulled in to name a shot shape.
inline constexpr double kDegToRadIr = M_PI / 180.0;
inline constexpr double kRadToDegIr = 180.0 / M_PI;

// Every threshold both reads use. COACH-SET CONFIG, not view literals — these are
// judgements about when a golfer should be told their ball curved, and they belong
// somewhere a coach can be shown them. The defaults are the design brief's placeholders
// and are still awaiting confirmation from the coach who set `centreMm`.
//
// SINCE REFACTOR 6.3b THE SHAPE'S START AND CURVE ARE THE PACK'S (ShotDirectionBounds, lmShapeClass): startT, curveT
// and severeAxisT are the compound miss's own halves only (lmCompoundHalves: the stored `compoundMiss` measurement's
// definition), and severeCurveDeg also judges a no-axis read's severity and bounds "slight".
struct LmInferenceTuning {
    double startT         = 1.0;   // ° — the compound miss's start half
    double curveT         = 2.0;   // ° — the compound miss's curve half (spin axis)
    double severeAxisT    = 10.0;  // ° — the compound miss's severity (spin axis)
    double severeCurveDeg = 4.5;   // ° — measured curvature beyond this is severe (the compound miss; a no-axis read)
    // Fallback only, for a device that reports no spin axis. Face-to-path predicts
    // curvature far less well (see the note above lmFlightShape), so these exist to
    // produce SOME read rather than the right one.
    double f2pCurveT      = 1.0;   // °
    double f2pSevereT     = 4.0;   // °
    // "SLIGHT" HAS NO FIELD OF ITS OWN: it is HALF of each severe boundary (the pack's spin-axis corridor edge,
    // severeCurveDeg, f2pSevereT), computed in lmShapeClass, so moving a severe boundary moves its
    // half with it. The panel never says it — its nine windows have no such word — but the Club
    // delivery section and the report's Delivery page do ("Pull, slight fade"), and they ask THIS
    // classifier rather than keeping thresholds of their own (they did once, and drifted: a 1.8°
    // axis was a curve there and straight here). With the shipped pack: 5° of axis, 2.25° of measured
    // curvature, 2° of face-to-path.
    double centreMm       = 3.0;   // mm — inside this in BOTH axes is flushed
    double severeVMm      = 10.0;  // mm — vertical miss beyond this is fat/thin, not high/low
    // The hosel, for the one strike this panel never names. `lmStrikeQuality` stops at "Heel"
    // because the millimetres are already on the card and a card does not need to shout; the
    // DIAGNOSTIC pack does need the boundary, because `shank` is a condition of its own with its
    // own causes and its own drill. Provisional and the least-evidenced number here: a shanked
    // ball often leaves the device nothing to read at all, so this fires on the strikes near
    // enough the hosel to still be measured, and the ones past it are silent rather than wrong.
    double hoselMm        = -30.0; // mm — a strike this far toward the heel is off the hosel
};

// ── flight shape ─────────────────────────────────────────────────────────────

// The nine windows: where it started × how it curved.
//
// START DIRECTION GIVES THE WINDOW, THE SPIN AXIS GIVES THE CURVE.
//
// The axis leads because it is what physically bends the ball: the Magnus force's lateral
// component is Cl·sin(axis), so the axis IS the curvature, up to spin rate and time of
// flight. Face-to-path is one step further back — it is a cause of the axis, not of the
// flight, and the gap between them is where a read goes wrong.
//
// This is a correction, and it was measured rather than reasoned. Over 25 shots the model
// originally shipped with — face-to-path leading, the axis only corroborating severity —
// disagreed with the ball on nine of them. Against the curvature the ball actually flew:
//
//     spin axis    r = +0.994          face-to-path   r = +0.845
//
// and where the two disagreed in sign, the axis was right both times: a shot with the face
// 2.9° closed to the path and the axis 2.3° right curved RIGHT, and was called a draw.
// Worse, a shot with a 16.3° axis that curved 25 yards right had a face-to-path of exactly
// 1.0° and so was called dead straight.
//
// SEVERITY: fade against slice, draw against hook. Until refactor 6.3b it was also read from how much the ball curved
//
//     curvature = offline − carry·tan(startDir)
//
// (the finish less where the ball would have finished had it flown straight down its own start line, as an angle).
// Since 6.3b the shape is the pack's (lmShapeClass): a slice or a hook is the spin axis past its corridor, as the
// pack fires it. The measured curvature still decides the compound miss's severity (its own halves), a no-axis
// read's, and whether a fade is "slight"; on its own it no longer turns a fade into a slice (NOTES-6.3b D-2).
struct LmFlightShape {
    bool    has = false;
    int     windowIdx = 1;    // 0 pull · 1 straight · 2 push
    int     curveIdx  = 1;    // 0 draw/hook · 1 straight · 2 fade/slice
    QString name;             // "Pull–fade", "Straight", "Hook", …
    QString evidence;         // "start 1.3° left · axis 4.7° right · curved 8.9 yd right"
    // Whether the curve was promoted: hook rather than draw, slice rather than fade. Exposed so
    // that lmCompoundMiss can ASK this function whether the shot qualifies instead of re-deriving
    // the answer from the same inputs. Two derivations of one judgement is the drift the whole
    // header is arranged to prevent, and the mixed strictness below is why it would have happened:
    // the window comparison is strict and the severity comparison is not, so a second reader
    // normalising both to one ratio disagrees on the boundary shots and nowhere else — the hardest
    // kind of difference to notice.
    bool    severe    = false;
    // The curvature the name was decided from, when carry and offline were both reported.
    bool    hasCurve  = false;
    double  curveYd   = 0.0;  // + = right for a right-hander, raw (unmirrored)
    double  curveDeg  = 0.0;
};

// HOW MUCH THE BALL ACTUALLY CURVED: the finish, less where it would have finished had it flown
// straight down its own start line. Not the offline — a pull that never curved still finishes left,
// and calling that a hook is the error this replaces.
//
// It lives out here, rather than inside lmFlightShape where it was written, because a second reader
// arrived: the `compoundMiss` metric the diagnostic pack grades `pull_hook` and `push_slice` from.
// Two computations of "how much did it curve" that agreed today would be two that could disagree
// after one edit, and the panel and the pack would then name the same shot differently — which is
// the whole failure this extraction exists to make impossible.
struct LmCurvature {
    bool   has = false;
    double yd  = 0.0;   // + = right for a right-hander, RAW (unmirrored) — it is a measurement
    double deg = 0.0;   // the same, as an angle, so a wedge and a driver are comparable
};

inline LmCurvature lmCurvature(const std::optional<double> &startDirDeg,
                               const std::optional<double> &carryYd,
                               const std::optional<double> &offlineYd)
{
    LmCurvature out;
    if (!startDirDeg || !carryYd || !offlineYd
        || !std::isfinite(*startDirDeg) || !std::isfinite(*carryYd) || !std::isfinite(*offlineYd)
        || *carryYd <= 0.0)
        return out;

    const double straightYd = *carryYd * std::tan(*startDirDeg * kDegToRadIr);
    out.yd  = *offlineYd - straightYd;
    out.deg = std::atan2(out.yd, *carryYd) * kRadToDegIr;
    out.has = true;
    return out;
}

// A signed lateral quantity as the evidence line says it: magnitude, unit, side word.
// Never a bare minus — "start −1.3°" makes a reader work out a sign convention that the
// word "left" simply tells them.
inline QString lmSideText(double v, int decimals, const QString &unit,
                          const QString &leftWord, const QString &rightWord)
{
    const QString mag = lmFormat(std::abs(v), decimals);
    // `unit` carries its own leading space where the typography wants one ("°" sets
    // tight against the figure, " yd" does not), so that is decided once by the caller
    // rather than guessed per unit here.
    //
    // A reading that ROUNDS TO ZERO gets no side word. "0.0° right" is not a fact about
    // the shot, it is the sign bit of a number too small to have one, and this line is
    // an audit trail — the one place on the panel that must not overstate what was seen.
    const bool zero = std::all_of(mag.cbegin(), mag.cend(), [](QChar c) {
        return c == QLatin1Char('0') || c == QLatin1Char('.');
    });
    if (zero)
        return mag + unit;
    return mag + unit + QStringLiteral(" ") + (v < 0.0 ? leftWord : rightWord);
}

// ── THE ONE DIRECTION RULE (refactor 6.3b, audit F-05 / G-01) ────────────────────────────────────
//
// Where a shot started and which way it curved, by the PACK'S CORRIDORS: the same boundaries the diagnostic pack
// fires `pull`, `push`, `slice`, `hook`, `fade`, `draw`, `pull_hook` and `push_slice` by, so the launch monitor card,
// Club delivery (and the report's Delivery page) name a shot exactly as the factors and Tendencies do. There were two
// classifiers and they disagreed: this header's own 1° / 2° windows on the card and in Club delivery, the pack's ~4°
// corridors in the factors, so a 7 iron starting 2.5° left read "Pull–fade" on the card and "Straight" in Tendencies.
//
// The boundaries are the pack's, resolved (shotDirectionBounds, Diagnostics/engine/shot_direction_bounds.h: the
// corridor edges of m_lmLaunchDirection and m_spinAxis under the grade policy, and the fade / draw / compound signals'
// thresholds). The defaults below ARE the shipped pack under the standard policy, asserted by lm_corridor_test, so a
// caller with no pack in hand still names a shot as the shipped pack does.
struct ShotDirectionBounds {
    double startLo  = -4.0, startHi  = 4.0;    // sig_pull (<), sig_push (>): m_lmLaunchDirection's corridor edges
    double axisLo   = -10.0, axisHi  = 10.0;   // sig_hook (<), sig_slice (>): m_spinAxis's corridor edges
    double fadeFrom = 4.0,  fadeTo   = 10.000000000000002;    // sig_fadeBeyondStraight (>), sig_fadeShortOfSlice (<)
    double drawFrom = -4.0, drawTo   = -10.000000000000002;   // sig_drawBeyondStraight (<), sig_drawShortOfHook (>)
    double pushSlice = 1.0, pullHook = -1.0;   // sig_pushSlice (>), sig_pullHook (<): on the compound miss (below)
};

// The rule's answer. Indices are in the GLOSS space (glossedSideIndex: a left-hander's right-of-target is a pull).
struct LmShapeClass {
    bool has       = false;
    int  windowIdx = 1;      // 0 pull · 1 straight · 2 push           (gloss)
    int  curveIdx  = 1;      // 0 draw/hook · 1 straight · 2 fade/slice (gloss)
    bool severe    = false;  // hook rather than draw, slice rather than fade (or a compound miss)
    bool slight    = false;  // a curve (curveIdx ≠ 1), not severe, and at most halfway to severe
    bool fromAxis  = false;  // the curve was read from the spin axis, not face-to-path
    bool compound  = false;  // a pull-hook or a push-slice: the compound miss decided both halves
};

// The compound miss's OWN halves: the panel's pre-6.3b windows (startT, curveT, severeAxisT, severeCurveDeg and the
// face-to-path fallback), unmirrored. NOT a shape classifier: it is the definition of the stored `compoundMiss`
// measurement (swing_doc_writer.cpp writes it at the shot's write), and changing it would change a recorded product
// on every launch monitor shot. So it is kept exactly, and the rule below reads the compound miss's value against the
// pack's own thresholds, as the pack does. Its 1° start half is not the pack's 4° start corridor: a proposal for the
// Coaching Lead (NOTES-6.3b D-1), never built here.
inline LmShapeClass lmCompoundHalves(const std::optional<double> &startDeg,
                                     const std::optional<double> &spinAxisDeg,
                                     const std::optional<double> &faceToPathDeg,
                                     const LmCurvature &curvature,
                                     const LmInferenceTuning &t = LmInferenceTuning())
{
    LmShapeClass out;
    const bool haveAxis = spinAxisDeg   && std::isfinite(*spinAxisDeg);
    const bool haveF2p  = faceToPathDeg && std::isfinite(*faceToPathDeg);
    if (!startDeg || !std::isfinite(*startDeg) || (!haveAxis && !haveF2p))
        return out;
    const double start = *startDeg;
    const double axis  = haveAxis ? *spinAxisDeg   : 0.0;
    const double f2p   = haveF2p  ? *faceToPathDeg : 0.0;
    const bool haveCurve = curvature.has;
    const double curveSignal = haveAxis ? axis : f2p;
    const double curveThresh = haveAxis ? t.curveT : t.f2pCurveT;
    out.severe = (haveAxis  && std::abs(axis) >= t.severeAxisT)
                 || (haveCurve && std::abs(curvature.deg) >= t.severeCurveDeg)
                 || (!haveAxis && std::abs(f2p) > t.f2pSevereT);
    out.windowIdx = start < -t.startT ? 0 : (start > t.startT ? 2 : 1);
    out.curveIdx  = curveSignal < -curveThresh ? 0 : (curveSignal > curveThresh ? 2 : 1);
    out.fromAxis = haveAxis;
    out.has = true;
    return out;
}

// The compound miss's value from readings already in hand (lmCompoundMiss below states the metric).
inline std::optional<double> lmCompoundMissValue(const std::optional<double> &startDirDeg,
                                                 const std::optional<double> &spinAxisDeg,
                                                 const std::optional<double> &faceToPathDeg,
                                                 const LmCurvature &c,
                                                 const LmInferenceTuning &t = LmInferenceTuning())
{
    const LmShapeClass s = lmCompoundHalves(startDirDeg, spinAxisDeg, faceToPathDeg, c, t);
    if (!s.has)
        return std::nullopt;
    // Both halves must have earned their own word, and both must point the SAME WAY. windowIdx and curveIdx share an
    // encoding — 0 is the left end, 2 the right, 1 the middle — so agreeing is simply being equal and not being 1. A
    // pull-fade fails here and reads 0, which is the whole distinction: it is a different shot, not a milder version.
    if (s.windowIdx == 1 || s.curveIdx == 1 || s.windowIdx != s.curveIdx || !s.severe)
        return 0.0;
    // Each half divided by its own threshold, and the SMALLER of the two reported: the one that came closest to not
    // making it is what the claim rests on. Both ratios are >= 1 here by construction.
    const double startRatio = std::abs(*startDirDeg) / t.startT;
    double severity = 0.0;
    const bool haveAxis = spinAxisDeg && std::isfinite(*spinAxisDeg);
    if (haveAxis)
        severity = std::max(severity, std::abs(*spinAxisDeg) / t.severeAxisT);
    if (c.has)
        severity = std::max(severity, std::abs(c.deg) / t.severeCurveDeg);
    if (!haveAxis && faceToPathDeg && std::isfinite(*faceToPathDeg))
        severity = std::max(severity, std::abs(*faceToPathDeg) / t.f2pSevereT);
    return std::copysign(std::min(startRatio, severity), s.windowIdx == 2 ? 1.0 : -1.0);
}

// THE RULE. Inputs are RAW, target-relative readings, as the pack reads them: `startDeg` the start direction (a caller
// with no ball reading may hand it the face angle as the start's proxy, and says so with `startIsBall`), `spinAxisDeg`
// the axis, `faceToPathDeg` the fallback a device with no axis gives, `curvature` lmCurvature() of the same shot.
//
//   start   pull below startLo, push above startHi (sig_pull / sig_push), else straight;
//   curve   with an axis: slice above axisHi, hook below axisLo (severe); else fade inside (fadeFrom, fadeTo), draw
//           inside (drawTo, drawFrom); else straight. With no axis the pack fires no curve: the face-to-path fallback
//           (LmInferenceTuning's f2pCurveT / f2pSevereT, and the measured curvature's severeCurveDeg) names one, as it
//           always did, so a read is still made (NOTES-6.3b D-2);
//   both    a compound miss beyond pushSlice / pullHook (the ball's pair only) is a push-slice / pull-hook: both halves
//           on its side and severe, as the pack's two compound conditions bring their halves (tendencyShotMisses).
//
// The comparisons are the engine's: strict, beyond the edge. Classified in the raw frame, THEN glossed for the hand
// (glossedSideIndex), so an asymmetric corridor (a user's override) is read on the side it was authored for.
inline LmShapeClass lmShapeClass(const std::optional<double> &startDeg,
                                 const std::optional<double> &spinAxisDeg,
                                 const std::optional<double> &faceToPathDeg,
                                 const LmCurvature &curvature,
                                 bool leftHanded = false,
                                 const ShotDirectionBounds &b = ShotDirectionBounds(),
                                 const LmInferenceTuning &t = LmInferenceTuning(),
                                 bool startIsBall = true)
{
    LmShapeClass out;
    const bool haveAxis = spinAxisDeg   && std::isfinite(*spinAxisDeg);
    const bool haveF2p  = faceToPathDeg && std::isfinite(*faceToPathDeg);
    // A start, and SOMETHING that speaks to the curve. Either alone names half a shot.
    if (!startDeg || !std::isfinite(*startDeg) || (!haveAxis && !haveF2p))
        return out;

    const double start = *startDeg;
    int  window = start < b.startLo ? 0 : (start > b.startHi ? 2 : 1);
    int  curve  = 1;
    bool severe = false;
    double size = 0.0, halfSevere = 0.0;   // for "slight": the curve's measure and half its severe edge
    if (haveAxis) {
        const double a = *spinAxisDeg;
        const bool slice = a > b.axisHi, hook = a < b.axisLo;
        const bool fade  = a > b.fadeFrom && a < b.fadeTo, draw = a < b.drawFrom && a > b.drawTo;
        severe = slice || hook;
        curve  = slice ? 2 : hook ? 0 : fade ? 2 : draw ? 0 : 1;
        size = std::abs(a);
        halfSevere = 0.5 * (curve == 0 ? -b.axisLo : b.axisHi);
    } else {
        const double f = *faceToPathDeg;
        curve  = f < -t.f2pCurveT ? 0 : (f > t.f2pCurveT ? 2 : 1);
        severe = std::abs(f) > t.f2pSevereT || (curvature.has && std::abs(curvature.deg) >= t.severeCurveDeg);
        size = std::abs(f);
        halfSevere = 0.5 * t.f2pSevereT;
    }
    if (startIsBall) {
        const std::optional<double> c = lmCompoundMissValue(startDeg, spinAxisDeg, faceToPathDeg, curvature, t);
        if (c && *c > b.pushSlice)      { window = 2; curve = 2; severe = true; out.compound = true; }
        else if (c && *c < b.pullHook)  { window = 0; curve = 0; severe = true; out.compound = true; }
    }
    // "SLIGHT" HAS NO BOUNDARY OF ITS OWN: a curve at most HALF its severe edge, and, when the distances were
    // reported, a measured curvature at most half of severeCurveDeg. Words only (Club delivery's "Pull, slight fade").
    out.slight = curve != 1 && !severe && size <= halfSevere
                 && (!curvature.has || std::abs(curvature.deg) <= 0.5 * t.severeCurveDeg);

    out.windowIdx = glossedSideIndex(window, leftHanded);
    out.curveIdx  = glossedSideIndex(curve, leftHanded);
    out.severe    = severe;
    out.fromAxis  = haveAxis;
    out.has = true;
    return out;
}

// Classify the shot's shape.
//
// HANDEDNESS MIRRORS THE GLOSS, NEVER THE SIGN — the rule already stated for the whole
// metric catalogue in src/Vocabulary/Metrics/metric_descriptor.h. The device records device-frame
// geometry (gcquad_csv_parser.cpp: spin axis "positive tilting right for a right-handed
// golfer"), and a ball that finished physically right finished physically right for
// either golfer. What changes is what that is CALLED: for a left-hander, right of target
// is a pull and curving right is a draw. So the inputs are negated for a left-hander to
// classify, and the stored readings are untouched.
//
// windowIdx/curveIdx are in the SAME gloss space as `name`, so the lit cell of the 3 × 3
// always agrees with the words printed beside it. A grid that stayed geometric while the
// label mirrored would show a left-hander a picture contradicting its own caption.
inline LmFlightShape lmFlightShape(const std::optional<double> &startDirDeg,
                                   const std::optional<double> &spinAxisDeg,
                                   const std::optional<double> &faceToPathDeg,
                                   const std::optional<double> &carryYd,
                                   const std::optional<double> &offlineYd,
                                   bool leftHanded = false,
                                   const ShotDirectionBounds &bounds = ShotDirectionBounds(),
                                   const LmInferenceTuning &t = LmInferenceTuning())
{
    LmFlightShape out;

    // How much the ball actually curved — see lmCurvature(), which owns the arithmetic.
    const LmCurvature c = lmCurvature(startDirDeg, carryYd, offlineYd);

    // The classification is the one direction rule's (lmShapeClass, the pack's corridors) — the one every
    // shape-naming surface shares, the factors and Tendencies included.
    // gate rule: PP_GATE("rule.lm.flightShape")
    const LmShapeClass k = lmShapeClass(startDirDeg, spinAxisDeg, faceToPathDeg, c, leftHanded, bounds, t);
    if (!k.has)
        return out;
    const bool haveAxis = k.fromAxis;

    out.hasCurve = c.has;
    out.curveYd  = c.yd;         // raw, unmirrored — it is a measurement
    out.curveDeg = c.deg;

    const bool severe = k.severe;
    out.severe    = severe;
    out.windowIdx = k.windowIdx;
    out.curveIdx  = k.curveIdx;

    const QString window = out.windowIdx == 0 ? QStringLiteral("Pull")
                         : out.windowIdx == 2 ? QStringLiteral("Push")
                                              : QString();
    const QString curve  = out.curveIdx == 0 ? (severe ? QStringLiteral("Hook")
                                                       : QStringLiteral("Draw"))
                         : out.curveIdx == 2 ? (severe ? QStringLiteral("Slice")
                                                       : QStringLiteral("Fade"))
                                             : QString();

    // An EN DASH joins the two, not a hyphen: "Pull–fade" is a compound of two
    // independent reads, not one hyphenated word, and at 14 px the hyphen reads as
    // punctuation inside a single term.
    if (window.isEmpty() && curve.isEmpty()) out.name = QStringLiteral("Straight");
    else if (window.isEmpty())               out.name = curve;
    else if (curve.isEmpty())                out.name = window;
    else out.name = window + QStringLiteral("–") + curve.toLower();

    // The audit trail, quoting the RAW readings — physical left and right, not the
    // mirrored gloss. For a left-hander the name says "Pull" while the evidence says
    // "1.3° left", and both are true: that is where the ball went and that is what it is
    // called. Mirroring the evidence too would leave nothing on the card stating the
    // measurement itself.
    //
    // IT CITES WHAT DECIDED THE NAME, which is why it no longer leads with face-to-path:
    // an audit trail that quoted a number the classification did not use would be a
    // justification rather than a reason. Face-to-path is still on the card, as its own
    // annotation, where a coach reads it as the thing the golfer can change.
    QString ev = QStringLiteral("start ")
                 + lmSideText(*startDirDeg, 1, QStringLiteral("°"),
                              QStringLiteral("left"), QStringLiteral("right"));
    if (haveAxis)
        ev += QStringLiteral(" · axis ")
              + lmSideText(*spinAxisDeg, 1, QStringLiteral("°"),
                           QStringLiteral("left"), QStringLiteral("right"));
    else
        ev += QStringLiteral(" · face ")
              + (*faceToPathDeg > 0.0 ? QStringLiteral("+") : QString())
              + lmFormat(*faceToPathDeg, 1) + QStringLiteral("° to path");
    if (out.hasCurve)
        ev += QStringLiteral(" · curved ")
              + lmSideText(out.curveYd, 1, QStringLiteral(" yd"),
                           QStringLiteral("left"), QStringLiteral("right"));
    else if (offlineYd && std::isfinite(*offlineYd))
        ev += QStringLiteral(" · finishes ")
              + lmSideText(*offlineYd, 1, QStringLiteral(" yd"),
                           QStringLiteral("left"), QStringLiteral("right"));
    out.evidence = ev;

    out.has = true;
    return out;
}

// ── the compound miss ────────────────────────────────────────────────────────

// ONE NUMBER THAT MEANS "IT STARTED OFF LINE AND THEN CURVED FURTHER THE SAME WAY".
//
// This exists for the diagnostic pack, not for the panel, and the reason is structural. A
// condition fires when ANY of its signals fires — the engine ORs them (characteristic_engine.cpp,
// the anyFired branch) and there is no AND anywhere in the vocabulary. So `pull_hook`, which is
// precisely "pull AND hook", cannot be written as two signals: two would fire on a plain pull and
// name the smother. The pack has one place to put a conjunction, and it is here, in the producer.
//
// THE TRICK IS THE MINIMUM OF TWO NORMALISED RATIOS. Divide each half of the claim by its own
// threshold — the start direction by the pull/push boundary, the curvature by the hook/slice
// boundary — and each is 1.0 exactly at the point its own word starts to apply. The smaller of the
// two therefore exceeds 1.0 if and only if BOTH did. A single threshold on the minimum is the
// conjunction, exactly, with no new test kind and no second signal.
//
// THE SIGN CARRIES THE SIDE, so one metric serves both compounds and they cannot both fire: it is
// positive only when the ball started right and curved right, negative only when both went left,
// and ZERO whenever the two disagree. A pull-fade and a push-draw are not weak compound misses,
// they are the opposite kind of shot — the two halves cancelling is the whole point of the shape —
// so they read 0 rather than something small. The pack's `pull_hook excludes push_slice` edge is
// then a statement this arithmetic already guarantees rather than one it has to police.
//
// THE CONTRACT IS |value| >= 1 EXACTLY WHEN ITS OWN HALVES (lmCompoundHalves) NAME THE SHOT A PULL AND A HOOK, OR A
// PUSH AND A SLICE — the panel's pre-6.3b windows, kept so the stored metric never moves. It holds by construction:
// this function decides nothing, it asks lmCompoundHalves and then measures. lm_inferred_reads_test sweeps the pair
// over a grid and requires zero disagreements. Since 6.3b the card and the pack read this value the same way (above
// pushSlice / below pullHook: lmShapeClass), so a compound miss is named alike everywhere.
//
// The pack's threshold signals fire strictly ABOVE ±1, so they differ from the panel on values of
// exactly 1.0 — a shot whose weaker half landed precisely on its threshold. Left alone: that is one
// value of one double in a physical measurement, and the alternative is an authored number that is
// not the boundary it claims to be.
//
// RAW, UNMIRRORED, POSITIVE IS RIGHT. Same convention as `lm.launchDirection`, which `sig_pull`
// and `sig_push` already read unmirrored — the pack has no handedness anywhere in its measure
// source, so a metric that mirrored itself would be the only one that did and would disagree with
// the two signals it sits beside. Handedness belongs to the gloss (see lmFlightShape), and the
// gloss is the panel's job.
struct LmCompoundMiss {
    // `has` is a statement about the READ, not the value: a shot that started left and faded has a
    // perfectly good compound-miss reading of zero, and the absence of one is a different fact.
    bool   has   = false;
    double value = 0.0;   // ratio · + = started and curved RIGHT · − = both left · 0 = they disagree
};

inline LmCompoundMiss lmCompoundMiss(const std::optional<double> &startDirDeg,
                                     const std::optional<double> &spinAxisDeg,
                                     const std::optional<double> &faceToPathDeg,
                                     const std::optional<double> &carryYd,
                                     const std::optional<double> &offlineYd,
                                     const LmInferenceTuning &t = LmInferenceTuning())
{
    LmCompoundMiss out;
    // THE COMPOUND MISS'S OWN HALVES (lmCompoundHalves), read RIGHT-HANDED, always: the metric is stated in the
    // geometry, which is the same shot for either golfer. Since refactor 6.3b they are no longer the card's
    // classifier (that is the pack's corridors, lmShapeClass), and they are kept exactly so the stored metric does not
    // move (NOTES-6.3b D-1).
    const std::optional<double> v = lmCompoundMissValue(startDirDeg, spinAxisDeg, faceToPathDeg,
                                                        lmCurvature(startDirDeg, carryYd, offlineYd), t);
    if (!v)
        return out;
    out.has   = true;
    out.value = *v;
    return out;
}

// ── strike quality ───────────────────────────────────────────────────────────

// Where on the face it was struck, and nothing else.
//
// SMASH DOES NOT ENTER THE CLASSIFICATION. It is shown beside the read as context
// because a golfer wants it there, but a strike is flushed or it is not by where it
// landed on the face — letting smash vote would make a fast swing's toe strike read as
// centred, which is the opposite of the thing this card exists to show.
struct LmStrikeRead {
    bool    has = false;
    QString name;        // "Flushed", "Toe", "Low", "Thin", …
    QString evidence;    // "smash +0.01 vs μ", or empty when there is no session mean
};

// Classify the strike.
//
// NO HANDEDNESS PARAMETER, deliberately. Toe and heel are parts of the clubhead, and the
// device names them from the face's own frame — a strike 3 mm toward the toe is toward
// the toe for either golfer. What DOES mirror for a left-hander is the drawing (§5.4 of
// the design brief: marker offset, axis words, shaft direction), and that is the panel's
// business, not this classification's. Adding a flip here would rename a correct reading.
inline LmStrikeRead lmStrikeQuality(const std::optional<double> &locationMm,
                                    const std::optional<double> &heightMm,
                                    const std::optional<double> &smashLatest,
                                    const std::optional<double> &smashMean,
                                    const LmInferenceTuning &t = LmInferenceTuning())
{
    LmStrikeRead out;
    // gate rule: PP_GATE("rule.lm.strikeQuality")
    if (!locationMm || !heightMm
        || !std::isfinite(*locationMm) || !std::isfinite(*heightMm))
        return out;

    const double h = *locationMm;    // heel(−) ← → toe(+)
    const double v = *heightMm;      // low(−)  ← → high(+)
    const double ah = std::abs(h), av = std::abs(v);

    if (ah <= t.centreMm && av <= t.centreMm) {
        out.name = QStringLiteral("Flushed");
    } else if (av >= ah) {
        // Vertical names it. Ties go to the vertical miss because it is the one that
        // costs distance, and it is the one a golfer can feel.
        if (av > t.severeVMm)
            out.name = v > 0.0 ? QStringLiteral("Fat") : QStringLiteral("Thin");
        else
            out.name = v > 0.0 ? QStringLiteral("High") : QStringLiteral("Low");
    } else {
        // Horizontal at any magnitude — there is no severe tier here, because a toe
        // strike is a toe strike and the millimetres are already printed on the card.
        out.name = h > 0.0 ? QStringLiteral("Toe") : QStringLiteral("Heel");
    }

    if (smashLatest && smashMean && std::isfinite(*smashLatest) && std::isfinite(*smashMean)) {
        const double d = *smashLatest - *smashMean;
        out.evidence = QStringLiteral("smash ")
                       + (d > 0.0 ? QStringLiteral("+") : QString())
                       + lmFormat(d, 2) + QStringLiteral(" vs μ");
    }

    out.has = true;
    return out;
}

// ── Low point from the launch monitor's attack angle (issue #92) ─────────────────────────────────
//
// A GC Quad reports the attack angle but not the low point, and the two are one geometry: near the
// bottom the clubhead travels a circle of radius R about the swing centre, so a head still
// descending at α below horizontal as it reaches the ball bottoms out R·sin(−α) further on. Stated
// in INCHES, POSITIVE AHEAD OF THE BALL — the same convention as `lowPointAhead`, whose preferred
// rung this becomes (m_lowPointAhead's preferKeys), since the launch monitor is primary.
//
// R is the club's standard length plus an arm term (kArcArmM), chosen so a men's 7-iron (37 in)
// gives 1.46 m — the radius behind the familiar "about an inch of low point per degree of attack
// angle" for a mid-iron. That is an ASSUMPTION, not a measurement of this golfer, and the σ says so:
// the device's attack angle at an assumed ±0.5° and the radius at ±10 %, in quadrature. The camera
// arc fits cannot set R — their radii scatter far more by club than the clubs do (issue #92).
struct LmLowPoint {
    bool   has   = false;
    double value = 0.0;   // in · + = the arc bottomed out AHEAD of the ball
    double sigma = 0.0;   // in · 1σ, from the assumed inputs above
};

inline constexpr double kArcArmM             = 0.52;   // R = club length + this (7-iron ⇒ 1.46 m)
inline constexpr double kArcRadiusDefaultM   = 1.46;   // a club the standards table does not know
inline constexpr double kArcRadiusRelSigma   = 0.10;
inline constexpr double kLmAttackSigmaDeg    = 0.5;

inline LmLowPoint lmLowPointFromAttack(const std::optional<double> &attackDeg, double arcRadiusM)
{
    LmLowPoint out;
    if (!attackDeg || !std::isfinite(*attackDeg) || !(arcRadiusM > 0.0) || std::abs(*attackDeg) >= 45.0)
        return out;
    constexpr double kInPerM = 1.0 / 0.0254;
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double a = *attackDeg * kDegToRad;
    out.value = -arcRadiusM * std::sin(a) * kInPerM;
    const double dValueDa = arcRadiusM * std::cos(a) * kInPerM;              // in per radian
    const double sA = dValueDa * kLmAttackSigmaDeg * kDegToRad;
    const double sR = std::abs(out.value) * kArcRadiusRelSigma;
    out.sigma = std::sqrt(sA * sA + sR * sR);
    out.has = true;
    return out;
}

} // namespace pinpoint::analysis
