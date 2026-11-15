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

// THE PIPELINE'S ONE REBASE (refactor step 1.5; step 1 design §4.5; information_model.md §7,
// R-ANA-6 "one time base"). The stages run on the analysed window's own clock — the ring clock live,
// already window-relative for a window loaded from disk — and PipelineAnalyzer::analyze takes the
// finished analysis to WINDOW time here, once, with the window's start (ShotAnalysisJob::windowT0Us,
// the Recording's t0Us), and says so (SwingAnalysis::timeBase = Window). Nothing downstream guesses.
//
// WHICH FIELDS. Exactly the instants the per-block writers send through windowTimeUs (swing_doc_
// analysis.cpp and the *_json.h writers beside each producer), by the same rule — so writing the
// rebased analysis as it is gives, byte for byte, the block the writer made from the clock-time
// analysis with its t0 (analysis_block_roundtrip_test [2]) — plus the ladder's events
// (segmentation.events), which the writer carries as `phases[]` and the live replay's Finish clamp
// reads. Durations and offsets (ms from impact, dtUs, a clock offset) are not instants and are left
// alone, as the writers leave them. A field a writer adds through windowTimeUs must be added here.
//
// Vocabulary-level helper beside the rule it applies (windowTimeUs, swing_analysis.h): pure.

#include "swing_analysis.h"

namespace pinpoint::analysis {

namespace rebase_detail {

inline void at(int64_t &t, int64_t t0) { t = windowTimeUs(t, t0); }

template <class Frames>
inline void eachT(Frames &frames, int64_t t0)
{
    for (auto &f : frames) at(f.t_us, t0);
}

} // namespace rebase_detail

// Take `a` from its window's clock to window time (µs from the window's start, `t0Us` on that
// clock) and stamp it Window. An analysis already in window time is left as it is.
inline void rebaseToWindowTime(SwingAnalysis &a, int64_t t0Us)
{
    using rebase_detail::at;
    using rebase_detail::eachT;
    if (a.timeBase == TimeBase::Window)
        return;
    const int64_t t0 = t0Us;

    // metrics[]: the grid and each phase sample
    for (MetricSeries &m : a.series) {
        for (int64_t &t : m.t_us) at(t, t0);
        eachT(m.phaseSamples, t0);
    }
    // phases[] and the ladder they are; the segmentation bounds
    eachT(a.phases, t0);
    eachT(a.segmentation.events, t0);
    at(a.segmentation.swingStartUs, t0);
    at(a.segmentation.swingEndUs, t0);
    // kinematicSequence: impact and each node's peak (the bounds are ms from impact)
    at(a.kinematicSequence.impactUs, t0);
    for (KsNode &n : a.kinematicSequence.nodes) at(n.tPeakUs, t0);
    // pose2d, poseDtl: frames, the smoothed companion, the dense synth
    for (PoseTrack2D *p : { &a.pose2d, &a.poseDtl }) {
        eachT(p->frames, t0);
        eachT(p->smoothed, t0);
        eachT(p->smoothedSynth, t0);
    }
    // club: samples, the predicted and synth tiers, the positions, the wedge observations and the
    // two ladder instants it used
    {
        ShaftTrack2D &s = a.shaft;
        eachT(s.samples, t0);
        eachT(s.predicted, t0);
        eachT(s.synth, t0);
        eachT(s.positions, t0);
        eachT(s.wedgeObs, t0);
        at(s.onsetTUs, t0);
        at(s.topTUs, t0);
    }
    // ball: frames and the launch
    eachT(a.ball.frames, t0);
    at(a.ball.launchTUs, t0);
    // impact: frames, the ball's leaving and the head's path
    eachT(a.impact.frames, t0);
    at(a.impact.ballLeaveTUs, t0);
    eachT(a.impact.path, t0);
    // clubDtl: samples, the bands and the 3-D synth
    eachT(a.shaftDtl.samples, t0);
    for (DtlBand &b : a.shaftDtl.bands) { at(b.loUs, t0); at(b.hiUs, t0); }
    eachT(a.shaftDtl.synth3d, t0);
    // club3d: samples and the η(t) knots
    eachT(a.shaft3d.samples, t0);
    for (int64_t &t : a.shaft3d.eta.knotsUs) at(t, t0);
    // skeleton3d: the frame grid
    for (int64_t &t : a.skeleton3d.t_us) at(t, t0);
    // addressMarks: each view's frame and each DTL position's frame and instant
    at(a.addressMarks.faceOn.frameTUs, t0);
    at(a.addressMarks.dtl.frameTUs, t0);
    for (addressmarks::DtlPhaseMarks &p : a.addressMarks.dtlPhases) {
        at(p.v.frameTUs, t0);
        at(p.eventTUs, t0);
    }

    a.timeBase = TimeBase::Window;
}

} // namespace pinpoint::analysis
