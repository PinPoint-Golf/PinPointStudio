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

// THE KINEMATIC SEQUENCE CHECKS (trunk_imu_sequence_grf_design.md §10.3 facets F1–F7 and F9, the
// impact checks I1–I5, §10.6 the ranking, §10.8 E4).
//
// ONE SOURCE OF TRUTH. Every check here is read off a SignalRow — the engine's own verdict for one
// signal (evaluateSignal, on the same graded source detect() used), persisted on the shot's ledger
// record. Nothing in this file looks at the Sequence object, a peak time or a speed, so the tile and
// the diagnosis cannot disagree: if a check reads ✗ here, the engine fired that signal.
//
// Header-only and Qt-Core-only, like diagnostic_ledger.h, so the session model, the Charts series
// model and the tests all include it without a link-line change.
//
// NO COUNT IS SHOWN (17 Oct 2026). Every surface draws the thirteen marks and their reasons, never
// "n of m": the count mostly measured how many checks the cameras could make ("1 of 1" on 79 of 147
// swings), and the §10.6 validation found no consistent relation to clubhead speed (ρ −0.07…+0.22).
// passed / assessable stay on the struct for the ranking and the tests; they are not published.

#include "diagnostic_ledger.h"
#include "../Metrics/gate_catalogue.h"   // the words of the gate or rule behind a "—" (claimWords)

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

// ── The checks, in facet order ─────────────────────────────────────────────────────────────────

struct SequenceCheckDef {
    const char *facet;      // "F1" …
    const char *signalId;   // the pack signal that IS the check (core.json)
    const char *label;      // golfer words, short — the hover list's lead
    const char *question;   // the §10.3 question, in golfer words
};

// THE TIMING IS READ AGAINST IMPACT, EACH SEGMENT ON ITS OWN. Every peak time is measured from
// impact, and a peak after the ball is a fault in every one of the four series, whatever the others
// did (Cheetham 2008: professionals peak the pelvis 87, the thorax 68 and the lead arm 65 ms before
// impact). I1–I4 ask exactly that, one segment each; the club should peak AT the ball, so I5 also
// asks that it is not well before it. F1–F3 ask the ORDER.
inline const std::array<SequenceCheckDef, 13> &sequenceCheckDefs()
{
    static const std::array<SequenceCheckDef, 13> k{ {
        { "I1", "sig_pelvisBeforeImpact",  "Hips peak before impact",     "Do the hips peak before the ball?" },
        { "I2", "sig_thoraxBeforeImpact",  "Chest peaks before impact",    "Does the chest peak before the ball?" },
        { "I3", "sig_leadArmBeforeImpact", "Lead arm peaks before impact", "Does the lead arm peak before the ball?" },
        { "I4", "sig_clubBeforeImpact",    "Club not after impact",        "Does the club peak no later than the ball?" },
        { "I5", "sig_clubPeakEarly",       "Club not early",               "Does the club peak at the ball, not well before it?" },
        { "F1", "sig_sequenceOrder",   "Hips before chest",      "Do the hips peak before the chest?" },
        { "F2", "sig_armBeforeThorax", "Chest before arm",       "Does the chest peak before the lead arm?" },
        { "F3", "sig_clubBeforeArm",   "Arm before club",        "Does the lead arm peak before the club?" },
        { "F4", "sig_thoraxGainLow",   "Chest faster than hips", "Is the chest's peak faster than the hips'?" },
        { "F5", "sig_armGainLow",      "Arm faster than chest",  "Is the lead arm's peak faster than the chest's?" },
        { "F6", "sig_clubGainLow",     "Club well above arm",    "Is the club's peak well above the arm's?" },
        { "F7", "sig_noPelvisBraking", "Hips slow by impact",    "Have the hips slowed by impact?" },
        { "F9", "sig_transitionOrder", "Hips start down first",  "Do the hips start down before the chest?" },
    } };
    return k;
}

inline QStringList sequenceCheckSignalIds()
{
    QStringList out;
    for (const SequenceCheckDef &d : sequenceCheckDefs()) out << QLatin1String(d.signalId);
    return out;
}

