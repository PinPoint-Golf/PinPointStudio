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

// THE PACK'S DIRECTION BOUNDARIES, RESOLVED (refactor 6.3b, audit F-05 / G-01). Header-only, Rules ring.
//
// The one direction rule (lmShapeClass, LaunchMonitor/inferred/lm_inferred_reads.h) names a shot's start and curve by the
// boundaries the diagnostic pack fires its direction conditions by. This reads them off the pack and the norms the
// way the engine does (characteristic_engine.cpp): an outsideCorridor signal fires beyond the LATER of its measure's
// deviation edge (deviationEdges, at the grade policy) and its Ideal edge (bandEdgesOf), strictly; a threshold signal
// beyond its threshold. Resolved in the shot's context and the golfer's cohort, so a user's corridor override and the
// grade policy move the card and Club delivery with the findings.
//
// Each boundary the pack does not state (a signal missing, of another test, or a measure with no norm) keeps the
// shipped default (ShotDirectionBounds), which is the shipped pack under the standard policy (lm_corridor_test).

#include "../../LaunchMonitor/inferred/lm_inferred_reads.h"
#include "../content/characteristic_pack.h"
#include "../model/context_tree.h"
#include "../content/norm_provider.h"

#include <algorithm>

namespace pinpoint::analysis {

inline ShotDirectionBounds shotDirectionBounds(const CharacteristicPack &pack, const INormProvider &norms,
                                               const QString &contextId, const GradePolicy &policy = {},
                                               const Cohort &athlete = {})
{
    ShotDirectionBounds b;
    const QString ctx = contextId.isEmpty() ? kDefaultContextId() : contextId;   // NormMeasureSource's default

    // An outsideCorridor signal's edge on its own tail.
    const auto corridorEdge = [&](const char *signalId, double &edge) {
        const Signal *s = pack.signal(QString::fromLatin1(signalId));
        if (!s || s->test != SignalTest::OutsideCorridor || s->measures.size() != 1) return;
        const Measure *m = pack.measure(s->measures.front());
        if (!m) return;
        const NormResolution res = norms.resolve(m->id, ctx, athlete);
        if (!res.found()) return;
        double devLo = 0.0, devHi = 0.0;
        deviationEdges(*res.norm, m->shape, policy, devLo, devHi);
        const NormBandEdges e = bandEdgesOf(*res.norm, m->shape, policy);
        if (s->direction.value_or(Direction::High) == Direction::High) {
            if (!e.highOpen) edge = std::max(devHi, e.idealHi);
        } else {
            if (!e.lowOpen) edge = std::min(devLo, e.idealLo);
        }
    };
    // A threshold signal's number.
    const auto threshold = [&](const char *signalId, double &value) {
        const Signal *s = pack.signal(QString::fromLatin1(signalId));
        if (s && s->test == SignalTest::Threshold && s->threshold) value = *s->threshold;
    };

    corridorEdge("sig_pull",  b.startLo);
    corridorEdge("sig_push",  b.startHi);
    corridorEdge("sig_hook",  b.axisLo);
    corridorEdge("sig_slice", b.axisHi);
    threshold("sig_fadeBeyondStraight", b.fadeFrom);
    threshold("sig_fadeShortOfSlice",   b.fadeTo);
    threshold("sig_drawBeyondStraight", b.drawFrom);
    threshold("sig_drawShortOfHook",    b.drawTo);
    threshold("sig_pushSlice",          b.pushSlice);
    threshold("sig_pullHook",           b.pullHook);
    return b;
}

} // namespace pinpoint::analysis
