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

// WORK-ONS — pure, header-only, no Qt-GUI. Two reductions on top of diagnostic_ledger.h
// (docs/design/work_ons_design.md):
//
//   reduceSessionWorkOns()  one session's ledger -> the 3–5 faults that session says to work
//                           on, plus what the session could and could not say about every
//                           other condition. Written beside the ledger as work_ons.json.
//   aggregateWorkOns()      every session's record for one golfer -> the WORK ONS list on the
//                           home screen: which faults keep coming back, which are easing, and
//                           which have gone.
//
// THE SESSION LEDGER answers "what keeps happening in THIS session". This answers "what keeps
// happening to THIS GOLFER", and it is built so the second question cannot outrun the first:
//
//   1. A work-on is a session PATTERN and nothing less. Selection only ever chooses AMONG the
//      conditions the ledger's own gate passed (conditionTier == Pattern); it cannot promote a
//      Watching condition, and a three-shot session yields no work-ons at all.
//   2. A fault is something a golfer can WORK ON. Ball-flight outcomes are where an explanation
//      ends, capacities are screened and equipment is bought — none is a movement to practise —
//      so only Fault and Setup conditions are listed. Delivery numbers fill the list only when a
//      session measured too few movements to reach the floor.
//   3. ABSENCE IS NOT IMPROVEMENT. A later session clears a work-on only by being QUIET on it —
//      enough assessable shots that the firing rate is evidently under half — never by failing
//      to measure it, and never by being too short to reach Pattern. The ledger's rule 1 (not
//      assessable is neither clean nor a zero) one level up.
//   4. EVERY NUMBER IS INJECTED (WorkOnOptions), and what is persisted per session is stamped
//      with kWorkOnRuleVersion so a change to the rule re-derives every session rather than
//      aggregating two rules' answers.
//
// The aggregate is a PURE FUNCTION OF THE SESSION RECORDS. Nothing accumulates: deleting a
// session, re-analysing one or changing the rule changes the list by re-running it, which is
// what the incremental fault profile could not do.
//
// Unit-tested standalone in src/Analysis/tests/work_ons_test.cpp.

#include "diagnostic_ledger.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

// work_ons.json's shape, and the selection rule that filled it. Two numbers because they
// change for different reasons: a new field is a new schema, a new ordering is a new rule.
// 2: every entry carries `reasons` (which rule placed it) and the record says `linksTestable`.
inline constexpr int kWorkOnSchemaVersion = 2;
// 2: a symptom goes behind its cause only when the session's swings SUPPORT the link (see
// linkSupported); an authored link they cannot test is named (mayFollow) but does not demote.
// 3: the golfer's list puts SHARP work-ons first (WorkOnSummary::sharp), as every session's does.
// 4: when the session can test none of its authored links (linksTestable false), SWING ORDER
// decides before magnitude — setup, backswing, top, transition, downswing, impact, follow-through
// (docs/design/session_report_design.md §4.3, option A, Mark 20 Oct 2026). See WorkOnReasons.
inline constexpr int kWorkOnRuleVersion   = 4;

// "The top 3–5". Five is a cap; three is how far Delivery conditions may fill a session that
// measured fewer movement faults than that. Neither is a quota — a session with two patterns
// records two.
inline constexpr int kWorkOnMaxPerSession = 5;
inline constexpr int kWorkOnMinPerSession = 3;

// QUIET: the Wilson UPPER bound on the firing rate is under this — the session is evidence the
// fault fires on fewer than half the shots. The mirror of the pattern gate, and as ordinal: 0 of
// 4 clears it, 1 of 8 clears it, 0 of 3 does not (three shots cannot say "gone").
inline constexpr double kWorkOnQuietWilsonUb = 0.50;

// THE GOLFER'S RANKING is the ledger's own question asked across sessions: how often does the
// fault show on the swings where it COULD BE JUDGED, at the cautious end of what the evidence
// supports — the Wilson lower bound on (fired, assessable), swings and not sessions, each
// session's swings counting this much less per substantive session since.
//
// Two things fall out of that one number. A swing the capture could not judge is in neither
// count, so a fault only a second camera can see is not marked down for the sessions without
// one — it may have been there on every swing. And age shrinks the EVIDENCE, not the rate: 15
// of 15 four sessions ago is about 6 of 6 today, whose bound is lower, and lower again after
// every further session that could not look — it may equally have gone.
inline constexpr double kWorkOnRecencyDecay = 0.75;
// Quiet in this many consecutive conclusive sessions and the work-on is cleared.
inline constexpr int kWorkOnClearAfterQuiet = 2;

struct WorkOnOptions {
    int    maxPerSession  = kWorkOnMaxPerSession;
    int    minPerSession  = kWorkOnMinPerSession;
    double quietWilsonUb  = kWorkOnQuietWilsonUb;
    double recencyDecay   = kWorkOnRecencyDecay;
    int    clearAfterQuiet= kWorkOnClearAfterQuiet;
    int    minAssessable  = kDiagMinAssessableForPattern;
    double wilsonZ        = kDiagWilsonZ;
};

