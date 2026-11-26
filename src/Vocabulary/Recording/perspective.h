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

// THE ONE PERSPECTIVE TYPE (docs/design/rig_design.md §4.7): a camera's placement, in a header of its own so
// the rig state can hold it without including the whole Recording contract (recording.h includes this).
//
// The enum sits alone in a small namespace that carries Q_NAMESPACE and Q_ENUM_NS, so Qt's meta-object system
// knows its names; Q_NAMESPACE is never put on all of pinpoint::recording. `recording::Perspective` stays the
// name every C++ user writes (the `using` below). QML reaches it as `Perspective.FaceOn`, registered by the
// Adapters ring (src/Gui/shell/perspective_qml.h). The meta-object is made by AUTOMOC wherever this header is
// listed as a source (the QML module's PP_QML_SOURCES, and perspective_test): a target that streams the enum
// to QDebug, puts it in a QVariant or QCOMPAREs it must list this header too, or it does not link.
//
// Vocabulary ring: one value type and its code mapping, no rules.

#include <QObject>

namespace pinpoint::recording::camera_perspective {
Q_NAMESPACE

// ── The one perspective mapping ───────────────────────────────────────────────────────────────
// A camera's placement. The codes are what the swing file stores in streams[].setup.perspective and
// what a camera's `perspective` property (CameraInstance) holds; every reader goes through this mapping instead of
// comparing magic ints (2 = face-on, 4 = impact, 1 = down the line).
enum class Perspective : int { None = 0, DownTheLine = 1, FaceOn = 2, Other = 3, Impact = 4 };
Q_ENUM_NS(Perspective)

} // namespace pinpoint::recording::camera_perspective

namespace pinpoint::recording {

using Perspective = camera_perspective::Perspective;

inline Perspective perspectiveFromCode(int code)
{
    switch (code) {
    case 1:  return Perspective::DownTheLine;
    case 2:  return Perspective::FaceOn;
    case 3:  return Perspective::Other;
    case 4:  return Perspective::Impact;
    default: return Perspective::None;
    }
}
inline int perspectiveCode(Perspective p) { return static_cast<int>(p); }

} // namespace pinpoint::recording
