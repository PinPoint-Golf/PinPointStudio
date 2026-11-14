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

// THE ANALYSIS CONTEXT RECORDED — the athlete facts an analysis consumes, frozen at capture into
// the Recording so a re-analysis never reads a live profile (docs/developer/information_model.md
// §2, "The Recording contract"; IM-1, Mark 14 Nov 2026). It is a RECORDED INPUT: written once, by
// capture, through the one header writer; for a document older than the block, built once by the
// repository's reader from its legacy fields, unknowns left unknown.
//
// On disk it is the schema-tagged `analysisContext` block of the swing file. Its writer and reader
// are the store's (src/Export, step 1.2); this header is the value only.
//
// What it is NOT: the club the analysis uses. That is the EFFECTIVE club, which applies the golfer's
// correction (effectiveContext(), src/Analysis/effective_context.h). The captured club below stays
// what capture recorded, for provenance, whatever the golfer later says.
//
// Vocabulary ring: value types only, no rules beyond the spelling of a code.

#include <QMap>
#include <QString>
#include <vector>

#include "../Core/club_standards.h"   // pinpoint::ClubSet (men / women / junior)

namespace pinpoint::recording {

// Which arm leads. The codes are ShotAnalysisJob::handedness's (0 unknown, 1 right, 2 left).
enum class Handedness : int { Unknown = 0, Right = 1, Left = 2 };

// The athlete record's spelling ("Right" / "Left", any case) → the code. Anything else is unknown.
inline Handedness handednessFromName(const QString &name)
{
    if (name.compare(QLatin1String("Left"), Qt::CaseInsensitive) == 0)  return Handedness::Left;
    if (name.compare(QLatin1String("Right"), Qt::CaseInsensitive) == 0) return Handedness::Right;
    return Handedness::Unknown;
}

// A club's measured geometry, as the athlete's bag records it (athlete_controller.h club record).
// Every 0 / empty means "not measured"; the tracker then takes its own defaults.
struct ClubGeometry {
    double              lengthMm = 0.0;          // butt → sole, mm
    bool                lengthKnown = false;     // lengthMm came from a record (> 0)
    QString             shaftType;               // "steel" | "graphite" | "" (unknown)
    std::vector<double> bandCentersMm;           // retro-band centres from the butt (mm); empty = untaped
    double              hoselFromButtMm = 0.0;
    double              shaftLengthMm = 0.0;     // exposed shaft, bottom of grip → top of hosel
    double              handsEndMm = 0.0;        // where the hands end on the grip, from the butt
};

// The persistent club-length prior the live fuse used for THIS shot (club_length_fusion.h
// LengthPriorState), keyed to the captured club and the face-on camera. n == 0 = no prior joined.
struct LengthPrior {
    double px    = -1.0;   // ema (px); <= 0 = uninitialised
    double varPx = -1.0;   // variance (px²); < 0 = absent
    int    n     = 0;      // updates folded in
    bool present() const { return n > 0 && px > 0.0; }
};

// The club capture recorded: the session's club at the shot, with its geometry from the bag.
struct RecordedClub {
    QString      name;                  // canonical club id (club_vocabulary.h); EMPTY = undeclared,
                                        // never the "DRIVER" stub
    ClubGeometry geometry;
    LengthPrior  lengthPrior;
    double       standardLengthMm = 0.0;   // club_standards.h for the club set and height; 0 = none
};

struct AnalysisContext {
    Handedness   handedness = Handedness::Unknown;
    double       heightM = 0.0;                    // standing height; 0 = unknown
    // The cohort analysis consumes (D3): the club set on the swing's date. A document that does
    // not say takes the men's set, as every reader does today.
    ClubSet      clubSet = ClubSet::Men;
    RecordedClub club;                             // the CAPTURED club (provenance; see above)
    // The geometry of every club in the athlete's bag at capture, keyed by canonical club id (D2),
    // so a corrected club is resolvable without a live profile. Empty = no bag recorded.
    QMap<QString, ClubGeometry> bag;
};

} // namespace pinpoint::recording
