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

#include "athlete_store.h"

#include "athlete_files.h"
#include "session_store.h"
#include "swing_paths.h"
#include "../Core/atomic_write.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace pinpoint {

namespace {

const QString kSettingsGroup = QStringLiteral("athletes");

WriteResult failed(const QString &why) { return { WriteResult::Status::Failed, why }; }
WriteResult written() { return { WriteResult::Status::Written, {} }; }

// The record as athlete.json will hand it back: JSON's types, so a re-read compares equal.
AthleteRecord asStoredInJson(const AthleteRecord &r)
{
    return AthleteRecord::fromMap(QJsonObject::fromVariantMap(r.toMap()).toVariantMap());
}

// "Mark" → "Mark-2": the suffix fits inside sanitise's 64 characters, so the token survives a second
// pass through sanitise unchanged (the session store composes the folder from it).
QString numbered(const QString &base, int n)
{
    const QString suffix = QLatin1Char('-') + QString::number(n);
    QString stem = base.left(64 - suffix.size());
    while (stem.endsWith(QLatin1Char('-')) || stem.endsWith(QLatin1Char('.')))
        stem.chop(1);
    return (stem.isEmpty() ? QStringLiteral("athlete") : stem) + suffix;
}

} // namespace

AthleteStore::AthleteStore(std::function<QString()> libraryRoot, SessionRepository *sessions)
    : m_root(std::move(libraryRoot)), m_sessions(sessions)
{
}

AthleteStore::~AthleteStore() = default;

QString AthleteStore::libraryDir() const
{
    // As SessionStore::athleteFolder resolves the library: SET-03, else the app-data fallback.
    const QString root = m_root ? m_root().trimmed() : QString();
    return root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/swings")
                          : root;
}

SessionOwner AthleteStore::ownerFor(const AthleteId &id, const QString &token) const
{
    return { id, token, m_root ? m_root() : QString() };
}

// ── Reading ──────────────────────────────────────────────────────────────────────────────────────

QList<AthleteStore::Found> AthleteStore::scan() const
{
    QList<Found> out;
    QSet<AthleteId> seen;

    // The library's athlete.json files, by folder name.
    const QDir lib(libraryDir());
    for (const QString &name : lib.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString dir = lib.filePath(name);
        // The one reader of the file (AthleteFiles::read; R-PERS-1, J-40).
        const AthleteFiles::Read rd = AthleteFiles::read(dir);
        if (rd.state == AthleteFiles::Read::State::Missing)
            continue;   // not an athlete's folder (yet): a settings athlete's, the corpus, a scratch folder
        if (!rd.isRead()) {
            // Kept as it is for someone to look at: never listed, never overwritten.
            ppWarn() << "[AthleteStore]" << rd.why;
            continue;
        }
        const QJsonObject &o  = rd.json;
        const AthleteId    id = rd.id;
        if (seen.contains(id)) {
            ppWarn() << "[AthleteStore]" << AthleteFiles::athleteFile(dir) << "names athlete" << id.toString()
                     << "whose record is already in another folder - not listed twice";
            continue;
        }
        if (SwingPaths::sanitise(name) != name)
            ppWarn() << "[AthleteStore] the athlete folder" << dir
                     << "is not a name this app makes: its sessions are looked for under"
                     << SwingPaths::sanitise(name);
        seen.insert(id);
        Found fd;
        fd.id       = id;
        fd.owner    = ownerFor(id, name);
        fd.folder   = SessionStore::athleteFolder(fd.owner);
        fd.record   = AthleteRecord::fromMap(o.value(QStringLiteral("record")).toObject().toVariantMap());
        fd.source   = Athlete::Source::Library;
        fd.readOnly = rd.readOnly;
        fd.file     = o;
        out.append(fd);
    }

    // This machine's settings records that have no athlete.json yet (until M1).
    QSettings s = ppSettings();
    s.beginGroup(kSettingsGroup);
    for (const QString &key : s.childGroups()) {
        const AthleteId id = AthleteId::fromString(key);
        if (id.isNull() || seen.contains(id))
            continue;   // athlete.json wins
        s.beginGroup(key);
        QVariantMap m;
        for (const QString &k : s.childKeys())
            m.insert(k, s.value(k));
        s.endGroup();
        Found fd;
        fd.id          = id;
        fd.record      = AthleteRecord::fromMap(m);
        fd.owner       = { id, fd.record.name, m_root ? m_root() : QString() };
        fd.folder      = SessionStore::athleteFolder(fd.owner);   // today's derivation, in its one place
        fd.source      = Athlete::Source::Settings;
        fd.settingsKey = key;
        seen.insert(id);
        out.append(fd);
    }
    s.endGroup();
    return out;
}

