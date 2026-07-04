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

// Club-name DISPLAY formatting. Club ids are stored/keyed UPPERCASE (the
// canonical club_vocabulary.h vocabulary — bag keys, swing.json review.club,
// truth.json meta.club); this singleton is the ONE place that turns an id into
// the human-facing sentence-case label at the last moment before it is shown.
//   "DRIVER" -> "Driver", "7 IRON" -> "7 iron", "GAP WEDGE" -> "Gap wedge"
// Mirror of pinpoint::clubDisplayLabel (session_summary.h) for the C++ clubMix.
// Never feed display() output back into storage or index lookups.

pragma Singleton

import QtQuick

QtObject {
    function display(id) {
        if (!id)
            return ""
        return id.charAt(0).toUpperCase() + id.slice(1).toLowerCase()
    }
}
