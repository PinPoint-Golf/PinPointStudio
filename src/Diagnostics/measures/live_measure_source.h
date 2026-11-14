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

#include "club_baseline_provider.h"
#include "measure_sample.h"
#include "../norms/norm_measure_source.h"

#include <QHash>
#include <QString>

#include <memory>
#include <optional>

// THE LIVE VALUE SOURCE: a recorded swing, read as measures.
//
// `IMeasureValueSource` has been the seam since the engine was written and every implementation of
// it so far has been a test fake. This is the first one that reads a swing off disk, and it is the
// point where the pack stops being a document and starts making claims about a golfer.
//
// It owns almost no arithmetic. `measure_sample.h` already parses a swing.json into a
// SwingPhaseGrid and reduces a Measure over it with nullopt-never-zero semantics; the pack already
// resolves a measureId to a Measure. This class is the JOIN of those two plus one thing neither
// could carry alone — WHY a measure produced nothing.
//
// ── Why the missing reason is a first-class output ─────────────────────────────────────────────
//
// `std::optional<Value>` is the right return type and it is a lossy one. A review panel showing a
// greyed cell has to say something in it, and the four sentences below are genuinely different
// pieces of advice: buy a launch monitor, re-record with the camera further back, wait for a
// producer we have not written, or nothing at all because the pack does not carry that measure.
// Folding them into one "not available" would make the greyed half of the panel unreadable, and
// inventing a reason we cannot support would be worse — this is why the vocabulary below is
// MECHANICAL. "Metric not produced on this capture" is a statement about our own pipeline and is
// always true when it is said. "Shaft occluded at P6" would be a guess about a camera we cannot
// see, dressed as a diagnosis, and there is no code path here that could know it.
//
// ── What this class deliberately does not do ──────────────────────────────────────────────────
//
// It attaches no corridor and no grade — that is `NormMeasureSource`, and the split is the one
// norm_measure_source.h argues for: a value source knows what the swing did and knows nothing about
// what is normal. `detectForSwing()` at the bottom of this header is the convenience that stacks
// the two for one swing, and it is the whole of what a session model needs to call.

namespace pinpoint::analysis {

// WHY a measure could not be read. The `None` member is not padding: a caller iterating the pack
// wants one lookup that answers both "did this resolve" and "why not", and an empty QString for
// success is a sentinel a reader has to know about.
//
// An ENUM as well as a sentence because the two have different lifetimes. The sentence is UI copy
// and will be reworded, translated and shortened; the token is what a test pins and what a caller
// switches on to decide whether to offer "connect a launch monitor". Deriving either from the other
// would tie a behaviour to a string somebody is going to edit.
enum class MissingKind {
    None,                       // it read; there is no reason to give
    UnknownMeasure,             // no measure with this id in the loaded pack
    NoMetricBinding,            // the measure names no metricKey — Composed, or authored ahead of a producer
    NoAnalysis,                 // the swing carries no phase grid at all: unanalysed, or unsegmented
    NoLaunchMonitor,            // an lm.* measure, and this shot has no launch-monitor data whatever
    LaunchMonitorFieldAbsent,   // there IS launch-monitor data; this device did not report this field
    MetricNotProduced,          // the metric key is absent from this capture's analysis
    PhaseNotSegmented,          // the metric is here, but a phase the reducer needs was never found
    CaptureDataIssue,           // the recording itself is known broken (frames lost, IMU record
                                // inconsistent) — every row on the shot is withheld, not just one measure
    SessionScope,               // read over the session's swings (MeasureScope::Session), never off one
                                // swing — the session model reduces it (§10.8 E2)
    CompositeCoverage,          // a composite (MeasureKind::Composite): never a grid reading of its own.
                                // NormMeasureSource composes it from its parts, so this source is only
                                // ever asked about one after it was refused — too few parts read
                                // (posture_metrics_design.md §4.6, rule.diag.compositeCoverage)
    ClubBaselineNotEarned,      // a history.* measure with no baseline to read against: no provider, or
                                // too few of this golfer's shots with this club (rule.diag.clubBaselineMinShots)
    WindowNotCovered,           // a millisecond span after a phase (the reducer's windowMs): the phase read,
                                // but too much of the span had no reading to judge it (D5 round 3)
};

// The sentence a not-assessable cell shows. Generic on purpose — see the header comment on why
// these are mechanical statements about the pipeline rather than guesses about the capture.
QString missingReasonText(MissingKind k);

// One recorded swing, presented as measure values.
//
// Construction reads the phase grid, which prefers the swing's sidecar and falls back to a full
// swing.json parse (measure_sample.h). That fallback is a second on a 33 MB document, so a caller
// batching a session should build grids on a worker and hand them in through the second
// constructor rather than paying for the parse once per source.
class LiveMeasureSource final : public IMeasureValueSource {
public:
    // `writeSidecar` follows readPhaseGrid()'s contract exactly: false means "never fat-parse", and
    // an unindexed swing then reads as having no analysis rather than stalling a GUI thread.
    // `parsed` is readPhaseGrid()'s: the one parse a caller also reading the summary shares (#142).
    // The grid is read with the millisecond spans the PACK's measures name (msWindowsOf) added to
    // `cfg`, so a `windowMs` measure reads without every caller having to know which spans exist.
    LiveMeasureSource(const QString &swingDir, const CharacteristicPack &pack,
                      bool writeSidecar = true, const PhaseGridConfig &cfg = {},
                      SwingStore::LazyDocument *parsed = nullptr);