// ── Why a check could not be assessed ──────────────────────────────────────────────────────────
//
// The engine's refusal (SignalRefusal) folded to the three a golfer can be told apart, so this
// header needs no engine include; the session model does the folding where it holds both.
enum class SequenceGap {
    Unknown,       // anything else — "not measured on this swing"
    NotPlaced,     // a node the producer never placed (the metric was not produced for this capture)
    MixedScale,    // a ratio across two routes (§10.7 E3)
    Implausible,   // a reading the norm does not believe
    PeakAfterImpact, // the peak this check needs before impact came after it (hip braking)
    Withheld,      // a peak the producer drew but marked not for judging (the camera club peak, KB-18)
};

// The piece a measure stands for, in golfer words; empty for a measure this tile does not read.
inline QString sequencePieceOf(const QString &measureId)
{
    struct P { const char *id; const char *piece; };
    static const P k[] = {
        { "m_pelvisPeakTime",     "hip peak" },
        { "m_pelvisPeakSpeed",    "hip peak" },
        { "m_thoraxPeakTime",     "chest peak" },
        { "m_thoraxPeakSpeed",    "chest peak" },
        { "m_leadArmPeakTime",    "lead-arm peak" },
        { "m_leadArmPeakSpeed",   "lead-arm peak" },
        { "m_clubPeakTime",       "club peak" },
        { "m_clubPeakSpeed",      "club peak" },
        { "m_pelvisReversalTime", "hips' start down" },
        { "m_thoraxReversalTime", "chest's start down" },
        { "m_pelvisSpeedImpact",  "hip speed at impact" },
    };
    for (const P &p : k)
        if (measureId == QLatin1String(p.id)) return QLatin1String(p.piece);
    return QString();
}

// The link an order check fell back to, as the tile's label names it: "Hips before lead arm".
inline QString sequenceLinkName(const QString &measureId)
{
    if (measureId == QLatin1String("m_thoraxPeakTime"))  return QStringLiteral("chest");
    if (measureId == QLatin1String("m_leadArmPeakTime")) return QStringLiteral("lead arm");
    if (measureId == QLatin1String("m_clubPeakTime"))    return QStringLiteral("club");
    return QString();
}

inline QString sequenceGenericGap() { return QStringLiteral("not measured on this swing"); }

// The one-line reason a "—" check shows. `missingMeasureId` is the first measure the engine named.
inline QString sequenceGapReason(SequenceGap gap, const QString &missingMeasureId)
{
    switch (gap) {
    case SequenceGap::NotPlaced: {
        const QString piece = sequencePieceOf(missingMeasureId);
        if (piece.isEmpty()) return sequenceGenericGap();
        // A speed at an instant is measured, not placed; a peak or a reversal is placed.
        const bool instant = missingMeasureId == QLatin1String("m_pelvisSpeedImpact");
        return piece + (instant ? QStringLiteral(" not measured from these cameras")
                                : QStringLiteral(" not placed from these cameras"));
    }
    case SequenceGap::MixedScale:  return QStringLiteral("needs both peaks from one route");
    case SequenceGap::Implausible: return QStringLiteral("a reading here looks like a capture fault");
    case SequenceGap::PeakAfterImpact: {
        const QString piece = sequencePieceOf(missingMeasureId);
        return (piece.isEmpty() ? QStringLiteral("the peak") : piece)
             + QStringLiteral(" came after impact, so there was no slowing to read");
    }
    case SequenceGap::Withheld: {
        const QString piece = sequencePieceOf(missingMeasureId);
        return (piece.isEmpty() ? QStringLiteral("this peak") : piece)
             + QStringLiteral(" is charted but not judged: the camera cannot follow it through impact");
    }
    case SequenceGap::Unknown:     break;
    }
    return sequenceGenericGap();
}

// ── One check's state ──────────────────────────────────────────────────────────────────────────

enum class SequenceCheckState { Pass, Fail, Unsure, NotAssessable };