// Wilson score interval, UPPER bound. wilsonLowerBound()'s sibling, same conventions; no
// assessable shots is no evidence of quiet, so n == 0 returns 1.
inline double wilsonUpperBound(double fired, double assessable, double z = kDiagWilsonZ)
{
    if (!(assessable > 0.0) || !std::isfinite(fired) || !std::isfinite(assessable))
        return 1.0;
    const double n  = assessable;
    const double p  = std::clamp(fired / n, 0.0, 1.0);
    const double z2 = z * z;
    const double centre = p + z2 / (2.0 * n);
    const double inner  = p * (1.0 - p) / n + z2 / (4.0 * n * n);
    const double hi = (centre + z * std::sqrt(std::max(inner, 0.0))) / (1.0 + z2 / n);
    return std::isfinite(hi) ? std::min(1.0, hi) : 1.0;
}

// What the pack says a condition IS, as far as this file needs to know — marshalled by the
// caller from ConditionKind so no pack format reaches the arithmetic.
enum class WorkOnClass {
    Movement,   // Fault, Setup: what a lesson is about
    Delivery,   // impact geometry: listed only to fill a short list
    Excluded,   // Outcome, Capacity, Intent, Equipment: never a work-on
};

// What ONE session can say about ONE condition. Ordered: a later value is stronger evidence.
enum class SessionEvidence {
    NotMeasured,    // no assessable shot
    Inconclusive,   // looked, and neither gate was reached
    Quiet,          // evidently fires on under half the shots
    Pattern,        // the ledger's own Pattern tier
};

inline QString sessionEvidenceToString(SessionEvidence e)
{
    switch (e) {
    case SessionEvidence::Pattern:      return QStringLiteral("pattern");
    case SessionEvidence::Quiet:        return QStringLiteral("quiet");
    case SessionEvidence::Inconclusive: return QStringLiteral("inconclusive");
    case SessionEvidence::NotMeasured:  break;
    }
    return QStringLiteral("notMeasured");
}

inline SessionEvidence sessionEvidenceFromString(const QString &s)
{
    if (s == QStringLiteral("pattern"))      return SessionEvidence::Pattern;
    if (s == QStringLiteral("quiet"))        return SessionEvidence::Quiet;
    if (s == QStringLiteral("inconclusive")) return SessionEvidence::Inconclusive;
    return SessionEvidence::NotMeasured;
}

// The gate, isolated like conditionTier() so its boundaries are testable on their own.
inline SessionEvidence sessionEvidence(const ConditionLedger &l, const WorkOnOptions &opt = WorkOnOptions())
{
    if (l.assessable < 1) return SessionEvidence::NotMeasured;
    if (l.tier == Tier::Pattern) return SessionEvidence::Pattern;
    // gate rule: PP_GATE("rule.diag.workOnQuiet")
    if (l.assessable >= opt.minAssessable
        && wilsonUpperBound(l.effFired, l.effAssessable, opt.wilsonZ) < opt.quietWilsonUb)
        return SessionEvidence::Quiet;
    return SessionEvidence::Inconclusive;
}

// ── Where in the swing (rule v4) ────────────────────────────────────────────────
//
// THE SWING'S ORDER IS A FACT, NOT AN INFERENCE. When a session cannot test any of its authored
// links (every cause fired on every swing, or too few swings to pair), rule 2 has nothing to say,
// and v3 fell straight through to "the biggest miss first" — which put Jess's follow-through habit
// (chicken wing, P8) at the head of her list on 20 Oct, ahead of the stance she set up with. v4
// orders such a session by WHERE each habit is read, setup first: it is how coaches sequence work,
// and the pack's own edges mostly run that way.
//
// WHERE A HABIT IS READ is where its measure's reduction LANDS — the anchor of an `at`, the end of
// a delta, a rate or an extremum's window. That is condition_reading.h's measureReading() rule, the
// one the card's phase chip and the charts' condition → reading link already use, so the order and
// the "P4" printed beside the habit cannot disagree. The caller (SessionDiagnosticsModel::
// sessionWorkOns) reads it off the ledger's driving measure and passes it in as `swingPosOf`, keyed
// by condition id, on this scale:
//
//   P1 … P10                      1 … 10      (phaseLabel's spelling: "P1" … "P9"; Finish is P10)
//   takeaway                      1.5         between address and P2
//   transition                    4.5         between the top and P5
//   downswing                     5.5         between P5 and P6
//   max speed                     6.5         the hands' peak speed, before the ball
//   release                       7.5         just after the ball
//   a session-level habit         11          MeasureScope::Session — read over the session's
//                                             swings, never at a point in one; after every swing
//                                             position (kWorkOnSwingPosSession)
//   unknown / not supplied        11          the same: a caller that passes no positions gets
//                                             every habit tied, and so v3's order unchanged
//
// Two habits read at one position (knee flex lost by the top and reverse spine at the top are both
// P4) are ordered by magnitude between themselves, as v3 ordered everything.
inline constexpr double kWorkOnSwingPosSession = 11.0;

inline double workOnSwingPosOfPhase(const QString &label)
{
    const QString l = label.trimmed().toLower();
    if (l.isEmpty()) return kWorkOnSwingPosSession;
    if (l.startsWith(QLatin1Char('p'))) {
        bool ok = false;
        const int n = l.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 10) return double(n);
    }
    if (l == QLatin1String("takeaway"))   return 1.5;
    if (l == QLatin1String("transition")) return 4.5;
    if (l == QLatin1String("downswing"))  return 5.5;
    if (l == QLatin1String("max speed") || l == QLatin1String("maxspeed")) return 6.5;
    if (l == QLatin1String("release"))    return 7.5;
    if (l == QLatin1String("finish"))     return 10.0;
    return kWorkOnSwingPosSession;
}

