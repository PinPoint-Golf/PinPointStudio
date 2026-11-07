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

// CONSISTENCY (docs/design/consistency_design.md): where a swing repeats, where it gets loose, and what
// stood out on one shot. Value types in, value types out, in two layers:
//
//   consistencyInput   SessionSnapshot + alignment + env → ConsistencyInput. The sampling: every metric
//                      of every shot turned into readings, one row per shot.
//   consistencyResult  ConsistencyInput → ConsistencyResult. The stand-out, ball-flag and shuffle rules.
//                      Pure: no snapshot, no pack, no catalogue.
//
// The table's GRADES are not here: every reading and cell is graded by the variability grader
// (consistency_grades.h). The old steady / some / loose verdict and cell words were retired (Mark, 7 Nov).
//
// Neither touches a pack: areas and norms come through ConsistencyEnv. Both are safe on any thread with
// an env in hand. Take the production env (consistencyEnvFromPacks) on the GUI thread, and capture it by
// value in the worker.
//
// COLUMN 7, P7-P8, is sampled differently, on every session. P8's confidence is under the warp's gate
// on most shots of some sessions, so P8 often has no canonical time, and impact-to-P8 has two modes
// confidence does not separate. So the column never reads a P8 mark: it is the same 100 ms after impact
// on every shot of every session, on the shot's own clock, and it exists whether or not a shot has a P8.
//
// COST. Sampling is two windowed means per curve per shot, one inside alignedCurves and one for column 7's
// own clock. The shuffle does not re-run the stand-out
// rule 30 times: a shuffle moves a reading's rows whole, and both a row's residuals and the shots it is
// compared with are the same wherever it lands, so a row stands out or not once and for all. Each
// shuffle only counts which shot it landed on.

#include "../../Coaching/consistency_home.h"    // the table's shape: columns and areas
#include "session_series_types.h"
#include "session_warp.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <vector>

