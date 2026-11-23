/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE CLUB DELIVERY NUMBERS OF ONE SWING, OFF ITS PHASE GRID (refactor 5.5d; DEC-74). ONE RULE for the two places that
// read them: the Charts delivery section's shot input (Gui/charts/session_delivery.cpp deliveryInputFromGrid, for this
// session's shots) and the session summary's day readings (Coaching/session_day_readings.h, written by the shot's
// SessionDiagnostics, read by another session's Charts and report). Before 5.5d the second was the Charts presenter
// reading the other sessions' swing_phasegrid.json itself; the numbers are the same, because the rule is the same.
//
// A number "at" a position is the grid's value at that phase; an INSTANT reading is the value at its own phase, else
// the first value the grid holds for it (a phase sample anywhere). NaN: not read.
//
// Rules ring (src/Diagnostics/measures): values only, no file.

#include "measure_sample.h"

#include <array>
#include <cmath>
#include <limits>

namespace pinpoint::analysis {

inline constexpr int kDeliveryGridPositions = 7;   // P1..P7

// P1..P7 → the phases the delivery section reads them at.
inline const std::array<Phase, kDeliveryGridPositions> &deliveryGridPhases()
{
    static const std::array<Phase, kDeliveryGridPositions> p{ Phase::Address, Phase::ShaftParallelBack, Phase::MidBackswing,
                                                             Phase::Top, Phase::ArmParallelDown, Phase::Delivery, Phase::Impact };
    return p;
}

struct DeliveryGridNumbers {
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    std::array<double, kDeliveryGridPositions> plane{ kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };   // swingPlaneCurve
    std::array<double, kDeliveryGridPositions> hands{ kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN };   // handPlaneCurve
    double planeSigma = kNaN;                      // the swing plane curve's own σ
    double steepening = kNaN, shoulderPlane = kNaN, lieP1 = kNaN, lieP7 = kNaN, shaftLean = kNaN, lowPoint = kNaN;
    double ourPath = kNaN, ourAttack = kNaN;
    double lmPath = kNaN, lmFace = kNaN, lmFaceToPath = kNaN, lmAttack = kNaN, lmLoft = kNaN, lmLie = kNaN, lmLowPoint = kNaN;
    double lmLaunchDir = kNaN, lmSpinAxis = kNaN, lmCarry = kNaN, lmOffline = kNaN;

    // Any swing plane position read (DeliveryShotInput::hasPlane's rule).
    bool hasPlane() const
    {
        for (double v : plane) if (std::isfinite(v)) return true;
        return false;
    }
    // Any of the monitor's delivery readings (DeliveryShotInput::hasMonitor's rule: carry and offline are not delivery).
    bool hasMonitor() const
    {
        for (double v : { lmPath, lmFace, lmFaceToPath, lmAttack, lmLoft, lmLie, lmLowPoint, lmLaunchDir, lmSpinAxis })
            if (std::isfinite(v)) return true;
        return false;
    }
};

inline double deliveryGridAt(const SwingPhaseGrid &grid, const QString &key, Phase phase)
{
    const MetricPhaseGrid *g = grid.metric(key);
    const PhaseGridValue *v = g ? g->at(phase) : nullptr;
    return v && std::isfinite(v->value) ? v->value : DeliveryGridNumbers::kNaN;
}

inline double deliveryGridInstant(const SwingPhaseGrid &grid, const QString &key, Phase phase)
{
    const double v = deliveryGridAt(grid, key, phase);
    if (std::isfinite(v)) return v;
    if (const MetricPhaseGrid *g = grid.metric(key))
        for (const PhaseGridValue &x : g->values) if (std::isfinite(x.value)) return x.value;
    return DeliveryGridNumbers::kNaN;
}

inline DeliveryGridNumbers deliveryGridNumbersOf(const SwingPhaseGrid &grid)
{
    DeliveryGridNumbers n;
    for (int p = 0; p < kDeliveryGridPositions; ++p) {
        const Phase ph = deliveryGridPhases()[size_t(p)];
        n.plane[size_t(p)] = deliveryGridAt(grid, QStringLiteral("swingPlaneCurve"), ph);
        n.hands[size_t(p)] = deliveryGridAt(grid, QStringLiteral("handPlaneCurve"), ph);
    }
    if (const MetricPhaseGrid *g = grid.metric(QStringLiteral("swingPlaneCurve"))) n.planeSigma = g->seriesSigma;
    n.steepening    = deliveryGridInstant(grid, QStringLiteral("handPlaneSteepening"), Phase::Top);
    n.shoulderPlane = deliveryGridInstant(grid, QStringLiteral("shoulderPlane"), Phase::Address);
    n.lieP1         = deliveryGridAt(grid, QStringLiteral("shaftLie"), Phase::Address);
    n.lieP7         = deliveryGridAt(grid, QStringLiteral("shaftLie"), Phase::Impact);
    n.shaftLean     = deliveryGridInstant(grid, QStringLiteral("impactShaftLean"), Phase::Impact);
    n.lowPoint      = deliveryGridInstant(grid, QStringLiteral("lowPointAhead"), Phase::Impact);
    n.ourPath       = deliveryGridInstant(grid, QStringLiteral("clubPath"), Phase::Impact);
    n.ourAttack     = deliveryGridInstant(grid, QStringLiteral("attackAngle"), Phase::Impact);
    n.lmPath        = deliveryGridInstant(grid, QStringLiteral("lm.clubPath"), Phase::Impact);
    n.lmFace        = deliveryGridInstant(grid, QStringLiteral("lm.faceAngle"), Phase::Impact);
    n.lmFaceToPath  = deliveryGridInstant(grid, QStringLiteral("lm.faceToPath"), Phase::Impact);
    n.lmAttack      = deliveryGridInstant(grid, QStringLiteral("lm.attackAngle"), Phase::Impact);
    n.lmLoft        = deliveryGridInstant(grid, QStringLiteral("lm.dynamicLoft"), Phase::Impact);
    n.lmLie         = deliveryGridInstant(grid, QStringLiteral("lm.lieAngle"), Phase::Impact);
    n.lmLowPoint    = deliveryGridInstant(grid, QStringLiteral("lm.lowPointAhead"), Phase::Impact);
    n.lmLaunchDir   = deliveryGridInstant(grid, QStringLiteral("lm.launchDirection"), Phase::Impact);
    n.lmSpinAxis    = deliveryGridInstant(grid, QStringLiteral("lm.spinAxis"), Phase::Impact);
    n.lmCarry       = deliveryGridInstant(grid, QStringLiteral("lm.carryDistance"), Phase::Impact);
    n.lmOffline     = deliveryGridInstant(grid, QStringLiteral("lm.offline"), Phase::Impact);
    return n;
}

} // namespace pinpoint::analysis