// One supported cause → effect link, as the session's 2×2 over the shots on which both could be
// judged: a = both fired, b = the cause alone, c = the effect alone, d = neither. `id` is the OTHER
// end — the effect, on the cause's entry.
struct RootBecause {
    QString id;
    int a = 0, b = 0, c = 0, d = 0;
};

// WHICH RULE PUT AN ENTRY WHERE IT IS (session_report_design.md §4.2). The report's "Why this
// order" page prints each rule with a "Today" line generated from these, and nothing else, so the
// explanation is of the rule that actually ran, not a nicer-sounding one.
//
//   resolving    rule 1 — it stopped during the session, so it went to the back
//   rootBecause  rule 2 — the listed patterns it is a SUPPORTED cause of, each with its 2×2
//   behind       rule 2 — the supported cause it waits behind (the first of causedBy); empty = root
//   mayFollow    rule 2 — authored causes among the session's patterns these swings cannot test
//   swingPos     rule v4 — where in the swing it is read (the scale above); decides only when the
//                record's linksTestable is false
//   excess       rule 3 — ConditionLedger::firingExcess, how far out when it goes. The sort's score
//                adds 0.10 × the Wilson bound and the pack's demotions; this is the quantity the
//                page draws as bars.
struct WorkOnReasons {
    bool                  resolving = false;
    QVector<RootBecause>  rootBecause;
    QString               behind;
    QStringList           mayFollow;
    double                excess   = 0.0;
    double                swingPos = kWorkOnSwingPosSession;
};

// One fault a session says to work on.
struct WorkOnEntry {
    QString     id;
    QString     name;            // the pack's label when written; a reader re-resolves it
    int         rank = 0;        // 1-based position in the session's list
    WorkOnClass cls  = WorkOnClass::Movement;
    // Nothing among THIS session's patterns is shown to cause it. Roots are listed first: they
    // are where work starts, and a symptom follows its cause off the list.
    bool        root = true;
    QStringList causedBy;        // session patterns authored as its causes, the link SUPPORTED here
    QStringList mayFollow;       // ...authored as its causes, but this session cannot show the link

    int    fired = 0, assessable = 0;
    double wilsonLower = 0.0;
    double excess      = 0.0;    // ConditionLedger::firingExcess — how far out when it goes
    bool   resolving   = false;
    int    direction   = 0;      // modal direction when claimed, else 0

    // The typical reading on the firings, stated ONLY when every firing was graded against the
    // same corridor. A mixed-club session grades one measure against several, and a median
    // across them is a number no corridor describes.
    QString measureId;
    bool    hasTypical   = false;
    double  typicalValue = 0.0;
    double  corridorLo   = 0.0;
    double  corridorHi   = 0.0;
    CorridorShape corridorShape = CorridorShape::Unknown;

    WorkOnReasons reasons;       // which rule placed it (rule v4; §4.2)
};

struct WorkOnEvidence {
    QString         id;
    SessionEvidence state = SessionEvidence::NotMeasured;
    int             fired = 0, assessable = 0;
    double          excess = 0.0;
};

// One session's record — what work_ons.json holds.
struct SessionWorkOns {
    QString     sessionId;
    qint64      startMs = 0, endMs = 0;
    int         shotCount = 0;
    QStringList clubs;
    std::vector<WorkOnEntry>    entries;     // in rank order
    // COULD THIS SESSION TEST ANY AUTHORED LINK AMONG ITS PATTERNS — rule 2's precondition, and the
    // switch for v4's swing order. True when at least one authored cause → effect edge has both
    // ends a Pattern here, the effect a listable one (Movement or Delivery — the entries rule 2 can
    // reorder), and the pair TESTABLE (linkTestable below): the cause is not range-restricted (it
    // did not fire on every swing it was judged on, so there is something to compare) and the two
    // were judged together on at least LedgerOptions::minPairsForDependence swings. Testable is not
    // supported: a testable link whose Fisher test fails leaves this true and the order v3's.
    // False — no authored edge among the patterns, every authored cause on every swing, or too few
    // swings — and the list is in swing order before magnitude.
    bool                        linksTestable = false;
    // Every condition the session assessed at all. The aggregate needs this for rule 3: it has
    // to tell a session that was quiet on a fault from one that never looked.
    std::vector<WorkOnEvidence> evidence;

    const WorkOnEntry *entry(const QString &id) const
    {
        for (const WorkOnEntry &e : entries) if (e.id == id) return &e;
        return nullptr;
    }
    const WorkOnEvidence *evidenceFor(const QString &id) const
    {
        for (const WorkOnEvidence &e : evidence) if (e.id == id) return &e;
        return nullptr;
    }
    // Did this session say anything conclusive about anything? A session that did not is not a
    // step in the golfer's history — it neither ages nor clears a work-on.
    bool substantive() const
    {
        for (const WorkOnEvidence &e : evidence)
            if (e.state >= SessionEvidence::Quiet) return true;
        return false;
    }
};

// The authored causal edges among conditions, as (cause, effect).
using WorkOnCauses = std::vector<std::pair<QString, QString>>;

