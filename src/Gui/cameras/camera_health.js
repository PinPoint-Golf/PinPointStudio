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
.pragma library

// The camera health rule the session toolbar and its camera panel both read: "a
// connected camera still needs calibrating". The ONE place it is written (D1). Pure:
// it reads only the manager and settings it is given (cameraManager / appSettings, or
// fakes with the same shape). JS rather than a Q_INVOKABLE so a binding that calls it
// records the reads (the camera list and appSettings.cameraFixedInPlace) and
// re-evaluates when either changes.

// True once a connected camera is not "fixed in place" — a non-fixed camera needs
// (stereo) calibration this session; fixed cameras keep their calibration. Stays true
// until the camera is calibrated/fixed. `mgr.instances` is read so the answer follows
// connects and drops; its value is USED, so the read is never compiled away.
function anyNeedsCalibration(mgr, settings) {
    if (!mgr.instances) return false     // the dependency read; also guards a manager with none
    var list  = mgr.cameraList
    var fixed = settings.cameraFixedInPlace
    for (var i = 0; i < list.length; ++i)
        if (list[i].selected && fixed[list[i].cameraKey] !== true) return true
    return false
}
