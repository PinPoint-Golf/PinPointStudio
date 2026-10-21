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

// THE SESSION REPORT'S PURE REDUCTIONS (docs/design/session_report_design.md) — every choice the
// client report makes about WHAT to say, as functions of the session's ledger, its work-on record
// and the pack, so each rule can be asserted on a hand-built session and the report cannot say
// something the evidence does not hold. Header-only and unit-tested (src/Analysis/tests/
// session_report_test.cpp), like work_ons.h, whose record it reads.
//
//   pickStrengths()     "Going well" (§3.2): clean, judged habits; the whole-axis rule for outcomes
//   referenceSwing()    the swing nearest every target at once (§6)
//   chooseStill()       which swings and positions a work-on page pictures (§6)
//   rangeWords(), missMagnitude()/missWords()   the target and how far it was missed (§10 rule 5)
//   cameWith()          the habit's authored effects the session judged (§3, the work-on page)
//   causeGroups()       spotted / could be behind it / ruled out, and the lead case (§3.3)
//   worthChecking()     page 3's could-be causes across every work-on (§3.3, last paragraph)
//   workOnTimeline()    where each work-on sits on the swing (page 3)
//   photoRule()         whether swing photos may go in at all (§7.3)
//   explainOrder()      which ordering rule decided each place on today's list (§4.2)
//
// WORDS ARE NOT BUILT HERE. SessionReportModel owns every sentence template; these functions
// return ids, counts, swing indexes and enums. The two exceptions are functions whose whole job is
// a word choice — the range words and the magnitude words — and they reuse session_spread.h's
// number formatter so the report and the panel cannot print one edge two ways.
//
// INPUTS ARE THE MODEL'S OWN C++ OUTPUTS: SessionDiagnosticsModel::ledgerRows() (and shotRecords()
// for its size), sessionWorkOns(), and ReportPack — the pack's facts marshalled once by
// reportPackOf(), as work_ons.h's WorkOnClass/WorkOnCauses are, so no pack format reaches the rules.
// A HabitRun is one habit's per-swing readings against the corridor its chart draws
// (SessionDiagnosticsModel::spreadCorridorOf); nothing here goes through a QVariantMap.
//
// "JUDGED" is one definition everywhere in this file: assessable on at least kJudgedMinSwings
// swings, whatever the measure's status says (§3.3: the session's own evidence comes first). A
// SESSION-SCOPE ledger (MeasureScope::Session) has no per-swing verdict, so it is never a strength,
// never "came with it", and has no place on the swing timeline; as a CAUSE it is judged on its own
// counts like any other.
//
// HELD conditions (Condition::reportHold) are not trusted enough to headline, so they are not
// trusted to reassure either: never a strength, never "came with it", and as a cause never
// "ruled out" — they go under "could be behind it — your coach can look". The work-on list already
// leaves them off (SessionDiagnosticsModel::sessionWorkOns).

#include "work_ons.h"
#include "../Gui/diagnostics/session_spread.h"
#include "../Diagnostics/engine/characteristic.h"

#include <QDate>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis::report {

// ── Shared thresholds ───────────────────────────────────────────────────────────

// "Judged" (§3.3): three swings, the pattern gate's own floor (kDiagMinAssessableForPattern) — a
// condition the ledger could have called a habit on this evidence, it could also have cleared.
inline constexpr int    kJudgedMinSwings        = kDiagMinAssessableForPattern;
// Strengths (§3.2, as revised on Jess's 7 swings, §9.3): measurable on 80 % of the swings, and
// never fewer than three. Eight swings was a number tuned to Nick's ten.
inline constexpr double kStrengthMeasurableShare = 0.80;
inline constexpr int    kStrengthCap            = 3;
inline constexpr int    kStrengthOutcomes       = 1;    // one outcome + two movements
// Caps (§9.1 rule 4).
inline constexpr int    kCouldBeCap             = 6;
inline constexpr int    kRuledOutCap            = 4;
inline constexpr int    kWorthCheckingPerKind   = 5;
inline constexpr int    kStillWorkOns           = 3;    // stills on the first three work-ons only
// A pair of stills is shown only when the two swings differ by at least half the target range (§6).
inline constexpr double kPairMinRangeShare      = 0.5;
inline constexpr int    kAdultAge               = 18;   // §7.3

// ── The pack's facts ────────────────────────────────────────────────────────────

enum class FactKind { Fault, Setup, Delivery, Outcome, Capacity, Other };

struct ReportCondition {
    QString  id;
    FactKind kind = FactKind::Fault;
    int      prominence = int(Prominence::Occasional);   // 0 rare … 4 ubiquitous
    QString  axis;                // joins the two tails of one measure (whole-axis rule)
    bool     hasGolferWell = false;
    bool     screenable  = false; // latent, with a screen: "a quick physical test would tell"
    bool     liveMeasure = false; // some detecting signal reads a Live measure
    bool     held        = false; // Condition::reportHold
    bool     retired     = false; // Retired or Superseded
};

// An authored `causes` edge, `from` the cause `to` the effect; strength as int(Strength), 0 very
// weak … 4 very strong. Pack order is kept: it settles ties between edges of one strength.
struct ReportCause {
    QString from;
    QString to;
    int     strength = int(Strength::Moderate);
};

struct ReportPack {
    QHash<QString, ReportCondition> conditions;
    std::vector<ReportCause>        causes;

    const ReportCondition *condition(const QString &id) const
    {
        const auto it = conditions.constFind(id);
        return it == conditions.constEnd() ? nullptr : &it.value();
    }
};