// DOES THIS SESSION SUPPORT AN AUTHORED LINK, enough to put the effect behind its cause? The
// panel's own Conditionally-dependent grade (diagnostic_ledger.h gradeLinks): the cause must not
// fire on every shot (range-restricted — nothing to compare), there must be enough shots on which
// both were assessable, and the paired 2×2 must pass the one-sided Fisher test.
//
// AN AUTHORED EDGE IS A HYPOTHESIS, NOT A FINDING. Two faults that both fire on every swing say
// nothing about which drives which, and demoting the louder one on the strength of the authoring
// alone pushed early extension — the fault on all 31 swings of a session — off every list behind
// over the top, which also fired on all 31. The "moved together" grade needs a declared focus,
// which a session record does not carry, so it is not consulted here.
//
// Split in three so the report can quote what the test saw: linkPairs() is the paired 2×2,
// linkTestable() whether there is anything to test at all, linkSupported() the verdict.
inline RootBecause linkPairs(const ConditionLedger &cause, const ConditionLedger &effect)
{
    RootBecause t;
    t.id = effect.id;
    const size_t n = std::min(cause.run.size(), effect.run.size());
    for (size_t i = 0; i < n; ++i) {
        const ShotState u = cause.run[i], v = effect.run[i];
        if (u == ShotState::NotAssessable || v == ShotState::NotAssessable) continue;
        const bool uf = u == ShotState::Fired, vf = v == ShotState::Fired;
        if (uf && vf) ++t.a; else if (uf) ++t.b; else if (vf) ++t.c; else ++t.d;
    }
    return t;
}

inline bool linkTestable(const ConditionLedger &cause, const RootBecause &pairs,
                         const LedgerOptions &opt = LedgerOptions())
{
    return !cause.rangeRestricted
        && pairs.a + pairs.b + pairs.c + pairs.d >= opt.minPairsForDependence;
}

inline bool linkSupported(const ConditionLedger &cause, const ConditionLedger &effect,
                          const LedgerOptions &opt = LedgerOptions())
{
    const RootBecause t = linkPairs(cause, effect);
    if (!linkTestable(cause, t, opt)) return false;
    return fisherExactOneSided(t.a, t.b, t.c, t.d) <= opt.fisherAlpha;
}

