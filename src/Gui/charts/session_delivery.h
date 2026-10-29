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

// THE CLUB DELIVERY SECTION'S CORE (docs/design/club_delivery_chart_design.md, stage 2): everything the
// Club delivery section of Charts says about the swing plane and the ball, computed here in plain C++, so
// the presenter (charts_delivery_presenter.h) only serialises it and the QML only places it. No QObject,
// no files, no threads of its own. The twin of session_posture.h in form; none of its content.
//
// ── The readings (§1.1) ─────────────────────────────────────────────────────────────────────────
//   swingPlaneCurve, handPlaneCurve   at P1..P7 (Phase ids 0, 12, 8, 2, 13, 9, 5), the measure engine's
//                                     own sampler (buildPhaseGrid: the ±15 ms windowed median — the
//                                     numbers swing_phasegrid.json holds, which the evidence CSV holds)
//   handPlaneSteepening               the hands' downswing plane less their backswing one (one number)
//   shoulderPlane                     the plane through the ball line and the trail shoulder at address
//   shaftLie at P1 and P7, impactShaftLean, lowPointAhead, clubPath, attackAngle   our camera's
//   lm.clubPath, lm.faceAngle, lm.faceToPath, lm.attackAngle, lm.dynamicLoft, lm.lieAngle,
//   lm.lowPointAhead                                                               the monitor's
// A missing reading is NaN and prints "-", never 0.
//
// ── The rules this file keeps (§1.3, §6.2) ──────────────────────────────────────────────────────
//   · THE MONITOR IS PRIMARY. Where it delivers a quantity (path, face, attack, lie, loft, low point)
//     its value is used; ours only where it is absent, and the line says whose it is.
//   · A FIT IS ONE GOLFER'S, over the shots handed in (v1: the session, §8.2), never pooled; it is shown
//     only when it holds (n ≥ 12, |r| ≥ 0.5, the physical sign) and its refusal is said as a finding.
//     A fit reads the MONITOR's path and attack only: a camera path against a camera plane is not the
//     ball flight.
//   · NO ABSOLUTE ANGLE IS GRADED. The DTL camera is uncalibrated: lines are the golfer's own (their
//     address line, their shoulder plane), comparisons are with their own session.
//   · Nothing here grades, fires or changes a norm.
//
// ── Decisions this file takes where the design leaves room (stated once, here) ──────────────────
//   1. THE STORY'S ARITHMETIC ADDS UP ON THE PAGE. "Delivered … {d}° above where you set it" prints two
//      figures, so d is the difference of the two figures AS PRINTED (whole degrees), and "where you set
//      it" is |d| ≤ 1 on those figures. P3 is not printed, so its threshold (≥ 3°) reads the raw
//      difference and its figure is that difference rounded; the hands' threshold (≥ 3°) reads the raw
//      steepening, so Jess's +2.9 reads "on the plane" (§6.1).
//   2. "PEAKED AT THE TOP" is the maximum of P2..P5 at P3, P4 or P5 (P4 ± one position, in swing order);
//      at P2 it is "peaked at P2".
//   3. THE PUBLISHED SHAPE (§5.1, Morrison 2018) compares a = P3 − P7 with −2° and b = P4 − P7 with
//      +5.5°, each ±4°. a > +2: "is steep from P3"; else b > +9.5: "is steep at the top" (a fourth word:
//      the three of §5.1 leave a top far above the delivery unnamed); else b < +1.5 or a < −6: "stays
//      under"; else "matches". Said only where P3, P4 and P7 were read.
//   4. THE SLOT (§5.2) is read at P3..P6: above the shoulder plane by more than the curve's own σ (the
//      series σ; none stated: by any amount), or under the address line by more. P2 is left out: the
//      shaft lies along the line there and the curve dips by construction.
//   5. THE HANDS CARRY THE CURVE where it sits on the hand plane: a grid point after P1 whose value is
//      within kDeliveryHandsCarryDeg of the hand plane curve, both measured (not bridged), in runs of at
//      least kDeliveryHandsCarryMinMs. The curve does not mark its hand-carried points (a412aed3 fuses
//      the hand frames in with weight 1 − q and publishes one curve); where q → 0 the curve IS the hand
//      plane, so the two coincide. On the 165 library swings of the evidence CSV the gap is < 0.5° at P2
//      on 165, at P4 on 164 and at P6 on 127, and on 5 at P1, 20 at P3 and 5 at P7: the hands carry P2,
//      the top and the downswing to P6, the shaft carries address, P3 and impact — §1.1's own account.
//   6. "YOUR USUAL" is the session's median; a reading is in its usual place within kDeliveryUsualDeg. The head's
//      rule 1 on the plane at P6 places it by the fit's own tertiles (decision 8), as the ball-flight line does.
//      The no-monitor head (§4.3 rule 3) names the story reading (P1, P3, P4, P6, P7, steepening) whose
//      departure from its median is largest in degrees; under kDeliveryUsualDeg it says "where it
//      usually is". With fewer than kDeliverySessionBandShots shots there is no usual: the head is the
//      story's set and delivered.
//   7. THE STRONGER PATH FIT is the larger |r| of path ~ P6 plane and path ~ steepening, among fits that
//      hold; when neither holds, the larger |r| is the one said refused.
//   8. "STEEPEST THIRD": the tertiles (quantile7 at 1/3, 2/3) of the fitted reading over the shots handed
//      in that carry it — the fits' pool, the golfer's day (decision 13; §6.1 says the library: across days the camera moves).
//   9. FIGURES. Plane angles whole degrees; the monitor's path, face, attack, lie and loft one decimal
//      (its own resolution, and the design's words: "path went 4.3° left"); our camera's whole degrees;
//      a fit's slope one decimal, r two, scatter one; a middle half's ± one decimal. U+2212 minus.
//  10. SHOT SHAPE (§4.6) is the monitor's alone, and the BALL's first: start from lm.launchDirection and curve
//      from lm.spinAxis (the shape itself), else from the club, start from lm.faceAngle and curve from
//      lm.faceToPath (else face − path) — the 5 Oct monitor gave no face but did give the ball's start and
//      spin axis, and the grid stood empty (Mark, 29 Oct). Club: ±1° straight, "slight" up to
//      kDeliverySlightCurveDeg, past kDeliveryBigCurveDeg a hook (left) or a slice (right) (Jess 20 Oct shot 3,
//      face to path −13.8°, was a "draw"). Ball: the same start; the spin axis tilts about 1.5 times the face to
//      path (the library's 80 shots with both: median 1.53, same sign on 71), so its curve steps are the club's
//      × 1.5. Words mirror for a left-handed golfer.
//  11. LIE → FACE (§4.5): lm.lieAngle (toe up +) else shaftLie P7 − P1 (more upright +); loft lm.dynamicLoft
//      else the club's factory loft (athlete_controller.cpp's defaultClubRecordFor table; the athlete's own
//      club record is not in the snapshot). Δface = Δlie × sin(loft), toe up → face left (right-handed).
//      At a loft ≤ kDeliveryLieBarelyLoftDeg (a 3-wood and the driver) the line says the lie barely matters.
//  12. THE SESSION'S BAND (§4.7) is a box per position, P1..P7, of the shots' swing plane curve (shown from
//      kDeliverySessionBandShots shots); the session figure already draws the curves' middle half on its
//      own clock. "Varies most" names the widest middle half; "steadiest" the narrowest.
//  13. THE FITS POOL THE GOLFER'S DAY (§4.4, §8.2 as revised 29 Oct): this session's shots and the golfer's other
//      sessions on the same date — one camera set-up, so the uncalibrated camera's offset cancels — read from their
//      swing_phasegrid.json alone (deliveryInputFromPhaseGrid; the presenter never opens a sibling swing document). The
//      sentences say the pool ("On your 16 shots with the monitor on 20 Oct (3 sessions), …"); a day of one session
//      keeps the session's wording. The shot's place (decision 8) is over the same pool; the usual, the band, the slot
//      and the shapes stay the session's.