inline QString sequenceCheckStateName(SequenceCheckState s)
{
    switch (s) {
    case SequenceCheckState::Pass:          return QStringLiteral("pass");
    case SequenceCheckState::Fail:          return QStringLiteral("fail");
    case SequenceCheckState::Unsure:        return QStringLiteral("unsure");
    case SequenceCheckState::NotAssessable: break;
    }
    return QStringLiteral("na");
}

inline QString sequenceCheckMark(SequenceCheckState s)
{
    switch (s) {
    case SequenceCheckState::Pass:          return QStringLiteral("✓");
    case SequenceCheckState::Fail:          return QStringLiteral("✗");
    case SequenceCheckState::Unsure:        return QStringLiteral("?");
    case SequenceCheckState::NotAssessable: break;
    }
    return QStringLiteral("—");
}

// §10.6: "—" not assessable; "?" when quantified and 0.2 < pFire < 0.8; "✗" when it fired
// (pFire ≥ 0.8, or an unquantified firing); "✓" otherwise.
inline SequenceCheckState sequenceCheckStateOf(const SignalRow &g)
{
    if (g.state == ShotState::NotAssessable) return SequenceCheckState::NotAssessable;
    if (g.quantified && g.pFire >= 0.f) {
        if (g.pFire >= 0.8f) return SequenceCheckState::Fail;
        if (g.pFire >  0.2f) return SequenceCheckState::Unsure;
        return SequenceCheckState::Pass;
    }
    return g.state == ShotState::Fired ? SequenceCheckState::Fail : SequenceCheckState::Pass;
}

// The probability a check fired: its pFire, else its hard verdict restated (rowFireProb's rule).
inline double sequenceCheckFireProb(const SignalRow &g)
{
    if (g.pFire >= 0.f) return double(g.pFire);
    return g.state == ShotState::Fired ? 1.0 : 0.0;
}

// ── The shot's checks ──────────────────────────────────────────────────────────────────────────

struct SequenceCheck {
    QString            facet;
    QString            signalId;
    QString            label;
    QString            question;
    SequenceCheckState state      = SequenceCheckState::NotAssessable;
    float              pFire      = -1.f;
    bool               quantified = false;
    QString            reason;    // "—" only: why
    // "—" only: the gate record (`sequence.sightedBand`) or engine rule (`rule.diag.mixedScale`) behind
    // it, off SignalRow::notJudgedGate, and the reading a record acted on (`ks.pelvis`; empty for a rule,
    // which has no per-swing reading to open). Empty when nothing named it (an older ledger, a plain absence).
    QString            gate;
    QString            trustSubject;
};

struct SequenceChecks {
    std::vector<SequenceCheck> checks;   // I1–I5, F1–F7, F9, always all thirteen once recorded
    int    passed         = 0;           // ✓ — never displayed (no count on any surface)
    int    assessable     = 0;           // ✓ + ✗ + ? — the ranking's "anything graded at all"
    int    notAssessable  = 0;           // "—"
    // For ranking, never displayed (§10.6). Over the assessable checks.
    double expectedPassed = 0.0;         // Σ (1 − pFire)
    double sumPFire       = 0.0;         // Σ pFire — "expected failures"
    int    failedLikely   = 0;           // checks with pFire > 0.5
    bool   recorded       = false;       // the shot carries the engine's signal verdicts at all
};

