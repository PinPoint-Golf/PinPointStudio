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
#include <QPointF>
#include <QQuaternion>
#include <QString>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "types.h"   // pinpoint::SourceId, kInvalidSourceId

// Canonical intermediate + output data structures for the shot analyzer
// (design: docs/design/shot_analyzer_design.md). All rotation is QQuaternion — Euler
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

// Canonical placement-slot → anatomical role mapping — the single source of truth,
// shared by the swing exporter (analysis.bindings[].role + stream device.role) and
// the data viewer's settings fallback. Only Wrist Motion (sessionType 1) is mapped
// today; other session types resolve to Unknown until their placement UX lands.
inline SegmentRole segmentRoleForSlot(int sessionType, const QString &slot)
{
    if (sessionType == 1) {            // Wrist Motion
        if (slot == QLatin1String("A")) return SegmentRole::LeadForearm;
        if (slot == QLatin1String("B")) return SegmentRole::LeadHand;
        if (slot == QLatin1String("C")) return SegmentRole::LeadUpperArm;
    }
    return SegmentRole::Unknown;
}

// Stable string name for a role — written to swing.json (stream device.roleName,
// analysis.bindings[].roleName) and consumed by SwingLab / future post-hoc tools.
// Stable across enum renumbering; pair it with the int role value.
inline QString segmentRoleName(SegmentRole r)
{
    switch (r) {
    case SegmentRole::Pelvis:       return QStringLiteral("Pelvis");
    case SegmentRole::Thorax:       return QStringLiteral("Thorax");
    case SegmentRole::T12:          return QStringLiteral("T12");
    case SegmentRole::LeadUpperArm: return QStringLiteral("LeadUpperArm");
    case SegmentRole::LeadForearm:  return QStringLiteral("LeadForearm");
    case SegmentRole::LeadHand:     return QStringLiteral("LeadHand");
    case SegmentRole::TrailThigh:   return QStringLiteral("TrailThigh");
    case SegmentRole::LeadThigh:    return QStringLiteral("LeadThigh");
    case SegmentRole::Club:         return QStringLiteral("Club");
    case SegmentRole::Unknown:      break;
    }
    return QStringLiteral("Unknown");
}

// Resolved on the UI thread from AppSettings::imuPlacement + the live ImuInstance
// calibration snapshot (alignA/mountM are session-lifetime on the QObject — the
// worker can never read them, so they are copied into the job here).
struct ImuSegmentBinding {
    SourceId    source = kInvalidSourceId;
    SegmentRole role   = SegmentRole::Unknown;
    QQuaternion alignA;   // fusion-world  -> anatomical-world  (A)
    QQuaternion mountM;   // anatomical-body -> sensor-body     (M)
    // Calibration status snapshot (ImuInstance) — persisted so SwingLab can
    // filter a corpus by calibration provenance. `calibrated` is the composite
    // gate (anatCalibrated AND both mount checks within threshold).
    bool        anatCalibrated = false;
    bool        calibrated     = false;
    double      mountDeviationDeg    = 0.0;
    double      mountGravityErrorDeg = 0.0;
    QString     calibratedAtUtc;       // ISO8601 wallclock; empty = never calibrated
    double      calibAgeSec = -1.0;    // age at shot time; -1 = never calibrated
};

// APPEND-ONLY: phases persist as raw ints in swing.json and QML compares the
// ints directly (e.g. PpMetricGraph's `phase === 5`), so existing values must
// keep their positions. v2 ladder additions start at 8 (design addendum A.3).
enum class Phase {
    Address, Takeaway, Top, Transition, Downswing, Impact, Release, Finish,
    MidBackswing  = 8,    // P3 — lead arm parallel (backswing)
    Delivery      = 9,    // P6 — shaft parallel (downswing); hand-proxy until shaft-measured
    MaxSpeed      = 10,   //      peak hand angular speed (clubhead-speed proxy)
    FollowThrough = 11,   // P9 — lead arm parallel (follow-through)
};

// A detected swing-phase event on the shared phase timeline.
struct PhaseEvent {
    Phase   phase = Phase::Address;
    int64_t t_us  = 0;
    float   conf  = 1.0f;   // 0..1; low-confidence ticks fade in the UI
    // Which segment the event was measured from (provenance, design A.2.5);
    // Club once the shaft refinement lands. Unknown for anchors (Impact).
    SegmentRole provenance = SegmentRole::Unknown;
};

// The segmentation result (design addendum A.6/C.5): the event ladder plus
// the logical swing bounds consumers truncate to (export encode span, replay
// span, metric grid, heavy-stage scan bounds). The frozen window itself is
// never trimmed. conf == 0 means "bounds are just the window" — consumers
// needing real truncation must check it. Canonical home here (Qt-only) so it
// can ride SwingAnalysis and the jobs without pulling the segmenter header.
struct Segmentation {
    std::vector<PhaseEvent> events;        // time-ordered, monotone ladder
    int64_t swingStartUs = 0;              // Address − pad, clamped to coverage
    int64_t swingEndUs   = 0;              // Finish  + pad, clamped to coverage
    float   conf         = 0.0f;           // min over {Address, Top, Impact, Finish}
    int     version      = 2;              // 3 once the shaft refinement ran