    // The batched form: the caller already has the grid (a session scan builds one per swing on a
    // worker) and this costs nothing.
    LiveMeasureSource(SwingPhaseGrid grid, const CharacteristicPack &pack);

    // nullopt => not produced for this swing, and `missingKind()` then says which of the reasons
    // above applies. Never zero for an absent measure — the distinction between "not assessed" and
    // "assessed and fine" is the one the whole module exists to keep.
    std::optional<Value> value(const QString &measureId) const override;

    // The reason the LAST call to value() for this id gave, or None/empty for an id that read or
    // was never asked. Recorded in a mutable map rather than returned alongside the value because
    // the seam's signature is fixed and shared with every fake that implements it — a side channel
    // that only this implementation offers is honest about which of the two is the contract.
    MissingKind missingKind(const QString &measureId) const;
    QString     missingReason(const QString &measureId) const;

    // THE GATE BEHIND AN ABSENT OR WITHHELD MEASURE (trust_and_gates_design.md §4.4). When a measure's
    // metric stands on a sequence node (gates::subjectForMetricKey) and the swing's records say a gate
    // refused, bounded or withheld that node, this names it: the gate id, the subject, and the clause
    // a "not judged" cell reads ("pelvis peak out of sight"). Empty `gate` when no record speaks for it
    // — a swing analysed before records existed, or a measure no gate acts on.
    // A PHASE-LEVEL REASON comes first (plane stack D6): a measure value() found "not segmented" because a
    // phase its reducer needs has no reading, on a swing whose records refuse that phase itself
    // (gates::phaseSubject — positions.p8Refused on position.p8), names that record. Asked after value().
    struct GateCause { QString gate, subject, clause; };
    GateCause gateCause(const QString &measureId) const;
    // The same, for a subject named directly rather than through a measure: "shot", whose records
    // (frames missing, a sensor recording that does not hang together) withhold the whole swing — the
    // capture-integrity exclusion's gate. Empty on a swing analysed before records existed.
    GateCause gateForSubject(const QString &subject) const;

    // ── Identity, for context resolution ────────────────────────────────────
    //
    // Both come from the grid, which read them out of the swing doc (measure_sample.cpp). The club is
    // the DECLARED one (swingDocDeclaredClub: review.club, else capture.club.name) and never the
    // picker's "DRIVER" stub, so it is EMPTY for a shot that declared none — which is how this seam
    // tells an undeclared club from a declared driver (contextInferred() below).
    QString club() const;
    QString sessionId() const;
    QString swingDir() const;

    // The club as a node of the context tree. Never empty: an undeclared club resolves to the
    // default context (context_tree.h), and contextInferred() then says that node was assumed.
    QString contextId() const;

