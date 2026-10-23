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

// THE POSTURE SECTION'S CORE (docs/design/posture_chart_design.md, stage 2a): everything the Posture
// section of Charts draws and says, computed here in plain C++, so the presenter (stage 2b) only
// serialises it and the QML only places it. No QObject, no files, no threads of its own.
//
// ── The six rows (§1.1) ─────────────────────────────────────────────────────────────────────────
//   head height   headLift          face-on  cm        + = up
//   hip depth     pelvisThrust      DTL      cm        + = in (toward the ball); P3–P5 hipDepthButt (decision 15)
//   trunk bend    spineForwardBend  DTL      °         − = upright (bend lost)
//   lead knee     leadKneeFlexion   DTL      °         + = more bend
//   trail knee    trailKneeFlexion  DTL      °         + = more bend   (a STYLE: never matched)
//   pelvis height pelvisLift        face-on  % stance  + = up
// at P1..P8 (Phase ids 0, 12, 8, 2, 13, 9, 5, 14). A value is the CHANGE FROM ADDRESS.
//
// ── One number, one judge ───────────────────────────────────────────────────────────────────────
//   · VALUES are the diagnostics measures' own (`m_headLift_p2` … and the reused cells
//     m_headLiftBack, m_spineBendChangeTop, m_trailKneeFlex, m_pelvisLiftTop, m_headLiftImpact,
//     m_pelvisThrustImpact, m_spineBendChangeImpact, m_pelvisLiftImpact), reduced by the measure
//     engine's own sampler: postureInputFromShot() hands the shot's series to buildPhaseGrid()
//     (measure_sample.cpp) and reads each cell with reduceOverGrid(grid, measure) — exactly what
//     measure_dump and Session diagnostics read. A cell with no measure (P8 of the rows masked after
//     impact) is absent ("-"), never 0.
//   · BANDS (the good-player wash) are resolved at an EXPLICIT pattern node, `posture_level` or
//     `posture_squat_push`, falling back up the context tree (§7.3). Description, never a grade.
//   · FAULT LINES are the corridors of the conditions that already grade that row at that position,
//     resolved at the SHOT's grading context (never a posture node): the fault side keeps one judge.
//
// ── Decisions this file takes where the design leaves room (stated once, here) ──────────────────
//   1. "Outside the band" is beyond the band's Ideal edges (|z| > 1, z on the side's σ); an EXIT is
//      |z| > 2 (§4.4.4). A cell under a ⚠ is NEVER outside: it keeps its number and its ⚠ and short
//      reason, and gets no ring, no word, no W-D6 note, no exit and no departure (22 Oct review).
//      The ring's WORD names the side of the band the reading falls outside on — the row's pole word,
//      plusWord above the band, minusWord below it ("straighter", "in") — never the change from
//      address, and never a second number (the figure is printed above it).
//   2. A HEAD is "level" within ±1 cm of address (kPostureHeadLevelCm), and is "up"/"down" only when
//      it is outside the band AND not level. The design says both "up if above the band" (§5.3) and
//      "Nick's head is level; his loss of bend is all hips" (§4.5) while Nick's +0.1 cm at impact is
//      above the Level band's top (−0.5 cm); the tolerance makes both true.
//   3. The close-call GAP is the difference of the two distances AS SHOWN (2 dp), so the page never
//      says "1.41 against 1.23, gap 0.17". The 0.15 rule reads that same figure. "Between" applies
//      only while the nearer pattern is within d 2; past it the words are "no pattern fits closely".
//   4. EXITS exclude the trail knee, like the match: it is a style (§4.2).
//   5. A FAULT LINE belongs to a cell when the condition's signal is an outsideCorridor test on a
//      measure of the same series ANCHORED AT ADDRESS: a delta ending at that position, or an anchored
//      extremum whose window holds it (early extension's peak over P5–P7 draws on P5, P6 and P7). The
//      edge is where the signal fires: the later of the Ideal edge and the deviation edge (norm.h
//      deviationEdges, as characteristic_engine.cpp's outsideCorridor does).
//   6. REPEATABILITY compares middle halves NORMALISED BY THE BAND's σ (IQR / σ), so cm, ° and %
//      rows can be ranked together; the words give the middle half in the row's own unit.
//   7. Figures: cm to one decimal, ° and % stance in whole units; U+2212 minus; "-" when missing.
//   8. AN EXIT IS NAMED BY A DEPARTURE ONLY WHEN ONE OF THAT DEPARTURE'S CONDITIONS FIRED (§8: "it
//      grades nothing new"). This shot: the shot's own ShotDiagnostics.fired. Session (the medians):
//      the condition's session tier is "pattern" (SessionSnapshot::tiers, the ledger's own reduction);
//      for a condition the tiers do not hold, the ledger's own gate (diagnostic_ledger.h conditionTier
//      over wilsonLowerBound) on the shots' fired / clean counts. An exit no fired condition names keeps
//      its candidate departure (for "what moved" and the hover) and says the W-D6 words, "outside the
//      good-player band at P4, not far enough to be a fault" (§5.2). A shot with no diagnostics names
//      nothing. The departures list and the session's departure counts hold named departures only.
//      A departure is named ONLY BY ITS OWN CONDITIONS (§4.3, 22 Oct): loss_of_posture names none —
//      it is a composite of several parts, so it stays a Finding when it fires and never says
//      "hips in" or "standing up" for the chart.
//   9. ROOTS BEFORE SYMPTOMS (22 Oct). The head line leads with the named departure whose conditions
//      come first in the session's WORK-ON ORDER (SessionSnapshot::workOnOrder — the work-on list's own
//      reduction, Coaching/work_ons_pack.h), its exit stated with the good-player band — the row that
//      defines the departure before a trunk bend attributed to it, then the largest; a
//      departure none of whose conditions is ranked follows the ranked ones, by |z|. No order (no
//      ledger): the largest named exit, as before. The presenter orders Findings by the same order.
//      (Decision 11 widens the head line from the named EXITS to every FIRED departure condition.)
//  10. THE TRAIL KNEE'S BAND FOLLOWS ITS STYLE (§4.2, 22 Oct). Released (it loses ≥ 8° by P4,
//      postureTrailStyle): P2–P4 read the `posture_trail_released` node's band (−15 ± 8°, a coaching chain's published observation);
//      kept: the pattern's own (Kim's 0 ± 5°). P5–P7 are the pattern's either way. A shot is judged on
//      its own style; the session on the session's (its medians). The trail knee still never matches.
//  11. THE HEAD LINE PREFERS A FIRED FAULT (22 Oct, round 2). This shot: the top-ranked (work-on order, then
//      how far the cell sits from the band) FIRED condition that names a departure leads, read on the
//      departure's own row (D1 the hips, D2–D6 the head, D7 the lead knee) at the position the condition
//      read it (PostureShotInput::conditionPos) when that lies in the departure's window, else the window's
//      readable cell farthest from the band — "Your hips moved 5.3 cm toward the ball by impact; good players
//      stay between −1.5 and +3.5 cm." — whether or not that cell is an |z| > 2 exit. A cell under a ⚠ or
//      with no value is never read. Only when no departure condition fired: the largest exit in the W-D6
//      words, then the first W-D6 cell, then "stays inside". The session: the top-ranked departure one of
//      whose conditions is a session PATTERN (decision 8's test) leads its tail — with its shot count where
//      shots left by it, else its median reading as above; none: the departure counts as before.
//  12. A SESSION BETWEEN THE TWO PATTERNS (the medians' fit "between", Auto) heads with every shot's word:
//      "Between Level and Squat and push: Level on 17, between on 30 of 50 shots" (zero terms left out);
//      a clear pattern keeps "<Pattern> on N of M shots". The tail follows either way.
//  13. A ROW WITH NO VALUE AT ANY POSITION is said once, in the trust fold, with the gate that refused it
//      (its words and its evidence): "Hip depth: not measured on this shot — no ruler: no ball to measure
//      with in either view." The session says "on any shot" / "on 2 of 9 shots", the reason the shots
//      share most often. Rows with the same reason share one line.
//  14. THE NOUN. The session's words count "shots" (Charts); the report passes "swings" (its own word), and
//      every session sentence — the head, the pattern line, the trust lines — counts in it.
//  15. HIP DEPTH AT P3–P5 IS THE BUTT LINE (stage 4c, 22 Oct). Those three cells read m_hipDepthButt_p3/_p4/_p5
//      (hipDepthButt: the rear edge of the person mask at hip height, the change from address) with no ⚠, and
//      take full part in the match, the exits, the departures and "what moved". Where a shot has no butt line
//      at that position (no DTL ruler, or the mask refused that frame) the cell falls back to the hip joints
//      (m_pelvisThrust_pN) WITH the ⚠ ("the hip points turn with the pelvis") — never mixed silently: the shot
//      records the source per cell (PostureShotInput::hipSource), and the trust fold says which source each
//      cell used ("Hip depth at P3–P5: the butt line on 26 of 27 shots; the hip joints (⚠) on 1 — …"). The
//      session's box at a cell holds each shot's own source; its median is matched on the butt-line shots
//      alone when any has one, else on the joints, under the ⚠. P2, P6 and P7 stay on pelvisThrust.
//  16. MARK'S HONESTY RULE (22 Oct): "We are honest about the numbers we present. We don't hide from the fact that
//      they might be unreliable. We strive to improve and remove those warnings but trust the user to assess the data
//      intelligently." Every reading we have is shown, with a ⚠ and short words where it may be unreliable; the full
//      reason once in the trust fold. So (a) pelvis height at P3–P5 draws its authored-seed band (§7.1) under the
//      ⚠ and stays out of the match; (b) trunk bend at P3–P5 carries a SECOND reading under each cell — the back line
//      from the person mask (spineBackLine, m_spineBackLine_p3/_p4/_p5), "back line −3" with its own ⚠ ("can catch
//      the arms at the top"). It is not matched, not ringed, has no band of its own (it reads against the row's), and
//      the session draws the shots' median of it. The joint reading stays the matched one (§2.3).

