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

// TEST-ONLY stand-in for the app-only enum carrier the session wizard reads.
//
// The wizard uses SessionController.Wrist. The real class lives in session_controller.cpp,
// an app-only source (it pulls in the session machinery), so the module this test declares
// does not contain it. This declares the SAME QML name with the SAME enumerators and values
// and nothing else.
//
// The camera perspective needs no stand-in: QML names it Perspective.FaceOn, the one
// perspective type (src/Vocabulary/Recording/perspective.h), which every QML module registers
// through PP_QML_SOURCES (src/Gui/shell/perspective_qml.h), this suite's included.
//
// ⚠ THE VALUES MUST MATCH THE REAL HEADER EXACTLY:
//     src/Gui/session/session_controller.h   enum class Type  (l.55)
// Lint W6 (session_setup_lint_test) checks it.
//
// QML_UNCREATABLE: QML only ever reads its enum.

#include <QObject>
#include <QtQml/qqmlregistration.h>

class SetupSessionControllerStandIn : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SessionController)
    QML_UNCREATABLE("test stand-in: enum carrier only")
public:
    enum class Type { None = -1, Swing = 0, Wrist = 1, Grf = 2, Coach = 3 };
    Q_ENUM(Type)
};
