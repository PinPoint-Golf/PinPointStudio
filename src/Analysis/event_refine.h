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

// EventRefine — late-pipeline timeline-event refinement, V1 products-only
// (analysis_pipeline_fusion_architecture_proposal.md P3, event fusion). Mark's
// directive: stop chasing the address chicken-and-egg with upstream grip
// heuristics — fine-tune the events users SEE at the END of the pipeline from the
// finished shaft/ball/pose products, per his definition:
//
//   Address  = the last STATIC point before the CLUBHEAD departs the ball and
//              doesn't come back.
//   Takeaway = the instant of that final departure (the club leaves the ball for
//              good on the way to the top).
//
// Pure over plain value types (ShaftTrack2D / BallTrack2D / Segmentation) — NO
// SwingWindow, NO AnalysisContext, same cv-free contract as head_track.h — so it
// is unit-testable standalone (event_refine_test). The thin AnalysisStage glue
// (EventRefineStage, wrist_analyzer.cpp) slots between RequireProducts and
// BindDetail, mutates ctx.seg, and lets every downstream consumer (HeadTrack /
// FootMetrics addressUs, assessment P1, buildTrace, swing.json, timeline, SwingLab)
// pick the refined times up for free.
//
// EVIDENCE, three tiers (highest AVAILABLE decides; the others corroborate the
// confidence): A — measured θ within departThetaDeg of θ_ball on ShaftMeasured
// frames only (ShaftBallAnchored frames assert at-ball directly, θ = θ_ball by
// construction, excluded from the distance test); B — clubActivity <
// activityQuietSigma (the s0002 rescue, where the true quiet plateau is invisible
// to the grip-only stillness test); C — grip within a small radius of the P1
// fitted grip (always available, lowest authority).
//
// LAST-DEPARTURE / NO-RETURN: the takeaway is the end of the LAST genuine at-ball
// run (duration >= returnHoldMs; shorter = flicker, debounced out) before Top with
// no genuine at-ball run after it. This is exactly what rescues the s0002-class
// fidget where a FIRST-departure tk0 (ball_anchor.cpp) fires on the first fidget
// departure. Refined Address then re-walks addressHoldEndFrame (shaft_positions.h,
// REUSED not re-derived) back from that refined takeaway.
//
// SAFETY CONTRACT (V1): never inserts events (retimes existing ones only); NEVER
// touches Impact (the acoustic-anchored marker contract; all truth swings are
// acoustic-anchored); applies a refinement only when its fused confidence clears
// minConf AND the shift stays within maxShiftS, else the event is left untouched;
// enforces the monotone ladder (refinedAddress <= refinedTakeaway <= Top) with
// abstain-on-violation. refine.enabled=false ⇒ the stage never runs ⇒ ctx.seg is
// byte-identical AND code-path-identical to the pre-refine pipeline.

#include <QVariantMap>
#include <cstdint>

#include "swing_analysis.h"                // ShaftTrack2D, BallTrack2D, Segmentation
#include "analysis_tuning.h"               // tuning::apply
#include "../Core/pp_tuned_constants.h"    // tuned::refine::

namespace pinpoint::analysis {

// EventRefine knobs. Defaults track the frozen constants (pp_tuned_constants.h
// refine::); SwingLab sweeps them via "refine.*" dotted keys. FROZEN ON 2026-07-18
// (V1 evidence freeze, paired with ball::activity::kClubActivity — the load-bearing
// Tier-B input); "refine.enabled" = false still darks the whole stage out, the
// byte- and code-path-identical soak baseline.
struct EventRefineConfig {
    bool    enabled            = tuned::refine::kEnabled;            // refine.enabled (master)
    bool    takeaway           = tuned::refine::kTakeaway;           // refine.takeaway
    bool    address            = tuned::refine::kAddress;            // refine.address
    bool    impactResidual     = tuned::refine::kImpactResidual;     // refine.impactResidual (log-only)
    double  departThetaDeg     = tuned::refine::kDepartThetaDeg;     // refine.departThetaDeg
    double  activityQuietSigma = tuned::refine::kActivityQuietSigma; // refine.activityQuietSigma
    int     returnHoldMs       = tuned::refine::kReturnHoldMs;       // refine.returnHoldMs
    double  minConf            = tuned::refine::kMinConf;            // refine.minConf
    double  maxShiftS          = tuned::refine::kMaxShiftS;          // refine.maxShiftS

    static EventRefineConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        EventRefineConfig c;
        apply(ov, "refine.enabled",            c.enabled);
        apply(ov, "refine.takeaway",           c.takeaway);
        apply(ov, "refine.address",            c.address);
        apply(ov, "refine.impactResidual",     c.impactResidual);
        apply(ov, "refine.departThetaDeg",     c.departThetaDeg);
        apply(ov, "refine.activityQuietSigma", c.activityQuietSigma);
        apply(ov, "refine.returnHoldMs",       c.returnHoldMs);
        apply(ov, "refine.minConf",            c.minConf);
        apply(ov, "refine.maxShiftS",          c.maxShiftS);
        return c;
    }
};

// What a refine pass applied + diagnostics for the log/trace. refined == false and
// version untouched ⇒ the pass abstained (weak evidence / gate failure / no Top /
// monotone violation), which is the byte-identical outcome for a run that
// contributes nothing.
struct EventRefineResult {
    bool    refined         = false;   // any event retimed (⇒ seg.version bumped to 3)
    bool    takeawayRefined = false;
    bool    addressRefined  = false;
    int64_t takeawayUs      = -1;      // applied refined times (−1 = not applied)
    int64_t addressUs       = -1;
    float   conf            = 0.f;     // fused at-ball confidence at the departure frame L
    int     departFrame     = -1;      // L (last-departure frame; −1 = none found ⇒ abstain)
    int     addressFrame    = -1;      // refined Address hold-end frame (addressHoldEndFrame)
    int     tier            = 0;       // deciding tier at L: 0 none / 1 C / 2 B / 3 A
    // P6 residual-first telemetry (log-only; no swing.json field): launch − impact
    // when both exist. Computed regardless of whether any event was refined.
    bool    impactResidualValid = false;
    int64_t impactResidualUs    = 0;
};

// Refine the timeline events in `seg` from the finished shaft/ball products.
// Mutates seg IN PLACE (retimes Takeaway/Address t_us + conf + provenance = Club,
// shifts swingStartUs with the Address delta, bumps version to 3) and returns the
// applied mutations. See the header preamble for the tier/last-departure/safety
// contract. impactUs (job.impactUs) drives the impactResidual telemetry only —
// Impact is never refined.
EventRefineResult refineEvents(Segmentation &seg, const ShaftTrack2D &shaft,
                               const BallTrack2D &ball, int64_t impactUs,
                               const EventRefineConfig &cfg);

} // namespace pinpoint::analysis
