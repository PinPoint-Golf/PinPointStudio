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

// AthleteCacheRepository — an AthleteRepository that keeps ONLY an athlete's derived caches (FILE-ATH-02 tendencies.json,
// FILE-ATH-03 swing_themes.json), for the headless tools and the tests that make their own Athlete over a folder with no
// athlete.json (athlete_dump, work_ons_controller_test). It lists, loads, creates, saves and trashes nothing, and keeps no
// tuning log (an Athlete with no repository did not either). The files by AthleteFiles' names, written as AthleteStore
// writes them (refactor 3.7-f). Adapters ring (src/Stores). Header-only.

#include "athlete_files.h"
#include "../DomainModel/athlete_repository.h"

namespace pinpoint {

class AthleteCacheRepository final : public AthleteRepository
{
public:
    QList<AthleteId>    list() override { return {}; }
    Athlete            *load(const AthleteId &) override { return nullptr; }
    AthleteCreateResult create(const AthleteRecord &) override { return { nullptr, refuse() }; }
    WriteResult         save(Athlete &, const AthleteRecord &) override { return refuse(); }
    AthleteRemoval      trash(const AthleteId &) override { AthleteRemoval r; r.result = refuse(); return r; }
    AthleteActivity     readActivity(const SessionOwner &) const override { return {}; }
    WriteResult         appendTuningLine(const Athlete &, const QByteArray &) override
    {
        return { WriteResult::Status::Failed, QStringLiteral("no athlete repository") };
    }

    JsonFileRead readCache(const Athlete &athlete, Cache which, const QStringList &keys = {}) override
    {
        JsonFileRead out;
        if (athlete.folder().isEmpty())
            return out;
        const AthleteFiles::CacheRead r = AthleteFiles::readCache(path(athlete.folder(), which, keys));
        out.exists = r.exists;
        out.ok     = r.ok;
        out.root   = r.root;
        out.error  = r.error;
        ++reads;
        return out;
    }
    WriteResult writeCache(const Athlete &athlete, Cache which, const QJsonObject &root, const QStringList &keys = {}) override
    {
        if (athlete.folder().isEmpty())
            return { WriteResult::Status::Failed, QStringLiteral("The athlete has no folder for its derived files.") };
        if (!failWrites.isEmpty())
            return { WriteResult::Status::Failed, failWrites };
        QString why;
        if (!AthleteFiles::writeCache(path(athlete.folder(), which, keys), root, &why))
            return { WriteResult::Status::Failed, why };
        ++writes;
        return { WriteResult::Status::Written, {} };
    }

    static QString path(const QString &athleteDir, Cache which, const QStringList &keys = {})
    {
        return which == Cache::Tendencies ? AthleteFiles::tendenciesFile(athleteDir) : AthleteFiles::themesFile(athleteDir, keys);
    }

    // For the tests: reads and writes made; a non-empty reason makes every write fail with it (a write block).
    int     reads = 0, writes = 0;
    QString failWrites;

private:
    static WriteResult refuse() { return { WriteResult::Status::Failed, QStringLiteral("a cache-only athlete repository") }; }
};

} // namespace pinpoint