// ── One session ─────────────────────────────────────────────────────────────────
//
// `ledgers` is conditionLedgers(shots) — passed rather than recomputed so the list and the
// panel beside it are reductions of the SAME ledger. `classOf` answers for every condition id;
// an id it does not hold is Excluded (a condition the pack no longer authors is not coached).
// `swingPosOf` is where each condition is read in the swing (the scale above workOnSwingPosOfPhase);
// an id it does not hold sorts as kWorkOnSwingPosSession, so a caller passing none gets v3's order.
//
// THE ORDER (rule v4), each key consulted only when every earlier one ties:
//   1. still happening first — a pattern that stopped inside the session (`resolving`) goes last;
//   2. a root before what it causes — behind its cause only where this session SUPPORTS the link;
//   3. when the session could test none of its links (!linksTestable): earlier in the swing first;
//   4. the bigger miss first: firingExcess + 0.10 × the Wilson bound, less the pack's demotions;
//   5. the id, so the order is total.
// Movements fill the list to the cap; Delivery numbers only fill it to three, behind them.
inline SessionWorkOns reduceSessionWorkOns(const std::vector<ShotRecord> &shots,
                                           const std::vector<ConditionLedger> &ledgers,
                                           const QHash<QString, WorkOnClass> &classOf,
                                           const WorkOnCauses &causes,
                                           const QHash<QString, double> &swingPosOf,
                                           const WorkOnOptions &opt = WorkOnOptions())
{
    SessionWorkOns out;
    out.shotCount = int(shots.size());
    for (const ShotRecord &s : shots) {
        if (s.timestampMs > 0) {
            out.startMs = out.startMs == 0 ? s.timestampMs : std::min(out.startMs, s.timestampMs);
            out.endMs   = std::max(out.endMs, s.timestampMs);
        }
        if (!s.club.isEmpty() && !out.clubs.contains(s.club)) out.clubs.append(s.club);
    }

    QSet<QString> patternSet;
    for (const ConditionLedger &l : ledgers) {
        const SessionEvidence ev = sessionEvidence(l, opt);
        if (ev == SessionEvidence::NotMeasured) continue;
        out.evidence.push_back({ l.id, ev, l.fired, l.assessable, l.firingExcess });
        if (ev == SessionEvidence::Pattern) patternSet.insert(l.id);
    }

    struct Candidate { WorkOnEntry e; double score = 0.0; };
    std::vector<Candidate> movement, delivery;
    // Every supported link, cause id -> the effect with its 2×2, for the causes' rootBecause.
    QHash<QString, QVector<RootBecause>> supported;

    for (const ConditionLedger &l : ledgers) {
        if (l.tier != Tier::Pattern) continue;
        const WorkOnClass cls = classOf.value(l.id, WorkOnClass::Excluded);
        if (cls == WorkOnClass::Excluded) continue;

        Candidate c;
        c.e.id          = l.id;
        c.e.cls         = cls;
        c.e.fired       = l.fired;
        c.e.assessable  = l.assessable;
        c.e.wilsonLower = l.wilsonLower;
        c.e.excess      = l.firingExcess;
        c.e.resolving   = l.resolving;
        c.e.direction   = l.directionClaimed ? l.modalDirection : 0;
        c.e.measureId   = l.drivingMeasureId;
        for (const auto &edge : causes) {
            if (edge.second != l.id || edge.first == l.id || !patternSet.contains(edge.first)
                || c.e.causedBy.contains(edge.first) || c.e.mayFollow.contains(edge.first))
                continue;
            const ConditionLedger *from = nullptr;
            for (const ConditionLedger &k : ledgers) if (k.id == edge.first) { from = &k; break; }
            const RootBecause pairs = from ? linkPairs(*from, l) : RootBecause{};
            if (from && linkTestable(*from, pairs)) out.linksTestable = true;
            if (from && linkSupported(*from, l)) {
                c.e.causedBy.append(edge.first);
                RootBecause t = pairs;
                t.id = l.id;                                  // the effect, quoted on the cause
                supported[edge.first].append(t);
            } else {
                c.e.mayFollow.append(edge.first);
            }
        }
        c.e.root = c.e.causedBy.isEmpty();
        c.e.reasons.resolving = l.resolving;
        c.e.reasons.behind    = c.e.causedBy.isEmpty() ? QString() : c.e.causedBy.first();
        c.e.reasons.mayFollow = c.e.mayFollow;
        c.e.reasons.excess    = l.firingExcess;
        c.e.reasons.swingPos  = swingPosOf.value(l.id, kWorkOnSwingPosSession);

        // The typical reading, under the one-corridor rule above.
        std::vector<double> values;
        bool sameCorridor = true, haveCorridor = false;
        for (const ShotRecord &s : shots) {
            const ConditionRow *r = rowFor(s, l.id);
            if (!r || r->state != ShotState::Fired) continue;
            if (!std::isfinite(r->value)) { sameCorridor = false; break; }
            if (!haveCorridor) {
                c.e.corridorLo = r->corridorLo;
                c.e.corridorHi = r->corridorHi;
                c.e.corridorShape = r->corridorShape;
                haveCorridor = true;
            } else if (std::fabs(r->corridorLo - c.e.corridorLo) > 1e-9
                       || std::fabs(r->corridorHi - c.e.corridorHi) > 1e-9
                       || r->corridorShape != c.e.corridorShape) {
                sameCorridor = false;
                break;
            }
            values.push_back(r->value);
        }
        if (sameCorridor && haveCorridor && !values.empty()
            && c.e.corridorShape != CorridorShape::None
            && c.e.corridorShape != CorridorShape::Unknown) {
            c.e.hasTypical   = true;
            c.e.typicalValue = medianOf(values);
        }

        // THE CARD ROW'S ORDER, minus history. Magnitude first, the bound to settle a near-tie,
        // and the pack's two demotions (SessionDiagnosticsModel::rankScores). The fault profile's
        // bias is deliberately absent: what a session records must be a function of that session,
        // or the aggregate would be counting its own earlier answers.
        c.score = l.firingExcess + 0.10 * l.wilsonLower;
        const ConditionRow *latest = shots.empty() ? nullptr : rowFor(shots.back(), l.id);
        if (latest && !latest->material)       c.score -= 0.05;
        if (latest && latest->contextInferred) c.score -= 0.05;

        (cls == WorkOnClass::Movement ? movement : delivery).push_back(std::move(c));
    }

    // A fault the golfer already fixed inside the session goes behind the ones still firing, and
    // a symptom behind its cause. Then SHARP findings ahead of the rest (Mark, 15 Oct: out of order
    // and peaking after impact "are both really bad"): under v3 they led through their excess,
    // kDiagSharpExcess, but v4 puts swing position before magnitude, and on 07-04 and 10-08 that
    // pushed both off the list altogether — so sharpness is its own key, above position. Then —
    // when no link could be tested, so rule 2 had nothing to say — the swing's own order. Then
    // magnitude.
    const bool swingOrder = !out.linksTestable;
    const auto before = [swingOrder](const Candidate &x, const Candidate &y) {
        if (x.e.resolving != y.e.resolving) return !x.e.resolving;
        if (x.e.root != y.e.root)           return x.e.root;
        const bool xs = x.e.excess >= kDiagSharpExcess, ys = y.e.excess >= kDiagSharpExcess;
        if (xs != ys)                       return xs;
        if (swingOrder && x.e.reasons.swingPos != y.e.reasons.swingPos)
            return x.e.reasons.swingPos < y.e.reasons.swingPos;
        if (x.score != y.score)             return x.score > y.score;
        return x.e.id < y.e.id;
    };
    std::stable_sort(movement.begin(), movement.end(), before);
    std::stable_sort(delivery.begin(), delivery.end(), before);

    for (Candidate &c : movement) {
        // gate rule: PP_GATE("rule.diag.workOnLimits")
        if (int(out.entries.size()) >= opt.maxPerSession) break;
        out.entries.push_back(std::move(c.e));
    }
    for (Candidate &c : delivery) {
        if (int(out.entries.size()) >= std::min(opt.minPerSession, opt.maxPerSession)) break;
        out.entries.push_back(std::move(c.e));
    }
    for (size_t i = 0; i < out.entries.size(); ++i) out.entries[i].rank = int(i) + 1;

    // rootBecause: the LISTED entries each one is a supported cause of, in list order — what the
    // report can point at ("it went away on the swings where X did"), so never an effect that
    // missed the cap.
    for (WorkOnEntry &cause : out.entries) {
        const QVector<RootBecause> mine = supported.value(cause.id);
        for (const WorkOnEntry &effect : out.entries)
            for (const RootBecause &t : mine)
                if (t.id == effect.id) cause.reasons.rootBecause.append(t);
    }
    return out;
}