// The shot's checks from its persisted signal rows. A shot written before schema 5 carries none:
// `recorded` false, and every surface draws nothing rather than a row of "—".
inline SequenceChecks sequenceChecksOf(const std::vector<SignalRow> &rows)
{
    SequenceChecks out;
    for (const SequenceCheckDef &d : sequenceCheckDefs()) {
        const QString sid = QLatin1String(d.signalId);
        const SignalRow *row = nullptr;
        for (const SignalRow &g : rows)
            if (g.signalId == sid) { row = &g; break; }
        if (row) out.recorded = true;

        SequenceCheck c;
        c.facet    = QLatin1String(d.facet);
        c.signalId = sid;
        c.label    = QLatin1String(d.label);
        c.question = QLatin1String(d.question);
        if (row) {
            c.state      = sequenceCheckStateOf(*row);
            c.pFire      = row->pFire;
            c.quantified = row->quantified;
            c.reason     = row->reason;
            if (c.state == SequenceCheckState::NotAssessable) {
                c.gate         = row->notJudgedGate;
                c.trustSubject = row->notJudgedSubject;
            }
            // Fell all the way to impact: the hips-before-impact check (I1) already asks exactly
            // that, so this row stands aside rather than count the same comparison twice.
            if (row->comparedWith == QLatin1String("impact") && c.state != SequenceCheckState::NotAssessable) {
                c.state  = SequenceCheckState::NotAssessable;
                c.reason = QStringLiteral("no later link placed; the impact check covers it");
                c.gate.clear();
                c.trustSubject.clear();
            }
            // Fell back down the chain: say which pair was compared, and why.
            if (const QString link = sequenceLinkName(row->comparedWith);
                !link.isEmpty() && c.state != SequenceCheckState::NotAssessable) {
                c.label    = c.label.section(QLatin1Char(' '), 0, 1) + QLatin1Char(' ') + link;   // "Hips before lead arm"
                c.question = QStringLiteral("Compared with the %1, the next link this capture placed").arg(link);
            }
        }
        if (c.state == SequenceCheckState::NotAssessable) {
            if (c.reason.isEmpty()) c.reason = sequenceGenericGap();
            ++out.notAssessable;
        } else {
            c.reason.clear();
            c.gate.clear();
            c.trustSubject.clear();
            const double p = row ? sequenceCheckFireProb(*row) : 0.0;
            ++out.assessable;
            if (c.state == SequenceCheckState::Pass) ++out.passed;
            out.expectedPassed += 1.0 - p;
            out.sumPFire       += p;
            if (p > 0.5) ++out.failedLikely;
        }
        out.checks.push_back(std::move(c));
    }
    if (!out.recorded) { out.checks.clear(); out.notAssessable = 0; }
    return out;
}

inline SequenceChecks sequenceChecksOf(const ShotRecord &shot) { return sequenceChecksOf(shot.signalRows); }

// Why the "—" are there, in one short phrase: the reason they share, else the most common one.
// Empty when every check was assessed.
inline QString sequenceChecksGapNote(const SequenceChecks &s)
{
    QStringList reasons;
    QList<int>  counts;
    for (const SequenceCheck &c : s.checks) {
        if (c.state != SequenceCheckState::NotAssessable) continue;
        const int at = int(reasons.indexOf(c.reason));
        if (at < 0) { reasons << c.reason; counts << 1; }
        else        ++counts[at];
    }
    if (reasons.isEmpty()) return QString();
    int best = 0;
    for (int i = 1; i < counts.size(); ++i) if (counts[i] > counts[best]) best = i;
    return reasons[best];
}

// A gate's or rule's short words for a "—" cell ("out of sight", "mixed scales"); empty when unknown.
inline QString sequenceGateWords(const QString &gateId)
{
    if (gateId.isEmpty()) return QString();
    const gates::GateEntry *e = gates::gateEntry(gateId);
    return e ? gates::claimWords(*e) : QString();
}

