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
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE PINPOINT_WORKONS_DIR DEV FOLDER'S ATHLETE (refactor 3.7-f; J-56, REVIEW-f R2-m2). A dev and probe seam only (the
// golfer never sees it): Home is pointed at a folder that may be no listed athlete's. Its Athlete has THE FOLDER'S OWN
// ATHLETE ID, from its athlete.json (the caller reads it: the composition root, which may reach the store; never one made
// up from its name, C5). An id the athlete store already knows is never made a second time over the one session store (its
// legacy sessions' ids would collide and the held folders be re-pointed): the store's own Athlete is used. No record: no
// Athlete. Application ring, header-only.

#include "../DomainModel/athlete.h"

#include <QList>
#include <QString>

#include <functional>
#include <memory>

namespace pinpoint {

struct DevAthleteDoors {
    QList<Athlete *>                           held;     // the athletes the store has handed out
    QList<AthleteId>                           listed;   // the athletes the store lists
    std::function<Athlete *(const AthleteId &)> load;    // the store's load
    AthleteRepository                         *caches = nullptr;     // the dev Athlete's own (its caches beside its sessions)
    SessionRepository                         *sessions = nullptr;   // the one session store
};

// `id`/`record`: the folder's athlete.json (null id: none, or unreadable). `dev` holds the one made here. `why`: what was
// done, for the app log.
inline Athlete *devAthleteOf(const QString &folder, const AthleteId &id, AthleteRecord record, const DevAthleteDoors &doors,
                             std::unique_ptr<Athlete> &dev, QString *why)
{
    if (id.isNull()) {
        if (why) *why = QStringLiteral("no athlete record: nothing is listed (J-56)");
        return nullptr;
    }
    for (Athlete *a : doors.held)
        if (a && a->id() == id) {
            if (why) *why = QStringLiteral("its athlete id is a library athlete's: that Athlete is used (R2-m2)");
            return a;
        }
    if (doors.listed.contains(id) && doors.load) {
        if (Athlete *a = doors.load(id)) {
            if (why) *why = QStringLiteral("its athlete id is a library athlete's: that Athlete is used (R2-m2)");
            return a;
        }
    }
    if (dev && dev->id() == id)
        return dev.get();
    const QString name = folder.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    if (record.name.isEmpty()) record.name = name;
    dev = std::make_unique<Athlete>(doors.caches, doors.sessions, id);
    dev->setStored(record, folder, SessionOwner{ id, name, folder.section(QLatin1Char('/'), 0, -2) },
                   Athlete::Source::Library, /*readOnly=*/true);
    if (why) *why = QStringLiteral("an Athlete made for it, under its own athlete id");
    return dev.get();
}

} // namespace pinpoint