// Without positions: every habit ties on rule v4's key, so the order is v3's. The callers that
// predate swing order (and the tests that pin v3's behaviour) keep this form.
inline SessionWorkOns reduceSessionWorkOns(const std::vector<ShotRecord> &shots,
                                           const std::vector<ConditionLedger> &ledgers,
                                           const QHash<QString, WorkOnClass> &classOf,
                                           const WorkOnCauses &causes,
                                           const WorkOnOptions &opt = WorkOnOptions())
{
    return reduceSessionWorkOns(shots, ledgers, classOf, causes, QHash<QString, double>(), opt);
}

// ── One golfer ──────────────────────────────────────────────────────────────────

enum class WorkOnStatus {
    Active,    // a pattern in the latest session that could tell
    Easing,    // quiet in the latest such session, a pattern before it
    Cleared,   // quiet in opt.clearAfterQuiet consecutive such sessions — leaves the list
};

inline QString workOnStatusToString(WorkOnStatus s)
{
    switch (s) {
    case WorkOnStatus::Easing:  return QStringLiteral("easing");
    case WorkOnStatus::Cleared: return QStringLiteral("cleared");
    case WorkOnStatus::Active:  break;
    }
    return QStringLiteral("active");
}

// One session in one work-on's history. EVERY session gets a point, measured or not — the same
// reason a not-assessable shot gets a tick: a gap would read as "nothing happened".
struct WorkOnPoint {
    QString         sessionId;
    qint64          startMs = 0;
    SessionEvidence state = SessionEvidence::NotMeasured;
    bool            listed = false;
    int             rank = 0;
    int             fired = 0, assessable = 0;
    double          excess = 0.0;
};

struct WorkOnSummary {
    QString      id;
    QString      name;
    WorkOnStatus status = WorkOnStatus::Active;
    double       score  = 0.0;          // the Wilson lower bound on the two weighted counts below
    double       weightedFired = 0.0, weightedAssessable = 0.0;

    int sessionsListed     = 0;   // made a session's list
    int sessionsPattern    = 0;   // a pattern, listed or not
    int sessionsConclusive = 0;   // Pattern or Quiet
    // Across EVERY session, short ones included: swings that showed the fault, and swings on
    // which it could be judged at all. What the row quotes and what the order is built from.
    int swingsFired      = 0;
    int swingsAssessable = 0;

    std::vector<WorkOnPoint> history;   // oldest first, one per session
    int latestPatternIndex    = -1;     // into history
    int latestConclusiveIndex = -1;

    bool        hasLatest = false;
    WorkOnEntry latest;                 // from the newest session that listed it
    QString     latestSessionId;

    // The newest listing fired as a SHARP pattern (Condition::sharp). The entry's excess is the
    // median rowStrength over its firings, and every firing of a sharp condition reads exactly
    // kDiagSharpExcess — past any corridor excess a real reading reaches — so the median is that
    // number for a sharp condition and under it for every other.
    bool sharp() const { return hasLatest && latest.excess >= kDiagSharpExcess; }
};

// Sessions in any order; they are sorted here by start time, then id.
inline std::vector<WorkOnSummary> aggregateWorkOns(std::vector<SessionWorkOns> sessions,
                                                   const WorkOnOptions &opt = WorkOnOptions())
{
    std::stable_sort(sessions.begin(), sessions.end(),
                     [](const SessionWorkOns &a, const SessionWorkOns &b) {
                         if (a.startMs != b.startMs) return a.startMs < b.startMs;
                         return a.sessionId < b.sessionId;
                     });
    const int n = int(sessions.size());

    // Age in SUBSTANTIVE sessions since. A warm-up of three balls between two real sessions
    // must not make the first one older.
    std::vector<int> age(size_t(n), 0);
    {
        int later = 0;
        for (int i = n - 1; i >= 0; --i) {
            age[size_t(i)] = later;
            if (sessions[size_t(i)].substantive()) ++later;
        }
    }

    // First-listed order: the stable identity of the set, as conditionLedgers() keeps for shots.
    std::vector<QString> order;
    QSet<QString> seen;
    for (const SessionWorkOns &s : sessions)
        for (const WorkOnEntry &e : s.entries)
            if (!seen.contains(e.id)) { seen.insert(e.id); order.push_back(e.id); }

    std::vector<WorkOnSummary> out;
    out.reserve(order.size());
    for (const QString &id : order) {
        WorkOnSummary w;
        w.id = id;
        for (int i = 0; i < n; ++i) {
            const SessionWorkOns &s = sessions[size_t(i)];
            WorkOnPoint p;
            p.sessionId = s.sessionId;
            p.startMs   = s.startMs;
            if (const WorkOnEvidence *ev = s.evidenceFor(id)) {
                p.state      = ev->state;
                p.fired      = ev->fired;
                p.assessable = ev->assessable;
                p.excess     = ev->excess;
            }
            if (const WorkOnEntry *e = s.entry(id)) {
                p.listed = true;
                p.rank   = e->rank;
                w.hasLatest = true;
                w.latest = *e;
                w.latestSessionId = s.sessionId;
                if (!e->name.isEmpty()) w.name = e->name;
                ++w.sessionsListed;
            }
            w.swingsFired      += p.fired;
            w.swingsAssessable += p.assessable;
            const double fade = std::pow(opt.recencyDecay, double(age[size_t(i)]));
            w.weightedFired      += fade * double(p.fired);
            w.weightedAssessable += fade * double(p.assessable);
            if (p.state == SessionEvidence::Pattern) {
                ++w.sessionsPattern;
                w.latestPatternIndex = i;
            }
            if (p.state >= SessionEvidence::Quiet) {
                ++w.sessionsConclusive;
                w.latestConclusiveIndex = i;
            }
            w.history.push_back(std::move(p));
        }

        w.score = wilsonLowerBound(w.weightedFired, w.weightedAssessable, opt.wilsonZ);

        // Rule 3: only conclusive sessions move the status, newest first.
        int quietRun = 0;
        for (int i = n - 1; i >= 0; --i) {
            const SessionEvidence st = w.history[size_t(i)].state;
            if (st < SessionEvidence::Quiet) continue;
            if (st == SessionEvidence::Pattern) break;
            ++quietRun;
        }
        // gate rule: PP_GATE("rule.diag.workOnClear")
        w.status = quietRun >= opt.clearAfterQuiet ? WorkOnStatus::Cleared
                 : quietRun >= 1                   ? WorkOnStatus::Easing
                 :                                   WorkOnStatus::Active;
        out.push_back(std::move(w));
    }

    // Status, then SHARP FIRST, as in sessions: a sharp fault is serious in itself, and the
    // Wilson bound would otherwise sink the sequence faults, which few swings can assess, under
    // posture faults every swing can. Then the bound, then how far out, then the id.
    std::stable_sort(out.begin(), out.end(), [](const WorkOnSummary &a, const WorkOnSummary &b) {
        if (a.status != b.status) return int(a.status) < int(b.status);
        if (a.sharp() != b.sharp()) return a.sharp();
        if (a.score != b.score)   return a.score > b.score;
        if (a.latest.excess != b.latest.excess) return a.latest.excess > b.latest.excess;
        return a.id < b.id;
    });
    return out;
}

