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

// The app's screens, in Main.qml's StackLayout order. Every screen index in QML and C++ is named
// from here — never a raw integer (docs/design/ux_refactor_away_from_4_modes.md §6 Phase 1).
//
// Header-only and listed in PP_QML_SOURCES, so the app and both QML test modules (qml_ui_test,
// session_setup_ui_test) see the same type; NavigationController is app-only and cannot carry it.
// Named AppScreen because QtQuick already registers an attached type called Screen.
//
// Home stays first: NavigationController::navigateRail(Home) is the history-clearing reset.
//
// There is no Swing model screen: the DiagnosticModel editor (called "Swing model" wherever users
// see it) lives in Settings → Reference (slot 10), reached mid-session through Settings, which the
// session lock leaves open. No rail button (Mark, 28 Oct).
class AppScreen : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AppScreen exists for enum access only")

public:
    enum Value {
        Home = 0,
        Session,     // the one live/review session screen (ScreenWrist)
        Sessions,    // the current athlete's sessions (a ScreenAthleteForm on its Sessions tab)
        WorkOns,     // ScreenSwingDiagnostics
        Athlete,     // ScreenAthleteForm: new / edit athlete
        Athletes,    // ScreenAthletePicker
        System,      // ScreenResourceMonitor
        Settings,    // ScreenSettings
        Developer,   // PlayPage (unreachable from the UI, as before)
        Wizard,      // ScreenSessionSetup
        Count
    };
    Q_ENUM(Value)
};
