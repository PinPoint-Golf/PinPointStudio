/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// HOW A SCREEN REACHES A SESSION'S THIN ADAPTER (refactor step 3.7-a; build/refactor/step-3/BRIEF.md §4.10; D1: DEC-164;
// D3: DEC-165). `diagnosticsAdapters` in QML (a context property main.cpp sets, as `app` is):
//
//   diagnosticsAdapters.of(app.sessionAt(folder), surface)
//
// answers the Session's thin adapter for that surface ("panel": the stage panel; "cast": the wall cast) — a
// SessionDiagnosticsModel MADE BY C++ (SessionDiagnosticsModel::adapterFor), the Session's child, showing the Session's
// one SessionDiagnostics (LIVE-2). QML never creates one (lint A2). With no Session:
//   • a folder IN THE CURRENT GOLFER'S FOLDER that the golfer's Athlete does not hold — today a session whose file names
//     another athlete id (a session recorded on the cabin PC, opened on a machine whose record of the same golfer has
//     another id, until migration M1 gives one id: 3.0's gate) — keeps TODAY'S PATH: an engine-mode model, made here by
//     C++ (not by QML), that judges the folder as the panel's private model did (NOTES-a.md J-a3; for Mark);
//   • any other folder — a session captured with no golfer (the library's `unknown` folder: Mark's N1, kept with no
//     diagnostics), or no folder yet — an EMPTY adapter for the surface, which judges nothing and shows the panel's cold
//     state; the panel says why (its no-golfer notice).
//
// THE ADAPTER'S SESSION-FACING HALF is here too (session_diagnostics_adapter.cpp: SessionDiagnosticsOwnerLink), so the
// model's own file links nothing of the domain model: only the app and the tests that link the Session and its owner
// compile this pair. NOT a QML type (not in PP_QML_SOURCES), like diag_parallel_run: the QML suites need none of it.
//
// Adapters ring (src/Gui): reads the Session and its SessionDiagnostics; writes nothing of theirs but through their
// commands (the live freeze through Session::freezeClubBaseline).

#include "session_diagnostics_model.h"   // whole, for moc (of()'s type)

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class SessionDiagnosticsAdapters : public QObject
{
    Q_OBJECT
public:
    explicit SessionDiagnosticsAdapters(QObject *parent = nullptr) : QObject(parent) {}
    // `session`: a pinpoint::Session (app.sessionAt's answer), or null; `folder`: the folder the screen chose. Never null.
    Q_INVOKABLE SessionDiagnosticsModel *of(QObject *session, const QString &surface, const QString &folder = QString());
    // The current golfer's folder (main.cpp: AthleteController's), for the not-held case above.
    void setAthleteFolder(std::function<QString()> folder) { m_athleteFolder = std::move(folder); }

private:
    QHash<QString, SessionDiagnosticsModel *> m_empty;    // one empty adapter per surface, children of this
    QHash<QString, SessionDiagnosticsModel *> m_unheld;   // one engine-mode model per surface (today's path), ditto
    std::function<QString()>                  m_athleteFolder;
};