// Marshal the pack once. Iterates the pack's vectors directly (no pack lookup members), so this
// header needs nothing linked beyond what it includes.
inline ReportPack reportPackOf(const CharacteristicPack &pack)
{
    QHash<QString, const Signal *>  sig;
    QHash<QString, const Measure *> meas;
    for (const Signal &s : pack.signalDefs) sig.insert(s.id, &s);
    for (const Measure &m : pack.measures)  meas.insert(m.id, &m);

    ReportPack out;
    for (const Condition &c : pack.conditions) {
        ReportCondition r;
        r.id = c.id;
        switch (c.kind) {
        case ConditionKind::Fault:    r.kind = FactKind::Fault;    break;
        case ConditionKind::Setup:    r.kind = FactKind::Setup;    break;
        case ConditionKind::Delivery: r.kind = FactKind::Delivery; break;
        case ConditionKind::Outcome:  r.kind = FactKind::Outcome;  break;
        case ConditionKind::Capacity: r.kind = FactKind::Capacity; break;
        default:                      r.kind = FactKind::Other;    break;
        }
        r.prominence    = int(c.prominence);
        r.axis          = c.axis;
        r.hasGolferWell = !c.golferWell.trimmed().isEmpty();
        r.screenable    = c.observability == Observability::Latent && !c.screenRef.isEmpty();
        r.held          = !c.reportHold.isEmpty();
        r.retired       = c.state == ConditionState::Retired || c.state == ConditionState::Superseded;
        for (const QString &sid : c.detectedBy) {
            const Signal *s = sig.value(sid, nullptr);
            if (!s) continue;
            for (const QString &mid : s->measures) {
                const Measure *m = meas.value(mid, nullptr);
                if (m && m->status == MeasureStatus::Live) r.liveMeasure = true;
            }
        }
        out.conditions.insert(r.id, r);
    }
    for (const Edge &e : pack.edges)
        if (e.type == EdgeType::Causes) out.causes.push_back({ e.from, e.to, int(e.strength) });
    return out;
}

// ── Small shared reads ──────────────────────────────────────────────────────────

// The work-on's 1-based rank in the session's list, 0 when it is not listed.
inline int workOnRank(const SessionWorkOns &w, const QString &id)
{
    const WorkOnEntry *e = w.entry(id);
    return e ? e->rank : 0;
}

inline int judgedOf(const ConditionLedger *l) { return l ? l->assessable : 0; }
inline bool isJudged(const ConditionLedger *l) { return judgedOf(l) >= kJudgedMinSwings; }

// ── One habit's readings ────────────────────────────────────────────────────────

// One habit's reading on every swing of the session, in swing order (NaN where it could not be
// judged), against the corridor its run chart draws (spreadCorridorOf: the norm for the session's
// commonest context, the stored band only when none resolves). A multi-club session is drawn
// against one corridor, and so is chosen against one here — the page and the choice agree.
struct HabitRun {
    QString             id;
    std::vector<double> values;
    SpreadCorridor      corridor;
};

// THE TARGET RANGE is the PASS band: inside it the condition does not fire (SpreadCorridor's signal
// edges). A side that does not grade (lowOpen / highOpen) is open: no reading there is a miss.
struct PassBand {
    bool   known    = false;
    double lo = 0.0, hi = 0.0;     // an open side collapses onto mu, as SpreadCorridor does
    bool   lowOpen  = false;
    bool   highOpen = false;
    double width() const { return hi - lo; }
};

inline PassBand passBandOf(const SpreadCorridor &c)
{
    PassBand p;
    if (!c.known) return p;
    p.lo = c.signalLo;
    p.hi = c.signalHi;
    p.lowOpen  = c.lowOpen;
    p.highOpen = c.highOpen;
    p.known = p.width() > 0.0 && std::isfinite(p.width());
    return p;
}

// HOW FAR OUTSIDE THE TARGET RANGE, IN RANGE WIDTHS (§6): 0 inside it or on a side that does not
// grade, the distance past the nearer pass edge divided by the band's width otherwise — so a degree
// of knee flex and a percent of hand rise add up fairly. NaN for no reading or no range.
inline double missInWidths(const PassBand &p, double v)
{
    if (!p.known || !std::isfinite(v)) return std::numeric_limits<double>::quiet_NaN();
    if (!p.lowOpen  && v < p.lo) return (p.lo - v) / p.width();
    if (!p.highOpen && v > p.hi) return (v - p.hi) / p.width();
    return 0.0;
}

// The TYPICAL swing: the one whose reading is nearest the session median, the earlier on a tie.
// -1 when no swing was judged. (§6: "the typical swing (median value)".)
inline int typicalSwing(const HabitRun &h)
{
    std::vector<double> v;
    for (double x : h.values) if (std::isfinite(x)) v.push_back(x);
    if (v.empty()) return -1;
    const double med = medianOf(v);
    int best = -1;
    double bestD = std::numeric_limits<double>::infinity();
    for (int i = 0; i < int(h.values.size()); ++i) {
        const double x = h.values[size_t(i)];
        if (!std::isfinite(x)) continue;
        const double d = std::fabs(x - med);
        if (d < bestD - 1e-12) { bestD = d; best = i; }
    }
    return best;
}

// ── Strengths (§3.2) ────────────────────────────────────────────────────────────

struct StrengthPick {
    QString id;
    bool    outcome      = false;   // what the ball did; else a movement (fault or setup)
    int     judged       = 0;       // swings it was judged on — and clean on every one
    int     effectOfRank = 0;       // the best-ranked work-on it is an authored effect of; 0 none
    bool    causeOfWorkOn = false;  // authored as a cause of some work-on (and clean: ruled out)
    int     prominence   = 0;
};