namespace pinpoint::charts {

// ── the rules' constants. All first guesses on one golfer (doc §4); the goldens pin them ─────────
inline constexpr int    kConsistencySamplesPerSpan = 5;     // doc §4.1: five points inside a span. A stand-out takes the
                                                            // WEAKEST of the five (§4.3 "across the whole span")
inline constexpr int    kConsistencyMinSpanSamples = 3;     // doc §4.1: a span needs a spread at 3 of its 5 points
inline constexpr double kConsistencySpreadDivisor  = 1.349; // doc §4.1: the middle half ÷ 1.349
inline constexpr int    kConsistencyMinClubShots   = 3;     // doc §4.1: a club under 3 shots is left out, counting every shot
                                                            // with the label. The club's median INCLUDES the shot
inline constexpr int    kConsistencyMinSpreadShots = 5;     // doc §4.1: a spread needs at least 5 shots
inline constexpr int    kConsistencyMinOthers      = 6;     // doc §4.3: a stand-out needs 6 other shots at the point
inline constexpr double kConsistencyStandOutZ      = 3.5;   // doc §4.3: robust spreads from the others' median
inline constexpr double kConsistencyStandOutSigma  = 2.0;   // doc §4.3: and past 2σ, where the reading has a σ
inline constexpr int    kConsistencyMinBallShots   = 8;     // doc §4.4: 8 shots of the club with the reading
inline constexpr double kConsistencyFenceIqr       = 1.5;   // doc §4.4: the Tukey fence
inline constexpr int    kConsistencyShuffles       = 30;    // contract: shuffled sessions behind the yardstick (doc §2.4)
inline constexpr quint64 kConsistencyShuffleSeed   = 5;     // contract: splitmix64's seed, shared with the tally
inline constexpr double kConsistencyShuffleP95     = 0.95;  // doc §4.3: "above the shuffled session's 95th percentile"
inline constexpr double kConsistencyAfterImpactMs = 100.0;      // Mark, 6 Nov: P7-P8 is this long on every shot of every session.
                                                               // The P8 mark is unreliable (#24), and a length per session made
                                                               // cells depend on the length and sessions not comparable
inline constexpr int    kConsistencyMinKeptAnchors = 4;        // contract: a shot with fewer of P1..P7 kept as anchors is guarded
inline constexpr int    kConsistencyImpactColumn = 6;          // doc §4.1: Impact reads P7 itself, in P6-P7
inline constexpr int    kConsistencyBackswingTimingColumn = 3; // doc §4.1: backswing timing is stamped at impact but sits in P3-P4
inline constexpr int    kConsistencyBackswingFirst = 1;        // the headline's backswing: P1-P2 ...
inline constexpr int    kConsistencyBackswingLast  = 3;        // ... to P3-P4
inline constexpr int    kConsistencyDownswingFirst = 4;        // the headline's downswing: P4-P5 ...
inline constexpr int    kConsistencyDownswingLast  = 6;        // ... to P6-P7
inline constexpr int    kConsistencyHeadlineMinAreas = 2;      // one area's reading is not the swing: no headline from it

// The P-position each column ends at: Phase ids of P1..P8.
inline constexpr int kConsistencyPhases[kConsistencyColumns] = { phase::Address, phase::P2, phase::P3, phase::Top,
                                                                 phase::P5,      phase::P6, phase::Impact, phase::P8 };

// What the maths needs from the packs (CONTRACT "The env"). Production: consistency_env.h.
struct ConsistencyEnv {
    std::function<QString(const QString &key)> areaOf;                                    // area token, "" = unplaced
    std::function<double(const QString &key, int phase, const QString &club)> normSigma;  // NaN = no norm
};

// ── Layer 1 ─────────────────────────────────────────────────────────────────────────────────────

struct ConsistencyShot {
    int     ordinal = 0;
    QString club;
    bool    guarded = false;    // see consistencyGuarded
    double  carry = kNaN;       // lm.carryDistance, NaN without one
    double  offline = kNaN;     // lm.offline, NaN without one
};

enum class ConsistencyReadingKind { Span, Position };

// One metric at one column: a value per shot per sample, NaN where there is none.
struct ConsistencyReading {
    QString key, label, unit;
    QString area;               // "" = unplaced: kept here, never in a cell or a stand-out
    int     column = -1;
    ConsistencyReadingKind kind = ConsistencyReadingKind::Position;
    double  ownSigma = kNaN;    // the reading's measurement σ
    double  normSigma = kNaN;   // the median over its shots of the norm's σ for each shot's club
    int     samples = 1;        // kConsistencySamplesPerSpan, or 1 for a position
    std::vector<double> values; // shots × samples, row by row