// The QML shape — PpChSeqTable binds it; empty map = nothing recorded.
//   { notAssessable, gapNote,
//     checks: [{ facet, signalId, label, question, state, mark, pFire, quantified, reason, gate, trustSubject,
//                gateWords, detail }] }
// expectedPassed / sumPFire / failedLikely ride along for ranking and the tests; no surface prints them.
// No "n of m" line and no passed / assessable: the count was removed from every surface (17 Oct).
inline QVariantMap sequenceChecksToVariant(const SequenceChecks &s)
{
    if (!s.recorded) return {};
    QVariantList list;
    for (const SequenceCheck &c : s.checks) {
        list << QVariantMap{ { QStringLiteral("facet"),      c.facet },
                             { QStringLiteral("signalId"),   c.signalId },
                             { QStringLiteral("label"),      c.label },
                             { QStringLiteral("question"),   c.question },
                             { QStringLiteral("state"),      sequenceCheckStateName(c.state) },
                             { QStringLiteral("mark"),       sequenceCheckMark(c.state) },
                             { QStringLiteral("pFire"),      double(c.pFire) },
                             { QStringLiteral("quantified"), c.quantified },
                             { QStringLiteral("reason"),     c.reason },
                             // the gate or rule behind a "—" and its short words ("out of sight",
                             // "mixed scales"); trustSubject opens the reading in the Trust panel
                             // (empty for a rule: words only, nothing to open)
                             { QStringLiteral("gate"),         c.gate },
                             { QStringLiteral("trustSubject"), c.trustSubject },
                             { QStringLiteral("gateWords"),    sequenceGateWords(c.gate) },
                             // What the hover list prints after the label: why for "—", the
                             // doubt for "?", the question itself otherwise.
                             { QStringLiteral("detail"),
                               c.state == SequenceCheckState::NotAssessable ? c.reason
                               : c.state == SequenceCheckState::Unsure ? QStringLiteral("too close to call on this swing")
                                                                       : c.question } };
    }
    return { { QStringLiteral("notAssessable"),  s.notAssessable },
             { QStringLiteral("gapNote"),        sequenceChecksGapNote(s) },
             { QStringLiteral("expectedPassed"), s.expectedPassed },
             { QStringLiteral("sumPFire"),       s.sumPFire },
             { QStringLiteral("failedLikely"),   s.failedLikely },
             { QStringLiteral("checks"),         list } };
}

// ── Ranking the session's swings (§10.6) ───────────────────────────────────────────────────────
//
// Best-sequenced first: fewest checks failed with pFire > 0.5, then the smaller Σ pFire, then the
// faster clubhead (a missing speed sorts after any measured one), then shot id for a stable order.
// A swing with NO assessed check is not ranked at all — "0 failed of 0" is not a best swing, it is
// a swing nobody could grade — and is left out of the list.
struct SequenceRankEntry {
    int    shotId        = -1;
    int    assessable    = 0;
    int    failedLikely  = 0;
    double sumPFire      = 0.0;
    double clubheadSpeed = std::numeric_limits<double>::quiet_NaN();
};

inline std::vector<int> rankBySequence(std::vector<SequenceRankEntry> entries)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const SequenceRankEntry &e) { return e.assessable <= 0; }),
                  entries.end());
    std::sort(entries.begin(), entries.end(), [](const SequenceRankEntry &a, const SequenceRankEntry &b) {
        if (a.failedLikely != b.failedLikely) return a.failedLikely < b.failedLikely;
        if (a.sumPFire != b.sumPFire)         return a.sumPFire < b.sumPFire;
        const bool af = std::isfinite(a.clubheadSpeed), bf = std::isfinite(b.clubheadSpeed);
        if (af != bf) return af;
        if (af && a.clubheadSpeed != b.clubheadSpeed) return a.clubheadSpeed > b.clubheadSpeed;
        return a.shotId < b.shotId;
    });
    std::vector<int> out;
    out.reserve(entries.size());
    for (const SequenceRankEntry &e : entries) out.push_back(e.shotId);
    return out;
}

inline SequenceRankEntry sequenceRankEntryOf(const ShotRecord &shot)
{
    const SequenceChecks s = sequenceChecksOf(shot);
    SequenceRankEntry e;
    e.shotId        = shot.shotId;
    e.assessable    = s.assessable;
    e.failedLikely  = s.failedLikely;
    e.sumPFire      = s.sumPFire;
    e.clubheadSpeed = shot.clubheadSpeed;
    return e;
}