// "GOING WELL" (§3.2). A strength is a condition the session JUDGED and that NEVER SHOWED:
//   · judged on at least 80 % of the session's swings, and never fewer than three;
//   · fired on none of them; per-swing (a session-scope ledger has no swing to be clean on);
//   · a movement (Fault or Setup) or an outcome, with golferWell to say it in; not held, not retired;
//   · an OUTCOME only when its WHOLE AXIS was clean — no other condition on the same axis fired on
//     any swing. Nick, 20 Oct: "Nothing off the heel" was clean, but toe strike showed once, so
//     calling the heel a strength would have been true and misleading; "No slices" takes its place.
// Ranked by (1) being an authored EFFECT of a work-on — "it has not spread", the better-ranked
// work-on first — then an authored cause of one, then neither; (2) prominence, clean on a common
// fault counting for more; (3) movements before outcomes; (4) id. One outcome and two movements are
// chosen; when either kind runs short the other fills, to three. Returned outcome first, then the
// movements, each in rank order (the cover's "Going well" column).
inline std::vector<StrengthPick> pickStrengths(const std::vector<ConditionLedger> &ledgers, int swings,
                                               const SessionWorkOns &workOns, const ReportPack &pack)
{
    const int need = std::max(kJudgedMinSwings, int(std::ceil(kStrengthMeasurableShare * swings - 1e-9)));

    QSet<QString> firedAxis;                         // axes on which anything showed
    for (const ConditionLedger &l : ledgers) {
        if (l.fired < 1) continue;
        if (const ReportCondition *c = pack.condition(l.id))
            if (!c->axis.isEmpty()) firedAxis.insert(c->axis);
    }

    std::vector<StrengthPick> all;
    for (const ConditionLedger &l : ledgers) {
        const ReportCondition *c = pack.condition(l.id);
        if (!c || l.sessionScope || l.fired != 0 || l.assessable < need) continue;
        if (c->held || c->retired || !c->hasGolferWell) continue;
        const bool movement = c->kind == FactKind::Fault || c->kind == FactKind::Setup;
        const bool outcome  = c->kind == FactKind::Outcome;
        if (!movement && !outcome) continue;
        if (outcome && !c->axis.isEmpty() && firedAxis.contains(c->axis)) continue;   // whole-axis rule

        StrengthPick p;
        p.id = l.id;
        p.outcome = outcome;
        p.judged = l.assessable;
        p.prominence = c->prominence;
        for (const ReportCause &e : pack.causes) {
            if (e.to == l.id) {
                const int r = workOnRank(workOns, e.from);
                if (r > 0 && (p.effectOfRank == 0 || r < p.effectOfRank)) p.effectOfRank = r;
            }
            if (e.from == l.id && workOnRank(workOns, e.to) > 0) p.causeOfWorkOn = true;
        }
        all.push_back(p);
    }

    const auto tier = [](const StrengthPick &p) { return p.effectOfRank > 0 ? 0 : p.causeOfWorkOn ? 1 : 2; };
    std::stable_sort(all.begin(), all.end(), [&](const StrengthPick &a, const StrengthPick &b) {
        if (tier(a) != tier(b)) return tier(a) < tier(b);
        if (a.effectOfRank != b.effectOfRank) return a.effectOfRank < b.effectOfRank;
        if (a.prominence != b.prominence) return a.prominence > b.prominence;
        if (a.outcome != b.outcome) return !a.outcome;
        return a.id < b.id;
    });

    std::vector<StrengthPick> outcomes, movements;
    for (const StrengthPick &p : all) (p.outcome ? outcomes : movements).push_back(p);
    int nOut = std::min<int>(kStrengthOutcomes, int(outcomes.size()));
    int nMov = std::min<int>(kStrengthCap - nOut, int(movements.size()));
    nOut = std::min<int>(kStrengthCap - nMov, int(outcomes.size()));          // movements short: outcomes fill

    std::vector<StrengthPick> out(outcomes.begin(), outcomes.begin() + nOut);
    out.insert(out.end(), movements.begin(), movements.begin() + nMov);
    return out;
}

// ── The reference swing (§6) ────────────────────────────────────────────────────

struct ReferenceSwing {
    int         index    = -1;      // into the session's swings, 0-based; -1 = none can be named
    double      miss     = 0.0;     // summed, in range widths
    int         judgedOn = 0;       // how many of the habits it was judged on
    bool        longest  = false;   // the session's longest carry (only when carries were given)
    QStringList inside;             // the habits it was inside the target range on
};

// THE SWING NEAREST EVERY TARGET AT ONCE: the smallest summed miss across the work-ons, each miss in
// units of its own range's width (missInWidths). Only habits with a range count; a session-scope
// habit has no run and is not passed. A swing must be judged on as many of the habits as any swing
// was — otherwise a swing nobody could measure scores a perfect zero (the mock's prototype did
// this). Ties go to the longer carry (`carry`, metres or yards, NaN for none), then the earlier
// swing. The page says "your longest" only when `longest` is true.
inline ReferenceSwing referenceSwing(const std::vector<HabitRun> &habits, const std::vector<double> &carry = {})
{
    ReferenceSwing out;
    size_t n = 0;
    for (const HabitRun &h : habits) n = std::max(n, h.values.size());
    if (n == 0) return out;

    std::vector<double> miss(n, 0.0);
    std::vector<int>    judged(n, 0);
    int most = 0;
    for (const HabitRun &h : habits) {
        const PassBand p = passBandOf(h.corridor);
        if (!p.known) continue;
        for (size_t i = 0; i < h.values.size(); ++i) {
            const double m = missInWidths(p, h.values[i]);
            if (!std::isfinite(m)) continue;
            miss[i] += m;
            most = std::max(most, ++judged[i]);
        }
    }
    if (most == 0) return out;

    const auto carryOf = [&](size_t i) {
        return i < carry.size() && std::isfinite(carry[i]) ? carry[i] : -std::numeric_limits<double>::infinity();
    };
    int best = -1;
    for (size_t i = 0; i < n; ++i) {
        if (judged[i] != most) continue;
        if (best < 0) { best = int(i); continue; }
        const size_t b = size_t(best);
        if (miss[i] < miss[b] - 1e-12 || (std::fabs(miss[i] - miss[b]) <= 1e-12 && carryOf(i) > carryOf(b)))
            best = int(i);
    }
    out.index = best;
    out.miss = miss[size_t(best)];
    out.judgedOn = most;

    double longest = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < n; ++i) longest = std::max(longest, carryOf(i));
    out.longest = std::isfinite(longest) && carryOf(size_t(best)) >= longest;

    for (const HabitRun &h : habits) {
        const PassBand p = passBandOf(h.corridor);
        if (size_t(best) < h.values.size()) {
            const double m = missInWidths(p, h.values[size_t(best)]);
            if (std::isfinite(m) && m == 0.0) out.inside << h.id;
        }
    }
    return out;
}

