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

#include "wrist_assessment_types.h"

#include <array>
#include <cstdint>
#include <vector>

// Wrist Motion assessment engine — Phase 0 input contract.
//
// This is the ONLY surface the engine consumes (design §9, "decoupling and testability"). A live
// adapter over the IK/segmentation producers (Phase 3) and a synthetic fixture source (Phase 0)
// both implement IWristAngleSource; the engine never sees the producers directly. Header-only.
//
// The angle TIME SERIES carries NEUTRAL-relative degrees (same reference as the existing
// MetricSeries.value — vs the calibration neutral), NOT delta-from-address. Δ-from-address
// (value@Pn − value@P1) is derived downstream by the banding tier (Phase 1).

namespace pinpoint::analysis {

constexpr int idx(PpJointDof d)      { return static_cast<int>(d); }
constexpr int idx(PpSwingPosition p) { return static_cast<int>(p); }

// One timestamped per-DOF angle sample as produced upstream.
struct PpJointAngleSample {
    int64_t t_us       = 0;     // absolute µs (matches the producer TimeGrid domain)
    double  valueDeg   = 0.0;   // neutral-relative degrees
    bool    available  = true;  // false = upstream had no usable value at this sample (a gap)
    float   confidence = 1.0f;  // 0..1 per-sample trust (source-aware, design §4)

    // Singularity proxy: |middle/pitch Euler term| in degrees for THIS DOF's decomposition
    // (design §2.3 — only the middle term needs singularity checking). 0 for DOFs read on a
    // non-middle axis (no gimbal risk). The SAMPLER thresholds the windowed value of this against
    // PpWristSamplingConfig::gimbalThresholdDeg to decide Indeterminate.
    double  pitchProxyDeg = 0.0;
};

// A full per-DOF angle time series. `present` distinguishes "this DOF is instrumented for this
// swing" (cf. metric_extractor only emitting forearm/elbow when a LeadUpperArm binding exists)
// from a momentary gap at individual samples. `baseConfidence` is the source-aware floor (design §4).
struct PpJointAngleSeries {
    PpJointDof dof = PpJointDof::LeadWristFlexExt;
    bool       present = false;
    std::vector<PpJointAngleSample> samples;   // ascending t_us
    float      baseConfidence = 1.0f;
};

// Pn timestamps + per-position segmentation confidence (from the segmenter, design §3).
struct PpSwingPositionTimeline {
    struct Entry {
        bool    present = false;
        int64_t t_us    = 0;
        float   conf    = 1.0f;
    };
    std::array<Entry, kNumPos> positions{};
    const Entry &at(PpSwingPosition p) const { return positions[idx(p)]; }
};

// The abstract input seam. Phase 0 wires FixtureWristAngleSource; Phase 3 wires the real adapter.
class IWristAngleSource {
public:
    virtual ~IWristAngleSource() = default;
    virtual PpHandedness handedness() const = 0;
    // nullptr when the DOF has no series for this swing (resolves to a Gap cell).
    virtual const PpJointAngleSeries *series(PpJointDof dof) const = 0;
    virtual PpSwingPositionTimeline timeline() const = 0;
};

// Sampled-cell state. Gap and Indeterminate both render Grey in the UI but are semantically
// distinct: Gap = no data was available; Indeterminate = data existed but the decomposition was
// near gimbal lock so the value is untrustworthy (design §2.3). Never a fabricated value.
enum class PpCellStatus { Ok, Gap, Indeterminate };

struct PpWristAngleCell {
    double       valueDeg   = 0.0;                  // meaningful only when status == Ok
    PpCellStatus status     = PpCellStatus::Gap;
    float        confidence = 0.0f;                 // aggregated; 0 unless Ok
    bool available() const { return status == PpCellStatus::Ok; }
};

// The engine's canonical sampled input: one cell per (DOF, Pn). Always canonical right-handed
// (a left-handed source has already been mirrored). Indexed [dof][position].
struct PpWristAngleSet {
    PpHandedness handedness = PpHandedness::Unknown;   // original handedness, provenance only
    std::array<std::array<PpWristAngleCell, kNumPos>, kNumDof> cells{};

    const PpWristAngleCell &cell(PpJointDof d, PpSwingPosition p) const {
        return cells[idx(d)][idx(p)];
    }
    PpWristAngleCell &cell(PpJointDof d, PpSwingPosition p) {
        return cells[idx(d)][idx(p)];
    }
};

// A generic, directly-settable in-memory IWristAngleSource. Both the live adapter
// (wrist_analysis_adapter — parses shotReplay.analysisDetail into one) and the synthetic test
// fixtures build on it, so production code does not depend on the fixtures header.
class InMemoryWristAngleSource : public IWristAngleSource {
public:
    void setHandedness(PpHandedness h) { m_hand = h; }
    void setTimeline(const PpSwingPositionTimeline &t) { m_timeline = t; }
    void setSeries(const PpJointAngleSeries &s)
    {
        m_series[idx(s.dof)]  = s;
        m_present[idx(s.dof)] = true;
    }

    PpHandedness handedness() const override { return m_hand; }
    const PpJointAngleSeries *series(PpJointDof d) const override
    {
        return m_present[idx(d)] ? &m_series[idx(d)] : nullptr;
    }
    PpSwingPositionTimeline timeline() const override { return m_timeline; }

private:
    PpHandedness            m_hand = PpHandedness::Right;
    PpSwingPositionTimeline m_timeline;
    std::array<PpJointAngleSeries, kNumDof> m_series{};
    std::array<bool, kNumDof>               m_present{};
};

} // namespace pinpoint::analysis
