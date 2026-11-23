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

// A scratch athlete library for the athlete tests (refactor step 2.3): a temporary library folder, the
// session store and the athlete store over it (the OS trash never used: deletes land in the library's own
// trash, inside the temporary folder), and an AthleteController wired to both — through the callables over
// the athlete store (callablesOver), as main.cpp wires the app — and to a synchronous one session list over the same
// athletes (refactor step 5.7b: the activity is its rows'). The entries' summaries are the test's own read (the swing
// folders counted, nothing else), so no swing reader is linked.
// The caller points QSettings at a scratch folder first (QSettings::setPath), so nothing here can reach
// the real settings file or the real library.

#include "athlete_controller.h"
#include "../../DomainModel/session_roll_up.h"
#include "../../Export/athlete_store.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_paths.h"
#include "../session/session_list_presenter.h"

#include <QTemporaryDir>

#include <memory>

// The composition root's callables (D18 (c)) over a test's athlete store, as main.cpp makes them.
inline pinpoint::AthleteCallables callablesOver(pinpoint::AthleteStore &store)
{
    return {
        [&store] { return store.list(); },
        [&store](const pinpoint::AthleteId &id) { return store.load(id); },
        [&store](const pinpoint::AthleteRecord &record, pinpoint::WriteResult *result) {
            const pinpoint::AthleteCreateResult made = store.create(record);
            if (result)
                *result = made.result;
            return made.athlete;
        },
        [&store](const pinpoint::SessionOwner &owner) { return store.readActivity(owner); },
        [&store](bool create, pinpoint::WriteResult *result) { return store.unassigned(create, result); },
    };
}

struct AthleteTestLibrary {
    QTemporaryDir                         dir;
    pinpoint::SessionStore                sessions;
    std::unique_ptr<pinpoint::AthleteStore> athletes;
    pinpoint::SessionListPresenter        list;         // before the controller: it goes after it
    std::unique_ptr<AthleteController>    controller;

    AthleteTestLibrary()
    {
        athletes = std::make_unique<pinpoint::AthleteStore>([this] { return root(); }, &sessions);
        athletes->setOsTrashForTest(false);
        athletes->setLockWaitForTest(200);
        sessions.setLockWaitForTest(200);
        sessions.setOsTrashForTest(false);   // a discarded empty session goes to the library's own trash, never ~/.Trash
        // The summary's swing count, as SessionIndexSummaries::read lists them (the rest stays NA).
        sessions.setSummaryReads([](const QString &dir) {
            pinpoint::SessionIndexSummary s;
            s.read   = true;
            s.swings = pinpoint::rollUpSwings({}, int(pinpoint::SwingPaths::swingFolders(dir).size()));
            return s;
        });
        list.setSynchronous(true);
    }

    QString root() const { return dir.path(); }

    // A controller over this library (synchronous activity, so a test reads it at once).
    AthleteController &makeController()
    {
        controller = std::make_unique<AthleteController>();
        controller->setSynchronousActivity(true);
        controller->setCallables(callablesOver(*athletes), &sessions);
        controller->setSessionList(&list);
        return *controller;
    }
};