    double at(int shot, int sample) const { return values[size_t(shot) * size_t(samples) + size_t(sample)]; }
};

struct ConsistencyInput {
    std::vector<ConsistencyShot>    shots;      // strike order
    std::vector<ConsistencyReading> readings;   // by (key, column), bytewise. A reading with no value on any shot is left out
    double afterImpactMs = kConsistencyAfterImpactMs;   // P7-P8's length, always the constant; here for the dump
};

ConsistencyInput consistencyInput(const SessionSnapshot &session, const SessionAlignment &alignment,
                                  const ConsistencyEnv &env);

// True when a shot's P-ladder cannot be trusted to compare it span by span: it was not aligned on its
// P-positions, or kept fewer than 4 of P1..P7 as anchors (impact is one), or a P1..P7 was dropped as an
// outlier segment or for running backwards. A lost P8 never counts, and a P dropped for low confidence
// counts only through the 4. A shot with no curve of any key has nothing to mis-time (launch monitor
// only) and is never guarded. Otherwise a null warp is guarded.
bool consistencyGuarded(const ShotSeries &shot, const ShotWarp *warp);

// True for a reading that rests on the shot's P-ladder, so a guarded shot does not count it: a span, and the
// backswing timing read at P3-P4.
bool consistencyGuardCovers(const ConsistencyReading &reading);

// What a guarded shot is told, in one place: the row's caution and the shot's note both say this.
inline QString consistencyGuardWords() { return QStringLiteral("the timing of this swing's positions looks unusual"); }

// ── Layer 2 ─────────────────────────────────────────────────────────────────────────────────────

// A reading that reached the table: enough shots for a spread (a position 5 finite values, a span a spread at 3 of
// its 5 points). Only these can stand out.
struct ConsistencyJudged {
    QString key;
    int     column = -1;
    int     reading = -1;       // index into ConsistencyInput::readings
};

struct ConsistencyStandOut {
    int     ordinal = 0;
    QString key;
    int     column = -1;
    int     reading = -1;       // index into ConsistencyInput::readings
    double  z = kNaN, dv = kNaN;
    int     sample = -1;        // the weakest point; 0 for a position
};

struct ConsistencyBallFlag {
    int         ordinal = 0;
    QStringList flags;          // "short" then "offline"; a shot with neither has no entry
};

struct ConsistencyResult {
    std::vector<ConsistencyJudged>   readings;  // by (key, column)
    std::vector<ConsistencyStandOut> standouts; // strike order; within a shot biggest |z| first, then (key, column)
    std::vector<ConsistencyBallFlag> flags;     // strike order
    double shuffledMean = 0.0;                  // stand-outs a shot in a shuffled session
    double p95 = 0.0;                           // the 95th percentile of a shuffled session's busiest shot
};

ConsistencyResult consistencyResult(const ConsistencyInput &input);

// What the launch monitor made of one shot, for "the ball" and "the club": the readings of it past a
// Tukey fence of its own club's values, needing 8 of them (doc §4.4), the ball's first, each list in a
// fixed order. Both fences count here, so a long carry is a row though only a short one is a flag. Not
// part of ConsistencyResult: the tally has nothing to check, the fence being the flags' own.
struct ConsistencyLmRow {
    QString key, label, unit;
    bool    ball = true;            // the ball, else the club
    double  value = kNaN;
    int     side = 0;               // +1 past the upper fence, -1 past the lower
    std::vector<double> values;     // one per shot in strike order: the club's own, NaN for any other club
};
std::vector<ConsistencyLmRow> consistencyLmRows(const SessionSnapshot &session, int ordinal);

// Whether the stand-out rules had anything to test on one shot (`shot` = its index), so that a shot with no
// rows is not called unremarkable when it was never compared. A swing test needs a reading the guard leaves
// counted, and 6 other shots at one of its points; a launch-monitor test, 8 values of the shot's club.
struct ConsistencyComparable {
    bool measured = false;      // a camera reading of a placed metric on this shot
    bool swing = false;         // a swing test could run
    bool launchMonitor = false; // a launch-monitor reading the rows read, on this shot
    bool ball = false;          // a launch-monitor test could run
};
ConsistencyComparable consistencyComparable(const SessionSnapshot &session, const ConsistencyInput &input,
                                            const ConsistencyResult &result, int shot);

// The rules one at a time, for the tests and for Charts' Apart (doc §5), which shares the stand-out rule.

// Each value less the median of its club at that sample. `values` is clubs.size() × samples, row by row.
std::vector<double> consistencyResiduals(const std::vector<double> &values, int samples, const QStringList &clubs);

// The middle half ÷ 1.349 of the finite values; NaN under 5.
double consistencySpread(const std::vector<double> &values);

// One shot against the others, itself left out, at the weakest point of the reading.
// z, dv and sample mean something only when standsOut.
struct ConsistencyPoint {
    bool   standsOut = false;
    double z = kNaN, dv = kNaN;
    int    sample = -1;
};
ConsistencyPoint consistencyStandOutOf(const std::vector<double> &residuals, int samples, int shot, double ownSigma);

// splitmix64, the generator the tally shares, so the two shuffles are the same shuffle.
class ConsistencySplitMix {
public:
    explicit ConsistencySplitMix(quint64 seed) : m_state(seed) {}
    quint64 next()
    {
        m_state += 0x9E3779B97F4A7C15ULL;
        quint64 z = m_state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
private:
    quint64 m_state;
};

// A Fisher-Yates shuffle of 0..n-1 from identity: row i of the shuffled reading is row perm[i] of the real one.
std::vector<int> consistencyPermutation(int n, ConsistencySplitMix &rng);

} // namespace pinpoint::charts
