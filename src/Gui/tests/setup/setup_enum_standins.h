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

// TEST-ONLY stand-ins for the two app-only enum carriers the session wizard reads.
//
// The wizard uses CameraInstance.FaceOn / .DownTheLine / .Impact / .None and
// SessionController.Wrist. The real classes live in camera_instance.cpp and
// session_controller.cpp, which are app-only sources (they pull in the capture and
// session machinery), so the module this test declares does not contain them. These
// declare the SAME QML names with the SAME enumerators and values and nothing else.
//
// ⚠ THE VALUES MUST MATCH THE REAL HEADERS EXACTLY:
//     src/Gui/cameras/camera_instance.h      enum Perspective (l.146)
//     src/Gui/session/session_controller.h   enum class Type  (l.55)
// Lint W6 (session_wizard_refactor_design.md §7.5) is what will check that; until it
// exists, a change to either real enum must be copied here by hand.
//
// Both are QML_UNCREATABLE: QML only ever reads their enums.

#include <QObject>
#include <QtQml/qqmlregistration.h>

class SetupCameraInstanceStandIn : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CameraInstance)
    QML_UNCREATABLE("test stand-in: enum carrier only")
public:
    enum Perspective { None = 0, DownTheLine = 1, FaceOn = 2, Other = 3, Impact = 4 };
    Q_ENUM(Perspective)
};

class SetupSessionControllerStandIn : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SessionController)
    QML_UNCREATABLE("test stand-in: enum carrier only")
public:
    enum class Type { None = -1, Swing = 0, Wrist = 1, Grf = 2, Coach = 3 };
    Q_ENUM(Type)
};
