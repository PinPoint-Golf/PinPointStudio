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

import QtQuick
import QtQuick3D

// A View3D that is never shown, held for the window's lifetime, so destroying a page's 3-D view
// never destroys the LAST View3D in that window.
//
// ⚠ Qt 6.11.0's debug Quick 3D frees the window's 3-D renderer state when its last View3D goes,
// while the render thread is still using it: on Windows/D3D11 the next sync writes into freed
// memory (0xfeeefeee… in Qt6Quick3DRuntimeRenderd) and the process dies — every time, with a
// plain View3D { camera, light, cube } unloaded from a Loader (2026-10-08). Qt's own runner on the
// RELEASE libraries survives the same file; one more View3D anywhere in the window, even this
// invisible one, makes the debug libraries survive it too. The release libraries may carry the
// same use-after-free silently, so the app holds one of these per top-level window rather than
// relying on some other screen's 3-D view staying alive.
//
// It costs nothing: an invisible item is never rendered. Put one in every top-level window that
// can host a View3D (Main.qml, PpSessionDiagnosticsWindow.qml), and in test windows that load and
// unload 3-D views (src/Gui/tests/setup/support). src/Gui/tests/probes/view3d_unload.qml and
// the session_setup_ui suite are the checks.
View3D {
    width: 1
    height: 1
    visible: false
    enabled: false
}