// ── Which still (§6) ────────────────────────────────────────────────────────────

enum class StillKind { None, Change, Pair, Typical };

// The pack's `picture.kind`. Empty — content not authored yet — means "let the evidence choose":
// a pair when one would show a visible difference, the typical swing when not.
inline StillKind stillKindOf(const QString &kind)
{
    if (kind == QLatin1String("none"))    return StillKind::None;
    if (kind == QLatin1String("change"))  return StillKind::Change;
    if (kind == QLatin1String("typical")) return StillKind::Typical;
    return StillKind::Pair;
}

struct StillChoice {
    StillKind kind = StillKind::None;
    // change:  the typical swing at address (first) and at the page's position (second)
    // pair:    the swing closest to the target (first) and the furthest (second), both at the position
    // typical: one swing (first), at the position
    int     first  = -1;
    int     second = -1;
    QString firstPhase;
    QString secondPhase;
    bool    addressLine = false;   // change: the address line is drawn too, dashed (§6 "The line")
    bool    fellBack    = false;   // a pair was asked for (or left to the evidence) and the two
                                   // swings were too alike: "every swing looked much like this one"
};

// WHICH SWINGS, AT WHICH POSITION (§6), from the pack's picture and the measure the page draws:
//   · none — no photos (photoRule), not one of the first kStillWorkOns work-ons, kind "none", no
//     position, or no swing judged;
//   · change — for a reading taken from address: the typical swing at P1 and at the position, the
//     line drawn on both (a change "at P1" is just the typical swing);
//   · pair — the swings closest to and furthest from the target ON THIS MEASURE (missInWidths; the
//     model's bookends use the driving measure, which can differ — for early extension they did),
//     only when their readings differ by at least half the range's width; otherwise typical;
//   · typical — the swing nearest the median.
inline StillChoice chooseStill(const HabitRun &h, const QString &pictureKind, const QString &phase,
                               int rank, bool photos)
{
    StillChoice out;
    const StillKind asked = stillKindOf(pictureKind);
    if (!photos || rank < 1 || rank > kStillWorkOns || asked == StillKind::None || phase.isEmpty())
        return out;
    const int typ = typicalSwing(h);
    if (typ < 0) return out;

    auto typical = [&](bool fellBack) {
        out.kind = StillKind::Typical;
        out.first = typ;
        out.firstPhase = phase;
        out.fellBack = fellBack;
        return out;
    };

    if (asked == StillKind::Change) {
        if (phase.compare(QLatin1String("P1"), Qt::CaseInsensitive) == 0) return typical(false);
        out.kind = StillKind::Change;
        out.first = out.second = typ;
        out.firstPhase  = QStringLiteral("P1");
        out.secondPhase = phase;
        out.addressLine = true;
        return out;
    }
    if (asked == StillKind::Typical) return typical(false);

    const PassBand p = passBandOf(h.corridor);
    if (!p.known) return typical(true);
    int closest = -1, furthest = -1;
    double cm = std::numeric_limits<double>::infinity(), fm = -1.0;
    for (int i = 0; i < int(h.values.size()); ++i) {
        const double m = missInWidths(p, h.values[size_t(i)]);
        if (!std::isfinite(m)) continue;
        if (m < cm - 1e-12) { cm = m; closest = i; }
        if (m > fm + 1e-12) { fm = m; furthest = i; }
    }
    if (closest < 0 || furthest < 0 || closest == furthest
        || std::fabs(h.values[size_t(furthest)] - h.values[size_t(closest)]) < kPairMinRangeShare * p.width())
        return typical(true);
    out.kind = StillKind::Pair;
    out.first  = closest;
    out.second = furthest;
    out.firstPhase = out.secondPhase = phase;
    return out;
}

// ── Range words and magnitude words (§10 rule 5) ────────────────────────────────

// A number in the golfer's sign (Measure::golferSign: the value shown is raw × sign), with its
// unit, on session_spread.h's formatter: the true minus, "-" for none, no space before a degree.
inline QString golferValueWords(double raw, const QString &unit, int decimals, int golferSign = 1)
{
    const double v = std::isfinite(raw) ? raw * (golferSign < 0 ? -1.0 : 1.0) : raw;
    return spreadWithUnit(spreadNumber(v, decimals, /*trimZeros*/ true), unit);
}

// THE TARGET RANGE IN WORDS, in the golfer's sign: "12° or less", "1.3 or more", "5 to 25 mm",
// "−5° to 10°". The pass band's edges (where the habit starts to show), the open side never named.
// With golferSign −1 the range is mirrored — a floor on the raw reading is a ceiling in the words'
// direction ("trail-knee bend lost by the top: 12° or less"). Decimals off the corridor's own span
// with trailing zeros dropped, as spreadCorridorWords quotes it. Empty when there is no range.
inline QString rangeWords(const SpreadCorridor &c, const QString &unit, int golferSign = 1)
{
    const PassBand p = passBandOf(c);
    if (!p.known) return QString();
    const bool flip = golferSign < 0;
    const double lo = flip ? -p.hi : p.lo, hi = flip ? -p.lo : p.hi;
    const bool lowOpen = flip ? p.highOpen : p.lowOpen, highOpen = flip ? p.lowOpen : p.highOpen;
    const int d = spreadDecimals(std::max(c.faultHi - c.faultLo, p.width()));
    auto n = [&](double v) { return spreadNumber(v, d, /*trimZeros*/ true); };
    if (lowOpen && highOpen) return QString();
    if (lowOpen)  return spreadWithUnit(n(hi), unit) + QStringLiteral(" or less");
    if (highOpen) return spreadWithUnit(n(lo), unit) + QStringLiteral(" or more");
    if (unit.startsWith(QChar(0x00B0)))       // degrees ride on both numbers: "−5° to 10°"
        return spreadWithUnit(n(lo), unit) + QStringLiteral(" to ") + spreadWithUnit(n(hi), unit);
    return spreadWithUnit(n(lo) + QStringLiteral(" to ") + n(hi), unit);
}