// ── The session's sequence consistency (F10; §10.6, §10.8 E2, §10.9 P2/P7/P8) ─────────────────
//
// ONE VERDICT FOR THE SESSION, NEVER ONE PER SWING (NR-04, NR-11). Two readings, each reduced here
// from the persisted shot records and handed to the engine by the session model as the session-scope
// measures m_ksOrderShare and m_ksTimingSpread:
//
//   ORDER SHARE — over the swings whose three peak-order checks (F1 hips before chest, F2 chest
//   before arm, F3 arm before club) were ALL assessed, i.e. all four peaks placed: "in professional
//   order" claims the whole chain, so a swing that placed only the arm and the club says nothing
//   about the hips and is not counted. A swing is in order when none of the three fired. Read off the
//   SignalRows — the engine's own verdicts (E4), never the Sequence object. The value is the Wilson
//   UPPER bound on the in-order share, 1 − wilsonLowerBound(out of order, resolved) (P7): the fault
//   "fewer than half in order" is asserted only on the lower bound of the FAULT rate, the ledger's own
//   rule, so a short session is never accused of an order it has not shown.
//
//   TIMING SPREAD — per segment (pelvis 19, chest 14, lead arm 8 ms SD; Cheetham et al. 2008, 19 PGA
//   tour professionals), the SD of the placed peak instants POOLED WITHIN CLUB (a club change moves
//   peak timing and is not inconsistency), less the mean measurement variance σ_t² of those peaks
//   (P8: camera σ_t runs to 40 ms against an 8 ms professional arm SD, so the raw SD would fire on
//   noise), over the professional SD. The value is the largest ratio over the segments with enough
//   degrees of freedom; its σ is the delta-method σ of that segment's SD estimate.
//
// Header-only and engine-free like the rest of this file: the session model builds the engine's
// readings from this and evaluates the pack's signals on them.

struct SequenceSpreadSegmentDef {
    const char *measureId;   // the per-swing peak-time measure (ShotRecord::peakTimes rows)
    const char *piece;       // golfer words
    double      proSdMs;     // the professional SD, Cheetham et al. 2008
};

inline const std::array<SequenceSpreadSegmentDef, 3> &sequenceSpreadSegments()
{
    static const std::array<SequenceSpreadSegmentDef, 3> k{ {
        { "m_pelvisPeakTime",  "hips",     19.0 },
        { "m_thoraxPeakTime",  "chest",    14.0 },
        { "m_leadArmPeakTime", "lead arm",  8.0 },
    } };
    return k;
}

// The three peak-order checks whose joint verdict is "in professional order".
inline const std::array<const char *, 3> &sequenceOrderSignalIds()
{
    static const std::array<const char *, 3> k{ { "sig_sequenceOrder", "sig_armBeforeThorax",
                                                  "sig_clubBeforeArm" } };
    return k;
}

inline QString sequenceSpreadCohort() { return QStringLiteral("19 PGA tour professionals, Cheetham 2008"); }

struct SessionOrderShare {
    int    resolved = 0;      // swings with all three order checks assessed (hard)
    int    inOrder  = 0;      // …of which none fired (hard) — the caption's k of n
    double effOut   = 0.0;    // Σ w·P(out of order): soft with LedgerOptions::softTier, else hard
    double effN     = 0.0;    // Σ w
    double lowerOut = 0.0;    // wilsonLowerBound(effOut, effN)
    double value    = std::numeric_limits<double>::quiet_NaN();   // 1 − lowerOut; NaN unless available
    bool   available = false; // resolved >= minSwings
};

struct SessionSpreadSegment {
    QString measureId;
    QString piece;
    double  proSdMs    = 0.0;
    int     n          = 0;   // placed peaks counted (in clubs with at least two)
    int     dof        = 0;   // Σ (n_club − 1)
    double  sdObsMs    = std::numeric_limits<double>::quiet_NaN();   // pooled within-club SD
    double  meanVarMs2 = 0.0;                                         // mean σ_t² of those peaks
    double  sdTrueMs   = std::numeric_limits<double>::quiet_NaN();   // √max(0, s² − mean σ_t²)
    double  ratio      = std::numeric_limits<double>::quiet_NaN();   // sdTrue / proSD
    double  ratioSigma = std::numeric_limits<double>::quiet_NaN();
    bool    available  = false;  // dof >= minSwings − 1
};

struct SessionTimingSpread {
    std::array<SessionSpreadSegment, 3> segments;
    int    best      = -1;    // the segment the value is read from
    double value     = std::numeric_limits<double>::quiet_NaN();
    double sigma     = std::numeric_limits<double>::quiet_NaN();
    bool   available = false;
};