#include "session_series_types.h"
#include "../typeset/box_stats.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <array>
#include <cmath>
#include <vector>

namespace pinpoint::analysis { struct SwingPhaseGrid; }

namespace pinpoint::charts {

inline constexpr int kDeliveryPositions = 7;   // P1..P7, index 0..6

// §6.1, each with its reason.
inline constexpr int    kDeliveryFitMinShots        = 12;    // Mark's (28) and Jess's (16) fits pass; below, r is noise
inline constexpr double kDeliveryFitMinR            = 0.5;   // Mark −0.80, Jess −0.63 shown; Nick −0.22 refused (§2.2)
inline constexpr double kDeliveryGoingBackDeg       = 3.0;   // |P3 − P1|: about twice the curve's σ at P3
inline constexpr double kDeliveryHandsDeg           = 3.0;   // |steepening|: Jess's median +2.9 reads "on the plane"
inline constexpr double kDeliveryDeliveredSameDeg   = 1.0;   // |P7 − P1| as printed: "where you set it" (§5.1)
inline constexpr double kDeliveryShapeStartDeg      = 1.0;   // ±1° start: the monitor's own resolution class
inline constexpr double kDeliveryShapeCurveDeg      = 1.0;   // ±1° face-to-path: the same
inline constexpr double kDeliverySlightCurveDeg     = 3.0;   // decision 10: "slight fade" up to here
inline constexpr double kDeliverySpinAxisPerFaceToPath = 1.5; // decision 10: spin axis ≈ 1.5 × face to path (library median 1.53)
inline constexpr double kDeliveryBigCurveDeg        = 6.0;   // decision 10: over this the ball curves too far to call a
                                                             // draw or a fade — a hook (left) or a slice (right)
inline constexpr int    kDeliverySessionBandShots   = 5;     // the band means nothing below
inline constexpr double kDeliveryUsualDeg           = 3.0;   // decision 6: the going-back word's own step
inline constexpr double kDeliveryHandsCarryDeg      = 0.5;   // decision 5: the curve sits on the hand plane
inline constexpr double kDeliveryHandsCarryMinMs    = 20.0;  // decision 5: one knot of the curve's spline
inline constexpr double kDeliveryLieBarelyLoftDeg   = 15.0;  // decision 11: × sin(15°) = a quarter of the lie
inline constexpr double kDeliveryLieFlatDeg         = 0.5;   // a sole within this sat level
// The published shape (§3, §5.1; Morrison, McGrath & Wallace 2018): P3 and P4 against the delivery (P7).
inline constexpr double kDeliveryShapeP3Deg         = -2.0;
inline constexpr double kDeliveryShapeP4Deg         = 5.5;
inline constexpr double kDeliveryShapeBandDeg       = 4.0;

const std::array<int, kDeliveryPositions> &deliveryPhaseIds();   // P1..P7 → Phase enum ids
QString deliveryPositionTag(int pos);    // "P1" … "P7"
QString deliveryPositionWhen(int pos);   // "address", "P2", "P3", "the top", "P5", "P6", "impact"
// The club's factory loft (decision 11), NaN when the club is not known: "7 IRON" → 34.
double  deliveryNominalLoft(const QString &club);
// The club as words: "7 IRON" → "7-iron", "GAP WEDGE" → "gap wedge", "" → "club".
QString deliveryClubWords(const QString &club);
// Every series deliveryInputFromShot reads (a reader of a shot from its detail keeps these).
const QStringList &deliveryMetricKeys();
// The series whose curve on show adds the section: the plane keys and the monitor's delivery keys.
const QStringList &deliveryOnShowKeys();

// A stretch of the shot's clock, ms from impact.
struct DeliverySpan { double t0Ms = kNaN, t1Ms = kNaN; };

// ── One shot's input ────────────────────────────────────────────────────────────────────────────

struct DeliveryShotInput {
    int     ordinal = 0;
    int     shotId  = -1;
    QString swingDir, club;
    QString session;                                  // the session folder's name ("2026-10-20_Jess-Ratcliffe_Wrist_02")
    bool    thisSession = true;                       // false: another session of the golfer's day (decision 13)
    bool    leftHanded = false;                       // the words mirror (§4.6); the snapshot carries none today

