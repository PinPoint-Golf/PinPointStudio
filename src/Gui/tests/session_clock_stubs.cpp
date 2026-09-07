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

// The one AthleteController symbol SessionController::start() references — the
// preferred-club seed. Stubbed rather than linked so the session-clock suite
// stays free of the athlete store and the whole Diagnostics norm stack behind
// it (athlete_controller.cpp -> norm_pack.h -> context_tree/pack_io/...).
// Same shape as reanalysis_stubs.cpp.
//
// The suite drives SessionController with a null AthleteController, so this is
// never called; it exists to satisfy the linker. Seeding from a real athlete
// record is AthleteController's behaviour and belongs in its own suite.

#include "athlete/athlete_controller.h"

QString AthleteController::effectivePrimaryClub(const QString &) const
{
    return {};
}
