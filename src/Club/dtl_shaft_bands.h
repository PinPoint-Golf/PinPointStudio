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

// The sighted-band rule (dtl_shaft_tracker_design.md §5.8, and the band-EDGE
// rule of dtl_continuous_track_design_update.md §3.1), pure std so it is
// unit-testable without a frame, a witness or OpenCV.
//
// A band is a maximal run of sighted frames at least minBandFrames long. A
// shorter run is a flicker in ρ̂_D at an end-on edge and is not solved — unless
// the edge rule admits it, on either of two grounds:
//
//  (a) NEXT TO A FULL BAND: the run is at least edgeMinFrames long and within
//      edgeMaxGap frames of a full band with NO end-on frame in the hole between.
//      The hole is then a quarantined or undecodable frame or two, not the
//      schedule saying the club has gone end-on: the run is the band's own edge
//      that a pose flicker broke off.
//  (b) WELL SIGHTED IN ITS OWN RIGHT: the run is at least edgeMinFrames long and
//      the MEDIAN of its own ρ̂_D is at or above the caller's margin (rhoOk[i]
//      per frame). The six-frame minimum exists for a run whose own ρ̂_D sits AT
//      rhoSolveMin — a flicker of the threshold; a five-frame run at ρ̂_D 0.95
//      is not that, it is a fully sighted club whose NEIGHBOURS flickered.
//      MEASURED, 07-04 s7 with pinned poses: the whole impact band was two such
//      runs (ρ̂ 0.73–0.97 and 0.58–0.99) separated by three end-on frames, and
//      the old rule published nothing at impact.
//
// Either way the run becomes a band of its own — solved independently, joined
// to nothing; the Held tier never crosses the hole. "Full band" is decided
// first, over the whole sequence, so a chain of short runs cannot admit each
// other. Bands come back sorted by frame index.

#include <cstddef>
#include <vector>

namespace pinpoint::analysis::dtlbands {

struct Run {
    int  lo = -1, hi = -1;   // inclusive
    bool edge = false;       // admitted by the edge rule rather than by length
};

struct Refused {
    int lo = -1, hi = -1;    // a sighted run that became no band
};

struct Result {
    std::vector<Run>     bands;
    std::vector<Refused> refused;
};

// sighted[i] != 0 ⇒ the frame is solvable; endOn[i] != 0 ⇒ the schedule called
// the frame end-on (only consulted inside a hole); rhoOk[i] != 0 ⇒ the frame's
// own ρ̂_D clears the edge rule's margin (ground (b); an empty vector disables
// it). All n long.
inline Result sightedRuns(const std::vector<char>& sighted, const std::vector<char>& endOn,
                          int minBandFrames, bool edgeEnabled, int edgeMinFrames, int edgeMaxGap,
                          const std::vector<char>& rhoOk = {})
{
    Result out;
    const int n = int(sighted.size());
    std::vector<Run> runs;
    for (int i = 0; i < n; ) {
        if (!sighted[size_t(i)]) { ++i; continue; }
        int j = i;
        while (j + 1 < n && sighted[size_t(j + 1)]) ++j;
        runs.push_back(Run{ i, j, false });
        i = j + 1;
    }
    std::vector<char> full(runs.size(), 0);
    for (size_t r = 0; r < runs.size(); ++r)
        full[r] = (runs[r].hi - runs[r].lo + 1) >= minBandFrames ? 1 : 0;

    const auto holeClean = [&](int a, int b) {   // frames a..b exclusive of both ends
        for (int k = a + 1; k < b; ++k)
            if (k >= 0 && k < int(endOn.size()) && endOn[size_t(k)]) return false;
        return true;
    };
    for (size_t r = 0; r < runs.size(); ++r) {
        if (full[r]) { out.bands.push_back(runs[r]); continue; }
        bool admit = false;
        if (edgeEnabled && (runs[r].hi - runs[r].lo + 1) >= edgeMinFrames) {
            if (r > 0 && full[r - 1]) {
                const int gap = runs[r].lo - runs[r - 1].hi - 1;
                if (gap <= edgeMaxGap && holeClean(runs[r - 1].hi, runs[r].lo)) admit = true;
            }
            if (!admit && r + 1 < runs.size() && full[r + 1]) {
                const int gap = runs[r + 1].lo - runs[r].hi - 1;
                if (gap <= edgeMaxGap && holeClean(runs[r].hi, runs[r + 1].lo)) admit = true;
            }
            if (!admit && int(rhoOk.size()) == n) {
                int ok = 0;
                const int len = runs[r].hi - runs[r].lo + 1;
                for (int k = runs[r].lo; k <= runs[r].hi; ++k) if (rhoOk[size_t(k)]) ++ok;
                if (2 * ok >= len + (len % 2 == 0 ? 0 : 1)) admit = true;   // median ⇒ more than half
            }
        }
        if (admit) { Run e = runs[r]; e.edge = true; out.bands.push_back(e); }
        else       out.refused.push_back(Refused{ runs[r].lo, runs[r].hi });
    }
    return out;
}

} // namespace pinpoint::analysis::dtlbands
