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

#include <cstdint>
#include <vector>

#include "imu_vision_fuser.h"   // FusedStreams
#include "swing_analysis.h"     // Phase, PhaseEvent, SegmentRole

namespace pinpoint::analysis {

// Swing-phase segmentation v2 (design addendum A.1–A.6): impact is the anchor,
// every other event is found by deterministic backward/forward search from it,
// gated by physiological duration priors — never by scanning forward from the
// window edge (v1's waggle trap). Detectors run on the measured inertials
// (phase_signals) with zero-phase filtering and sub-grid refinement; every
// event carries confidence + segment provenance, and the assembled chain is
// monotone (violators are dropped by confidence, never reordered).
//
// Pure over FusedStreams — standalone-tested in
// src/Analysis/tests/phase_segmenter_test.cpp.

// Thresholds + duration gates (design A.5 defaults; literature priors padded
// generously). All rates °/s, durations µs.
struct SegmentationConfig {
    double  fcEnvelopeHz       = 10.0;     // zero-phase LP for envelopes/rates

    // Stillness (A.4): gyro AND accel quiet across all bound segments.
    double  stillGyroDps       = 15.0;
    double  stillAccelTolG     = 0.08;

    // Top — backward from impact through the positive signed-plane-rate run.
    int64_t topMinBeforeImpactUs = 180000;
    int64_t topMaxBeforeImpactUs = 600000;
    int64_t topImpactSlackUs     = 60000;  // rate may cross zero AT impact (ball reversal)

    // Takeaway — back-chain from Top with hysteresis.
    double  takeawayFracOfPeak = 0.08;     // θ_low = max(frac·Ω_peak, takeawayMinDps)
    double  takeawayMinDps     = 15.0;
    int64_t takeawayQuietUs    = 150000;   // sustained dip that ends the chain
    int64_t backswingMinUs     = 500000;   // gate on Top − Takeaway
    int64_t backswingMaxUs     = 1800000;

    // Address — last sustained stillness before Takeaway.
    int64_t addressStillMinUs  = 300000;

    // Transition — pelvis axial reversal near Top.
    int64_t transBeforeTopUs   = 250000;
    int64_t transAfterTopUs    = 80000;
    double  transMinMeanDps    = 10.0;     // pelvis downswing rate floor

    // Multi-segment voting / cross-checks.
    int64_t voteAgreeUs        = 40000;    // hand vs forearm
    int64_t thoraxAgreeUs      = 60000;    // thorax Top cross-check

    // MaxSpeed search window end.
    int64_t maxSpeedPostImpactUs = 50000;

    // Finish — relaxed quiet sustained after impact.
    double  finishGyroDps      = 30.0;
    int64_t finishMinAfterImpactUs = 200000;
    int64_t finishSustainUs    = 250000;
    int64_t finishMinUs        = 400000;   // gate on Finish − Impact
    int64_t finishMaxUs        = 2000000;

    // Swing-bound pads around Address / Finish (A.6).
    int64_t boundPadUs         = 250000;
};

// The segmentation result (design A.6): the event ladder plus the logical
// swing bounds consumers truncate to. The frozen window itself is never
// trimmed. conf == 0 means "bounds are just the window" — consumers needing
// real truncation (heavy-stage bounding, export trim) must check it.
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

class PhaseSegmenter {
public:
    static Segmentation segment(const FusedStreams &streams, int64_t impactUs,
                                const SegmentationConfig &cfg = {});
};

} // namespace pinpoint::analysis