void AthleteStore::setShotDoors(const ShotDoors &doors)
{
    m_shotDoors = doors;
    for (const std::unique_ptr<Athlete> &a : m_owned)
        a->setShotDoors(doors);
}

void AthleteStore::setDiagnosticsDoors(const DiagnosticsDoors &doors)
{
    m_diagnosticsDoors = doors;
    for (const std::unique_ptr<Athlete> &a : m_owned)
        a->setDiagnosticsDoors(doors);
}

Session *AthleteStore::loadedSession(const SessionId &id) const
{
    for (const std::unique_ptr<Athlete> &a : m_owned)
        if (Session *s = a->loadedSession(id))
            return s;
    return nullptr;
}

Athlete *AthleteStore::hold(const Found &f)
{
    Athlete *a = m_held.value(f.id);
    if (!a) {
        m_owned.push_back(std::make_unique<Athlete>(this, m_sessions, f.id));
        a = m_owned.back().get();
        a->setShotDoors(m_shotDoors);
        a->setDiagnosticsDoors(m_diagnosticsDoors);
        m_held.insert(f.id, a);
    }
    if (f.source == Athlete::Source::Library) {
        m_files.insert(f.id, f.file);
        m_settingsKey.remove(f.id);
    } else {
        m_settingsKey.insert(f.id, f.settingsKey);
        m_files.remove(f.id);
    }
    a->setStored(f.record, f.folder, f.owner, f.source, f.readOnly);
    return a;
}

QList<AthleteId> AthleteStore::list()
{
    QList<AthleteId> ids;
    for (const Found &f : scan()) {
        hold(f);
        ids.append(f.id);
    }
    return ids;
}

Athlete *AthleteStore::load(const AthleteId &id)
{
    if (id.isNull())
        return nullptr;
    if (Athlete *a = m_held.value(id))
        return a;
    list();
    return m_held.value(id);
}

AthleteActivity AthleteStore::readActivity(const SessionOwner &owner) const
{
    // A reader of its own: SessionStore keeps an identity map and belongs to one thread.
    SessionStore reader;
    const QList<SessionIndexEntry> index = reader.enumerate(owner);
    return Athlete::activityOf(index, reader);
}

// ── Writing ──────────────────────────────────────────────────────────────────────────────────────