struct SessionSequenceConsistency {
    SessionOrderShare   share;
    SessionTimingSpread spread;
    int                 minSwings = tuned::sequence::kSessionMinSwings;
};

inline SessionSequenceConsistency sessionSequenceConsistency(const std::vector<ShotRecord> &shots,
                                                             const LedgerOptions &opt = LedgerOptions(),
                                                             int minSwings = tuned::sequence::kSessionMinSwings)
{
    SessionSequenceConsistency out;
    out.minSwings = minSwings;

    // ── order share ──
    for (int i = 0; i < int(shots.size()); ++i) {
        const ShotRecord &shot = shots[size_t(i)];
        bool all = true, anyFired = false;
        double pIn = 1.0;
        for (const char *sid : sequenceOrderSignalIds()) {
            const SignalRow *row = nullptr;
            for (const SignalRow &g : shot.signalRows)
                if (g.signalId == QLatin1String(sid)) { row = &g; break; }
            // A check that fell back down the chain compared a pair other than its own, so the
            // four-peak order is not known from it.
            if (!row || row->state == ShotState::NotAssessable || !row->comparedWith.isEmpty()) { all = false; break; }
            if (row->state == ShotState::Fired) anyFired = true;
            pIn *= 1.0 - sequenceCheckFireProb(*row);
        }
        if (!all) continue;
        const double w = shotWeight(shot, i, opt);
        ++out.share.resolved;
        if (!anyFired) ++out.share.inOrder;
        out.share.effN   += w;
        out.share.effOut += w * (opt.softTier ? (1.0 - pIn) : (anyFired ? 1.0 : 0.0));
    }
    // gate rule: PP_GATE("rule.diag.sequenceOrderShare")
    out.share.lowerOut  = wilsonLowerBound(out.share.effOut, out.share.effN, opt.wilsonZ);
    // gate rule: PP_GATE("rule.diag.sequenceSessionMin")
    out.share.available = out.share.resolved >= minSwings;
    if (out.share.available) out.share.value = 1.0 - out.share.lowerOut;

    // ── timing spread ──
    for (size_t k = 0; k < sequenceSpreadSegments().size(); ++k) {
        const SequenceSpreadSegmentDef &d = sequenceSpreadSegments()[k];
        SessionSpreadSegment &seg = out.spread.segments[k];
        seg.measureId = QLatin1String(d.measureId);
        seg.piece     = QLatin1String(d.piece);
        seg.proSdMs   = d.proSdMs;

        // Club groups in first-seen order (never hash order — the sums must not depend on it).
        QStringList clubs;
        std::vector<std::vector<std::pair<double, double>>> groups;   // (value, σ)
        for (const ShotRecord &shot : shots) {
            for (const PeakTimeRow &t : shot.peakTimes) {
                if (t.measureId != seg.measureId || !std::isfinite(t.valueMs)) continue;
                int g = int(clubs.indexOf(shot.club));
                if (g < 0) { g = int(clubs.size()); clubs << shot.club; groups.emplace_back(); }
                groups[size_t(g)].push_back({ t.valueMs, t.sigmaMs });
            }
        }
        double ss = 0.0, varSum = 0.0;
        for (const auto &g : groups) {
            if (g.size() < 2) continue;
            double mean = 0.0;
            for (const auto &v : g) mean += v.first;
            mean /= double(g.size());
            for (const auto &v : g) {
                ss += (v.first - mean) * (v.first - mean);
                if (std::isfinite(v.second)) varSum += v.second * v.second;
            }
            seg.n   += int(g.size());
            seg.dof += int(g.size()) - 1;
        }
        if (seg.dof <= 0) continue;
        const double s2 = ss / double(seg.dof);
        seg.sdObsMs    = std::sqrt(s2);
        seg.meanVarMs2 = varSum / double(seg.n);
        seg.sdTrueMs   = std::sqrt(std::max(0.0, s2 - seg.meanVarMs2));
        seg.ratio      = seg.sdTrueMs / seg.proSdMs;
        // σ of s² under normal theory, carried to the ratio by the delta method; the SD is floored at
        // the professional level so the σ stays finite when the de-noising reaches zero.
        const double sigmaS2 = s2 * std::sqrt(2.0 / double(seg.dof));
        seg.ratioSigma = sigmaS2 / (2.0 * seg.proSdMs * std::max(seg.sdTrueMs, seg.proSdMs));
        seg.available  = seg.dof >= minSwings - 1;
        if (seg.available && (out.spread.best < 0 || seg.ratio > out.spread.value)) {
            out.spread.best  = int(k);
            out.spread.value = seg.ratio;
            out.spread.sigma = seg.ratioSigma;
        }
    }
    out.spread.available = out.spread.best >= 0;
    return out;
}