#include "session_series_types.h"
#include "../typeset/box_stats.h"
#include "../../Diagnostics/norms/norm.h"   // GradePolicy

#include <QString>
#include <QStringList>

#include <QHash>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace pinpoint::analysis {
struct CharacteristicPack;
class INormProvider;
}

namespace pinpoint::charts {

inline constexpr int    kPostureRows         = 6;
inline constexpr int    kPosturePositions    = 8;      // P1..P8, index 0..7
inline constexpr int    kPostureSessionShots = 5;      // §4.4.6: the session chooses the pattern from 5 shots
inline constexpr double kPostureCloseCall    = 0.15;   // §4.4.3
inline constexpr double kPostureExitZ        = 2.0;    // §4.4.4
inline constexpr double kPostureTrailKeptDeg = 8.0;    // §4.4.5: kept if it loses less than this by P4
inline constexpr double kPostureHeadLevelCm  = 1.0;    // decision 2 above

enum class PostureRow : int { Head = 0, Hips, Bend, LeadKnee, TrailKnee, PelvisHeight };
enum class PosturePattern : int { Level = 0, SquatPush = 1 };   // a pattern MODE adds −1 = Auto
enum class PostureCamera : int { FaceOn = 0, Dtl = 1 };

struct PostureRowSpec {
    PostureRow    row;
    QString       key;          // "head" | "hips" | "bend" | "leadKnee" | "trailKnee" | "pelvisHeight"
    QString       metricKey;    // the series
    QString       label;        // "Head height"
    QString       noun;         // "head" — for sentences
    QString       unit;         // the unit as shown: "cm" | "°" | "% stance"
    int           decimals = 0;
    PostureCamera camera = PostureCamera::FaceOn;
    QString       plusWord, minusWord;   // the direction words: "up"/"down", "in"/"back", …
    QString       poleWord;              // the word at the row's right ("up", "in", "upright", …)
    std::array<QString, kPosturePositions> measureIds;   // [0] (P1) empty; "" = no measure there
};

const std::array<PostureRowSpec, kPostureRows> &postureRowSpecs();
const PostureRowSpec &postureRowSpec(PostureRow r);
const std::array<int, kPosturePositions> &posturePhaseIds();   // P1..P8 → Phase enum ids
QString posturePositionTag(int pos);     // "P1" … "P8"
QString posturePositionWhen(int pos);    // "address", "P2", "P3", "the top", "P5", "P6", "impact", "P8"
QString posturePatternName(PosturePattern p);   // "Level" | "Squat and push"
QString posturePatternContext(PosturePattern p);   // "posture_level" | "posture_squat_push"
QString postureCameraWords(PostureCamera c);   // "face-on" | "down the line"
QString postureTrailReleasedContext();           // "posture_trail_released" (decision 10)

// The ⚠ every capture carries today (§2.3): the reason, or "" when the cell is not warned. Hip depth carries
// none by construction since stage 4c: its P3–P5 ⚠ is the shot's, where it fell back to the joints (decision 15).
QString postureWarnReason(PostureRow r, int pos);
// Decision 15: the hip-depth cells read on the butt line (P3–P5), the joints' measure there, and the ⚠ words a
// joint reading at those cells carries.
bool    postureHipButtPos(int pos);
QString postureHipJointsMeasure(int pos);   // "m_pelvisThrust_p4"; "" outside P3–P5
QString postureHipJointsReason();           // "the hip points turn with the pelvis"
QString postureHipButtKey();                // "hipDepthButt": the series those cells read (a reader of a shot keeps it)
// Decision 16: the trunk-bend row's second reading at P3–P5, the back line.
bool    postureBackLinePos(int pos);            // P3..P5
QString postureBackLineKey();                   // "spineBackLine"
QString postureBackLineMeasure(int pos);        // "m_spineBackLine_p4"; "" outside P3–P5
QString postureBackLineLabel();                 // "back line"
QString postureBackLineWarnShort();             // "can catch the arms at the top"
QString postureBackLineTrust();                 // the trust fold's one sentence on the two readings
// The value as the profile prints it: "+0.8", "−10", "0", "-".
QString postureFigure(PostureRow r, double v);

using PostureGrid  = std::array<std::array<double, kPosturePositions>, kPostureRows>;   // NaN = absent
using PostureFlags = std::array<std::array<bool, kPosturePositions>, kPostureRows>;
using PostureWords = std::array<std::array<QString, kPosturePositions>, kPostureRows>;
PostureGrid postureEmptyGrid();
std::array<double, kPosturePositions> postureEmptyCells();   // NaN at every position

// ── Bands and fault lines ───────────────────────────────────────────────────────────────────────

struct PostureBand {
    bool    has = false;
    double  mu = kNaN, sigmaLo = kNaN, sigmaHi = kNaN;
    double  lo = kNaN, hi = kNaN;             // the wash: the Ideal edges (bandEdgesOf)
    double  watchLo = kNaN, watchHi = kNaN;   // where the norm's Action begins (for a fuller wash)
    QString measureId;
    QString contextId;                        // where the row was found (posture_squat_push, any, …)
    bool    inherited = false;
    QString source;                           // "literature" | "heuristic" | "seated" | …
    QString citation;
    int     n = 0;
    double z(double v) const;                 // on the side's σ; NaN without a band or a value
};
using PostureBandGrid = std::array<std::array<PostureBand, kPosturePositions>, kPostureRows>;

struct PostureFault {
    bool    has = false;
    double  edge = kNaN;      // the value at which the condition fires (in the row's unit)
    QString conditionId, label, measureId, contextId;
};
struct PostureFaultCell { PostureFault lo, hi; };
using PostureFaultGrid = std::array<std::array<PostureFaultCell, kPosturePositions>, kPostureRows>;

// A condition's reach onto the grid (decision 5), from the pack once.
struct PostureFaultLink {
    PostureRow row;
    int        pos = 0;
    bool       high = true;
    QString    conditionId, label, measureId;
    analysis::Shape shape = analysis::Shape::Target;
};

// A rig-sensitive address measure (§7.4): the pack's `rigSensitive` flag decides the list, never a hard-coded one.
// `name` is the golfer's words (the measure's golferName, capitalised, its "at address" left to the block's title),
// else its label; `camera` the view it reads (viewNeeded, else the posture row on the same series, else down the line).
struct PostureAbsoluteSpec {
    QString measureId, label, unit, metricKey;
    QString name;
    PostureCamera camera = PostureCamera::Dtl;
};

// THE GOLFER'S OWN EARLIER SESSIONS FROM THE SAME SET-UP (§7.4.2), as the presenter reads them off the athlete folder
// (charts_posture_presenter.cpp readAddressHistory): each earlier session's diagnostics ledger — the measure's graded
// `value` on each swing, the engine's own number — kept when it carries the same camera on that view. Where the camera
// STOOD is recorded by no session (there is no placement record and no calibration yet), so the key is the view and
// the camera's serial; a session whose serial is not recorded is kept, and said so.
struct PostureAddressHistory {
    bool looked = false;                       // an athlete folder was read
    struct View {
        bool    known = false;                 // this session's streams were read
        bool    present = false;               // …and carry this view
        QString serial;                        // this session's camera on it ("" not recorded)
        int     kept = 0;                      // earlier sessions kept for this view (a reading of it on some swing)
        int     otherCamera = 0;               // left out: another camera on this view
        int     unrecorded = 0;                // kept, their camera's serial not recorded
        int     swings = 0;                    // swings with any reading of this view in the kept sessions
    };
    std::array<View, 2> views;                 // by PostureCamera
    QHash<QString, std::vector<double>> values;   // measureId → one reading per swing, the kept sessions'
    QHash<QString, int> sessions;              // measureId → the kept sessions those readings came from
    int dataWarned = 0;                        // swings with a data warning among the kept readings (counted, as the session box counts its own)
};

// The pack and the norms the section reads, with both patterns' bands resolved once.
struct PostureEnv {
    const analysis::CharacteristicPack           *pack = nullptr;
    std::shared_ptr<const analysis::INormProvider> norms;
    analysis::GradePolicy                         policy;
    PostureBandGrid level, squatPush;
    // The released trail-knee style's band (decision 10): the trail-knee row at P2–P4 only, and only
    // where the norm row sits AT posture_trail_released (never a fallback up the tree).
    PostureBandGrid trailReleased;
    std::vector<PostureFaultLink>    faultLinks;
    std::vector<PostureAbsoluteSpec> absolutes;   // the pack's rigSensitive measures (§7.4)

