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

// WHY THERE IS NO P8 (plane stack D6, 13 Nov 2026; the FA follow-up). FA bounded P8 (shaft_positions.h
// findP8Crossing): the first DOWN → UP shaft crossing timed impact + 40 … 200 ms, refused after a vertical
// stall. Where it refuses, the readings that need P8 — m_leadKneeFlex (P7 → P8), m_leadArmToTorso (the change
// P7 → P8) and the seven `_p8` changes from address — have nothing to read, and until D6 their cells said
// only "the phase this reads at was not segmented". This record names the real reason.
//
// ONE RECORD, positions.p8Refused, REFUSED (no value: the readings it explains are not produced; a Downgraded
// record would claim a value kept at lower trust, which is false), subject `position.p8` — the PHASE, not a
// metric key. Fired only when the document's ladder has NO P8 and the face-on tracker recorded why
// (ShaftTrack2D::p8Refusal): a P8 the IMU segmenter supplied stands, and then nothing was refused.
//
// WHY THE SUBJECT IS THE PHASE AND `covers` IS EMPTY. The readings that need P8 read many series keys
// (leadKneeFlexion, leadArmToTorso, the trail knee, the elbows …), and each of those keys is also read at
// other positions, where it reads fine. A cascade covering the keys would mark the whole reading refused —
// in the Trust panel, and as the reason for ANY missing measure on that key. So the reason is resolved at
// read time instead, from the measure's own reducer (LiveMeasureSource::gateCause): a measure that needs a
// phase its reading has no value at, on a swing whose records refuse that phase (gates::phaseSubject), names
// this record. That is the phase-level reason the grader lacked.
//
// CODES (one per cause; P8Refusal):
//   noCrossing     the shaft never came back up through level after impact + p8MinAfterImpactUs
//   late           its first rise through level came after impact + p8MaxAfterImpactUs: too late to be the
//                  follow-through (FA: a tracker parked on a bright upright line timed it at the finish)
//   wrongSense     the first crossing after the window opened went DOWN: the track had flipped end for end
//   verticalStall  the shaft sat within p8StallBandDeg of upright for p8StallMinUs first: the tracker was on
//                  something upright in the background, not the club
// NoImpact fires nothing: with no impact there is no P7 either, and the missing impact is the reason the
// tracker's own records and the ladder already give.
// Evidence: code, minAfterMs, maxAfterMs, stallBandDeg, stallMinMs (the window and stall the tracker used).

#include "shaft_positions.h"
#include "../Analysis/gate_record.h"
#include "../Analysis/swing_analysis.h"

#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

namespace p8_refusal {

inline const QString &subject() { static const QString k = QStringLiteral("position.p8"); return k; }
inline const QString &gateId()  { static const QString k = QStringLiteral("positions.p8Refused"); return k; }

} // namespace p8_refusal

// The record for a track whose positions found no P8, against the ladder the document carries; nullopt when
// the ladder has a P8, when nothing was refused, or when the cause was no impact (see the top of the file).
inline std::optional<GateRecord> p8RefusalRecord(uint8_t why, const std::vector<PhaseEvent> &ladder,
                                                 const PositionsConfig &cfg)
{
    if (std::any_of(ladder.begin(), ladder.end(),
                    [](const PhaseEvent &e) { return e.phase == Phase::ShaftParallelThrough; }))
        return std::nullopt;
    const char *code = nullptr;
    switch (P8Refusal(why)) {
    case P8Refusal::NoCrossing:    code = "noCrossing";    break;
    case P8Refusal::Late:          code = "late";          break;
    case P8Refusal::WrongSense:    code = "wrongSense";    break;
    case P8Refusal::VerticalStall: code = "verticalStall"; break;
    case P8Refusal::None:
    case P8Refusal::NoImpact:      return std::nullopt;
    }
    if (!code) return std::nullopt;
    const QVariantMap ev{ { QStringLiteral("code"), QString::fromLatin1(code) },
                          { QStringLiteral("minAfterMs"), double(cfg.p8MinAfterImpactUs) / 1000.0 },
                          { QStringLiteral("maxAfterMs"), double(cfg.p8MaxAfterImpactUs) / 1000.0 },
                          { QStringLiteral("stallBandDeg"), cfg.p8StallBandDeg },
                          { QStringLiteral("stallMinMs"), double(cfg.p8StallMinUs) / 1000.0 } };
    return GateRecord{ PP_GATE("positions.p8Refused"), p8_refusal::subject(), GateEffect::Refused, ev, {}, {}, {} };
}

} // namespace pinpoint::analysis
