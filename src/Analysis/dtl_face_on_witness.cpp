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

#include "dtl_face_on_witness.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "shaft_track_shared.h"   // shaftshared::unwrap
#include "../Core/pp_debug.h"

namespace pinpoint::analysis {

// Lifted from swinglab_run's --dtl block (2026-09-21) — SwingLab calls this now, and
// its club_dtl.json not moving by a byte is the gate that says the lift was verbatim.
FaceOnWitness buildFaceOnWitness(const ShaftTrack2D& fo, const ShaftDecideTrace* trace,
                                 int64_t impactUs)
{
    FaceOnWitness wit;
    std::vector<double> thetaRaw;
    std::vector<double> visLen, visLenAll;
    wit.tUs.reserve(fo.samples.size());
    for (const ShaftSample2D &s : fo.samples) {
        wit.tUs.push_back(s.t_us);
        thetaRaw.push_back(s.thetaRad);
        wit.thetaDotRadS.push_back(s.thetaDotRadS);
        // The face-on grip's IMAGE ROW (px, NOT normalised — the DTL side fits
        // y_D = a·y_F + b in pixels). Both cameras see vertical, so this one column
        // is a free cross-view witness on the DTL anchor, and it is what catches the
        // post-impact invented hands (§5.2) — hand confidence will not, and will not
        // say so.
        wit.gripYPx.push_back(s.gripPx.y());
        if ((s.flags & ShaftMeasured) && s.visibleLenPx > 0.0) {
            visLenAll.push_back(s.visibleLenPx);
            // In the face-on IMAGE PLANE only: a near-horizontal shaft (P2, the top,
            // P6) has both ends at the hands' depth. The near-vertical frames do not —
            // at address and impact the head is ~0.5 m nearer the lens than the hands
            // and perspective reads the club 10–17 % long (design §4.2).
            if (std::abs(std::cos(s.thetaRad)) >= 0.94)
                visLen.push_back(s.visibleLenPx);
        }
    }
    // Too few in-plane samples to trust a percentile ⇒ the whole series.
    if (visLen.size() < 8) visLen = visLenAll;
    // NOT redundant: ShaftSample2D::thetaRad is the DP's θ wrapped to [0, 2π)
    // (shaft_track_assembly reconcile: fmod(...,360)), whatever swing_analysis.h's
    // comment says — the corpus tracks show ~6.1 rad steps across the branch cut.
    // Interpolating those in `at()` would sweep the witness the wrong way round the
    // circle.
    wit.thetaUnwrapRad = shaftshared::unwrap(thetaRaw);
    // ρ_F's denominator: the p90 of the MEASURED, IN-PLANE visible length. Not the
    // max — one blurred over-long ridge would otherwise set the scale for the whole
    // swing — and not the whole series: a p95 over frames that include the magnified
    // address hold came out 328–351 px against an in-plane 290–321 on four of six
    // swings, which turned ρ_F 1.00 into 0.86 at P2/P6 and ρ̂_D 0.00 into 0.51. The
    // end-on gap never opened and the solve ran one band straight through the top.
    if (!visLen.empty()) {
        std::sort(visLen.begin(), visLen.end());
        const double pos = 0.90 * double(visLen.size() - 1);
        const size_t lo = size_t(std::floor(pos));
        const size_t hi = std::min(lo + 1, visLen.size() - 1);
        wit.fullLenPx = visLen[lo] + (pos - double(lo)) * (visLen[hi] - visLen[lo]);
    }
    // ρ_F ONLY where visibleLenPx is a club length. On a non-measured sample the
    // field carries a frame-edge clamp, not a shaft, and dividing it by the p95 gave
    // ρ_F ≫ 1 → 1 − u_x² < 0 → a NaN ρ̂_D that every downstream test read as
    // "sighted", which is the exact inversion of the schedule's job. NaN says
    // unknown, out loud.
    wit.rhoF.reserve(fo.samples.size());
    for (const ShaftSample2D &s : fo.samples)
        wit.rhoF.push_back(((s.flags & ShaftMeasured) && wit.fullLenPx > 0.0)
                               ? std::min(1.0, s.visibleLenPx / wit.fullLenPx)
                               : std::numeric_limits<double>::quiet_NaN());
    // Tier + phase from the same run's trace, which emits one entry per emitted
    // frame in the same order as its samples. If the two ever disagree in length the
    // trace is not this track's, and the flags answer instead (see the header).
    const bool traceAligned = trace
                           && trace->frameIdx.size() == fo.samples.size()
                           && trace->tier.size() == fo.samples.size();
    wit.tier.assign(fo.samples.size(), FoTier::Pred);
    wit.phase.assign(fo.samples.size(), -1);
    if (traceAligned) {
        for (size_t i = 0; i < fo.samples.size(); ++i) {
            const int t = trace->tier[i];
            if (t >= 0 && t <= 5) wit.tier[i] = FoTier(uint8_t(t));
            const int f = trace->frameIdx[i];
            if (f >= 0 && f < int(trace->phases.phase.size()))
                wit.phase[i] = int(trace->phases.phase[size_t(f)]);
        }
    } else {
        if (trace && !fo.samples.empty())
            ppWarn() << "[DtlWitness] trace/sample counts differ (" << qlonglong(trace->frameIdx.size())
                     << "vs" << qlonglong(fo.samples.size()) << ") — witness tiers from the sample flags";
        for (size_t i = 0; i < fo.samples.size(); ++i) {
            const uint16_t f = fo.samples[i].flags;
            if (f & ShaftCoasted) continue;
            if (f & ShaftMeasured)   wit.tier[i] = FoTier::Ray;
            else if (f & ShaftWedge) wit.tier[i] = FoTier::Wedge;
        }
    }
    for (const ShaftPosition &p : fo.positions)
        wit.ladder.push_back({ p.p, p.t_us });
    if (wit.tUs.size() > 1) {
        std::vector<int64_t> d;
        d.reserve(wit.tUs.size() - 1);
        for (size_t i = 1; i < wit.tUs.size(); ++i) d.push_back(wit.tUs[i] - wit.tUs[i - 1]);
        std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
        wit.frameIntervalUs = d[d.size() / 2];
    }
    wit.impactUs = impactUs;
    if (trace) wit.chir = trace->chir;
    return wit;
}

} // namespace pinpoint::analysis
