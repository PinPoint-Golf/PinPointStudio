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

#include "wrist_assessment_contract.h"

#include <array>
#include <vector>

// Wrist Motion assessment engine — Phase 0 synthetic fixtures (the golden-test inputs).
//
// A FixtureWristAngleSource is an in-memory IWristAngleSource built from per-DOF Pn ANCHORS plus a
// deterministic, symmetric jitter cloud around each anchor — so the windowed median recovers the
// anchor exactly (no RNG, identical every run). Anchors are in degrees, authored delta-from-address
// (P1 = 0) so a cell value equals the figure a coach would read.
//
// Provenance of the numbers: the themed mockup (PinPointStudio_WristDiagnostics_mockup.html) shows
// a *faulty* example swing (its DOFS[].values arrays — a cast+flip) alongside a clean *ghost*
// reference. The CLEAN fixture here is the fault-free reference (the mockup ghost / band centres);
// each FAULT fixture is the clean trace with the faulted DOF(s) overridden to the mockup's faulty
// signature. (Phase 2 keys its golden-fault tests off these.)

namespace pinpoint::analysis {

// Pn timestamp grid: 1.0 s base, 100 ms apart (windows never overlap at ±15 ms).
inline int64_t fixturePosTimeUs(int p) { return 1000000 + static_cast<int64_t>(p) * 100000; }

// A per-DOF authored trace: 8 anchors + a present mask (false ⇒ an intentional gap at that Pn).
struct DofTrace {
    PpJointDof            dof;
    std::array<bool, 8>   present;
    std::array<double, 8> anchor;     // degrees, delta-from-address; unused where !present
    float                 baseConf;
};

// The fault-free reference swing — five instrumented DOFs (the mockup's five strips). Trail-wrist
// extension has no P8 sample (the mockup's "no data" diamond), exercising the gap path. baseConf
// values mirror the mockup's per-strip confidence (source-aware, design §4).
inline const std::vector<DofTrace> &cleanTraces()
{
    static const std::vector<DofTrace> kTraces = {
        // lead wrist radial–ulnar (lag): retains the angle into P6 — no early collapse.
        { PpJointDof::LeadWristRadUln,
          { true, true, true, true, true, true, true, true },
          { 0, 10, 28, 38, 38, 30, 8, 0 }, 0.86f },
        // lead wrist flex–ext (face): stays flexed into impact — no flip.
        { PpJointDof::LeadWristFlexExt,
          { true, true, true, true, true, true, true, true },
          { 0, 1, 4, 9, 11, 12, 9, 4 }, 0.84f },
        // lead forearm rotation (roll): supinates in the backswing, rotates to pronation by P8.
        { PpJointDof::LeadForearmRot,
          { true, true, true, true, true, true, true, true },
          { 0, -8, -16, -20, -12, -5, 1, 3 }, 0.72f },
        // trail wrist extension (tray): retained then released; P8 has no data (gap).
        { PpJointDof::TrailWristFlexExt,
          { true, true, true, true, true, true, true, false },
          { 0, 15, 32, 45, 40, 29, 12, 0 }, 0.52f },
        // lead elbow flexion (structure): holds width, no chicken-wing.
        { PpJointDof::LeadElbowFlex,
          { true, true, true, true, true, true, true, true },
          { 0, 3, 5, 7, 5, 4, 5, 7 }, 0.60f },
    };
    return kTraces;
}

// The fixtures' in-memory source is just the generic settable source from the contract header.
using FixtureWristAngleSource = InMemoryWristAngleSource;

// Build a source from authored traces. A symmetric jitter multiset around each anchor guarantees
// median == anchor; a left-handed source pre-multiplies each anchor by mirrorSign(dof) so the
// sampler's mirror canonicalises it back to the right-handed twin.
inline FixtureWristAngleSource buildFixtureSource(const std::vector<DofTrace> &traces,
                                                  PpHandedness handedness)
{
    FixtureWristAngleSource src;
    src.setHandedness(handedness);

    PpSwingPositionTimeline tl;
    for (int p = 0; p < kNumPos; ++p)
        tl.positions[p] = { true, fixturePosTimeUs(p), 0.9f };
    src.setTimeline(tl);

    // Symmetric about 0 ⇒ the windowed median returns the anchor exactly. Scrambled order so the
    // series reads like noisy data rather than a ramp.
    static const double kJitter[5] = { 0.75, -1.5, 0.0, 1.5, -0.75 };
    constexpr int64_t kDt = 5000;   // ±10 ms span, inside the ±15 ms default window

    for (const DofTrace &tr : traces) {
        const double sgn = (handedness == PpHandedness::Left) ? mirrorSign(tr.dof) : 1.0;
        PpJointAngleSeries ser;
        ser.dof            = tr.dof;
        ser.present        = true;
        ser.baseConfidence = tr.baseConf;
        for (int p = 0; p < kNumPos; ++p) {
            if (!tr.present[p])
                continue;                       // leave the gap — no samples near this Pn
            const double  a = tr.anchor[p] * sgn;
            const int64_t t = fixturePosTimeUs(p);
            for (int k = -2; k <= 2; ++k) {
                PpJointAngleSample s;
                s.t_us          = t + static_cast<int64_t>(k) * kDt;
                s.valueDeg      = a + kJitter[k + 2];
                s.available     = true;
                s.confidence    = 1.0f;
                s.pitchProxyDeg = 0.0;
                ser.samples.push_back(s);
            }
        }
        src.setSeries(ser);
    }
    return src;
}

namespace detail {
inline DofTrace &traceFor(std::vector<DofTrace> &traces, PpJointDof dof)
{
    for (DofTrace &t : traces)
        if (t.dof == dof)
            return t;
    return traces.front();   // unreachable for the authored set
}
} // namespace detail

// ---- the five named fixtures + the left-handed twin -------------------------------------------

inline FixtureWristAngleSource makeCleanSwing()
{
    return buildFixtureSource(cleanTraces(), PpHandedness::Right);
}

// Same swing, left-handed: every anchor sign-flipped per mirrorSign(). After the sampler mirrors it
// back, the resulting angle set is identical to makeCleanSwing()'s (the mirror golden).
inline FixtureWristAngleSource makeCleanSwingLeftHanded()
{
    return buildFixtureSource(cleanTraces(), PpHandedness::Left);
}

// F4 cast / early release: lead-wrist radial set collapses before P6 (mockup faulty line P5/P6).
inline FixtureWristAngleSource makeCastSwing()
{
    std::vector<DofTrace> t = cleanTraces();
    DofTrace &radUln = detail::traceFor(t, PpJointDof::LeadWristRadUln);
    radUln.anchor[idx(PpSwingPosition::P5)] = 14;   // lag dumped early
    radUln.anchor[idx(PpSwingPosition::P6)] = 2;    // ~retention checkpoint far below corridor
    return buildFixtureSource(t, PpHandedness::Right);
}

// F3 flip / scoop at impact: lead wrist adds extension P6→P7 (Δ negative); trail wrist flattens.
inline FixtureWristAngleSource makeFlipSwing()
{
    std::vector<DofTrace> t = cleanTraces();
    DofTrace &fe = detail::traceFor(t, PpJointDof::LeadWristFlexExt);
    fe.anchor[idx(PpSwingPosition::P6)] = 2;
    fe.anchor[idx(PpSwingPosition::P7)] = -7;    // extension into impact (Δ ≈ −9)
    fe.anchor[idx(PpSwingPosition::P8)] = -14;
    DofTrace &trail = detail::traceFor(t, PpJointDof::TrailWristFlexExt);
    trail.anchor[idx(PpSwingPosition::P6)] = 22; // trail-side early flattening (corroboration)
    trail.anchor[idx(PpSwingPosition::P7)] = 6;
    return buildFixtureSource(t, PpHandedness::Right);
}

// F1 open face at top: excessive cup at P4 (Δ < −15°).
inline FixtureWristAngleSource makeOpenFaceTopSwing()
{
    std::vector<DofTrace> t = cleanTraces();
    DofTrace &fe = detail::traceFor(t, PpJointDof::LeadWristFlexExt);
    fe.anchor[idx(PpSwingPosition::P4)] = -20;   // deep cup at the face checkpoint
    return buildFixtureSource(t, PpHandedness::Right);
}

// F7 holding off / blocked release: forearm stays supinated through P7→P8 (under the corridor).
inline FixtureWristAngleSource makeHoldingOffSwing()
{
    std::vector<DofTrace> t = cleanTraces();
    DofTrace &rot = detail::traceFor(t, PpJointDof::LeadForearmRot);
    rot.anchor[idx(PpSwingPosition::P7)] = -10;  // does not rotate toward pronation
    rot.anchor[idx(PpSwingPosition::P8)] = -8;
    return buildFixtureSource(t, PpHandedness::Right);
}

// F6 over-rotation / over-release: forearm pronates past the corridor through P7→P8.
inline FixtureWristAngleSource makeOverRotationSwing()
{
    std::vector<DofTrace> t = cleanTraces();
    DofTrace &rot = detail::traceFor(t, PpJointDof::LeadForearmRot);
    rot.anchor[idx(PpSwingPosition::P7)] = 10;   // rotates toward pronation too far / too fast
    rot.anchor[idx(PpSwingPosition::P8)] = 15;
    return buildFixtureSource(t, PpHandedness::Right);
}

// F8 chicken wing: lead elbow folds past the corridor at P7→P8 (forearm left clean → Watch path).
inline FixtureWristAngleSource makeChickenWingSwing()
{
    std::vector<DofTrace> t = cleanTraces();
    DofTrace &el = detail::traceFor(t, PpJointDof::LeadElbowFlex);
    el.anchor[idx(PpSwingPosition::P7)] = 14;
    el.anchor[idx(PpSwingPosition::P8)] = 18;
    return buildFixtureSource(t, PpHandedness::Right);
}

// The exact player line from the themed mockup (DOFS[].values) — a cast+flip example shot. This is
// the Phase-1 in-app DEMO driver so the docked view matches the mockup; it is replaced by the live
// IK/segmentation adapter in Phase 3.
inline const std::vector<DofTrace> &mockupDemoTraces()
{
    static const std::vector<DofTrace> kTraces = {
        { PpJointDof::LeadWristRadUln,
          { true, true, true, true, true, true, true, true },
          { 0, 8, 25, 34, 28, 14, 6, -2 }, 0.86f },
        { PpJointDof::LeadWristFlexExt,
          { true, true, true, true, true, true, true, true },
          { 0, -3, -8, -12, -6, 2, -7, -14 }, 0.84f },
        { PpJointDof::LeadForearmRot,
          { true, true, true, true, true, true, true, true },
          { 0, -10, -18, -20, -12, -6, -1, 7 }, 0.72f },
        { PpJointDof::TrailWristFlexExt,
          { true, true, true, true, true, true, true, false },
          { 0, 14, 31, 42, 38, 22, 6, 0 }, 0.52f },
        { PpJointDof::LeadElbowFlex,
          { true, true, true, true, true, true, true, true },
          { 0, 3, 6, 8, 5, 4, 9, 16 }, 0.60f },
    };
    return kTraces;
}

inline FixtureWristAngleSource makeMockupDemoSwing()
{
    return buildFixtureSource(mockupDemoTraces(), PpHandedness::Right);
}

} // namespace pinpoint::analysis
