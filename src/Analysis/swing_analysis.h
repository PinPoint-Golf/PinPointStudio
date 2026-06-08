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

#include <QHash>
#include <QMetaType>
#include <QQuaternion>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

#include "types.h"   // pinpoint::SourceId, kInvalidSourceId

// Canonical intermediate + output data structures for the shot analyzer
// (design: docs/SHOT_ANALYZER_DESIGN.md). All rotation is QQuaternion — Euler
// appears only at the UI readout. Header-only value types, passed by value /
// shared_ptr across the QtConcurrent worker boundary.
namespace pinpoint::analysis {

// Degradation tier, chosen at job-build time from camera-calib + IMU availability.
enum class ReconstructionTier { Angles2D, Mono3DPlusImu, Stereo3D, ClubInstrumented };

// Anatomical segment an IMU is mounted on. Unknown = unmapped placement slot.
enum class SegmentRole {
    Unknown = 0,
    Pelvis, Thorax, T12,
    LeadUpperArm, LeadForearm, LeadHand,
    TrailThigh, LeadThigh,
    Club,
};

// Resolved on the UI thread from AppSettings::imuPlacement + the live ImuInstance
// calibration snapshot (alignA/mountM are session-lifetime on the QObject — the
// worker can never read them, so they are copied into the job here).
struct ImuSegmentBinding {
    SourceId    source = kInvalidSourceId;
    SegmentRole role   = SegmentRole::Unknown;
    QQuaternion alignA;   // fusion-world  -> anatomical-world  (A)
    QQuaternion mountM;   // anatomical-body -> sensor-body     (M)
};

enum class Phase { Address, Takeaway, Top, Transition, Downswing, Impact, Release, Finish };

// A detected swing-phase event on the shared phase timeline.
struct PhaseEvent {
    Phase   phase = Phase::Address;
    int64_t t_us  = 0;
    float   conf  = 1.0f;   // 0..1; low-confidence ticks fade in the UI
};

// One labelled point on a metric's curve at a key swing phase.
struct PhaseSample {
    Phase   phase = Phase::Address;
    int64_t t_us  = 0;
    double  value = 0.0;
    QString band;           // "green"/"yellow"/"red" at scored phases, else ""
};

// A per-frame metric curve over the window's TimeGrid plus sparse phase samples.
struct MetricSeries {
    QString key, label, unit;
    std::vector<int64_t> t_us;            // shared TimeGrid (ascending, absolute us)
    std::vector<double>  value;           // one per t_us — the continuous curve
    std::vector<PhaseSample> phaseSamples;
    std::optional<double> bandLo, bandHi; // ideal/tour band (unit-space, one-sided ok)
    bool flexPositive = true;             // stored-sign polarity (flip only at the label)
};

// A metric scored against its reference band.
struct ScoredMetric {
    QString key;
    double  value    = 0.0;
    double  mu       = 0.0;
    double  sigma    = 1.0;
    bool    oneSided = false;
    double  subScore = 0.0;   // 0..100 after band falloff
    QString band;             // "green"/"yellow"/"red"
    Phase   phase    = Phase::Impact;
    double  weight   = 0.0;
};

// A ranked coaching fault derived from a metric deviation.
struct Fault {
    QString id, title, cause, drill;
    double  pointsLost = 0.0;   // weight * (100 - subScore), the ranking key
    Phase   phase      = Phase::Impact;
};

struct ScoreBreakdown {
    int                overall = 0;       // 0..100, weighted geometric mean
    QHash<QString,int> perRegion;         // "rotation","wrist","tempo",...
    QHash<QString,int> perPhase;
    std::vector<ScoredMetric> metrics;    // full audit trail
};

// The rich detail behind ShotAnalysisResult::detail — the full analyzed swing.
struct SwingAnalysis {
    int tier = static_cast<int>(ReconstructionTier::Angles2D);
    std::vector<MetricSeries> series;
    std::vector<PhaseEvent>   phases;
    ScoreBreakdown            score;
    std::vector<Fault>        faults;
};

} // namespace pinpoint::analysis

Q_DECLARE_METATYPE(std::shared_ptr<pinpoint::analysis::SwingAnalysis>)
