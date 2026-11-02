/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "launch_monitor_reading.h"

#include <QByteArray>
#include <QString>

#include <optional>
#include <vector>

namespace pinpoint::lm {

// Reads Foresight Premier's session files: SessionLiveSnapshot.<userId>.json (the range
// session in progress, one session object) and SessionUploadQueue.<userId>.json (ended
// sessions awaiting upload, {"Sessions":[…]}). Both share one session schema, so one parser
// takes either. See docs/design/foresight_premier_connector.md §2.1 and §2.6.
//
// PURE FUNCTION BY DESIGN, the same split as parseLastShotCsv: no settings, no logging, no
// timers, no file I/O. ForesightPremierMonitor owns the polling, the per-shot bookkeeping and
// the late club data; everything decided here is tested against captured files.
//
// UNITS. Premier writes SI: m/s and degrees. Speeds become mph; angles, spin and mm pass
// through. SIGNS (§2.6.5, checked against Premier's screen on 2026-11-02, right-handed): every
// field is right-of-target positive and matches PinPoint's convention as-is, EXCEPT
// FaceImpactHorizontalMm, which is heel-positive and is negated into strikeLocation (toe+).
//
// CLUB DATA. `ClubData` is null on a shot's first write (it lands one shot late) and stays
// null for a club with no stickers. Inside the object a single field can be null on its own
// (one sticker: speed, efficiency, attack and path only). A null field inside the object is
// a `noData` drop; a whole-null ClubData drops nothing here, because on first sight that is
// "pending", not "missing" — the monitor decides when it has become missing
// (addClubNotMeasuredDrops).
//
// A FORMAT CHANGE IS NOT A TORN READ. Premier writes these files whole and closed, so a
// document that is valid JSON but lacks the session shape or a required shot field (an id,
// a time, a ball speed) means the format has changed under us; parsePremierSessionFile
// returns nullopt with *formatChanged set, and the monitor goes to its error state rather
// than emitting partial readings. Invalid JSON returns nullopt with formatChanged false: a
// read caught mid-write, to be retried.

// The delay from ball contact to `CreatedAtUtc`. Premier stamps a shot when the spin message
// arrives, not at launch: +529 to +626 ms over five shots on 2026-11-02 (§2.6.6).
constexpr qint64 kPremierCreatedAfterContactMs = 580;

struct PremierShot {
    QString localShotId;            // globally unique (a GUID), unlike FSX2020's counter
    QString sessionId;              // LocalSessionId of the session it sits in
    qint64  createdUtcMs = 0;       // CreatedAtUtc, epoch ms
    bool    hidden       = false;   // never seen true; skipped if a future build uses it
    bool    hasClubData  = false;   // ClubData was an object (possibly with null fields)
    // Ball data always; club data when hasClubData. deviceShotId, deviceClub and contactAtMs
    // are filled; deviceKind, sourcePath and readAtMs are the monitor's to set.
    LaunchMonitorReading reading;
};

struct PremierFile {
    int sessionCount = 0;
    std::vector<PremierShot> shots;     // file order: sessions, then groups, then shots
};

std::optional<PremierFile> parsePremierSessionFile(const QByteArray &bytes,
                                                   QString *error = nullptr,
                                                   bool *formatChanged = nullptr);

// A shot whose ClubData never came: mark every raw club field the reading lacks as a
// `noData` drop, so the board says "not measured" instead of leaving a silent gap. The
// derived fields (face to path, spin loft) are covered by lm_gates' derivation table.
void addClubNotMeasuredDrops(LaunchMonitorReading &reading);

} // namespace pinpoint::lm