bool AthleteStore::folderTaken(const QString &token) const
{
    if (QFileInfo::exists(QDir(libraryDir()).filePath(token)))
        return true;
    // A settings athlete's folder that is not made yet (no session so far) is still theirs.
    for (const Athlete *a : m_held)
        if (QFileInfo(a->folder()).fileName().compare(token, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

WriteResult AthleteStore::writeFile(const AthleteId &id, const QString &folder, const AthleteRecord &record)
{
    QJsonObject file = m_files.value(id);   // unknown top-level keys kept
    file.insert(QStringLiteral("schema"), QString::fromLatin1(kSchema));
    file.insert(QStringLiteral("athleteId"), id.toString());
    file.insert(QStringLiteral("record"), QJsonObject::fromVariantMap(record.toMap()));

    FolderLock lock(folder);
    if (lock.lock(lockWait()) != FolderLock::Result::Acquired)
        return failed(lock.errorString().isEmpty() ? QStringLiteral("The athlete folder %1 is locked.").arg(folder)
                                                   : lock.errorString());
    const AtomicWriteResult w = atomicWrite(AthleteFiles::athleteFile(folder),
                                            QJsonDocument(file).toJson(QJsonDocument::Indented),
                                            AtomicWriteOptions::intoExistingFolder());
    if (!w.ok) {
        ppWarn() << "[AthleteStore]" << w.error << w.detail;
        return failed(w.error);
    }
    m_files.insert(id, file);
    return written();
}

WriteResult AthleteStore::writeSettings(const QString &key, const AthleteRecord &before, const AthleteRecord &after)
{
    // Only the keys that changed, as today's writers did: a key never set stays unset.
    const QVariantMap was = before.toMap();
    const QVariantMap now = after.toMap();
    QSettings s = ppSettings();
    s.beginGroup(kSettingsGroup);
    s.beginGroup(key);
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (!was.contains(it.key()) || was.value(it.key()) != it.value())
            s.setValue(it.key(), it.value());
    for (auto it = was.cbegin(); it != was.cend(); ++it)
        if (!now.contains(it.key()))
            s.remove(it.key());
    s.endGroup();
    s.endGroup();
    s.sync();
    if (s.status() != QSettings::NoError)
        return failed(QStringLiteral("The athlete's record could not be saved to the settings file %1.").arg(s.fileName()));
    return written();
}

AthleteCreateResult AthleteStore::create(const AthleteRecord &record)
{
    AthleteCreateResult out;
    if (record.name.trimmed().isEmpty()) {
        out.result = failed(QStringLiteral("An athlete needs a name."));
        return out;
    }
    list();   // the folders taken, as they are now

    // The folder, ONCE (DEC-04, D4): the sanitised name, numbered when taken.
    const QString base  = SwingPaths::sanitise(record.name);
    QString       token = base;
    for (int n = 2; folderTaken(token); ++n)
        token = numbered(base, n);

    const AthleteId    id(QUuid::createUuid());
    const SessionOwner owner  = ownerFor(id, token);
    const QString      folder = SessionStore::athleteFolder(owner);
    if (!QDir().mkpath(folder)) {
        out.result = failed(QStringLiteral("The athlete folder %1 could not be made.").arg(folder));
        return out;
    }
    const AthleteRecord stored = asStoredInJson(record);
    out.result = writeFile(id, folder, stored);
    if (!out.result.ok()) {
        QDir().rmdir(folder);   // only if nothing else is in it
        return out;
    }
    Found f;
    f.id = id; f.folder = folder; f.owner = owner; f.record = stored; f.file = m_files.value(id);
    out.athlete = hold(f);
    ppInfo() << "[AthleteStore] created" << record.name.trimmed() << id.toString() << "in" << folder;
    return out;
}

WriteResult AthleteStore::save(Athlete &athlete, const AthleteRecord &record)
{
    if (athlete.readOnly())
        return { WriteResult::Status::ReadOnly,
                 QStringLiteral("The record of %1 was saved by a newer PinPoint Studio and is not changed by this one.")
                     .arg(athlete.record().name) };
    if (record.name.trimmed().isEmpty())
        return failed(QStringLiteral("An athlete needs a name."));
    if (record == athlete.record())
        return { WriteResult::Status::Unchanged, {} };

    if (athlete.source() == Athlete::Source::Settings) {
        const QString key = m_settingsKey.value(athlete.id(), athlete.id().toString());
        const WriteResult w = writeSettings(key, athlete.record(), record);
        if (!w.ok())
            return w;
        // Today's derivation: a settings athlete's folder follows its name until M1 (E-02's last case).
        const SessionOwner owner{ athlete.id(), record.name, m_root ? m_root() : QString() };
        athlete.setStored(record, SessionStore::athleteFolder(owner), owner, Athlete::Source::Settings, false);
        return w;
    }

    const AthleteRecord stored = asStoredInJson(record);
    const WriteResult   w      = writeFile(athlete.id(), athlete.folder(), stored);
    if (w.ok())   // the folder never moves: a rename changes the name only (E-02)
        athlete.setStored(stored, athlete.folder(), athlete.sessionOwner(), Athlete::Source::Library, false);
    return w;
}

bool AthleteStore::trashPath(const QString &path, const QString &athleteDir, QString *where, QString *why) const
{
    if (m_osTrash && QFile::moveToTrash(path)) {
        *where = QStringLiteral("the system trash");
        return true;
    }
    // No trash on this volume (a share): the library's own, as housekeeping lists and purges it
    // (FILE-LIB-01): <library>/.pinpoint-trash/<athlete>/<stamp>_<name>, one entry.
    const QFileInfo dir(athleteDir);
    const QString   library  = QDir::cleanPath(dir.absolutePath());
    const QString   token    = dir.fileName();
    const QString   name     = QFileInfo(path).fileName();
    const QString   trashDir = library + QStringLiteral("/.pinpoint-trash/") + token;
    if (!QDir().mkpath(trashDir)) {
        *why = QStringLiteral("No trash on this volume, and %1 could not be made.").arg(trashDir);
        return false;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString base  = trashDir + QLatin1Char('/') + stamp + QLatin1Char('_')
                        + (name == token ? token : token + QLatin1Char('_') + name);
    QString dest = base;
    for (int n = 2; QFileInfo::exists(dest) && n < 100; ++n)
        dest = base + QLatin1Char('-') + QString::number(n);
    if (!QDir().rename(path, dest)) {
        *why = QStringLiteral("No trash on this volume, and %1 could not be moved to %2.").arg(path, dest);
        return false;
    }
    *where = dest;
    return true;
}

AthleteRemoval AthleteStore::trash(const AthleteId &id)
{
    AthleteRemoval out;
    list();   // every athlete as it is now: who else resolves to this folder
    Athlete *a = load(id);
    if (!a) {
        out.result = failed(QStringLiteral("No such athlete."));
        return out;
    }
    if (a->readOnly()) {
        out.result = { WriteResult::Status::ReadOnly,
                       QStringLiteral("The record of %1 was saved by a newer PinPoint Studio and is not changed by this one.")
                           .arg(a->record().name) };
        out.note = out.result.error;
        return out;
    }

    const QString name       = a->record().name;
    const QString folder     = a->folder();
    const QString file       = AthleteFiles::athleteFile(folder);
    const bool    hasFolder  = QFileInfo(folder).isDir();
    const bool    inSettings = a->source() == Athlete::Source::Settings;
    const auto    removeSettingsGroup = [this, &id] {
        QSettings s = ppSettings();
        s.beginGroup(kSettingsGroup);
        s.remove(m_settingsKey.value(id, id.toString()));
        s.endGroup();
        s.sync();
    };
    const auto evict = [this, &id] {
        m_held.remove(id);   // the Athlete itself lives on (m_owned) for any holder that still points at it
        m_files.remove(id);
        m_settingsKey.remove(id);
    };

    if (hasFolder) {
        // J-30 (a): the folder's athlete.json names ANOTHER athlete — never move someone else's folder.
        if (QFileInfo::exists(file)) {
            const AthleteId owner = AthleteFiles::read(folder).id;   // the one reader (J-40); null if unreadable
            if (owner != id) {
                QString other = QStringLiteral("another athlete");
                if (const Athlete *o = m_held.value(owner)) other = o->record().name;
                out.result = failed(QStringLiteral("%1 was not deleted: the folder %2 holds the record of %3.")
                                        .arg(name, folder, other));
                out.note = out.result.error;
                ppWarn() << "[AthleteStore]" << out.note;
                return out;
            }
        }
        // J-30 (b): another athlete resolves to the same folder — drop only this athlete's record.
        const QString here = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
        for (const Athlete *o : std::as_const(m_held)) {
            if (o == a || QDir::cleanPath(QFileInfo(o->folder()).absoluteFilePath()).compare(here, Qt::CaseInsensitive) != 0)
                continue;
            if (!inSettings) {   // its record is the athlete.json in the shared folder
                QString where, why;
                if (!trashPath(file, folder, &where, &why)) {
                    out.result = failed(why);
                    out.note   = why;
                    ppWarn() << "[AthleteStore]" << why;
                    return out;
                }
            }
            removeSettingsGroup();
            evict();
            out.result      = written();
            out.folderMoved = false;
            out.note = QStringLiteral("%1 was deleted, but the folder %2 was kept: it is shared with %3, whose sessions are in it.")
                           .arg(name, folder, o->record().name);
            ppInfo() << "[AthleteStore]" << out.note;
            return out;
        }

        // The folder is this athlete's alone: the whole of it goes (DEC-79). A settings athlete's record
        // travels with it, so a restore brings the athlete back.
        bool wroteFile = false;
        if (inSettings && !QFileInfo::exists(file)) {
            out.result = writeFile(id, folder, asStoredInJson(a->record()));
            if (!out.result.ok()) {
                out.note = out.result.error;
                return out;
            }
            wroteFile = true;
        }
        QString where, why;
        if (!trashPath(folder, folder, &where, &why)) {
            if (wroteFile)
                QFile::remove(file);   // as it was: M1 writes it, not a failed delete
            ppWarn() << "[AthleteStore]" << why;
            out.result = failed(why);
            out.note   = why;
            return out;
        }
        out.folderMoved = true;
        ppInfo() << "[AthleteStore] deleted" << name << id.toString() << "- its folder" << folder << "moved to" << where;
    }

    // The settings record goes last, only once the folder has moved (a stale copy of a moved athlete's,
    // too: it would bring the athlete back from settings).
    removeSettingsGroup();
    evict();
    out.result = written();
    return out;
}

WriteResult AthleteStore::appendTuningLog(const QString &logFile, const QByteArray &line, int lockWaitMs)
{
    const AtomicWriteResult w = AthleteFiles::appendTuningLine(logFile, line, lockWaitMs);
    return w.ok ? written() : failed(w.detail.isEmpty() ? w.error : w.error + QStringLiteral(" (") + w.detail + QLatin1Char(')'));
}

WriteResult AthleteStore::appendTuningLine(const Athlete &athlete, const QByteArray &line)
{
    if (athlete.folder().isEmpty())
        return failed(QStringLiteral("The athlete has no folder for its tuning log."));
    return appendTuningLog(AthleteFiles::tuningLogFile(athlete.folder()), line, lockWait());
}

} // namespace pinpoint
