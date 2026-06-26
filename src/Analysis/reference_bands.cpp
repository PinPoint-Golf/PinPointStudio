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

#include "reference_bands.h"

#include <array>

// ConfigReferenceBandProvider v1 table. Green corridors are Δ-from-address degrees per
// (DOF, position), seeded from design §5 (the mockup's per-DOF bandLo/bandHi). The amber margin is
// per-DOF (degrees added either side of the green corridor). These are STARTING HEURISTICS (design
// §11) — every number is expected to move once tuned against real/known swings; that is exactly why
// they live behind the provider seam rather than in the engine. Only the four instrumented strips +
// lead-elbow have bands today; every other DOF (trail side, shoulders) has no producer, so the
// provider returns an invalid band and the engine greys those cells.

namespace pinpoint::analysis {

namespace {

struct DofBands {
    std::array<double, kNumPos> lo;
    std::array<double, kNumPos> hi;
    std::array<bool,   kNumPos> has;   // false = no corridor at this position (e.g. trail wrist P8)
    double margin;                     // amber margin, degrees either side of green
};

// Helper: all-positions-present mask.
constexpr std::array<bool, kNumPos> kAll{ true, true, true, true, true, true, true, true };

const DofBands *bandsFor(PpJointDof dof)
{
    //                                P1    P2    P3    P4    P5    P6    P7    P8
    static const DofBands kRadUln{
        { -3.0,  0.0, 18.0, 26.0, 26.0, 20.0, -2.0, -8.0 },
        {  3.0, 20.0, 38.0, 50.0, 50.0, 40.0, 18.0,  8.0 }, kAll, 5.0 };
    static const DofBands kFlexExt{
        { -5.0, -6.0, -7.0, -6.0, -3.0,  1.0,  1.0, -4.0 },
        {  5.0,  8.0, 11.0, 16.0, 18.0, 18.0, 15.0, 12.0 }, kAll, 5.0 };
    static const DofBands kForearm{
        { -4.0, -16.0, -24.0, -29.0, -21.0, -13.0, -8.0, -5.0 },
        {  4.0,   0.0,  -8.0, -11.0,  -3.0,   3.0,  8.0,  5.0 }, kAll, 5.0 };
    static const DofBands kTrailWrist{
        { -4.0,  5.0, 22.0, 33.0, 28.0, 18.0,  4.0, 0.0 },
        {  4.0, 25.0, 42.0, 57.0, 52.0, 40.0, 20.0, 0.0 },
        { true, true, true, true, true, true, true, false },   // no corridor at P8
        6.0 };
    static const DofBands kElbow{
        { -3.0, -2.0, -2.0, -2.0, -2.0, -2.0, -2.0, -2.0 },
        {  6.0,  8.0, 10.0, 12.0, 10.0, 10.0, 12.0, 12.0 }, kAll, 4.0 };

    switch (dof) {
    case PpJointDof::LeadWristRadUln:   return &kRadUln;
    case PpJointDof::LeadWristFlexExt:  return &kFlexExt;
    case PpJointDof::LeadForearmRot:    return &kForearm;
    case PpJointDof::TrailWristFlexExt: return &kTrailWrist;
    case PpJointDof::LeadElbowFlex:     return &kElbow;
    default:                            return nullptr;   // no producer / no band yet
    }
}

} // namespace

Band ConfigReferenceBandProvider::band(PpJointDof dof, PpSwingPosition pos, const BandContext &ctx) const
{
    const DofBands *t = bandsFor(dof);
    const int p = static_cast<int>(pos);
    if (t == nullptr || p < 0 || p >= kNumPos || !t->has[p])
        return {};                 // invalid — engine greys the cell
    // SwingLab bands.* margin override (negative ⇒ table default).
    const double ovMargin = ctx.tuning.marginFor(dof);
    const double margin   = (ovMargin >= 0.0) ? ovMargin : t->margin;
    Band b;
    b.greenLo = t->lo[p];
    b.greenHi = t->hi[p];
    b.amberLo = b.greenLo - margin;
    b.amberHi = b.greenHi + margin;
    b.valid   = true;
    return b;
}

namespace {
// Face-corridor shift per archetype (degrees on lead-wrist flex-ext): bowed-tour tolerates more bow,
// cupped-tour more cup. Starting heuristics (design §11).
double archetypeFaceOffset(int archetype)
{
    switch (archetype) {
    case 1:  return 10.0;    // bowed
    case 2:  return -10.0;   // cupped
    default: return 0.0;     // neutral
    }
}
} // namespace

Band ArchetypeBandProvider::band(PpJointDof dof, PpSwingPosition pos, const BandContext &ctx) const
{
    Band b = m_config.band(dof, pos, ctx);
    if (b.valid && dof == PpJointDof::LeadWristFlexExt) {
        const double off = archetypeFaceOffset(ctx.archetype);
        b.greenLo += off; b.greenHi += off;
        b.amberLo += off; b.amberHi += off;
    }
    return b;
}

std::unique_ptr<IReferenceBandProvider> makeReferenceBandProvider(BandProviderKind kind)
{
    switch (kind) {
    case BandProviderKind::Config:    return std::make_unique<ConfigReferenceBandProvider>();
    case BandProviderKind::Archetype: return std::make_unique<ArchetypeBandProvider>();
    }
    return std::make_unique<ConfigReferenceBandProvider>();
}

} // namespace pinpoint::analysis