enum class MissSize {
    Unknown,   // no range, or no swing judged — say nothing about size
    Inside,    // no judged swing outside the range on this measure
    Small,     // outside the range, and no swing past the fault line
    Some,      // some swings past the fault line, fewer than half of those outside
    Clear,     // at least half of the swings outside the range went past the fault line
};

struct MissMagnitude {
    MissSize size = MissSize::Unknown;
    int judged    = 0;    // swings with a reading
    int outside   = 0;    // ...outside the target range
    int pastFault = 0;    // ...past the fault line (where Action begins)
};

// HOW FAR OUT, FROM THE CORRIDOR'S OWN GRADES (§10 rule 5): the fault line is the authored line
// where the grade becomes Action, so "never past it" is a fact about the session, not an adjective.
// Nick's reverse spine (9 of 10, median −1° against 0° to 24°) never passed it: a small miss.
inline MissMagnitude missMagnitude(const HabitRun &h)
{
    MissMagnitude m;
    const SpreadCorridor &c = h.corridor;
    const PassBand p = passBandOf(c);
    if (!p.known) return m;
    for (double v : h.values) {
        if (!std::isfinite(v)) continue;
        ++m.judged;
        if (missInWidths(p, v) > 0.0) ++m.outside;
        if ((!c.highOpen && v > c.faultHi) || (!c.lowOpen && v < c.faultLo)) ++m.pastFault;
    }
    if (m.judged == 0)            m.size = MissSize::Unknown;
    else if (m.outside == 0)      m.size = MissSize::Inside;
    else if (m.pastFault == 0)    m.size = MissSize::Small;
    else if (2 * m.pastFault < m.outside) m.size = MissSize::Some;
    else                          m.size = MissSize::Clear;
    return m;
}

// The words, lower case, to follow "this is" / a semicolon. Counts are swings out of the swings
// with a reading ("on 3 of 10 swings", "on all 10 swings"), never a percentage. Empty for Unknown.
inline QString missWords(const MissMagnitude &m)
{
    auto on = [&](int k) {
        return k == m.judged ? QStringLiteral("on all %1 swings").arg(m.judged)
                             : QStringLiteral("on %1 of %2 swings").arg(k).arg(m.judged);
    };
    switch (m.size) {
    case MissSize::Inside: return QStringLiteral("inside the range on every swing we could measure");
    case MissSize::Small:  return QStringLiteral("a small miss: just outside the range, never far");
    case MissSize::Some:   return QStringLiteral("mostly a small miss, but well past the range ") + on(m.pastFault);
    case MissSize::Clear:  return QStringLiteral("a clear miss: well past the range ") + on(m.pastFault);
    case MissSize::Unknown: break;
    }
    return QString();
}

// ── What came with it (§3, the work-on page) ────────────────────────────────────

enum class CameWithState {
    Showed,      // on at least half the swings it was judged on, and at least two
    Clean,       // on none of them — said as its golferWell ("on every swing your hips turned in place")
    Sometimes,   // in between: now and then, not "came with it"
};

struct CameWithItem {
    QString       id;
    CameWithState state = CameWithState::Clean;
    int           fired = 0, judged = 0;
    int           strength = int(Strength::Moderate);
    bool          hasGolferWell = false;
};

// THE HABIT'S AUTHORED EFFECTS THAT THE SESSION JUDGED. Co-occurrence, never cause: the page says a
// Showed effect "fits the coaching model, not proof", and a Clean one is the "it has not spread"
// evidence. Excluded: effects that are themselves work-ons (that link is on page 3 and under the
// effect's own "what we spotted"), held, retired or unknown conditions, session-scope ledgers, and
// anything judged on fewer than kJudgedMinSwings swings. Showed first, then Clean, then Sometimes;
// within each the strongest edge first, then pack order. (The mock counted "half" as ⌊n/2⌋; this is
// at least half, which differs only on an odd count.)
inline std::vector<CameWithItem> cameWith(const QString &id, const std::vector<ConditionLedger> &ledgers,
                                          const SessionWorkOns &workOns, const ReportPack &pack)
{
    std::vector<CameWithItem> out;
    QSet<QString> seen;
    for (const ReportCause &e : pack.causes) {
        if (e.from != id || e.to == id || seen.contains(e.to)) continue;
        seen.insert(e.to);
        const ReportCondition *c = pack.condition(e.to);
        const ConditionLedger *l = ledgerFor(ledgers, e.to);
        if (!c || c->held || c->retired || !l || l->sessionScope || !isJudged(l)) continue;
        if (workOnRank(workOns, e.to) > 0) continue;
        CameWithItem it;
        it.id = e.to;
        it.fired = l->fired;
        it.judged = l->assessable;
        it.strength = e.strength;
        it.hasGolferWell = c->hasGolferWell;
        it.state = l->fired == 0                                  ? CameWithState::Clean
                 : (l->fired >= 2 && 2 * l->fired >= l->assessable) ? CameWithState::Showed
                                                                    : CameWithState::Sometimes;
        out.push_back(it);
    }
    std::stable_sort(out.begin(), out.end(), [](const CameWithItem &a, const CameWithItem &b) {
        if (a.state != b.state) return int(a.state) < int(b.state);
        return a.strength > b.strength;
    });
    return out;
}

// ── What might be behind it (§3.3) ──────────────────────────────────────────────

enum class CauseGroup { Spotted, CouldBe, RuledOut };