    const PhaseEvent *eventFor(Phase p) const {
        for (const PhaseEvent &e : events)
            if (e.phase == p) return &e;
        return nullptr;
    }
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

// ── ShaftTracker camera tracks (canonical output shapes) ────────────────────
// Produced by PoseRunner (S0) and ShaftTracker (S2) over the face-on camera;
// persisted in swing.json and replay-overlaid in PpCameraFrame (S4). Kept here
// (Qt-only) so consumers of SwingAnalysis never pull the OpenCV-typed
// detection headers.

// One offline-posed frame: 17 COCO keypoints + both hands' centroid anchors,
// all normalized 0..1 frame coordinates (PoseResult convention).
struct PoseFrame2D {
    int64_t t_us = 0;
    std::array<QPointF, 17> kp{};
    std::array<float, 17>   conf{};
    QPointF leadHand, trailHand;     // hand centroids; COCO wrists on fallback
    float   handConf = 0.f;          // 0 when wrist-fallback
};

struct PoseTrack2D {
    pinpoint::SourceId camera = pinpoint::kInvalidSourceId;
    std::vector<PoseFrame2D> frames;
};

enum ShaftSampleFlags : uint8_t {
    ShaftMeasured      = 0x01,  // vision measurement fused at this sample
    ShaftImuBridged    = 0x02,  // IMU channel fused (no vision this sample)
    ShaftCoasted       = 0x04,  // predict-only (neither channel)
    ShaftWedge         = 0x08,  // vision measurement was a blur-wedge centroid
    ShaftHeadProjected = 0x10,  // headPx projected from grip + L·dir(θ), not measured
};

struct ShaftSample2D {
    int64_t t_us         = 0;
    QPointF gripPx;             // anchor used for detection (image px)
    QPointF headPx;             // measured terminus blob, or projected (see flags)
    double  thetaRad     = 0.0; // unwrapped, RTS-smoothed image angle (atan2 convention)
    double  thetaDotRadS = 0.0; // smoothed angular velocity
    double  visibleLenPx = 0.0; // ridge extent (median/hold-filtered — θ is the precision channel)
    float   conf         = 0.f; // 0..1 from the smoothed θ posterior variance
    uint8_t flags        = 0;
};

struct ShaftTrack2D {
    pinpoint::SourceId camera = pinpoint::kInvalidSourceId;
    bool  valid = false;        // coverage gate over the swing span (all-or-nothing for consumers)
    float coverage = 0.f;       // fraction of span frames Measured|ImuBridged
    float imuVisionCorr = 0.f;  // Pearson corr of vision vs IMU θ̇ (0 = no channel) — health metric
    int   frameWidth  = 0;      // camera dims so px samples can be normalized by consumers
    int   frameHeight = 0;
    std::vector<ShaftSample2D> samples;
};

// The IMU→segment binding as persisted in swing.json (keyed by the device
// serial, which is stable across runs — SourceIds are not). Lets the SwingLab
// offline runner re-fuse a recorded swing with the exact calibration the app
// used (alignA/mountM are session-lifetime and otherwise unrecoverable).
struct BindingRecord {
    QString     serial;                       // FormatDescriptor::device_serial
    SegmentRole role = SegmentRole::Unknown;
    QQuaternion alignA, mountM;
    // Calibration status at shot time (mirrors ImuSegmentBinding; see there).
    bool        anatCalibrated = false;
    bool        calibrated     = false;
    double      mountDeviationDeg    = 0.0;
    double      mountGravityErrorDeg = 0.0;
    QString     calibratedAtUtc;
    double      calibAgeSec = -1.0;
};

// The rich detail behind ShotAnalysisResult::detail — the full analyzed swing.
struct SwingAnalysis {
    int tier = static_cast<int>(ReconstructionTier::Angles2D);
    std::vector<MetricSeries> series;
    std::vector<PhaseEvent>   phases;
    ScoreBreakdown            score;
    std::vector<Fault>        faults;
    // Swing bounds + ladder meta (phases above IS segmentation.events — the
    // doc writer persists only the bounds/conf/version from here).
    Segmentation              segmentation;
    // IMU calibration snapshot per bound device (empty when no IMUs).
    std::vector<BindingRecord> bindings;
    PoseTrack2D               pose2d;  // face-on offline pose (empty when no camera ran)
    ShaftTrack2D              shaft;   // face-on club track (check .valid before use)
};

} // namespace pinpoint::analysis

Q_DECLARE_METATYPE(std::shared_ptr<pinpoint::analysis::SwingAnalysis>)
