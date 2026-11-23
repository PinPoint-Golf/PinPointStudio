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

// The app's half of the review views' door (shot_data_door.h; refactor 5.8a): it links the Session, the Shot and the
// Recording reader, so it is the app's (CMakeLists.txt), not the QML module's. main.cpp installs it.

#include "shot_data_door.h"

namespace pinpoint { struct LoadedRecording; }

namespace pinpoint::review {

// The door over the Session a view is given (`app.session`): its Shot at the folder (Session::shotAtFolder, the edge
// translation, DEC-90), that Shot's events, the owners' facts and its Recording's read.
ShotDataResolver makeShotDataResolver();

// What a view takes from one read of a Recording (the IMU lanes copied out of its window). Exposed for the tests.
ShotDataRead shotDataOf(const LoadedRecording &loaded);

} // namespace pinpoint::review