// How a could-be cause could be checked — the subgroups the page prints, in golfer words there:
enum class CouldBeCheck {
    None,           // (not a could-be cause)
    PhysicalTest,   // "A quick physical test would tell": latent, with a screen
    CoachCanLook,   // "Your coach can look": no live measure (planned, no producer) — or held
    HiddenToday,    // "Hidden on today's swings": measurable, judged on fewer than three swings
};

struct CauseItem {
    QString      id;
    CauseGroup   group = CauseGroup::CouldBe;
    CouldBeCheck check = CouldBeCheck::None;
    int          strength   = int(Strength::Moderate);
    int          fired = 0, judged = 0;
    int          workOnRank = 0;        // spotted: its own work-on number, 0 if not listed
    bool         supported  = false;    // spotted: linkSupported(cause, habit) on these swings
    bool         everySwing = false;    // spotted: fired on every swing it was judged on
};

enum class CauseLead {
    NoneAuthored,   // "We don't know of a cause for this one yet."
    Spotted,        // "One possible cause showed up on the same swings…" (supported: "the swings point to it")
    Unchecked,      // "We couldn't pin down what drives this one today…"
    AllRuledOut,    // "We checked every cause we know of for this one, and each was fine…"
};

struct CauseGroups {
    CauseLead lead = CauseLead::NoneAuthored;
    bool spottedSupported  = false;    // some spotted cause's link is supported here
    bool spottedEverySwing = false;    // every spotted cause fired on every swing it was judged on —
                                       // why the lead says "we can't prove one drives the other"
    std::vector<CauseItem> spotted;    // all of them
    std::vector<CauseItem> couldBe;    // strongest first, kCouldBeCap at most
    int couldBeMore = 0;               // "N more, less likely"
    std::vector<CouldBeCheck> couldBeOrder;   // subgroups as printed: the likeliest cause's first
    std::vector<CauseItem> ruledOut;   // strongest first, kRuledOutCap at most
    int ruledOutMore = 0;              // "N more"
    std::vector<CauseItem> all;        // every authored cause, strongest first, uncapped
};

// EVERY AUTHORED CAUSE, SORTED BY WHAT THIS SESSION KNOWS ABOUT IT (§3.3; honesty rule 4: inferred
// causes appear only as questions, and every one the session could not check is NAMED with how it
// could be). In this order of precedence:
//   1. latent with a screen                → could be: a quick physical test would tell
//   2. held (reportHold)                   → could be: your coach can look — an untrusted reading
//                                            can neither spot nor rule out
//   3. a Pattern this session              → spotted (with its work-on number when listed)
//   4. judged on ≥ 3 swings, not a habit   → ruled out, "on these swings, with these cameras" —
//                                            whatever its measure's status says
//   5. judged on fewer, measurable today   → could be: hidden on today's swings
//   6. judged on fewer, no live measure    → could be: your coach can look
// Strongest authored edge first, pack order on a tie. Caps: spotted all, could-be 6, ruled-out 4.
inline CauseGroups causeGroups(const QString &id, const std::vector<ConditionLedger> &ledgers,
                               const SessionWorkOns &workOns, const ReportPack &pack)
{
    CauseGroups g;
    std::vector<ReportCause> edges;
    QSet<QString> seen;
    for (const ReportCause &e : pack.causes)
        if (e.to == id && e.from != id && !seen.contains(e.from)) { seen.insert(e.from); edges.push_back(e); }
    std::stable_sort(edges.begin(), edges.end(),
                     [](const ReportCause &a, const ReportCause &b) { return a.strength > b.strength; });

    const ConditionLedger *effect = ledgerFor(ledgers, id);
    for (const ReportCause &e : edges) {
        const ReportCondition *c = pack.condition(e.from);
        if (c && c->retired) continue;                     // not a claim the pack still makes
        const ConditionLedger *l = ledgerFor(ledgers, e.from);
        CauseItem it;
        it.id = e.from;
        it.strength = e.strength;
        it.fired  = l ? l->fired : 0;
        it.judged = judgedOf(l);
        if (c && c->screenable) {
            it.check = CouldBeCheck::PhysicalTest;
        } else if (c && c->held) {
            it.check = CouldBeCheck::CoachCanLook;
        } else if (l && l->tier == Tier::Pattern) {
            it.group = CauseGroup::Spotted;
            it.workOnRank = workOnRank(workOns, e.from);
            it.supported  = effect && linkSupported(*l, *effect);
            it.everySwing = l->rangeRestricted;
        } else if (isJudged(l)) {
            it.group = CauseGroup::RuledOut;
        } else {
            it.check = (c && c->liveMeasure) ? CouldBeCheck::HiddenToday : CouldBeCheck::CoachCanLook;
        }
        g.all.push_back(it);
    }

    int couldBe = 0, ruled = 0;
    for (const CauseItem &it : g.all) {
        switch (it.group) {
        case CauseGroup::Spotted:
            g.spotted.push_back(it);
            break;
        case CauseGroup::CouldBe:
            if (++couldBe <= kCouldBeCap) {
                g.couldBe.push_back(it);
                if (std::find(g.couldBeOrder.begin(), g.couldBeOrder.end(), it.check) == g.couldBeOrder.end())
                    g.couldBeOrder.push_back(it.check);
            }
            break;
        case CauseGroup::RuledOut:
            if (++ruled <= kRuledOutCap) g.ruledOut.push_back(it);
            break;
        }
    }
    g.couldBeMore  = std::max(0, couldBe - kCouldBeCap);
    g.ruledOutMore = std::max(0, ruled - kRuledOutCap);

    g.spottedEverySwing = !g.spotted.empty();
    for (const CauseItem &it : g.spotted) {
        g.spottedSupported  = g.spottedSupported || it.supported;
        g.spottedEverySwing = g.spottedEverySwing && it.everySwing;
    }
    g.lead = g.all.empty()      ? CauseLead::NoneAuthored
           : !g.spotted.empty() ? CauseLead::Spotted
           : couldBe > 0        ? CauseLead::Unchecked
                                : CauseLead::AllRuledOut;
    return g;
}

