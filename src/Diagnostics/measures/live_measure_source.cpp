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

#include "live_measure_source.h"
#include "../../Metrics/gate_catalogue.h"   // the gate behind an absent measure (gateCause)

#include "../norms/context_tree.h"

#include <utility>

namespace pinpoint::analysis {

namespace {

// Everything a launch monitor produces is namespaced. One place, because the missing-reason branch
// and hasLaunchMonitor() must agree about what counts as a launch-monitor metric or a shot with a
// monitor attached could still be told it has none.
const QLatin1String kLmPrefix("lm.");

} // namespace

QString missingReasonText(MissingKind k)
{
    // MECHANICAL PHRASING, and it is a rule rather than a preference. Every sentence here is a
    // statement about our own pipeline or the pack, so it is true whenever it is shown. The
    // tempting alternative — "the shaft was occluded", "the golfer left frame" — reads as more
    // helpful and is a guess: nothing on this path sees the video, and a confident wrong reason
    // sends a golfer to re-record a capture that was fine.
    switch (k) {
    case MissingKind::None:                     return QString();
    case MissingKind::UnknownMeasure:           return QStringLiteral("no measure with this id in the loaded pack");
    case MissingKind::NoMetricBinding:          return QStringLiteral("this measure has no metric behind it yet");
    case MissingKind::NoAnalysis:               return QStringLiteral("this swing has no analysis to read");
    case MissingKind::NoLaunchMonitor:          return QStringLiteral("no launch monitor data on this shot");
    case MissingKind::LaunchMonitorFieldAbsent: return QStringLiteral("not reported by the launch monitor on this shot");
    case MissingKind::MetricNotProduced:        return QStringLiteral("metric not produced on this capture");
    case MissingKind::PhaseNotSegmented:        return QStringLiteral("the phase this reads at was not segmented");
    case MissingKind::CaptureDataIssue:         return QStringLiteral("frames were lost during capture, so this shot is not assessed");
    case MissingKind::SessionScope:             return QStringLiteral("read over the session, never one swing");
    case MissingKind::CompositeCoverage:        return QStringLiteral("too few of its parts read on this shot");
    }
    return QString();
}

// ── Construction ────────────────────────────────────────────────────────────

LiveMeasureSource::LiveMeasureSource(const QString &swingDir, const CharacteristicPack &pack,
                                     bool writeSidecar, const PhaseGridConfig &cfg)
    : m_grid(readPhaseGrid(swingDir, writeSidecar, cfg)), m_pack(pack)
{
    // readPhaseGrid() sets swingDir on every path it returns a grid from, but not on the empty
    // grid it returns for a missing or unreadable document — and a source that cannot say which
    // swing it failed on is unloggable.
    if (m_grid.swingDir.isEmpty())
        m_grid.swingDir = swingDir;
}

LiveMeasureSource::LiveMeasureSource(SwingPhaseGrid grid, const CharacteristicPack &pack)
    : m_grid(std::move(grid)), m_pack(pack)
{
}

// ── Reading ─────────────────────────────────────────────────────────────────

void LiveMeasureSource::note(const QString &measureId, MissingKind k) const
{
    m_reasons.insert(measureId, k);
}

std::optional<IMeasureValueSource::Value> LiveMeasureSource::value(const QString &measureId) const
{
    const Measure *m = m_pack.measure(measureId);
    if (m == nullptr) {
        note(measureId, MissingKind::UnknownMeasure);
        return std::nullopt;
    }

    // A Composed measure names facets rather than a catalogue key, and nothing in a swing.json
    // produces one — reduceOverGrid() says the same thing by returning nullopt on an empty
    // metricKey, and the point of checking first is that "there is no producer for this" and
    // "the producer ran and this capture has nothing" are different sentences for the panel.
    // A SESSION measure has no per-swing value by construction (MeasureScope): it is reduced over the
    // session's shot records by the session model, so a per-swing read is refused before the grid is
    // consulted — and refused in words that say so, not as a metric this capture failed to produce.
    if (m->scope == MeasureScope::Session) {
        note(measureId, MissingKind::SessionScope);
        return std::nullopt;
    }

    // A COMPOSITE has no grid reading of its own: it is its parts, combined under their norms by
    // NormMeasureSource, which never asks this source for the composite's id. The only caller that
    // does is one explaining a refusal after the fact, and the refusal a composite meets for want of
    // values is too few parts — so that is the sentence, not "no metric behind it yet".
    if (m->kind == MeasureKind::Composite) {
        note(measureId, MissingKind::CompositeCoverage);
        return std::nullopt;
    }

    if (m->metricKey.isEmpty() && m->preferKeys.isEmpty()) {
        note(measureId, MissingKind::NoMetricBinding);
        return std::nullopt;
    }

    // An empty grid is an unanalysed or unsegmented swing, and reporting each of its measures as
    // "metric not produced" would blame the producers for a swing nothing ever ran over.
    if (m_grid.isEmpty()) {
        note(measureId, MissingKind::NoAnalysis);
        return std::nullopt;
    }

    if (const std::optional<GridReading> v = reduceOverGridReading(m_grid, *m)) {
        note(measureId, MissingKind::None);

        // CONFIDENCE IS 1.0, and that is a gap rather than a claim. The swing doc records a per-
        // phase confidence and the grid does not carry it, so there is no honest per-measure number
        // to report here; inventing one from the phase ladder would put a figure in the ledger that
        // no producer stands behind. The channel is live either way — NormMeasureSource demotes an
        // inferred context through it — so a future grid that keeps phase confidence lands here
        // with nothing else to change.
        Value out{ v->value, 1.0f };
        // The route is provenance, not uncertainty: carried whatever the σ switch says, because the
        // ratio test's same-scale rule refuses a cross-scale quotient with or without a σ on it.
        out.route  = v->route;
        out.direct = v->direct;
        // So is the producer's "drawn but not judged": it is not a σ, and must hold either way.
        out.withheld = v->withheld;
        // The two ends of a change, for a composite's part rows ("address 34° → impact 24°").
        seriesEndpoints(*m, out.atStart, out.atEnd);
        // The measurement σ (session_diagnostics_design.md §A8.3), behind its switch so the off
        // state reproduces the pre-uncertainty source exactly.
        if (m_withSigma) {
            out.sigma     = v->sigma;
            out.grossRisk = v->grossRisk;
            out.sigmaSrc  = v->sigmaSrc;
        }
        return out;
    }

    // reduceOverGrid() returns one nullopt for two situations, and the panel needs them apart. The
    // discriminator is exactly the one the reduction itself uses first: no such metric, or a phase
    // the reducer wanted that the segmenter never found.
    // NOT ONE KEY BUT THE WHOLE LADDER, or a measure that prefers the device would always blame the
    // device. `m_lowPointAhead` tries `lm.lowPointAhead` and then our own `lowPointAhead`: on a
    // camera swing with no monitor the first rung is absent and the second is what actually failed,
    // and "needs a launch monitor" would be the wrong sentence to put in front of that golfer.
    //
    // So: if NO rung is present, the reason comes from the LAST one — the fallback, the rung that
    // was this capture's real chance. If some rung IS present, the metric was produced and the
    // reduction is what came up empty, which is the phase answer below.
    const QStringList ladder = measureKeyLadder(*m);
    bool anyPresent = false;
    for (const QString &key : ladder)
        if (m_grid.metric(key) != nullptr) anyPresent = true;

    if (!anyPresent) {
        const QString &blame = ladder.constLast();
        if (blame.startsWith(kLmPrefix))
            note(measureId, hasLaunchMonitor() ? MissingKind::LaunchMonitorFieldAbsent
                                               : MissingKind::NoLaunchMonitor);
        else
            note(measureId, MissingKind::MetricNotProduced);
        return std::nullopt;
    }

    note(measureId, MissingKind::PhaseNotSegmented);
    return std::nullopt;
}

void LiveMeasureSource::seriesEndpoints(const Measure &m, double &atStart, double &atEnd) const
{
    // Only a reading BETWEEN two instants has two ends. A value read at one phase stays NaN at both:
    // its series may already be address-relative (pelvis thrust), but saying "0 → x" for it would be
    // this source inventing an anchor the measure never names.
    const Reducer &r = m.reducer;
    const bool anchoredExtremum = r.kind == ReducerKind::Extremum && r.anchor.has_value();
    if (!(r.kind == ReducerKind::Delta || anchoredExtremum) || !r.anchor.has_value()) return;

    // The rung that ANSWERED, which is the first in the ladder the reduction itself succeeds on —
    // the same walk reduceOverGridReading() makes, so the ends belong to the value beside them.
    for (const QString &key : measureKeyLadder(m)) {
        const std::optional<double> v = reduceOverGrid(m_grid, key, r);
        if (!v) continue;
        const MetricPhaseGrid *mg = m_grid.metric(key);
        const PhaseGridValue  *a  = mg ? mg->at(*r.anchor) : nullptr;
        if (!a) return;
        atStart = a->value;
        if (r.kind == ReducerKind::Delta) {
            if (const PhaseGridValue *b = mg->at(r.window.second)) atEnd = b->value;
        } else {
            atEnd = a->value + *v;   // the signed deviation, put back on the series' own scale
        }
        return;
    }
}

LiveMeasureSource::GateCause LiveMeasureSource::gateCause(const QString &measureId) const
{
    const Measure *m = m_pack.measure(measureId);
    if (!m || m_grid.gatesVersion <= 0) return {};
    QStringList keys = measureKeyLadder(*m);
    for (const QString &key : keys) {
        // a sequence node's metric speaks through its node; any other metric through itself, or
        // through the cascade that covers it (a refused club track) — whose subject the reason names
        const QString ks = gates::subjectForMetricKey(key);
        const QString subject = ks.isEmpty() ? key : ks;
        if (const GateRecord *r = gates::primaryRecord(m_grid.gates, subject))
            return { r->gate, r->subject, gates::notJudgedClause(r->subject, r->gate) };
    }
    return {};
}

LiveMeasureSource::GateCause LiveMeasureSource::gateForSubject(const QString &subject) const
{
    if (subject.isEmpty() || m_grid.gatesVersion <= 0) return {};
    if (const GateRecord *r = gates::primaryRecord(m_grid.gates, subject))
        return { r->gate, r->subject, gates::notJudgedClause(r->subject, r->gate) };
    return {};
}

MissingKind LiveMeasureSource::missingKind(const QString &measureId) const
{
    return m_reasons.value(measureId, MissingKind::None);
}

QString LiveMeasureSource::missingReason(const QString &measureId) const
{
    return missingReasonText(missingKind(measureId));
}

// ── Identity ────────────────────────────────────────────────────────────────

QString LiveMeasureSource::club() const      { return m_grid.club; }
QString LiveMeasureSource::sessionId() const { return m_grid.sessionId; }
QString LiveMeasureSource::swingDir() const  { return m_grid.swingDir; }

QString LiveMeasureSource::contextId() const { return contextIdForClub(m_grid.club); }

bool LiveMeasureSource::hasLaunchMonitor() const
{
    for (const MetricPhaseGrid &mg : m_grid.metrics)
        if (mg.key.startsWith(kLmPrefix))
            return true;
    return false;
}

// ── The stack ───────────────────────────────────────────────────────────────

LiveDetection detectForSwing(const LiveMeasureSource &values, const CharacteristicPack &pack,
                             const std::shared_ptr<const INormProvider> &norms,
                             const GradePolicy &policy, const Cohort &athlete,
                             const QString &contextIdOverride, const QStringList &signalIds)
{
    LiveDetection out;
    out.club      = values.club();
    out.contextId = contextIdOverride.isEmpty() ? values.contextId() : contextIdOverride;

    // ONE context id feeds both halves: the norm lookup (which corridor judges the number) and the
    // condition bindings (whether this kind of shot is asked the question at all). They were always
    // meant to be the same node, and nothing but this function guarantees it.
    const NormMeasureSource graded(values, norms, out.contextId, policy, &pack, athlete);

    out.result = detect(pack, graded, norms ? &norms->contexts() : nullptr, out.contextId);
    out.signalOutcomes.reserve(size_t(signalIds.size()));
    for (const QString &sid : signalIds) out.signalOutcomes.push_back(evaluateSignal(pack, sid, graded));
    return out;
}

} // namespace pinpoint::analysis
