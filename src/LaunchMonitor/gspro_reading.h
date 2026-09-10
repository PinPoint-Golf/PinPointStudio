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

#include <gspro/gspro.h>

#include <QString>

#include <optional>

namespace pinpoint::lm {

// One decoded GSPro Open Connect message → one LaunchMonitorReading.
//
// PURE FUNCTION BY DESIGN, the same split as parseLastShotCsv: no socket, no
// settings, no logging, no clock. GsProMonitor owns the QTcpServer, the library
// handle and the connection table; everything decided here can be tested against
// a JSON string literal, which is how the whole mapping is covered without a
// launch monitor or a network.
//
// ⚠ PRESENCE IS A BITMASK AND ZERO IS NOT ABSENCE. libgspro reports which fields
// were ON THE WIRE (`present`) separately from their values, because some clients
// omit a quantity they did not measure and others send 0.0 for it. A mapping that
// tested `!= 0.0` would silently turn a measured zero — a putt at 0.0° of attack,
// a dead-straight face angle — into "not reported", and turn an unreported field
// into a confident zero. Every line below gates on the bit.
//
// ⚠ THE CLUB IS NOT ON THE WIRE. The protocol has no club field in the shot
// message at all: it travels the other way, server → client, so that GSPro can
// tell a launch monitor to switch to putting mode. `deviceClub` is therefore left
// EMPTY rather than guessed — see GsProMonitor::setPlayerClub for the direction
// this actually flows in.
//
// UNITS: the message declares "Yards" or "Meters" and libgspro converts nothing,
// which is right for a library and not enough for a reading. What the field
// governs is not stated by the vendor (protocol §3.6, unknown U3): every client
// that can be read sends mph for speeds and yards for distances, and no client is
// known to send "Meters" at all. So this function converts DISTANCES under
// "Meters" and leaves speeds alone, and says so in the flags it sets — the
// alternative is a carry number 9% wrong with nothing to show for it. When U3 is
// settled against real hardware this is the one place that changes.
//
// Returns nullopt when the message is not a usable reading: not a shot (a
// heartbeat or a status message carries no numbers, and GSPro answers both with
// the same 200 — protocol §4.2), or a shot that arrived with no measured value at
// all. `error` receives a reason when given.
std::optional<LaunchMonitorReading> readingFromGsProMessage(const gsp_message &message,
                                                            QString *error = nullptr);

// Exposed for tests and for the monitor's log line: the yards-per-metre factor and
// the mph-per-metre-per-second factor this mapping applies under `Units: "Meters"`.
double gsProMetresToYards(double metres);

} // namespace pinpoint::lm