// ── Serialisation — work_ons.json ───────────────────────────────────────────────
//
// Unlike diagnostics.json this file holds VERDICTS, and it may because it is a stamped
// derivation and not the evidence: the rows stay in the ledger, and whoever writes this file
// records what it was derived from and re-derives when that moves (WorkOnsController).

inline QString workOnClassToString(WorkOnClass c)
{
    switch (c) {
    case WorkOnClass::Delivery: return QStringLiteral("delivery");
    case WorkOnClass::Excluded: return QStringLiteral("excluded");
    case WorkOnClass::Movement: break;
    }
    return QStringLiteral("movement");
}

inline WorkOnClass workOnClassFromString(const QString &s)
{
    if (s == QStringLiteral("delivery")) return WorkOnClass::Delivery;
    if (s == QStringLiteral("excluded")) return WorkOnClass::Excluded;
    return WorkOnClass::Movement;
}

inline QJsonObject toJson(const SessionWorkOns &s)
{
    QJsonArray entries;
    for (const WorkOnEntry &e : s.entries) {
        QJsonObject o;
        o[QStringLiteral("id")]          = e.id;
        o[QStringLiteral("name")]        = e.name;
        o[QStringLiteral("rank")]        = e.rank;
        o[QStringLiteral("class")]       = workOnClassToString(e.cls);
        o[QStringLiteral("root")]        = e.root;
        o[QStringLiteral("causedBy")]    = QJsonArray::fromStringList(e.causedBy);
        o[QStringLiteral("mayFollow")]   = QJsonArray::fromStringList(e.mayFollow);
        o[QStringLiteral("fired")]       = e.fired;
        o[QStringLiteral("assessable")]  = e.assessable;
        o[QStringLiteral("wilsonLower")] = e.wilsonLower;
        o[QStringLiteral("excess")]      = e.excess;
        o[QStringLiteral("resolving")]   = e.resolving;
        o[QStringLiteral("direction")]   = e.direction;
        o[QStringLiteral("measureId")]   = e.measureId;
        if (e.hasTypical) {
            o[QStringLiteral("typicalValue")]  = e.typicalValue;
            o[QStringLiteral("corridorLo")]    = e.corridorLo;
            o[QStringLiteral("corridorHi")]    = e.corridorHi;
            o[QStringLiteral("corridorShape")] = corridorShapeToString(e.corridorShape);
        }
        QJsonObject why;
        why[QStringLiteral("resolving")] = e.reasons.resolving;
        QJsonArray roots;
        for (const RootBecause &r : e.reasons.rootBecause)
            roots.append(QJsonObject{ { QStringLiteral("id"), r.id }, { QStringLiteral("a"), r.a },
                                      { QStringLiteral("b"), r.b }, { QStringLiteral("c"), r.c },
                                      { QStringLiteral("d"), r.d } });
        why[QStringLiteral("rootBecause")] = roots;
        why[QStringLiteral("behind")]      = e.reasons.behind;
        why[QStringLiteral("mayFollow")]   = QJsonArray::fromStringList(e.reasons.mayFollow);
        why[QStringLiteral("excess")]      = e.reasons.excess;
        why[QStringLiteral("swingPos")]    = e.reasons.swingPos;
        o[QStringLiteral("reasons")] = why;
        entries.append(o);
    }

    QJsonArray evidence;
    for (const WorkOnEvidence &e : s.evidence) {
        QJsonObject o;
        o[QStringLiteral("id")]         = e.id;
        o[QStringLiteral("state")]      = sessionEvidenceToString(e.state);
        o[QStringLiteral("fired")]      = e.fired;
        o[QStringLiteral("assessable")] = e.assessable;
        o[QStringLiteral("excess")]     = e.excess;
        evidence.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kWorkOnSchemaVersion;
    root[QStringLiteral("ruleVersion")]   = kWorkOnRuleVersion;
    root[QStringLiteral("sessionId")]     = s.sessionId;
    root[QStringLiteral("startMs")]       = double(s.startMs);
    root[QStringLiteral("endMs")]         = double(s.endMs);
    root[QStringLiteral("shotCount")]     = s.shotCount;
    root[QStringLiteral("clubs")]         = QJsonArray::fromStringList(s.clubs);
    root[QStringLiteral("linksTestable")] = s.linksTestable;
    root[QStringLiteral("workOns")]       = entries;
    root[QStringLiteral("evidence")]      = evidence;
    return root;
}

// `ok` is false for a file from a newer schema or a different rule: its answer is not this
// build's answer and must be re-derived, not aggregated.
inline SessionWorkOns sessionWorkOnsFromJson(const QJsonObject &root, bool *ok = nullptr)
{
    SessionWorkOns s;
    const bool current = root.value(QStringLiteral("schemaVersion")).toInt() == kWorkOnSchemaVersion
                      && root.value(QStringLiteral("ruleVersion")).toInt() == kWorkOnRuleVersion;
    if (ok) *ok = current;
    if (!current) return s;

    s.sessionId = root.value(QStringLiteral("sessionId")).toString();
    s.startMs   = qint64(root.value(QStringLiteral("startMs")).toDouble());
    s.endMs     = qint64(root.value(QStringLiteral("endMs")).toDouble());
    s.shotCount = root.value(QStringLiteral("shotCount")).toInt();
    for (const QJsonValue &v : root.value(QStringLiteral("clubs")).toArray()) s.clubs.append(v.toString());
    s.linksTestable = root.value(QStringLiteral("linksTestable")).toBool();

    for (const QJsonValue &v : root.value(QStringLiteral("workOns")).toArray()) {
        const QJsonObject o = v.toObject();
        WorkOnEntry e;
        e.id          = o.value(QStringLiteral("id")).toString();
        e.name        = o.value(QStringLiteral("name")).toString();
        e.rank        = o.value(QStringLiteral("rank")).toInt();
        e.cls         = workOnClassFromString(o.value(QStringLiteral("class")).toString());
        e.root        = o.value(QStringLiteral("root")).toBool(true);
        for (const QJsonValue &c : o.value(QStringLiteral("causedBy")).toArray()) e.causedBy.append(c.toString());
        for (const QJsonValue &c : o.value(QStringLiteral("mayFollow")).toArray()) e.mayFollow.append(c.toString());
        e.fired       = o.value(QStringLiteral("fired")).toInt();
        e.assessable  = o.value(QStringLiteral("assessable")).toInt();
        e.wilsonLower = o.value(QStringLiteral("wilsonLower")).toDouble();
        e.excess      = o.value(QStringLiteral("excess")).toDouble();
        e.resolving   = o.value(QStringLiteral("resolving")).toBool();
        e.direction   = o.value(QStringLiteral("direction")).toInt();
        e.measureId   = o.value(QStringLiteral("measureId")).toString();
        e.hasTypical  = o.contains(QStringLiteral("typicalValue"));
        if (e.hasTypical) {
            e.typicalValue  = o.value(QStringLiteral("typicalValue")).toDouble();
            e.corridorLo    = o.value(QStringLiteral("corridorLo")).toDouble();
            e.corridorHi    = o.value(QStringLiteral("corridorHi")).toDouble();
            e.corridorShape = corridorShapeFromString(o.value(QStringLiteral("corridorShape")).toString());
        }
        const QJsonObject why = o.value(QStringLiteral("reasons")).toObject();
        e.reasons.resolving = why.value(QStringLiteral("resolving")).toBool();
        for (const QJsonValue &r : why.value(QStringLiteral("rootBecause")).toArray()) {
            const QJsonObject t = r.toObject();
            e.reasons.rootBecause.append(RootBecause{ t.value(QStringLiteral("id")).toString(),
                                                      t.value(QStringLiteral("a")).toInt(),
                                                      t.value(QStringLiteral("b")).toInt(),
                                                      t.value(QStringLiteral("c")).toInt(),
                                                      t.value(QStringLiteral("d")).toInt() });
        }
        e.reasons.behind = why.value(QStringLiteral("behind")).toString();
        for (const QJsonValue &c : why.value(QStringLiteral("mayFollow")).toArray()) e.reasons.mayFollow.append(c.toString());
        e.reasons.excess   = why.value(QStringLiteral("excess")).toDouble();
        e.reasons.swingPos = why.value(QStringLiteral("swingPos")).toDouble(kWorkOnSwingPosSession);
        s.entries.push_back(std::move(e));
    }

    for (const QJsonValue &v : root.value(QStringLiteral("evidence")).toArray()) {
        const QJsonObject o = v.toObject();
        WorkOnEvidence e;
        e.id         = o.value(QStringLiteral("id")).toString();
        e.state      = sessionEvidenceFromString(o.value(QStringLiteral("state")).toString());
        e.fired      = o.value(QStringLiteral("fired")).toInt();
        e.assessable = o.value(QStringLiteral("assessable")).toInt();
        e.excess     = o.value(QStringLiteral("excess")).toDouble();
        s.evidence.push_back(std::move(e));
    }
    return s;
}

} // namespace pinpoint::analysis
