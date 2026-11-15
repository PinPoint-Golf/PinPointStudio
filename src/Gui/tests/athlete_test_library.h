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
// trash, inside the temporary folder), and an AthleteController wired to both — as main.cpp wires the app.
// The caller points QSettings at a scratch folder first (QSettings::setPath), so nothing here can reach
// the real settings file or the real library.

#include "athlete_controller.h"
#include "../../Export/athlete_store.h"
#include "../../Export/session_store.h"

#include <QTemporaryDir>

#include <memory>

struct AthleteTestLibrary {
    QTemporaryDir                         dir;
    pinpoint::SessionStore                sessions;
    std::unique_ptr<pinpoint::AthleteStore> athletes;
    std::unique_ptr<AthleteController>    controller;

    AthleteTestLibrary()
    {
        athletes = std::make_unique<pinpoint::AthleteStore>([this] { return root(); }, &sessions);
        athletes->setOsTrashForTest(false);
        athletes->setLockWaitForTest(200);
        sessions.setLockWaitForTest(200);
        sessions.setOsTrashForTest(false);   // a discarded empty session goes to the library's own trash, never ~/.Trash
    }

    QString root() const { return dir.path(); }

    // A controller over this library (synchronous activity, so a test reads it at once).
    AthleteController &makeController()
    {
        controller = std::make_unique<AthleteController>();
        controller->setSynchronousActivity(true);
        controller->setRepositories(athletes.get(), &sessions);
        return *controller;
    }
};
