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

// DTL deciding half, part 1 — evidence to a solved θ per sighted band
// (dtl_shaft_tracker_design.md §5.4–§5.8). SwingWindow-free, like decideTrack:
// pure over a FrameSource and plain vectors, so every stage is testable against
// synthetic frames.
//
// It does NOT call decideTrack(). That function IS the face-on decision layer —
// phase model, C2 schedule, ψ reconcile, the `verifiable` clause — and §2 records
// what it does when pointed at this view: coverage 0.77, VALID, a measured-tier
// line along the lead forearm through the whole top of the backswing.
//
// Order of business, per sighted band (§5.4–§5.8):
//   witness ─► ρ̂_D(t) ─► anchor quarantine ─► sighted bands ─► phase-aware clean
//   plate ─► per frame E2 on three channels (motion / local-contrast / raw) + the
//   E1 band lock ─► emission D1–D6 + the corridor ─► viterbiBanded per band.
//
// Two rules the whole file turns on, and neither is defensive coding:
//   · a NaN ρ̂_D is NOT sighted. Every comparison against NaN is false, so the
//     test is std::isfinite FIRST (dtl_shaft_types.h says this twice for a
//     reason), and the rate bound divides by max(ρ̂_D, rhoSolveMin), never by a
//     ρ̂_D that may be a stub's difference of near-1 numbers.
//   · evidence over the lit simulator screen must be POLARITY-FREE. The shaft
//     alternates black and white bands on mid-grey; a signed bright-ridge
//     response cancels along it and a wide bright forearm wins. That is why the
//     local-contrast channel exists and why it is not an optimisation.

#include <cstdint>
#include <vector>

#include "dtl_shaft_config.h"
#include "dtl_shaft_types.h"
#include "shaft_track_assembly.h"   // FrameSource

namespace pinpoint::analysis {

// Emission → per-band Viterbi over the 1° θ grid. `witness` is null in truthOnly
// mode (§6) and the solve must then run on DTL evidence alone: the corridor, the
// visibility schedule and the inherited span are all switched off, not defaulted.
// `bandsMm` are the retro-band centres from the club record (empty = unmarked);
// `clubLenMm` the butt→sole length.
// ── the DTL ball (§4.3) ──────────────────────────────────────────────────────
// Declared here, and separately testable, because the golf prior that validates
// it is a DIFFERENT SENTENCE in this view: face-on the ball is between the feet
// and below the ankle line; down the line it is beyond the toe line on the side
// the golfer faces, and FARTHER from the ankles than the grip is. §2 finding 5 is
// what happens when the face-on sentence is ported.
//
// `addrFrames` are the address-hold frame indices (before P1 − 100 ms) whose
// median image the search runs on; `postFrames` are frames after impact + 120 ms,
// used for the permanence-with-launch test — a static white thing that never
// leaves (the alignment stick's end, a mat marking) is not the ball. 0 or more
// than 1 surviving candidate ⇒ found = false with the reason recorded, because a
// ball detector that guesses between two is worse than one that abstains.
//
// ── and a SECOND cue, for the mat the first one cannot see a ball on ─────────
// MEASURED, 06-11, nine swings: the mat images at 253–254 under the ball, the
// ball is white, and the bright cue reports "no bright compact blob" about a
// ball that genuinely has no edge. What the scene does carry is the ball's
// CONTACT SHADOW — a crisp dark crescent at its lower rim. Two more frame sets
// are needed for it, and the reason each is what it is matters:
//   · `clubAwayFrames` — the same "club above the waist" window the phase-aware
//     clean plate uses (P2 + 40% of P2→P4, to P5). NOT the address hold: at
//     address the clubhead AND its own shadow sit on top of the ball, which is
//     why looking there finds a club, not a ball. A per-pixel median over them
//     removes the moving club, the arms and their shadows.
//   · `afterFrames` — impact + 150 ms to impact + 400 ms, where the ball has
//     gone. The crescent must have LOST its darkness there. A shoe scuff or a
//     tee hole fails that by construction, and that is the whole discriminator.
// `scalePxPerMm` sizes the ball (42.7 mm) so the centre can be placed one radius
// above the crescent; NaN ⇒ 0.012 × frameH is used instead and said so. The
// bright cue is unchanged and still decides wherever it finds a ball; the shadow
// cue is measured either way so the two can be compared in a trace.
DtlBall dtlFindBall(const FrameSource& frameAt,
                    const std::vector<int>& addrFrames,
                    const std::vector<int>& postFrames,
                    const std::vector<int>& clubAwayFrames,
                    const std::vector<int>& afterFrames,
                    const DtlAnchors& anchors,
                    int frameW, int frameH,
                    double scalePxPerMm,
                    const DtlShaftConfig& cfg);

DtlSolveState dtlSolve(const FrameSource& frameAt,
                       const std::vector<int64_t>& tUs,
                       const DtlAnchors& anchors,
                       const FaceOnWitness* witness,
                       int frameW, int frameH, double fps,
                       const std::vector<double>& bandsMm, double clubLenMm,
                       const DtlShaftConfig& cfg,
                       DtlDecideTrace* trace = nullptr);

} // namespace pinpoint::analysis
