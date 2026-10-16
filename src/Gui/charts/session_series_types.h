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

// THE CHARTS PANEL'S SESSION DATA — what every shot of a session contributes to the Charts panel
// (docs/design/charts_panel_traces_relations.md §6.1), as plain value types.
//
// Two halves meet here and nowhere else:
//   · SessionSeriesModel (session_series_model.h) FILLS these, off the GUI thread, from each
//     swing's analysis block. It holds the series, phases, sequence and diagnostics only — never
//     pose, never the rest of the analysis blob (the 28 MB document froze the GUI once).
//   · The session maths (session_traces.h, session_relations.h) READS them and nothing else, so it
//     can be tested on the 4 July fixture with no Qt Quick, no files and no threads.
//
// TIME. Every t_us here is on the SHOT'S OWN window-relative clock, the same clock shotReplay
// uses (disk_replay_source.cpp relUs: analysis t_us minus clock.t0_us), so a time read from a
// shot can be handed to shotReplay.seekToUs unchanged. impactUs is on that clock too. The maths
// works in ms from impact; converting is its job, not the model's.
//
// A shot is IMMUTABLE once published: the model swaps in a new shared_ptr when a shot is
// re-analysed, so a reader holding a SessionSnapshot never sees a shot change under it.

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace pinpoint::charts {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// One phase event (analysis.phases[]). `phase` is the Phase enum id (swing_analysis.h);
// TimelineLabels maps ids to P tags (Top = 2, P3 = 8, Impact = 5).
struct PhaseMark {
    int    phase = -1;
    qint64 tUs   = 0;
    double conf  = kNaN;
};

// A metric's value at one P-position (series.phaseSamples[]): the reading a card shows.
struct PhaseSample {
    int     phase = -1;
    qint64  tUs   = 0;
    double  value = kNaN;
    double  sigma = kNaN;   // NaN when the producer characterised none
    QString band;
};

// One metric of one shot (analysis.metrics[]).
struct MetricSeries {
    QString key;
    QString label;
    QString unit;
    std::vector<qint64>  tUs;     // ascending; empty or size 1 for an instant-only metric
    std::vector<double>  value;   // parallel to tUs
    std::vector<uint8_t> valid;   // parallel to tUs, 0 = bridged; EMPTY means every sample valid
    double sigma     = kNaN;      // series σ, NaN when absent
    int    sigmaKind = -1;        // -1 when absent
    std::vector<PhaseSample> phaseSamples;

    bool hasCurve() const { return tUs.size() >= 2; }
    bool isValid(size_t i) const { return valid.empty() || (i < valid.size() && valid[i] != 0); }
};

// The shot's diagnostics row (session diagnostics ledger, diagnostics.json).
struct ShotDiagnostics {
    struct Value {
        double value = kNaN, z = kNaN, lo = kNaN, hi = kNaN;
        int    dir   = 0;
        // ADDED 15 Oct (additive): the row's own detector confidence (ConditionRow::confidence), NaN
        // when the row carried none. The Wrist tab's findings show it; the wrist domain score
        // carries it on each verdict but never weighs by it (src/Diagnostics/engine/wrist_domain_score.h).
        double confidence = kNaN;
    };
    QString     club;
    bool        warmUp      = false;
    bool        dataWarning = false;
    QStringList fired;                    // condition ids, sorted
    QStringList clean;                    // condition ids, sorted
    QHash<QString, Value> values;         // conditionId → graded value
    // ADDED 16 Oct (ledger schema 8): the findings a gate kept from being judged on this shot, by the
    // reading the gate acted on — "ks.pelvis" → ["Late pelvis rotation", …] (condition labels, then the
    // sequence checks' signal ids where no label exists). The Trust panel lists them under the reading.
    QHash<QString, QStringList> notJudgedBy;

    // ADDED 14 Oct (additive; nothing above changed). Which reading drove each FIRED condition on
    // this shot: the ledger row's drivingMeasureId, resolved through the pack's condition link
    // (src/Diagnostics/engine/condition_reading.h). `measureId` is the ledger's own id, EMPTY when
    // the row named none; metricKey/phase are then the condition's first reading with a key.
    // phase is the Phase enum id (-1 none); metricKey "" when the reading has no curve key.
    struct Reading {
        QString measureId;
        QString metricKey;
        int     phase = -1;
        int     reducer = -1;   // ADDED: as ConditionInfo::reducer
    };
    QHash<QString, Reading> drivenBy;     // conditionId → reading; fired conditions only

    // ADDED 14 Oct (K4). The kinematic sequence checks, "Sequence checks: n of m", in the shape
    // Coaching/sequence_checks.h sequenceChecksToVariant() publishes — read off the ledger's own
    // per-signal verdicts (schema 5), never re-derived here. Empty when the row carries none.
    QVariantMap sequenceChecks;
};

struct ShotSeries {
    int     ordinal  = 0;     // 1..n in strike order within the session
    int     shotId   = -1;    // the session's shot id (ledger / carousel identity)
    QString swingDir;         // absolute swing directory (carousel identity)
    QString club;             // the swing's own club label
    qint64  impactUs = -1;    // window-relative µs; -1 when the swing has no impact
    std::vector<PhaseMark> phases;
    QHash<QString, MetricSeries> metrics;      // key → series
    QStringList metricOrder;                   // keys in the swing's own order
    QVariantMap kinematicSequence;             // the analysisDetail.kinematicSequence shape (ChartMetrics reads it)
    // analysis.gates (Analysis/gate_record.h) as stored — evidence times are ms from impact, so no
    // re-timing. gatesVersion 0 = analysed before records existed (the reader says so and falls back).
    QVariantList gates;
    int          gatesVersion = 0;
    ShotDiagnostics diagnostics;
    // ADDED 15 Oct (build/wrist-tab/CONTRACT.md I3): the wrist engine's checkpoint profile of this
    // shot, as session_wrist.h shotWristPayload() publishes it — EMPTY when the shot carries no wrist
    // DOF. Graded against the session's archetype (SessionSeriesModel::wristArchetype; -1 = each shot
    // resolves its own), recomputed from `metrics` + `phases` above when that changes, so no analysis
    // blob is held for it. Shape: session_wrist.h.
    QVariantMap wrist;

    const MetricSeries *metric(const QString &key) const {
        auto it = metrics.constFind(key);
        return it == metrics.constEnd() ? nullptr : &it.value();
    }
};

// A diagnostics condition and, when the catalogue knows it, the reading it grades
// (design §6.4: the link from a condition to { metricKey, phase }; phase -1 = unknown). Filled by
// SessionSeriesModel from condition_reading.h: the reading the session's fired rows name most
// often (ties → detectedBy order), else the condition's first reading with a key.
struct ConditionInfo {
    QString name;
    QString group;
    QString metricKey;
    int     phase = -1;
    // ADDED (additive): how the reading is reduced to one number, ReducerKind's value
    // (src/Metrics/metric_reducer.h): 0 At — the value AT `phase`; 1 Delta — the change over a window
    // ENDING at `phase`; 2 Rate — that change per second; 3 Extremum — the max/min over a window
    // ending at `phase` ("peak before P7": the peak is somewhere inside, not at `phase`).
    // -1 when there is no reading (a latent condition, or one the pack no longer holds).
    int     reducer = -1;
};

// What the panel reads: an immutable snapshot of the session.
struct SessionSnapshot {
    QString sessionDir;
    std::vector<std::shared_ptr<const ShotSeries>> shots;   // strike order, ordinal = index + 1
    QHash<QString, ConditionInfo> conditions;                // every condition that fired on some shot
    // ADDED 15 Oct: the session tier of EVERY condition the ledger holds — "clean" | "watching" |
    // "pattern" — from the ledger's own reduction (diagnostic_ledger.h conditionLedgers, over the
    // options the file was written with), so the Wrist tab says what Session diagnostics says.
    QHash<QString, QString> tiers;
    quint64 revision = 0;                                    // bumps on every change

    int count() const { return int(shots.size()); }
};

} // namespace pinpoint::charts