// ── Worth checking next time (page 3) ───────────────────────────────────────────

struct WorthCheck {
    QString      id;
    CouldBeCheck check = CouldBeCheck::None;
    QVector<int> ranks;                    // the work-ons it could sit behind, ascending
    int          strength = 0;             // its strongest edge into any of them
};

struct WorthCheckingGroup {
    CouldBeCheck            check = CouldBeCheck::None;
    std::vector<WorthCheck> items;         // kWorthCheckingPerKind at most
    int                     more = 0;      // the rest are on the work-on pages
};

// THE COULD-BE CAUSES ACROSS EVERY WORK-ON, those behind the most work-ons first (§3.3: on Nick's
// session five physical tests each sit behind two work-ons — "the most useful sentence the report
// can give a coach about the next session"). Every could-be cause counts, not only the six a page
// prints. Grouped by how to check — physical test, coach can look, hidden today, in that order,
// empty groups absent — and within a group: more work-ons first, then the strongest edge, then id.
// Five per group. A cause takes its check from the first work-on that lists it (they agree: the
// check is a fact about the cause, never about the habit).
inline std::vector<WorthCheckingGroup> worthChecking(const SessionWorkOns &workOns,
                                                     const std::vector<ConditionLedger> &ledgers,
                                                     const ReportPack &pack)
{
    QHash<QString, WorthCheck> by;
    QStringList order;
    for (const WorkOnEntry &w : workOns.entries) {
        const CauseGroups g = causeGroups(w.id, ledgers, workOns, pack);
        for (const CauseItem &it : g.all) {
            if (it.group != CauseGroup::CouldBe) continue;
            if (!by.contains(it.id)) { by.insert(it.id, WorthCheck{ it.id, it.check, {}, 0 }); order << it.id; }
            WorthCheck &c = by[it.id];
            if (!c.ranks.contains(w.rank)) c.ranks.append(w.rank);
            c.strength = std::max(c.strength, it.strength);
        }
    }
    std::vector<WorthCheckingGroup> out;
    for (CouldBeCheck k : { CouldBeCheck::PhysicalTest, CouldBeCheck::CoachCanLook, CouldBeCheck::HiddenToday }) {
        std::vector<WorthCheck> items;
        for (const QString &id : order) {
            WorthCheck c = by.value(id);
            if (c.check != k) continue;
            std::sort(c.ranks.begin(), c.ranks.end());
            items.push_back(c);
        }
        if (items.empty()) continue;
        std::stable_sort(items.begin(), items.end(), [](const WorthCheck &a, const WorthCheck &b) {
            if (a.ranks.size() != b.ranks.size()) return a.ranks.size() > b.ranks.size();
            if (a.strength != b.strength) return a.strength > b.strength;
            return a.id < b.id;
        });
        WorthCheckingGroup grp;
        grp.check = k;
        grp.more = std::max(0, int(items.size()) - kWorthCheckingPerKind);
        if (int(items.size()) > kWorthCheckingPerKind) items.resize(size_t(kWorthCheckingPerKind));
        grp.items = std::move(items);
        out.push_back(std::move(grp));
    }
    return out;
}

// ── The swing timeline (page 3) ─────────────────────────────────────────────────

// The timeline's six stops: 0 address · 1 backswing · 2 top · 3 coming down · 4 impact · 5 finish.
// A position between two anchors sits between their stops (transition, P4.5, is 2.5).
inline double timelineStop(double swingPos)
{
    static const double px[] = { 1, 2, 3, 4, 5, 6, 7, 8, 10 };
    static const double sx[] = { 0, 1, 1, 2, 3, 3, 4, 5, 5 };
    if (!(swingPos > px[0])) return 0.0;
    for (int i = 1; i < 9; ++i)
        if (swingPos <= px[i])
            return sx[i - 1] + (sx[i] - sx[i - 1]) * (swingPos - px[i - 1]) / (px[i] - px[i - 1]);
    return 5.0;
}

struct TimelineMark {
    int    rank = 0;
    double swingPos = 0.0;
    double stop = 0.0;      // timelineStop(swingPos)
    int    lane = 0;        // 0 for the first mark at a stop, 1 for the next there, …
};

struct Timeline {
    std::vector<TimelineMark> marks;          // in rank order
    int  sessionLevel    = 0;                 // work-ons read over the session: no place on it
    bool firstIsEarliest = false;             // work-on 1 is (tied for) the earliest in the swing
};

// WHERE EACH WORK-ON HAPPENS (page 3), off the position rule v4 ordered by (WorkOnReasons::swingPos),
// so the picture and the order cannot disagree. A session-level habit has no place on a swing and
// is counted instead. Marks at one stop take lanes in rank order.
inline Timeline workOnTimeline(const SessionWorkOns &workOns)
{
    Timeline t;
    double earliest = std::numeric_limits<double>::infinity();
    for (const WorkOnEntry &e : workOns.entries) {
        if (e.reasons.swingPos >= kWorkOnSwingPosSession) { ++t.sessionLevel; continue; }
        TimelineMark m;
        m.rank = e.rank;
        m.swingPos = e.reasons.swingPos;
        m.stop = timelineStop(m.swingPos);
        for (const TimelineMark &o : t.marks) if (std::fabs(o.stop - m.stop) < 0.25) ++m.lane;
        earliest = std::min(earliest, m.swingPos);
        t.marks.push_back(m);
    }
    for (const TimelineMark &m : t.marks)
        if (m.rank == 1) t.firstIsEarliest = m.swingPos <= earliest + 1e-9;
    return t;
}

// ── The photo rule (§7.3) ───────────────────────────────────────────────────────

enum class PhotoReason {
    Adult,               // 18 or over on the session date
    JuniorApproved,      // under 18, a parent or guardian's approval recorded
    JuniorNotApproved,   // under 18, no approval: no still and no reference swing anywhere
    NoDateOfBirth,       // treated as an adult; the report row says so, so the coach can add one
};