// "In sequence on 9 of 14 swings" — the hard count, which a golfer can check against the swings.
inline QString sequenceConsistencyLine(const SessionSequenceConsistency &c)
{
    if (c.share.resolved == 0) return QStringLiteral("In sequence: not assessed over the session");
    return QStringLiteral("In sequence on %1 of %2 swings").arg(c.share.inOrder).arg(c.share.resolved);
}

// The spread against the professional spread, cohort named (NR-01); ms in whole units, σ only as ±.
inline QString sequenceSpreadLine(const SessionSequenceConsistency &c)
{
    if (!c.spread.available)
        return QStringLiteral("Peak-timing spread: needs %1 swings of one club with a placed peak")
            .arg(c.minSwings);
    const SessionSpreadSegment &s = c.spread.segments[size_t(c.spread.best)];
    return QStringLiteral("Peak-timing spread: %1 %2× ± %3 the professional spread (%4 ms SD, %5)")
        .arg(s.piece, QString::number(s.ratio, 'f', 1), QString::number(s.ratioSigma, 'f', 1),
             QString::number(int(std::lround(s.proSdMs))), sequenceSpreadCohort());
}

// Why the order share is not read yet; empty once it is.
inline QString sequenceConsistencyGapNote(const SessionSequenceConsistency &c)
{
    if (c.share.available) return QString();
    return QStringLiteral("needs %1 swings with all four peaks placed (%2 so far)")
        .arg(c.minSwings)
        .arg(c.share.resolved == 0 ? QStringLiteral("none") : QString::number(c.share.resolved));
}

// The QML shape — the Charts KS Session view and the session card bind it.
//   { line, spreadLine, gapNote, whose, cohort, state, pFire, resolved, inOrder, shareValue,
//     shareAvailable, spreadValue, spreadSigma, spreadAvailable, spreadSegment }
// `state` / `pFire` are the ENGINE's verdict on the session condition, handed in by the caller.
inline QVariantMap sequenceConsistencyToVariant(const SessionSequenceConsistency &c,
                                                const QString &state, double pFire)
{
    const bool sp = c.spread.available;
    return { { QStringLiteral("line"),            sequenceConsistencyLine(c) },
             { QStringLiteral("spreadLine"),      sequenceSpreadLine(c) },
             { QStringLiteral("gapNote"),         sequenceConsistencyGapNote(c) },
             { QStringLiteral("whose"),           QStringLiteral("read over the session, never one swing") },
             { QStringLiteral("cohort"),          sequenceSpreadCohort() },
             { QStringLiteral("state"),           state },
             { QStringLiteral("pFire"),           pFire },
             { QStringLiteral("resolved"),        c.share.resolved },
             { QStringLiteral("inOrder"),         c.share.inOrder },
             { QStringLiteral("shareAvailable"),  c.share.available },
             { QStringLiteral("shareValue"),      c.share.available ? c.share.value : -1.0 },
             { QStringLiteral("spreadAvailable"), sp },
             { QStringLiteral("spreadValue"),     sp ? c.spread.value : -1.0 },
             { QStringLiteral("spreadSigma"),     sp ? c.spread.sigma : -1.0 },
             { QStringLiteral("spreadSegment"),
               sp ? c.spread.segments[size_t(c.spread.best)].piece : QString() } };
}

} // namespace pinpoint::analysis
