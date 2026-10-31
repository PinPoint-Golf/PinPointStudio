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

// The IMU health rules the session toolbar and its IMU panel both read: "a sensor's
// connection gave up" and "a connected sensor still needs calibrating". The ONE place
// they are written (D1). Pure: each reads only the manager or instance it is given
// (imuManager, or a fake with the same shape in the QML tests).
//
// ⚠ JS, NOT A Q_INVOKABLE ON ImuManager, ON PURPOSE. The answers change when an
// instance's own imuConnected / fullyCalibrated / stateLabel change, and the manager
// emits nothing then. Called from a binding, these functions' reads of those instance
// properties are recorded by the binding, so it re-evaluates; a C++ invokable's reads
// are not seen, and the toolbar badge and the Calibrate tile would go stale.
//
// The manager walks also read `mgr.instances` first: instanceFor() is an invokable and
// not reactive, so that read is what re-runs the binding when a sensor connects or
// drops. Its value is USED (a bare or unused read can be compiled away with its dependency).

// True when this live instance's connection gave up: not connected, and the driver
// says so in its own words ("Error" / "Not found"; "Not ready" = its settings didn't
// take, #105). A missing instance (not selected) has not given up.
function gaveUp(inst) {
    if (!inst || inst.imuConnected) return false
    var label = inst.stateLabel
    return label === "Error" || label === "Not found" || label === "Not ready"
}

// True when any listed sensor's connection gave up (gaveUp above).
function anyGaveUp(mgr) {
    if (!mgr.instances) return false     // the dependency read; also guards a manager with none
    var list = mgr.imuDeviceList
    for (var i = 0; i < list.length; ++i)
        if (gaveUp(mgr.instanceFor(list[i].id))) return true
    return false
}

// True when at least one connected IMU is not yet *successfully* calibrated. The rule
// is the device's own fullyCalibrated (ImuDeviceBase): Witmotion = anatomical solve AND
// both mount checks; HackMotion = the device's calibration state CALIBRATED. Never
// re-derive it from one kind's properties (#50).
function anyNeedsCalibration(mgr) {
    if (!mgr.instances) return false     // the dependency read; also guards a manager with none
    var list = mgr.imuDeviceList
    for (var i = 0; i < list.length; ++i) {
        var inst = mgr.instanceFor(list[i].id)
        if (inst && inst.imuConnected && inst.fullyCalibrated !== true) return true
    }
    return false
}
