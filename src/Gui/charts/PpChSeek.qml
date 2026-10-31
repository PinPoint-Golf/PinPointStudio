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
// PPCHSEEK — the ONE way a Charts block moves the video: a press on a position, a frame, a ledger row or a trace's
// scrub asks `PpChSeek.to(us)`, and this owns the guard every caller used to repeat — a render, a test or a probe
// without the app's shotReplay (or with none bound yet) seeks nothing, and a time the presenter could not place
// (us < 0) is never sent. Scrub begin / end stay with the scrubbing MouseArea (its geometry is its own).
pragma Singleton
import QtQuick

QtObject {
    // the app's ShotReplay (main.cpp, a root context property), when there is one
    function available() { return typeof shotReplay !== "undefined" && !!shotReplay }
    function to(us) { if (us >= 0 && available()) shotReplay.seekToUs(us) }
}
