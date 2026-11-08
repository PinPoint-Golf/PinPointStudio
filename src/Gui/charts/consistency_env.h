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

#include "session_consistency.h"                    // ConsistencyEnv

// THE PRODUCTION ENV of the Consistency maths (docs/design/consistency_design.md): the one place the
// rules and the sampling reach the shipped catalogue, conditions and norms, through two functions, so
// neither they nor their tests touch a pack.
//
// WHERE A READING IS PLACED (Appendix B). A reading's area comes from the conditions that detect on it,
// the group that uses it most (metricAreasForPack, swing_areas_pack.h). 29 readings have no such
// condition and are placed by hand: MetricDescriptor::consistencyArea, which wins when set. Four are
// deliberately unplaced and not read: compoundMiss (ball flight, which has no spans), shoulderPlane,
// swingPlaneIotaBack and swingPlaneIotaDown. areaOf answers "" for those, and for any other key that
// neither route places.
//
// THE NORM SIGMA OF A READING (CONTRACT "Norm sigma of a reading"). The first measure reading the key at
// the phase that has a norm in the club's context, 0.5*(sigmaLo+sigmaHi). Failing that the median over
// all the key's measures in that context, then in `any`. NaN when the key has no norm. A measure is the
// key's when the key is on its ladder, the join measuresForMetricAtPhase uses. Context comes only from
// contextIdForClub, so resolve's walk up from a club never reaches a posture_* or archetype_* row:
// those hang off full_swing beside the clubs.

namespace pinpoint::charts {

// TAKE IT ON THE GUI THREAD, then hand it to a worker by value. consistencyInput takes the env as an
// argument so that a worker has to be given one: a presenter holds one from its constructor. The two
// functions are then thread-safe, callable from any thread, at once, for the life of the process.
//
// The first call in the process does the work (reading the packs, walking the conditions) on the
// caller's thread, and it is finished when this returns. Later calls cost nothing. Like
// postureSharedEnv() and the wrist bands it keeps the norm set it found for the life of the process, so
// a corridor edited in the Diagnostic Model reaches this at the next launch. A worker holding it can
// therefore never meet resetSharedNormProvider(), which is not safe against a concurrent reader.
//
// WHY THE FIRST CALL IS THE GUI THREAD'S. sharedNormProvider()'s own first build is unsafe against a
// first build on another thread, and a reset against any reader. A first call from any other thread is
// a programming error: it asserts in a debug build and writes ppError in a release one.
ConsistencyEnv consistencyEnvFromPacks();

} // namespace pinpoint::charts