    const PostureBandGrid &bands(PosturePattern p) const { return p == PosturePattern::SquatPush ? squatPush : level; }
};

// Build an environment over a pack and a norm set (either may be null: no bands, no fault lines).
PostureEnv postureEnv(const analysis::CharacteristicPack *pack,
                      std::shared_ptr<const analysis::INormProvider> norms,
                      const analysis::GradePolicy &policy = {});
// The shipped pack (makeCharacteristicPackProvider) and sharedNormProvider(), built on first use.
const PostureEnv &postureSharedEnv();

PostureBandGrid  postureBands(const PostureEnv &env, PosturePattern p);
struct PostureTrailStyle;
// The pattern's bands with the trail knee's P2–P4 taken from its style (decision 10).
PostureBandGrid  postureBandsFor(const PostureEnv &env, PosturePattern p, const PostureTrailStyle &trail);
PostureFaultGrid postureFaults(const PostureEnv &env, const QString &gradingContextId);

// ── One shot's input ────────────────────────────────────────────────────────────────────────────

// Which reading a P3–P5 hip-depth cell holds (decision 15).
enum class PostureHipSource : int { None = 0, ButtLine, Joints };

struct PostureShotInput {
    int     ordinal = 0;
    int     shotId  = -1;
    QString swingDir, club;
    QString contextId;                        // the shot's grading context (fault lines resolve here)
    PostureGrid  value = postureEmptyGrid();  // the change from address; P1 = 0 when address was read
    std::array<bool, kPostureRows> present{}; // the shot carries the row's series
    PostureFlags gateWarn{};                  // a gate refused or bounded the reading: drawn with ⚠, not matched
    PostureWords gateWords;                   // the gate's words (also the reason for a "-")
    bool        graded = false;               // the shot carries a diagnostics row
    QStringList fired, clean;                 // ShotDiagnostics' own: the exits are named from `fired`
    QHash<QString, int> conditionPos;         // each fired condition's position on this shot (postureConditionPos)
    std::array<QString, kPostureRows> gateWhy;   // the refusing gate's evidence for the row ("No ball to measure
                                                 // with in either view."), "" none — the trust fold's reason
    std::vector<double> absolutes;            // parallel to PostureEnv::absolutes; NaN = absent
    // Decision 15: the hip-depth cell's source at P3–P5 (None elsewhere, and where no value was read). A Joints
    // cell is drawn with the ⚠ and never matched; the fixture seam's numbers there are the joints'.
    std::array<PostureHipSource, kPosturePositions> hipSource{};
    // Decision 16: the back line's change from address at P3–P5 (NaN elsewhere and where unread). Drawn under the
    // trunk-bend cells as a second reading; never in `value`, so never matched.
    std::array<double, kPosturePositions> backLine = postureEmptyCells();
};

// The cells this shot draws under a ⚠: its gates' (gateWarn) and the hip-depth cells read on the joints.
PostureFlags postureShotWarn(const PostureShotInput &shot);

// The position a condition is read at on this shot: the reading that drove (or graded) it, else its first
// reading in the pack; −1 when neither is one of P1..P8. `shot` may be null (the pack's alone).
int postureConditionPos(const ShotSeries *shot, const PostureEnv &env, const QString &conditionId);

// From the Charts snapshot's shot, through the measure engine's sampler (see the header note).
PostureShotInput postureInputFromShot(const ShotSeries &shot, const PostureEnv &env);
// A fixture seam: plain per-position changes; a row is present when any of its cells is finite.
// `fired`: the conditions the fixture's shot fired (graded when any is given); none names no exit.
// Hip depth at P3–P5 is taken as the joints' reading (decision 15: ⚠); set hipSource to ButtLine for the butt line.
PostureShotInput postureInputFromValues(const PostureGrid &changes, int ordinal = 1, const QStringList &fired = {});

// ── The pattern match (§4.4) ────────────────────────────────────────────────────────────────────

struct PostureMatch {
    bool    ok = false;                // at least one cell compared
    double  dLevel = kNaN, dSquatPush = kNaN;
    int     cells = 0;                 // cells compared
    PosturePattern nearest = PosturePattern::Level;
    double  gap = kNaN;                // |dSquatPush − dLevel| as shown (decision 3)
    QString fit;                       // "close" | "nearest" | "between" | "none"
    QString words;                     // "between Level and Squat and push (Squat and push nearer)"
    QString basis;                     // "both" | "faceOn" | "dtl" | "none"
    QString basisWords;                // "" for both; else which rows it matched on, and why
    QStringList rows;                  // the row keys compared
};

// d over P2–P7: no P8, no ⚠ cell (by construction, or in `warn`), no trail knee, no absent cell or band.
PostureMatch postureMatch(const PostureGrid &x, const std::array<bool, kPostureRows> &present,
                          const PostureFlags &warn, const PostureEnv &env);

// ── Exits and departures (§4.3, §4.4.4) ─────────────────────────────────────────────────────────

struct PostureExit {
    PostureRow row = PostureRow::Head;
    int     pos = 0;
    double  value = kNaN, z = kNaN;
    QString departure;                 // "D1" … "D7", "" when no departure describes it
    QString name;                      // the candidate's name: "Hips in", … ; "the trunk alone"; "" when none
    QStringList conditionIds;          // the conditions that name it (pack-checked)
    bool    named = false;             // one of conditionIds fired (decision 8): only then is `name` said
    QString words;                     // "P6 hips +6.5 cm: Hips in" (named) /
                                       // "P2 head +4.7 cm: outside the good-player band at P2, not far enough to be a fault"
};

struct PostureDeparture {
    QString id, name;
    QStringList conditionIds;
    int     firstPos = -1;
    std::vector<int> positions;
};

// Did this condition fire (decision 8)? An empty test: nothing fired, nothing is named.
using PostureFiredTest = std::function<bool(const QString &conditionId)>;
// Where a condition stands in the session's work-on order (decision 9): its index, or INT_MAX when the
// order does not hold it (or there is none).
int postureWorkOnRank(const QStringList *order, const QString &conditionId);
// A departure's rank: the best of its conditions' that `counts` (fired here / a session pattern).
int postureDepartureRank(const QStringList *order, const QStringList &conditionIds, const PostureFiredTest &counts = {});
// The W-D6 words at a position: "outside the good-player band at P4, not far enough to be a fault".
QString postureWd6Words(int pos);

std::vector<PostureExit> postureExits(const PostureGrid &x, const std::array<bool, kPostureRows> &present,
                                      const PostureFlags &warn, const PostureBandGrid &bands,
                                      const PostureEnv &env, const PostureFiredTest &fired = {});
// The NAMED departures (decision 8): from the named exits, and D5 when its signature holds (a D4
// candidate going back, the head above address at P7) and one of its conditions fired.
std::vector<PostureDeparture> postureDepartures(const std::vector<PostureExit> &exits, const PostureGrid &x,
                                                const PostureEnv &env, const PostureFiredTest &fired = {});

// The departure table (§4.3) as data, for the presenter's Findings: D1…D7 in order, each with its name
// and the conditions that name it (pack-checked).
struct PostureDepartureSpec { QString id, name; QStringList conditionIds; };
std::vector<PostureDepartureSpec> postureDepartureSpecs(const PostureEnv &env);
// Every condition the section speaks for — the fault lines', the departures', trail_knee_straighten
// (Q3: it stays a fault) and loss_of_posture (a Finding, naming no departure) — in pack order, pack-checked.
QStringList postureConditionList(const PostureEnv &env);

// ── "What moved" and the trail knee (§5.3, §4.4.5) ──────────────────────────────────────────────

struct PostureTrailStyle {
    bool    known = false;
    bool    kept = false;
    double  changeAtTop = kNaN;        // ° at P4
    QString word;                      // "kept" | "released"
    QString words;                     // "trail knee kept its flex" | "trail leg straightened 18°"
    bool released() const { return known && !kept; }
};
PostureTrailStyle postureTrailStyle(const PostureGrid &x);

struct PostureMoved {
    int     pos = 3;
    QString when;                      // "At the top" | "At impact"
    QString line;                      // the sentence
    bool    hipsIn = false, hipsWarned = false;
    QString head;                      // "up" | "down" | "level" | "inside" | ""
    QString bend;                      // "lost" | "gained" | ""
    bool    bendWarned = false;
    QString attribution;               // "hips in" | "hips in, then up" | "standing up" | "the trunk alone" | …
};
PostureMoved postureWhatMoved(const PostureGrid &x, const PostureFlags &warn, const PostureBandGrid &bands,
                              int pos, const PostureTrailStyle &trail);

// ── The views ───────────────────────────────────────────────────────────────────────────────────

struct PostureCellView {
    int     pos = 0;
    QString tag;                       // "P1" …
    double  value = kNaN;
    QString text;                      // the figure, "-" when absent
    bool    warned = false;            // ⚠ drawn
    QString warnReason;                // the ⚠ words (by construction, else the gate's)
    QString gateWords;                 // the gate's words, also on a "-"
    bool    counted = false;           // enters the match
    PostureBand band;                  // the chosen pattern's band at this cell
    double  z = kNaN;
    bool    outside = false;           // |z| > 1: ringed with `ring`
    QString ring;                      // the side's pole word: "in", "straighter" (decision 1); "" under a ⚠
    bool    exit = false;              // an exit (|z| > 2, exit-eligible)
    PostureFaultCell fault;            // the fault lines
    bool    beyondFault = false;
    int     firedShots = 0;            // shots on which a fault line's condition fired (shot view: 0/1)
    QString note;                      // the W-D6 words when outside the band but no fault fired
    // Decision 16: a second reading under the cell (trunk bend at P3–P5: the back line). Not matched, not ringed,
    // no band of its own. `alt` is set where the cell carries one, with or without a value.
    bool    alt = false;
    double  altValue = kNaN;
    QString altLabel;                  // "back line"
    QString altFigure;                 // "−3", "-"
    QString altText;                   // "back line −3"
    bool    altWarned = false;         // ⚠ drawn (on a value)
    QString altWarnShort;              // "can catch the arms at the top"
};

struct PostureRowView {
    PostureRow row = PostureRow::Head;
    QString key, label, metricKey, camera, unit, poleWord, plusWord, minusWord;
    bool    present = false;
    bool    style = false;             // the trail knee: shown, never matched
    QString styleWord;                 // "kept" | "released" on the trail knee
    std::array<PostureCellView, kPosturePositions> cells;
    QString absentWhy;                 // no value at any position: the gate's words and evidence ("" none; decision 13)
};

// ONE ADDRESS READING (§7.4.2): its number, the good-player band at the grading context, this session's box and the
// golfer's earlier sessions from the same set-up, and the ⚠ ("depends on where the camera stands") — off only on a
// trusted capture, which nothing can declare yet, so it is on everywhere today.
struct PostureAbsoluteView {
    QString measureId, label, unit, metricKey;
    QString name;                      // the golfer's words: "Forward bend from your hips"
    PostureCamera camera = PostureCamera::Dtl;
    double  value = kNaN;              // this shot's (shot view), the median (session view); NaN = "-"
    QString text;                      // "39°" | "-"
    PostureBand band;                  // the norm row the pack resolves at the grading context (no band: has false)
    QString bandText;                  // "good players 32 to 48°" | "no good-player band yet"
    box::BoxStats box;                 // this session's shots
    QString boxText;                   // "this session: median 39°, n 10" | "this session: no reading"
    box::BoxStats history;             // the golfer's earlier sessions from the same set-up (n 0: none)
    int     historySessions = 0;
    QString historyText;               // "your earlier sessions: median 38°, n 34 over 4 sessions" | ""
    QString historyWhy;                // the words where there is no history box ("" with one)
    bool    warned = true;             // ⚠ until a capture can be declared trusted (§7.4.3)
    QString warn, warnShort;           // "⚠", "depends on where the camera stands"
    QString trust;                     // §7.4.3, verbatim
};

struct PostureTrust {
    QStringList cameras;               // one line per camera
    QString     hipSource;             // decision 15: which reading hip depth at P3–P5 holds ("" no value there)
    QStringList absent;                // the rows with no value, and why (decision 13)
    QStringList warnings;              // the ⚠ reasons, then any gate words
    QString     bandSources;           // "Bands from 20 tour professionals (Kim 2021), …"
    QString     trunkDatum;            // §5.6
    QString     rigRule;               // §7.4.3 when absolutes are shown
    QStringList lines;                 // all of the above, in order, said once
};

struct PostureShotView {
    bool    available = false;
    int     ordinal = 0;
    int     patternMode = -1;          // −1 Auto, 0 Level, 1 Squat and push
    PosturePattern pattern = PosturePattern::Level;
    QString patternName;
    QString patternSource;             // "session" | "shot" | "chosen"
    QString patternSourceWords;        // "of your session" | "from this shot: fewer than five shots…" | "your choice"
    QString patternLine;               // "Level (nearest, of your session) · trail knee released"
    PostureMatch match;                // this shot's own distances
    PostureMatch sessionMatch;         // the session's, when it chose
    std::array<PostureRowView, kPostureRows> rows;
    std::vector<PostureExit>      exits;
    std::vector<PostureDeparture> departures;
    PostureTrailStyle trail;
    PostureMoved top, impact;
    QString headline;
    QStringList firedConditions;       // posture conditions fired on this shot (trail_knee_straighten among them)
    std::vector<PostureAbsoluteView> absolutes;   // the "At address" block: a row per reading the capture can produce
    QStringList addressLines;          // its trust lines (in `trust.lines` too): the rig rule, the set-up key, the
                                       // bands' source, the readings this capture cannot produce
    PostureTrust trust;
};

struct PosturePatternMark {
    int     ordinal = 0;
    bool    ok = false;
    PosturePattern nearest = PosturePattern::Level;
    bool    between = false;
    bool    differs = false;           // ringed: not the session's pattern
    double  dLevel = kNaN, dSquatPush = kNaN;
};

struct PostureDepartureCount {
    QString id, name;
    QStringList conditionIds;
    int     shots = 0;
    std::vector<int> ordinals;
    int     firstPos = -1;             // the earliest position it starts at on any shot
};

struct PostureRepeat {
    bool    has = false;
    PostureRow row = PostureRow::Head;
    int     pos = 0;
    double  iqr = kNaN, iqrOverSigma = kNaN;
    QString words;                     // "head at P7, middle half 2.1 cm"
};

struct PostureSessionRow {
    PostureRowView view;               // cells hold the MEDIAN, against the session's pattern
    std::array<box::BoxStats, kPosturePositions> boxes;
};

struct PostureSessionView {
    bool    available = false;
    int     shots = 0;                 // shots carrying a posture reading
    int     patternMode = -1;
    PosturePattern pattern = PosturePattern::Level;
    bool    fromSession = false;       // ≥ 5 shots, or chosen
    QString patternName, patternSource, patternSourceWords, patternLine;
    PostureMatch match;                // on the medians
    QString gradingContextId;          // the shots' most common grading context
    std::array<PostureSessionRow, kPostureRows> rows;
    std::vector<PostureExit>      exits;        // of the medians; named by a session-pattern condition (decision 8)
    std::vector<PostureDeparture> departures;   // of the medians, named only
    std::vector<PosturePatternMark> strip;
    int     onPattern = 0;             // shots whose own nearest is the session's pattern (not between)
    std::vector<PostureDepartureCount> departureCounts;   // across shots, most shots first: a shot counts when the
                                                          // departure is NAMED on it (its condition fired there)
    PostureRepeat tightest, loosest;
    QString repeatWords;
    PostureTrailStyle trail;
    PostureMoved top, impact;
    QString headline;
    std::vector<PostureAbsoluteView> absolutes;   // as PostureShotView's: value = the session's median
    QStringList addressLines;
    PostureTrust trust;
};

// The session: medians, boxes, the pattern (from ≥ 5 shots unless chosen), the strip, departures,
// repeatability. `patternMode` −1 Auto, 0 Level, 1 Squat and push. `tiers`: the ledger's session tier
// per condition (SessionSnapshot::tiers), which names the medians' exits (decision 8); may be null.
// `workOnOrder`: SessionSnapshot::workOnOrder, which leads the head line (decision 9); may be null.
// `noun`: what the words count, plural — "shots" (Charts), "swings" (the report; decision 14).
// `history`: the golfer's earlier sessions from the same set-up (the "At address" block's second box); may be null.
PostureSessionView postureSession(const std::vector<PostureShotInput> &shots, const PostureEnv &env,
                                  int patternMode = -1, const QHash<QString, QString> *tiers = nullptr,
                                  const QStringList *workOnOrder = nullptr, const QString &noun = QStringLiteral("shots"),
                                  const PostureAddressHistory *history = nullptr);

// One shot, judged against the session's pattern when the session chose one (≥ 5 shots), else its
// own nearest, said so; `patternMode` ≥ 0 overrides both. `session` may be null (no session).
// `workOnOrder` as for postureSession (decision 9).
// `history` as for postureSession. With a session, this shot's address readings ride on the session's boxes.
PostureShotView postureShot(const PostureShotInput &shot, const PostureEnv &env, int patternMode = -1,
                            const PostureSessionView *session = nullptr, const QStringList *workOnOrder = nullptr,
                            const PostureAddressHistory *history = nullptr);

// §7.4.3, verbatim (Mark, 22 Oct): said once, in the trust fold, whenever the "At address" block has a row.
QString postureRigRule();
// The ⚠'s few words under every address reading (§7.4.2).
QString postureRigWarnShort();
// The address reading's figure: "39°", "56 % foot length", "-" (cm to one decimal, the rest whole).
QString postureAddressFigure(double v, const QString &unit);

} // namespace pinpoint::charts