    std::array<double, kDeliveryPositions> plane;     // swingPlaneCurve at P1..P7
    std::array<double, kDeliveryPositions> hands;     // handPlaneCurve at P1..P7
    std::array<double, kDeliveryPositions> posMs;     // each position's instant, ms from impact
    double planeSigma    = kNaN;                      // the curve's own σ (the series σ)
    double steepening    = kNaN;                      // handPlaneSteepening
    double shoulderPlane = kNaN;
    double lieP1 = kNaN, lieP7 = kNaN;                // shaftLie at address and impact
    double shaftLean = kNaN;                          // impactShaftLean at impact
    double lowPoint  = kNaN;                          // lowPointAhead, in
    double ourPath = kNaN, ourAttack = kNaN;          // the camera's clubPath, attackAngle
    double lmPath = kNaN, lmFace = kNaN, lmFaceToPath = kNaN, lmAttack = kNaN;
    double lmLoft = kNaN, lmLie = kNaN, lmLowPoint = kNaN;
    double lmLaunchDir = kNaN, lmSpinAxis = kNaN;     // the ball's start (+ right) and spin axis (+ curves right)
    double nominalLoft = kNaN;

    // The curve as drawn (This shot's plot), ms from impact; and where the hands carry it (decision 5).
    double curveFromMs = kNaN, curveToMs = kNaN;
    std::vector<DeliverySpan> handsSpans;

