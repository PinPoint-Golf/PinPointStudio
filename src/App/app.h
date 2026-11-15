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

// App — the application's one object for the views, `app` in QML (refactor step 3.4a; build/refactor/step-3/
// BRIEF.md §4.10; D1: DEC-164; IM-S3-7). Application ring (src/App: DEC-11).
//
//   sessionAt(folder)   THE ONE INTERIM MEMBER (D1): the Session whose folder a view still chooses itself
//                       (the diagnostics panel's sessionDir, until step 4's sessionOnScreen replaces the
//                       choice), asked of the CURRENT Athlete, which translates the folder at the edge
//                       (Athlete::sessionAt; DEC-90). Null with no current athlete, or for a folder that is
//                       not one of its sessions. The Session stays its Athlete's child: QML never owns it
//
// Step 4 grows it (app.session, app.athlete, app.state: R-QML-1); nothing is re-pointed twice. It includes no
// repository (P11): the current athlete comes from the composition root as a function.
//
// THE COMPOSITION ROOT'S CALLABLES (D18 (c); DEC-167) are declared here, beside it, for the application
// services and AthleteController that take them (from run 3.4a-ii and steps 3.6, 3.8): functions over the
// athlete repository that main.cpp makes and hands out, so no service includes a repository and no new domain
// object stands between them (no Library object: IM-S3-11 not needed).

#include "../DomainModel/athlete.h"   // Athlete, AthleteRecord, WriteResult
#include "../DomainModel/session.h"   // Session (sessionAt's type, whole for moc)
#include "../Identity/identity.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace pinpoint {

// D18 (c): how the application services and AthleteController reach the athletes they act on. Made once by
// the composition root over its one athlete repository; the repository reads the library setting (SET-03) on
// every call, so a library re-pointed needs no new callables. The trash's purge joins them in step 3.8.
struct AthleteCallables {
    std::function<QList<AthleteId>()>                              listAthletes;    // the library's athletes
    std::function<Athlete *(const AthleteId &)>                    loadAthlete;     // the one instance (DEC-52)
    std::function<Athlete *(const AthleteRecord &, WriteResult *)> createAthlete;   // mints the AthleteId
};

class App : public QObject
{
    Q_OBJECT
public:
    // `currentAthlete`: the athlete the app is on (today AthleteController's; step 4's app.athlete).
    explicit App(std::function<Athlete *()> currentAthlete, QObject *parent = nullptr);

    Q_INVOKABLE pinpoint::Session *sessionAt(const QString &folder) const;

private:
    std::function<Athlete *()> m_currentAthlete;
};

} // namespace pinpoint
