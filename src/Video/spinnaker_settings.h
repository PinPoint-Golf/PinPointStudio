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

// The camera settings PPS owns on a FLIR camera, written on every connect
// (docs/design/flir_camera_settings.md). Kept apart from VideoInputSpinnaker so
// the app and tools/camera/spinnaker_settings_probe run the SAME writes: the
// probe drives the real cameras through role changes and measures the result.
//
// Depends on the Spinnaker SDK and QtCore only. Include Spinnaker's headers
// before Qt's (macro clashes such as 'signals'); this header does so itself.

#ifdef HAVE_SPINNAKER
#include "SpinnakerPlatform.h"
#undef SPINNAKER_DEPRECATED_CLASS
#define SPINNAKER_DEPRECATED_CLASS(msg) class SPINNAKER_API __declspec(deprecated(msg))
#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"

#include <QRectF>
#include <QStringList>
#include <QVariantMap>

namespace pinpoint::spinnaker {

// What the writes did, for the caller to log: the app forwards info to ppInfo
// and warn to ppWarn; the probe prints both.
struct SettingsLog {
    QStringList info;
    QStringList warn;
};

// What one connect asks of the camera, on top of the camera's factory set
// (loadFactoryDefaults). Every member decides a node's value; none means "leave
// it". exposureUs <= 0 is auto exposure (limited to the frame period), gainDb
// < 0 is auto gain (over the sensor's range; a request above the range is the
// range's top), strobe false is Line1 released, fps <= 0 is the maximum at
// this ROI, blackLevelLift is percent added to the camera's own calibrated
// BlackLevel. The impact camera differs from every other camera only in the
// values.
struct ConnectSettings {
    double exposureUs     = 0.0;
    double gainDb         = -1.0;
    bool   strobe         = false;
    double fps            = 0.0;
    double blackLevelLift = 0.0;
};

struct ConnectApplied {
    double      gainDb = -1.0;   // read back when locked, else -1
    SettingsLog log;
};

// The camera's own factory user set (UserSetSelector=Default, UserSetLoad):
// every node back to the value it left the factory with, including the ones
// PPS does not know about and the per-camera calibration (BlackLevel differs
// between the two studio Chameleon3s). ~25 ms. First thing on a connect,
// before anything else is written. False (with a warning) if the camera has
// no factory set or refuses the load.
bool loadFactoryDefaults(Spinnaker::GenApi::INodeMap &nodeMap, SettingsLog &out);

// Every node the writes above touch, read back as text: node name → value,
// "absent" / "unreadable" where the camera says so; Line1's selector-dependent
// nodes as "Line1.<node>", UserOutput1's value as "UserOutput1.Value". Safe
// while streaming (reads only, bar the line/user-output selectors). Shared by
// the app's readBackSettings() and the hardware probe.
QVariantMap readBack(Spinnaker::GenApi::INodeMap &nodeMap);

// Hardware ROI from a normalised crop; an empty rect (or the unit rect) is the
// full sensor. Before BeginAcquisition() only.
void applyRoi(Spinnaker::GenApi::INodeMap &nodeMap, const QRectF &crop, SettingsLog &out);

// Every tuning node, from the request alone. After applyRoi(), before
// BeginAcquisition(). See the ORDER note in the .cpp.
ConnectApplied applyConnectSettings(Spinnaker::GenApi::INodeMap &nodeMap, const ConnectSettings &s);

} // namespace pinpoint::spinnaker
#endif // HAVE_SPINNAKER
