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
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "../../Vocabulary/Recording/perspective.h"

// The Adapters ring's registration of the one perspective type (docs/design/rig_design.md §4.7, §9 question 10):
// QML names a camera's placement as `Perspective.FaceOn`, `Perspective.DownTheLine`, `Perspective.Impact`,
// `Perspective.Other` and `Perspective.None`, the codes the swing file and the settings store.
//
// It declares no enum of its own: QML_FOREIGN_NAMESPACE points the QML name at the Vocabulary header's
// namespace, so there is one type. Uncreatable: a namespace only carries enums.
//
// Header-only and listed in PP_QML_SOURCES beside perspective.h (cmake/PinPointQmlModule.cmake), so the app
// and both QML test modules (qml_ui_test, session_setup_ui_test) register the same name. This directory is on
// all three include paths, which the generated registration needs: it includes this header by its bare name
// behind __has_include, so a missing path would drop the type silently.
namespace PerspectiveForeign {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(pinpoint::recording::camera_perspective)
QML_NAMED_ELEMENT(Perspective)
} // namespace PerspectiveForeign
