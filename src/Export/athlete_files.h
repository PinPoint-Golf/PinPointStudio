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

// AthleteFiles — the names of an athlete folder's own files, spelled once (H-12; refactor step 2.3,
// brief §4.7). Adapters ring (src/Export), beside the repository that reads and writes them
// (athlete_store.h). Lint A4 names this file the owner of every name below.
//
//   <library>/<athlete>/
//     athlete.json            FILE-ATH-01: the record and the bag (DEC-05, DEC-151). Written by
//                             AthleteStore only
//     .pinpoint-lock          EXT-12: the advisory lock (FolderLock, src/Core/folder_lock.h)
//     <session folders>/      SessionFiles (session_files.h)
//
//     tendencies.json         FILE-ATH-02: the athlete's tendencies and club distances (named here; written by
//                             AthleteDiagnostics through AthleteRepository since refactor 3.7-f)
//     swing_themes[.<scope>[.<filter>]].json   FILE-ATH-03: the swing summary, per club scope and shot
//                             filter (every one: AthleteDiagnostics since 3.7-f, through AthleteRepository; a
//                             scope's or filter's from the sessions' judged rows, DEC-195)
//
//     outcome_corrections.jsonl   FILE-ATH-04: the tuning log, append-only (appendTuningLine, below)
//     (FILE-ATH-05, fault_profile.json, is retired: deleted with its reader and writer in refactor 3.8-ii; H-11, D-07.
//      It was never written, and no app or tool names it now.)
//
// Lint A4 names this file the owner of every name above (IM-S2-6: DEC-160; step 2.8b), so each is spelled
// here and nowhere else in src/. Header-only (like SessionFiles), so the diagnostics model's six link lines
// gain folder_lock.cpp only, not the store.

#include "../Core/atomic_write.h"   // AtomicWriteResult
#include "../Core/folder_lock.h"

#include "../Identity/identity.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QStringList>

namespace pinpoint {

class AthleteFiles
{
public:
    // "athlete.json"
    static QString athleteFileName() { return QStringLiteral("athlete.json"); }
    static QString athleteFile(const QString &athleteDir) { return athleteDir + QLatin1Char('/') + athleteFileName(); }

    // THE ONE READER OF athlete.json (R-PERS-1; step 2.8b, J-40): AthleteStore and the legacy-owner lookup read
    // it here. The one exception is the one-off migrator, tools/migrate/migrate_step2.cpp (its M1 scan and M4),
    // which reads the file itself under J-62 (Mark, 15 Nov 2026: the migration tool's own reads and writes are
    // accepted as a one-off) and is deleted in step 3.0. The file parsed and its envelope checked: the schema
    // `pinpoint.athlete/<n>` (n ≥ 1; n > 1 is a newer build's: `readOnly`) and an `athleteId`. Missing: the
    // folder has none (a settings athlete's, the corpus, a scratch folder). Unreadable: it is there but cannot
    // be opened, parsed, or is not an athlete file — kept as it is for someone to look at, never listed and
    // never overwritten (`why` says which). Header-only, so the swing door and the archiver (which name a
    // legacy session's owner by it: J-39, DEC-150) need none of the store's sources.
    struct Read {
        enum class State { Missing, Unreadable, Read } state = State::Missing;
        AthleteId   id;              // Read only
        int         version  = 0;    // the schema's n (Read only)
        bool        readOnly = false;   // written by a newer build (n > 1)
        QJsonObject json;            // the whole file (Read only), unknown keys and all
        QString     why;             // Unreadable: what is wrong, naming the file

        bool isRead() const { return state == State::Read; }
    };
    static Read read(const QString &athleteDir)
    {
        Read r;
        QFile f(athleteFile(athleteDir));
        if (!f.exists())
            return r;
        r.state = Read::State::Unreadable;
        if (!f.open(QIODevice::ReadOnly)) {
            r.why = QStringLiteral("cannot read %1 (%2)").arg(f.fileName(), f.errorString());
            return r;
        }
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        const QJsonObject   o   = doc.object();
        const QString schema = o.value(QStringLiteral("schema")).toString();
        const QString prefix = QStringLiteral("pinpoint.athlete/");
        const int version = schema.startsWith(prefix) ? schema.mid(prefix.size()).toInt() : 0;
        const AthleteId id = AthleteId::fromString(o.value(QStringLiteral("athleteId")).toString());
        if (pe.error != QJsonParseError::NoError || !doc.isObject() || version < 1 || id.isNull()) {
            r.why = QStringLiteral("not an athlete file (schema %1): %2").arg(schema, f.fileName());
            return r;
        }
        r.state    = Read::State::Read;
        r.id       = id;
        r.version  = version;
        r.readOnly = version > 1;
        r.json     = o;
        return r;
    }