    // True when the swing declared no club, so contextId() is the default by assumption rather than
    // by the shot's own record. detectForSwing() grades such a shot as an undeclared context: its
    // corridor readings are marked inferred and the engine demotes them (rule.diag.inferredContext,
    // "club assumed").
    bool contextInferred() const;

    // True when this shot carries launch-monitor data, evidenced by any `lm.` metric in the grid.
    //
    // The `launchMonitor` block of the swing doc would be the direct answer and is not consulted:
    // reading it means a second full parse of a document the grid already summarised, for a fact
    // the grid already implies. A block that produced no lm.* metrics at all is indistinguishable
    // from no block, and reports as no launch monitor — which is what a golfer's panel should say
    // either way.
    bool hasLaunchMonitor() const;

    const SwingPhaseGrid &grid() const { return m_grid; }

    // Whether readings carry their measurement σ (session_diagnostics_design.md §A8.3). Defaults to
    // tuned::diagUncertainty::kEnabled; the report tool turns it off to reproduce the hard verdicts.
    void setWithSigma(bool on) { m_withSigma = on; }
    bool withSigma() const     { return m_withSigma; }

    // The golfer's frozen usual numbers, read only by the two `history.*` measures (m_carryShortfall,
    // m_ballSpeedShortfall). Absent: those two are unreadable, and nothing else changes.
    void setClubBaselineProvider(std::shared_ptr<IClubBaselineProvider> provider)
    {
        m_clubBaseline = std::move(provider);
    }

private:
    void note(const QString &measureId, MissingKind k) const;
    // Value::atStart / atEnd for a Delta or anchored-Extremum measure; left NaN otherwise.
    void seriesEndpoints(const Measure &m, double &atStart, double &atEnd) const;
    // A reserved `history.*` measure: this shot's monitor reading against the club's baseline.
    std::optional<Value> historyValue(const Measure &m) const;

    SwingPhaseGrid                  m_grid;
    const CharacteristicPack       &m_pack;
    mutable QHash<QString, MissingKind> m_reasons;
    bool                            m_withSigma = tuned::diagUncertainty::kEnabled;
    std::shared_ptr<IClubBaselineProvider> m_clubBaseline;
};

// ── The whole stack, for one swing ──────────────────────────────────────────
//
// values -> norms -> grades -> findings, which is the chain a session model runs per shot and the
// one thing it should not have to assemble itself. Four objects have to agree about the context id
// (the norm lookup, the inferred-context demotion, the condition bindings and what the UI says it
// graded against) and assembling them at each call site is how three of them end up agreeing.
struct LiveDetection {
    DetectionResult result;
    QString         club;        // as recorded on the swing
    QString         contextId;   // the tree node it was graded at
    // The node was ASSUMED: the swing declared no club and no override named one, so it was graded at
    // the default context and every corridor reading carries the inferred-context demotion.
    bool            contextInferred = false;
    // The signals the caller asked for by id (`signalIds` below), each evaluated on the SAME graded
    // source detect() read, in the order asked — the kinematic sequence checks (§10.6, E4).
    std::vector<SignalOutcome> signalOutcomes;
};

// `contextIdOverride` wins when non-empty, and a context it names is a declared one (never inferred):
// a caller holding the user's own club selection knows the club even where the swing doc does not.
// Without one, a swing that declared no club (LiveMeasureSource::contextInferred) is graded as an
// undeclared context — NormMeasureSource's "not declared" case: resolved at the default, marked
// inferred, demoted by the engine — while the condition bindings still read the default node.
//
// The context tree comes from the norm provider, which is the same tree the norms were authored
// against — a second tree here could make a condition inapplicable at a node the norm resolved at.
LiveDetection detectForSwing(const LiveMeasureSource &values, const CharacteristicPack &pack,
                             const std::shared_ptr<const INormProvider> &norms,
                             const GradePolicy &policy = {}, const Cohort &athlete = {},
                             const QString &contextIdOverride = QString(),
                             const QStringList &signalIds = QStringList());

} // namespace pinpoint::analysis