    DeliveryShotInput();
    bool hasPlane() const;                            // any swing plane curve position read
    bool hasMonitor() const;                          // any lm.* delivery reading
    bool hasPath() const { return std::isfinite(lmPath); }
};

// From the Charts snapshot's shot, through the measure engine's sampler (see the header note).
DeliveryShotInput deliveryInputFromShot(const ShotSeries &shot);
// The same reading off a phase grid: the sampler's output, which is what swing_phasegrid.json holds. A shot read from its
// sidecar and the same shot read from the snapshot agree, position for position (the same phase ids, the same values).
pinpoint::analysis::SwingPhaseGrid deliveryPhaseGridOf(const ShotSeries &shot);   // the grid deliveryInputFromShot reads
DeliveryShotInput deliveryInputFromGrid(const pinpoint::analysis::SwingPhaseGrid &grid);
// A swing_phasegrid.json's root (decision 13: the day's other sessions are read from these alone). `read` false when the
// sidecar's schema or reduction is not this build's: then nothing is read, never a number. No posMs, no curve.
DeliveryShotInput deliveryInputFromPhaseGrid(const QJsonObject &root, const QString &swingDir, bool *read = nullptr);

// ── The golfer's day (decision 13) ─────────────────────────────────────────────────────────────

struct DeliveryDay {
    QString date;                                     // "2026-10-20"; "" unknown
    int     sessions = 1;                             // session folders in the pool with a reading, this one included
    std::vector<DeliveryShotInput> others;            // the day's other sessions' shots (thisSession false)
    int     staleGrids = 0;                           // of `others`: sidecars older than their swing document
};
// The fit's pool: the session's shots, then the day's others.
std::vector<DeliveryShotInput> deliveryPool(const std::vector<DeliveryShotInput> &shots, const DeliveryDay *day);
// What the pool is, for the sentences: "on 20 Oct (3 sessions)"; "" for a day of one session (the wording stays).
QString deliveryPoolWords(const DeliveryDay *day);

// Decision 5 on two curves (ms from impact, value, valid; an empty valid = all measured). `fromMs`: P1's instant.
std::vector<DeliverySpan> deliveryHandsSpans(const std::vector<double> &tMs, const std::vector<double> &curve,
                                             const std::vector<uint8_t> &curveValid,
                                             const std::vector<double> &handMs, const std::vector<double> &hand,
                                             const std::vector<uint8_t> &handValid, double fromMs);

// ── The plane story (§5.1) and the slot (§5.2) ─────────────────────────────────────────────────

struct DeliveryStory {
    bool    available = false;     // P1 and P7 read
    QString set, back, top, delivered, hands;   // the parts, each "" when its readings are missing
    QStringList parts;             // set · back · top · delivered (the read ones, in order)
    QString text;                  // "set on 59° · 13° steeper than address at P3 · peaked at the top · delivered at 61°, 2° above
                                   //  where you set it. Hands 12° steeper coming down."
    bool    shapeRead = false;     // P3, P4 and P7 read: the published shape is said
    QString shapeWord;             // "matches" | "is steep from P3" | "is steep at the top" | "stays under"
    QString shapeLine;             // the second line (Analyse only)
};
DeliveryStory deliveryPlaneStory(const DeliveryShotInput &in);

struct DeliverySlotMark { int pos = 0; QString side; QString words; };   // side "above" | "under"
struct DeliverySlot {
    bool    available = false;     // the address line and the shoulder plane are both read
    double  address = kNaN, shoulders = kNaN;
    std::vector<DeliverySlotMark> outside;
    QString text;                  // "inside the slot" | "above the shoulder plane at P3 — a steep backswing; …"
};
DeliverySlot deliverySlotWords(const DeliveryShotInput &in);

// ── The fits (§4.4, §6.1) ──────────────────────────────────────────────────────────────────────

enum class DeliveryX : int { PlaneP6 = 0, Steepening = 1 };
enum class DeliveryY : int { Path = 0, Attack = 1 };
double  deliveryXOf(const DeliveryShotInput &in, DeliveryX x);   // NaN when unread
double  deliveryYOf(const DeliveryShotInput &in, DeliveryY y);   // the MONITOR's only; NaN without one
QString deliveryXKey(DeliveryX x);     // "planeP6" | "steepening"
QString deliveryYKey(DeliveryY y);     // "path" | "attack"

struct DeliveryFit {
    DeliveryX x = DeliveryX::PlaneP6;
    DeliveryY y = DeliveryY::Path;
    int     n = 0;
    double  slope = kNaN, intercept = kNaN, r = kNaN, sd = kNaN;   // sd: the residual scatter, n − 2
    bool    holds = false;
    QString verdict;               // "shown" | "weak" | "wrongSign" | "tooFew" | "none"
    QString why;                   // the refusal's reason ("" when shown)
    std::vector<int>    ordinals;  // the shots fitted, with their points
    std::vector<double> xs, ys;
};
// Least squares of ys on xs (pairs with a NaN are dropped) with the §6.1 rule; `wantSign` the physical one.
DeliveryFit deliveryFit(const std::vector<double> &xs, const std::vector<double> &ys, int wantSign);
// The fit over a set of shots: y (the monitor's) against x. The physical sign is negative for all three pairs
// for a right-handed golfer: a steeper plane moves the path left and the attack down (a left-handed golfer's
// path goes right: positive).
DeliveryFit deliveryFitOf(const std::vector<DeliveryShotInput> &shots, DeliveryX x, DeliveryY y);

struct DeliveryFits {
    DeliveryFit pathP6, pathSteep, attackP6;
    const DeliveryFit &path() const;   // decision 7: the stronger path fit
    bool any() const { return pathP6.n > 0 || pathSteep.n > 0; }
};
DeliveryFits deliveryFits(const std::vector<DeliveryShotInput> &shots);

// The fit as words. `noun`: what is counted ("shots"; the report's "swings"); `pool`: deliveryPoolWords ("" one session).
QString deliveryFitSentence(const DeliveryFit &f, const QString &noun = QStringLiteral("shots"), const QString &pool = QString());
QString deliveryAttackSentence(const DeliveryFit &f);   // "" unless it holds

struct DeliveryPlace {
    bool    known = false;
    int     third = 0;             // −1 flattest third, 0 middle, +1 steepest third
    QString words;                 // "your steepest third" | "the middle of your range" | "your flattest third"
};
// Where `v` sits among the shots' readings of x (decision 8).
DeliveryPlace deliveryPlace(const std::vector<DeliveryShotInput> &shots, DeliveryX x, double v);

// ── At impact (§4.5) and shot shape (§4.6) ─────────────────────────────────────────────────────

struct DeliveryImpactLine {
    QString key;                   // "lie" | "lean" | "attack"
    QString text;
    QString source;                // "monitor" | "camera" | "both"
    double  value = kNaN;          // the line's reading (Δlie, lean, attack)
    double  face = kNaN;           // lie only: the face it turns, ° (+ = left for a right-hander)
    double  loft = kNaN;           // lie only: the loft used
    QString loftSource;            // "monitor" | "club"
};
std::vector<DeliveryImpactLine> deliveryImpactLines(const DeliveryShotInput &in);

struct DeliveryShape {
    bool    available = false;     // the ball's start and spin axis, else the face and its face-to-path (or path)
    bool    fromBall = false;      // read off the ball (launch direction, spin axis), not the club
    double  face = kNaN, faceToPath = kNaN;
    double  launchDir = kNaN, spinAxis = kNaN;
    int     start = 0;             // −1 left, 0 straight, +1 right (of the target, the ball's start)
    int     curve = 0;             // −1 curves left, 0 straight, +1 curves right
    bool    slight = false;
    QString startWord, curveWord;  // "pull" | "straight" | "push"; "slight fade" | "draw" | "hook" | "slice" | "straight" …
    QString word;                  // "Pull, slight fade" | "Straight"
};
DeliveryShape deliveryShotShape(const DeliveryShotInput &in);

// ── The session (§4.7) ─────────────────────────────────────────────────────────────────────────

struct DeliveryUsual {
    int    n = 0;                                     // shots with the curve
    std::array<double, kDeliveryPositions> plane;     // medians
    double steepening = kNaN, shoulderPlane = kNaN;
    DeliveryUsual();
};
DeliveryUsual deliveryUsual(const std::vector<DeliveryShotInput> &shots);

struct DeliveryBand {
    bool shown = false;            // ≥ kDeliverySessionBandShots shots with the curve
    int  n = 0;
    std::array<box::BoxStats, kDeliveryPositions> boxes;
};
DeliveryBand deliverySessionBand(const std::vector<DeliveryShotInput> &shots);

struct DeliveryVaries {
    bool    has = false;
    int     tightest = -1, loosest = -1;   // positions
    double  tightHalf = kNaN, looseHalf = kNaN;   // half the middle half, °
    QString words;                 // "Steadiest at P1 (middle half ±0.5°), loosest at P3 (±2.1°) — your path follows your
                                   //  plane (r −0.80)."
};
DeliveryVaries deliveryVariesMost(const DeliveryBand &band, const DeliveryFits &fits);

// ── The head (§4.3) and the trust lines (§4.9) ─────────────────────────────────────────────────

struct DeliveryHead {
    int     rule = 0;              // 1..4 (§4.3); 0 nothing to say
    QString text;
};
// `shots`: the session (where this shot's plane sits); `fits` over them; `usual` of them.
DeliveryHead deliveryHead(const DeliveryShotInput &in, const std::vector<DeliveryShotInput> &shots,
                          const DeliveryFits &fits, const DeliveryUsual &usual);

struct DeliveryTrust { QStringList lines; };
// `shots`: the session, for what the monitor never gave; `shot` may be null (Session).
// `day`: the fits' pool beyond the session (may be null).
DeliveryTrust deliveryTrust(const std::vector<DeliveryShotInput> &shots, const DeliveryShotInput *shot,
                            const DeliveryFits &fits, const QString &noun = QStringLiteral("shots"),
                            const DeliveryDay *day = nullptr);

// ── The views ──────────────────────────────────────────────────────────────────────────────────

struct DeliveryShotView {
    bool    available = false;     // the shot carries the plane or a monitor delivery reading
    bool    hasPlane = false, hasMonitor = false;
    QString mode;                  // "full" | "noDtl" (§4.3 rule 4: the plane absent) | "noMonitor"
    DeliveryHead  head;
    DeliveryStory story;
    DeliverySlot  slot;
    DeliveryFits  fits;            // the day's (decision 13)
    int     poolShots = 0;         // shots in the fits' pool
    QString poolWords;             // "on 20 Oct (3 sessions)" | ""
    bool    flightAvailable = false;
    QString flightText;            // the fit's sentence (shown or refused)
    QString attackText;
    QString thisLine;              // "This one: 65° at P6, your steepest third → path −4.3°."
    DeliveryPlace place;
    std::vector<DeliveryImpactLine> impact;
    DeliveryShape shape;
    DeliveryTrust trust;
};
// One shot against its session (`shots` may be empty: no fit, no usual). The fits and the shot's place are over the
// golfer's day (`day` may be null: the session alone); the usual is the session's.
DeliveryShotView deliveryShot(const DeliveryShotInput &in, const std::vector<DeliveryShotInput> &shots,
                              const DeliveryDay *day = nullptr);

struct DeliverySessionView {
    bool    available = false;
    int     shots = 0;             // shots with the plane or a monitor delivery reading
    int     planeShots = 0, monitorShots = 0;
    QString head;
    DeliveryFits fits;             // the day's (decision 13)
    int     poolShots = 0;
    QString poolWords;
    DeliveryUsual usual;
    DeliveryBand band;
    DeliveryVaries varies;
    QString flightText, attackText;
    std::vector<DeliveryShape> shapes;   // parallel to the shots handed in
    QString shapeLine;             // "Pull, slight fade on 5 of 8 shots" | ""
    DeliveryTrust trust;
};
// The band, the usual and the shapes are the session's; the fits are over the golfer's day (`day` may be null).
DeliverySessionView deliverySession(const std::vector<DeliveryShotInput> &shots,
                                    const QString &noun = QStringLiteral("shots"), const DeliveryDay *day = nullptr);

// The figures (decision 9): "59°", "−4.3°", "-".
QString deliveryDeg(double v);     // whole degrees
QString deliveryDeg1(double v);    // one decimal
QString deliveryDeg1Signed(double v);   // one decimal with its sign: "+3.5°", "−4.3°", "0.0°"
// "4.3° left" | "2.1° right" | "square": a direction on the ground, the monitor's sign (+ = right of the
// target line). Physical, so it never mirrors; the shot-shape words and the fits' physical sign do.
QString deliveryDirection(double v);

} // namespace pinpoint::charts