    // "tendencies.json" (FILE-ATH-02)
    static QString tendenciesFileName() { return QStringLiteral("tendencies.json"); }
    static QString tendenciesFile(const QString &athleteDir) { return athleteDir + QLatin1Char('/') + tendenciesFileName(); }
    // "swing_themes.json", "swing_themes.<scope>.json", "swing_themes.<scope>.<filter>.json" (FILE-ATH-03): one
    // ".<key>" per entry of `keys`, which the caller has made file-safe (clubScopeFileKey).
    static QString themesFile(const QString &athleteDir, const QStringList &keys = {})
    {
        QString name = QStringLiteral("swing_themes");
        for (const QString &k : keys) name += QLatin1Char('.') + k;
        return athleteDir + QLatin1Char('/') + name + QStringLiteral(".json");
    }

    // THE DERIVED CACHES' READ AND WRITE (FILE-ATH-02, FILE-ATH-03; refactor 3.7-f): a JSON object read whole, and written
    // whole through atomicWrite. AthleteStore's readCache / writeCache (and the tools' cache-only repository) forward here.
    // `exists` false for no file; `ok` false (with `error`) for one that is not a JSON object.
    struct CacheRead { bool exists = false; bool ok = false; QJsonObject root; QString error; };
    static CacheRead readCache(const QString &file)
    {
        CacheRead r;
        QFile f(file);
        if (!f.exists()) return r;
        r.exists = true;
        if (!f.open(QIODevice::ReadOnly)) { r.error = f.errorString(); return r; }
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) { r.error = QStringLiteral("not a JSON object"); return r; }
        r.ok   = true;
        r.root = doc.object();
        return r;
    }
    static bool writeCache(const QString &file, const QJsonObject &root, QString *why)
    {
        return atomicWrite(file, QJsonDocument(root).toJson(QJsonDocument::Compact), why);
    }

    // "outcome_corrections.jsonl" (FILE-ATH-04: the tuning log)
    static QString tuningLogFileName() { return QStringLiteral("outcome_corrections.jsonl"); }
    static QString tuningLogFile(const QString &athleteDir) { return athleteDir + QLatin1Char('/') + tuningLogFileName(); }

    // ".pinpoint-lock": FolderLock's own, named here so the list is complete.
    static QString lockFileName() { return FolderLock::fileName(); }

    // THE TUNING LOG's append (FILE-ATH-04: append-only, never rewritten; DEC-43): one line added to
    // `logFile` under its athlete folder's advisory lock, held for this one append (brief §4.9). The
    // athlete repository's write (AthleteStore::appendTuningLog forwards here).
    static AtomicWriteResult appendTuningLine(const QString &logFile, const QByteArray &line,
                                              int lockWaitMs = FolderLock::kWaitMs)
    {
        const QString folder = QFileInfo(logFile).absolutePath();
        FolderLock    lock(folder);
        if (lock.lock(lockWaitMs) != FolderLock::Result::Acquired)
            return { false, lock.errorString().isEmpty() ? QStringLiteral("The athlete folder %1 is locked.").arg(folder)
                                                         : lock.errorString(), lock.holder() };
        QFile f(logFile);
        if (!f.open(QIODevice::Append | QIODevice::Text))
            return { false, QStringLiteral("Could not append to %1.").arg(logFile), f.errorString() };
        QByteArray bytes = line;
        if (!bytes.endsWith('\n'))
            bytes.append('\n');
        if (f.write(bytes) != bytes.size() || !f.flush())
            return { false, QStringLiteral("Could not append to %1.").arg(logFile), f.errorString() };
        return { true, {}, {} };
    }
};

} // namespace pinpoint