struct PhotoDecision {
    bool        included = true;
    PhotoReason reason   = PhotoReason::NoDateOfBirth;
};

// Under 18 ON THE GIVEN DATE — the session's, so an old session's report does not change when the
// golfer turns 18. A 29 February birthday turns over on 28 February (QDate::addYears).
inline bool isJuniorOn(const QDate &dob, const QDate &on)
{
    return dob.isValid() && on.isValid() && dob.addYears(kAdultAge) > on;
}

// ONE RULE, NO PER-REPORT OVERRIDE: adult → yes; junior with approval → yes; junior without → no;
// no date of birth → yes, with the note. A session with no date (no swing carried a clock) cannot be
// aged against, so it is treated as the no-date-of-birth case — included, with the same note.
inline PhotoDecision photoRule(const QDate &dob, const QDate &sessionDate, bool approved)
{
    if (!dob.isValid() || !sessionDate.isValid()) return { true, PhotoReason::NoDateOfBirth };
    if (!isJuniorOn(dob, sessionDate))             return { true, PhotoReason::Adult };
    return approved ? PhotoDecision{ true, PhotoReason::JuniorApproved }
                    : PhotoDecision{ false, PhotoReason::JuniorNotApproved };
}

// ── Why this order (§4.2) ───────────────────────────────────────────────────────

// The rule that put an entry AFTER the one before it — the first of reduceSessionWorkOns' keys on
// which the two differ.
enum class OrderRule {
    First,            // work-on 1: nothing before it
    StillHappening,   // rule 1: the one before is still happening, this one stopped (resolving)
    CauseFirst,       // rule 2: this one waits behind a supported cause
    SwingOrder,       // rule v4: no link testable, and this one is later in the swing
    BiggestMiss,      // rule 3: same place in the swing (or links testable) — the smaller miss
    MovementsFirst,   // rule 4: a delivery number filling a short list, behind the movements
    Tie,              // nothing separated them but the id
};

// Why rule 2 could not run, for its "Today" line ("It could not be tested today…").
enum class LinksUntestable {
    Testable,            // some authored link could be tested (linksTestable)
    NoLinks,             // no authored cause → effect pair among today's habits
    CauseOnEverySwing,   // every authored cause among them showed on every swing it was judged on
    TooFewSwings,        // some cause varied, but too few swings had both judged to compare
};

struct OrderStep {
    QString   id;
    int       rank = 0;
    OrderRule decidedBy = OrderRule::First;
};

struct OrderExplanation {
    bool            linksTestable = false;
    LinksUntestable whyNot = LinksUntestable::NoLinks;
    std::vector<OrderStep> steps;             // one per entry, in rank order
    int resolving    = 0;                     // entries rule 1 moved back
    int behindCause  = 0;                     // entries waiting behind a supported cause
    int namedLinks   = 0;                     // entries with an authored, unsupported cause (mayFollow)
    int bySwingOrder = 0;                     // places decided by swing order
    int byMagnitude  = 0;                     // places decided by the size of the miss
};

// WHICH RULE DECIDED EACH PLACE TODAY, from the record's reasons and linksTestable alone — so page 3
// explains the order that was computed and cannot drift from it. `ledgers` and `pack` are needed
// only to say WHY no link could be tested: the same pairs reduceSessionWorkOns examined — an
// authored edge with both ends a Pattern here and the effect listable (a fault, setup or delivery,
// not held).
inline OrderExplanation explainOrder(const SessionWorkOns &w, const std::vector<ConditionLedger> &ledgers,
                                     const ReportPack &pack)
{
    OrderExplanation x;
    x.linksTestable = w.linksTestable;
    for (size_t i = 0; i < w.entries.size(); ++i) {
        const WorkOnEntry &e = w.entries[i];
        OrderStep s{ e.id, e.rank, OrderRule::First };
        if (i > 0) {
            const WorkOnEntry &p = w.entries[i - 1];
            if (p.cls != e.cls)                             s.decidedBy = OrderRule::MovementsFirst;
            else if (p.reasons.resolving != e.reasons.resolving) s.decidedBy = OrderRule::StillHappening;
            else if (p.root != e.root)                      s.decidedBy = OrderRule::CauseFirst;
            else if (!w.linksTestable && p.reasons.swingPos != e.reasons.swingPos)
                                                            s.decidedBy = OrderRule::SwingOrder;
            else if (p.excess != e.excess || p.wilsonLower != e.wilsonLower)
                                                            s.decidedBy = OrderRule::BiggestMiss;
            else                                            s.decidedBy = OrderRule::Tie;
        }
        if (s.decidedBy == OrderRule::SwingOrder)  ++x.bySwingOrder;
        if (s.decidedBy == OrderRule::BiggestMiss) ++x.byMagnitude;
        if (e.reasons.resolving)          ++x.resolving;
        if (!e.reasons.behind.isEmpty())  ++x.behindCause;
        if (!e.reasons.mayFollow.isEmpty()) ++x.namedLinks;
        x.steps.push_back(s);
    }

    if (w.linksTestable) { x.whyNot = LinksUntestable::Testable; return x; }
    bool anyPair = false, anyVaried = false;
    for (const ReportCause &e : pack.causes) {
        const ConditionLedger *from = ledgerFor(ledgers, e.from), *to = ledgerFor(ledgers, e.to);
        const ReportCondition *tc = pack.condition(e.to);
        if (!from || !to || e.from == e.to || from->tier != Tier::Pattern || to->tier != Tier::Pattern) continue;
        if (!tc || tc->held || !(tc->kind == FactKind::Fault || tc->kind == FactKind::Setup || tc->kind == FactKind::Delivery))
            continue;
        anyPair = true;
        if (!from->rangeRestricted) anyVaried = true;
    }
    x.whyNot = !anyPair ? LinksUntestable::NoLinks
             : anyVaried ? LinksUntestable::TooFewSwings
                         : LinksUntestable::CauseOnEverySwing;
    return x;
}

} // namespace pinpoint::analysis::report
