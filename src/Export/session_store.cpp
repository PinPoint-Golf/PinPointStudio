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

#include "session_store.h"

#include "session_files.h"
#include "swing_paths.h"

#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../Core/pp_debug.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

#include <algorithm>

namespace pinpoint {

namespace {

const QString kSchemaPrefix = QStringLiteral("pinpoint.session/");

QString isoUtc(const QDateTime &t)
{
    return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime parseUtc(const QJsonValue &v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toUTC() : QDateTime();
}

QString statusName(SessionStatus s)
{
    return s == SessionStatus::InProgress ? QStringLiteral("inProgress") : QStringLiteral("ended");
}

// The folder a session folder sits in (its athlete's), spelled as given.
QString parentOf(const QString &folder)
{
    return QFileInfo(folder).path();
}

// The athlete folder's token: the athlete's name sanitised, else the uuid's, else "unknown".
QString athleteToken(const SessionOwner &owner)
{
    return SwingPaths::sanitise(!owner.name.trimmed().isEmpty() ? owner.name : owner.athlete.toString());
}

// The best start time a session older than step 2 can be given at its first write: the earliest of its
// swing folders' creation times (their modification time where the volume keeps no creation time), else
// the session folder's own. Recorded as inferred (origin "backfilled"); M2 (step 2.8) reads the swings'
// own capture times instead.
QDateTime inferredLegacyStart(const QString &folder)
{
    const auto timeOf = [](const QString &path) {
        const QFileInfo fi(path);
        const QDateTime born = fi.birthTime();
        return (born.isValid() ? born : fi.lastModified()).toUTC();
    };
    QDateTime earliest;
    for (const QString &name : SwingPaths::swingFolders(folder)) {
        const QDateTime t = timeOf(folder + QLatin1Char('/') + name);
        if (t.isValid() && (!earliest.isValid() || t < earliest))
            earliest = t;
    }
    return earliest.isValid() ? earliest : timeOf(folder);
}

WriteResult failed(const QString &why) { return { WriteResult::Status::Failed, why }; }
WriteResult written() { return { WriteResult::Status::Written, {} }; }
WriteResult unchanged() { return { WriteResult::Status::Unchanged, {} }; }
WriteResult readOnly(const QString &folder)
{
    return { WriteResult::Status::ReadOnly,
             QStringLiteral("The session %1 was saved by a newer PinPoint Studio and is not changed by this one.")
                 .arg(folder) };
}

} // namespace

// ── The athlete's folder (until step 2.3) ────────────────────────────────────────────────────────

QString SessionStore::athleteFolder(const SessionOwner &owner)
{
    QString root = owner.libraryRoot.trimmed();
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/swings");
        ppWarn() << "[SwingExport] athleteLibraryPath is empty — falling back to" << root;
    }
    return root + QLatin1Char('/') + athleteToken(owner);
}

QString SessionStore::folderTypeLabel(const QString &type)
{
    if (type == QLatin1String("swing")) return QStringLiteral("Swing");
    if (type == QLatin1String("wrist")) return QStringLiteral("Wrist");
    if (type == QLatin1String("grf"))   return QStringLiteral("GRF");
    if (type == QLatin1String("coach")) return QStringLiteral("Coach");
    return {};
}

QDateTime SessionStore::nowUtc() const
{
    return (m_now ? m_now() : QDateTime::currentDateTimeUtc()).toUTC();
}

QDate SessionStore::today() const
{
    return nowUtc().toTimeZone(m_zone).date();
}

// ── Reading ──────────────────────────────────────────────────────────────────────────────────────

SessionStore::FileState SessionStore::readFile(const QString &folder, SessionIndexEntry &out,
                                               QJsonObject &file) const
{
    QFile f(SessionFiles::sessionFile(folder));
    if (!f.exists())
        return FileState::Missing;
    if (!f.open(QIODevice::ReadOnly)) {
        ppWarn() << "[SessionStore] cannot read" << f.fileName() << "-" << f.errorString();
        return FileState::Unreadable;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        ppWarn() << "[SessionStore] unreadable session file" << f.fileName() << "-" << pe.errorString();
        return FileState::Unreadable;
    }
    const QJsonObject o      = doc.object();
    const QString     schema = o.value(QStringLiteral("schema")).toString();
    const int version = schema.startsWith(kSchemaPrefix) ? schema.mid(kSchemaPrefix.size()).toInt() : 0;
    const SessionId id = SessionId::fromString(o.value(QStringLiteral("sessionId")).toString());
    if (version < 1 || id.isNull()) {
        ppWarn() << "[SessionStore] not a session file (schema" << schema << ", sessionId"
                 << o.value(QStringLiteral("sessionId")).toString() << "):" << f.fileName();
        return FileState::Unreadable;
    }
    const QDateTime started = parseUtc(o.value(QStringLiteral("startedAt")));
    out.identity       = SessionIdentity::fromSessionFile(id, folder, o.value(QStringLiteral("type")).toString(),
                                                          started, m_zone);
    out.folder         = folder;
    out.athlete        = AthleteId::fromString(o.value(QStringLiteral("athleteId")).toString());
    out.status         = o.value(QStringLiteral("status")).toString() == QLatin1String("inProgress")
                             ? SessionStatus::InProgress : SessionStatus::Ended;
    out.startedAtUtc   = started;
    out.endedAtUtc     = parseUtc(o.value(QStringLiteral("endedAt")));
    out.hasSessionFile = true;
    out.readOnly       = version > 1;
    file = o;
    return FileState::Read;
}

SessionIndexEntry SessionStore::readFolder(const QString &folder, const AthleteId &owner, QJsonObject &file,
                                           bool requireSessionName) const
{
    SessionIndexEntry e;
    file = {};
    const FileState state = readFile(folder, e, file);
    if (state == FileState::Read)
        return e;

    // No (readable) session file: a session older than step 2, read from its name (D3), in memory.
    if (requireSessionName && state == FileState::Missing) {
        const LegacyFolderName parsed = SessionIdentity::parseLegacyFolderName(
            QFileInfo(folder).fileName(), QFileInfo(parentOf(folder)).fileName());
        if (!parsed.isSessionName() && SwingPaths::swingFolders(folder).isEmpty())
            return {};   // not a session folder: no name this app made, and no swing in it
    }
    e.identity       = SessionIdentity::fromLegacyFolder(folder, owner, {}, m_zone);
    e.folder         = folder;
    e.athlete        = owner;
    e.status         = SessionStatus::Ended;
    e.hasSessionFile = false;
    // A session file that cannot be read is kept as it is for someone to look at: never overwritten.
    e.readOnly       = state == FileState::Unreadable;
    return e;
}

SessionStore::Held &SessionStore::hold(const SessionIndexEntry &entry, const QJsonObject &file)
{
    auto it = m_held.find(entry.id());
    if (it == m_held.end())
        return *m_held.insert(entry.id(), Held{ entry, file, false });
    // Held already. Take the fresh read (its day order included) and what only the map knows.
    //
    // ⚠ THE SAME ID CAN BE READ FROM ANOTHER PLACE: a copied session keeps its SessionId (D3 for a
    // legacy folder, and a copied session.json), so a library re-pointed to a copy of itself finds the
    // ids it already holds. The held folder is kept only when it IS the fresh one (spelled the same once
    // cleaned: "lib//athlete" and "lib/athlete" are one folder, and a holder's spelling never changes),
    // or when it is the session this run started and is still running — capture writes into that one.
    // Otherwise the fresh read's folder wins, or the map would hand out the old library's folder while
    // the settings name the new one (and a resume would write there).
    const bool sameFolder = QDir::cleanPath(it->entry.folder) == QDir::cleanPath(entry.folder);
    const bool running    = it->createdHere && it->entry.status == SessionStatus::InProgress;
    if (!sameFolder && running) {
        ppWarn() << "[SessionStore] session" << entry.id().toString() << "is also at" << entry.folder
                 << "- the running session keeps" << it->entry.folder;
        return *it;
    }
    const QString folder = sameFolder ? it->entry.folder : entry.folder;
    if (!sameFolder) {
        ppInfo() << "[SessionStore] session" << entry.id().toString() << "is now read from" << entry.folder
                 << "(was" << it->entry.folder << ")";
        it->createdHere = false;   // not the folder this run's start() made
    }
    it->entry        = entry;
    it->entry.folder = folder;
    it->file         = file;
    return *it;
}

QList<SessionIndexEntry> SessionStore::enumerate(const SessionOwner &owner)
{
    const QString athleteDir = athleteFolder(owner);
    QList<SessionIndexEntry> entries;
    QList<QJsonObject>       files;
    const QStringList names = QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        QJsonObject             file;
        const SessionIndexEntry e = readFolder(athleteDir + QLatin1Char('/') + name, owner.athlete, file, true);
        if (e.identity.isNull())
            continue;
        if (e.hasSessionFile && !owner.athlete.isNull() && !e.athlete.isNull() && e.athlete != owner.athlete)
            ppWarn() << "[SessionStore] the session file of" << e.folder << "names another athlete ("
                     << e.athlete.toString() << ") - listed, not re-owned";
        entries.append(e);
        files.append(file);
    }

    // n-of-day: each session's place among the athlete's sessions of its local day (F-06).
    QList<SessionIdentity> ids;
    ids.reserve(entries.size());
    for (const SessionIndexEntry &e : entries)
        ids.append(e.identity);
    SessionIdentity::assignDayOrder(ids);

    for (qsizetype i = 0; i < entries.size(); ++i) {
        entries[i].identity = ids.at(i);
        entries[i]          = hold(entries.at(i), files.at(i)).entry;
    }
    return entries;
}

SessionId SessionStore::todaysSession(const SessionOwner &owner, const QString &type)
{
    const QDate              day = today();
    const SessionIndexEntry *best = nullptr;
    // The latest: a session with a file before one without (a session-file session of today was made
    // after any legacy one), then by start time, then by its place in the day.
    const auto later = [](const SessionIndexEntry &a, const SessionIndexEntry &b) {
        if (a.hasSessionFile != b.hasSessionFile)
            return a.hasSessionFile;
        if (a.startedAtUtc != b.startedAtUtc)
            return a.startedAtUtc > b.startedAtUtc;
        return a.identity.nOfDay() > b.identity.nOfDay();
    };
    const QList<SessionIndexEntry> all = enumerate(owner);
    for (const SessionIndexEntry &e : all) {
        if (e.readOnly || e.identity.localDay() != day || e.identity.type() != type)
            continue;
        if (!best || later(e, *best))
            best = &e;
    }
    return best ? best->id() : SessionId();
}

std::optional<SessionIndexEntry> SessionStore::open(const SessionId &id) const
{
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd())
        return std::nullopt;
    return it->entry;
}

SessionId SessionStore::openFolder(const QString &folder, const AthleteId &legacyAthlete)
{
    if (folder.isEmpty())
        return {};
    QJsonObject             file;
    const SessionIndexEntry e = readFolder(folder, legacyAthlete, file, false);
    if (e.identity.isNull())
        return {};
    hold(e, file);
    return e.id();
}

QList<ShotRef> SessionStore::shots(const SessionId &id) const
{
    QList<ShotRef> out;
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd())
        return out;
    for (const QString &name : SwingPaths::swingFolders(it->entry.folder))
        if (const ShotRef ref(id, SwingPaths::swingNumberOf(name)); !ref.isNull())
            out.append(ref);
    std::sort(out.begin(), out.end());
    return out;
}

QString SessionStore::swingFolder(const ShotRef &shot) const
{
    const auto it = m_held.constFind(shot.session());
    if (shot.isNull() || it == m_held.constEnd())
        return {};
    return it->entry.folder + QLatin1Char('/') + SwingPaths::swingFolderName(shot.swingNumber());
}

// ── Writing ──────────────────────────────────────────────────────────────────────────────────────

WriteResult SessionStore::write(Held &h)
{
    if (h.entry.readOnly)
        return readOnly(h.entry.folder);

    QJsonObject o = h.file;   // keys this build does not know are kept
    const auto  set = [&o](const char *key, const QString &value) {
        if (value.isEmpty())
            o.remove(QLatin1String(key));
        else
            o.insert(QLatin1String(key), value);
    };
    o.insert(QStringLiteral("schema"), QLatin1String(kSchema));
    set("sessionId", h.entry.id().toString());
    set("athleteId", h.entry.athlete.toString());
    set("type", h.entry.identity.type());
    set("status", statusName(h.entry.status));
    set("startedAt", isoUtc(h.entry.startedAtUtc));
    set("endedAt", h.entry.status == SessionStatus::Ended ? isoUtc(h.entry.endedAtUtc) : QString());
    if (!o.value(QStringLiteral("origin")).isObject())
        o.insert(QStringLiteral("origin"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("captured") } });

    // The advisory lock, for this one write only (R-PERS-6; J-11).
    FolderLock lock(h.entry.folder);
    switch (lock.lock(m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs)) {
    case FolderLock::Result::Acquired:
        break;
    case FolderLock::Result::Locked:
        ppWarn() << "[SessionStore] not written, the folder is locked by" << lock.holder() << ":" << h.entry.folder;
        return failed(QStringLiteral("The session %1 is being changed by %2; it was not saved.")
                          .arg(h.entry.folder, lock.holder().isEmpty() ? QStringLiteral("another program")
                                                                        : lock.holder()));
    case FolderLock::Result::Failed:
        ppWarn() << "[SessionStore] not written:" << lock.errorString();
        return failed(lock.errorString());
    }
    const AtomicWriteResult w = atomicWrite(SessionFiles::sessionFile(h.entry.folder),
                                            QJsonDocument(o).toJson(QJsonDocument::Indented),
                                            AtomicWriteOptions::intoExistingFolder());
    if (!w.ok) {
        ppWarn() << "[SessionStore]" << w.error << "-" << w.detail;
        return failed(w.error);
    }
    h.file                 = o;
    h.entry.hasSessionFile = true;
    return written();
}

void SessionStore::endOthersInProgress(const QString &athleteDir, const SessionId &except)
{
    const QStringList names = QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        SessionIndexEntry e;
        QJsonObject       file;
        const QString     folder = athleteDir + QLatin1Char('/') + name;
        if (readFile(folder, e, file) != FileState::Read || e.status != SessionStatus::InProgress
            || e.id() == except)
            continue;
        Held &h = hold(e, file);
        const SessionIndexEntry before = h.entry;
        // Its real end is not known (a crash, or a session the app never ended): no end time is written
        // rather than a made-up one (P1). One InProgress session per athlete (DEC-78, DEC-73).
        h.entry.status     = SessionStatus::Ended;
        h.entry.endedAtUtc = {};
        h.createdHere      = false;
        if (const WriteResult r = write(h); r.ok()) {
            ppInfo() << "[SessionStore] ended a session still in progress:" << folder;
        } else {
            h.entry = before;
            ppWarn() << "[SessionStore] a session still in progress could not be ended:" << folder << "-" << r.error;
        }
    }
}

SessionStartResult SessionStore::start(const SessionOwner &owner, const QString &type, const QString &namingPattern)
{
    const QString athleteDir = athleteFolder(owner);
    endOthersInProgress(athleteDir, {});

    const QDateTime now  = nowUtc();
    const QString   base = SwingPaths::sessionFolderBase(namingPattern, now.toTimeZone(m_zone).date(),
                                                         athleteToken(owner), folderTypeLabel(type));
    int     counter = SwingPaths::nextSessionCounter(athleteDir, base);
    QString folder  = athleteDir + QLatin1Char('/') + SwingPaths::sessionFolderName(base, counter);
    while (QFileInfo::exists(folder))   // another writer took the name since the count
        folder = athleteDir + QLatin1Char('/') + SwingPaths::sessionFolderName(base, ++counter);
    if (!QDir().mkpath(folder)) {
        ppError() << "[SwingExport] failed to create session folder" << folder;
        return { {}, {}, failed(QStringLiteral("Could not create the session folder %1.").arg(folder)) };
    }

    Held h;
    h.entry.identity     = SessionIdentity::fromSessionFile(SessionId::create(), folder, type, now, m_zone);
    h.entry.folder       = folder;
    h.entry.athlete      = owner.athlete;
    h.entry.status       = SessionStatus::InProgress;
    h.entry.startedAtUtc = now;
    h.file.insert(QStringLiteral("origin"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("captured") } });
    h.createdHere = true;
    const WriteResult r = write(h);
    if (!r.ok()) {
        QDir().rmdir(folder);   // empty: made just now, and nothing could be written into it
        ppError() << "[SwingExport] the session file could not be written - no session started in" << folder;
        return { {}, {}, r };
    }
    const SessionId id = h.entry.id();
    m_held.insert(id, h);
    ppInfo() << "[SwingExport] begin session:" << folder << "(new)";
    return { id, folder, r };
}

WriteResult SessionStore::resume(const SessionId &id, const QString &type)
{
    {
        const auto it = m_held.constFind(id);
        if (it == m_held.constEnd())
            return failed(QStringLiteral("That session is not known."));
        if (it->entry.readOnly)
            return readOnly(it->entry.folder);
        // MD-05 (DEC-134): only a session started today, of the same type, as today's "extend".
        if (it->entry.identity.localDay() != today() || it->entry.identity.type() != type)
            return failed(QStringLiteral("Only a session of the same type started today can be continued."));
        endOthersInProgress(parentOf(it->entry.folder), id);
    }
    Held &h = m_held[id];   // found again: ending the others may have grown the map
    const SessionIndexEntry before    = h.entry;
    const QJsonObject       fileBefore = h.file;
    if (!h.entry.hasSessionFile) {
        // The first write to a session older than step 2: its file, with D3's id, and its start time
        // inferred — saying how (P1; origin "backfilled").
        const QDateTime start = inferredLegacyStart(h.entry.folder);
        h.entry.identity      = SessionIdentity::fromSessionFile(id, h.entry.folder, h.entry.identity.type(),
                                                                 start, m_zone);
        h.entry.startedAtUtc  = start;
        h.file.insert(QStringLiteral("origin"), QJsonObject{
            { QStringLiteral("kind"), QStringLiteral("backfilled") },
            { QStringLiteral("tool"), QCoreApplication::applicationName() },
            { QStringLiteral("version"), QCoreApplication::applicationVersion() },
            { QStringLiteral("at"), isoUtc(nowUtc()) },
            { QStringLiteral("read"), QStringLiteral("the folder's name (the id and type); its swing folders' "
                                                     "creation times (the start)") },
        });
    }
    h.entry.status     = SessionStatus::InProgress;
    h.entry.endedAtUtc = {};
    const WriteResult r = write(h);
    if (!r.ok()) {
        h.entry = before;
        h.file  = fileBefore;
        return r;
    }
    h.createdHere = false;   // somebody's earlier work: never the empty discard's
    ppInfo() << "[SwingExport] begin session:" << h.entry.folder << "(extend)"
             << "holding" << SwingPaths::swingFolders(h.entry.folder).size() << "swing(s)";
    return r;
}

WriteResult SessionStore::end(const SessionId &id, bool discardIfEmpty)
{
    const auto it = m_held.find(id);
    if (it == m_held.end())
        return unchanged();
    Held &h = *it;
    if (!h.entry.hasSessionFile && !h.entry.readOnly)
        return unchanged();   // a session older than step 2 that nothing resumed: nothing is in progress

    // ⚠ THE EMPTY DISCARD TAKES ONLY A FOLDER THIS RUN'S start() MADE, and only while it holds no swing
    // (DEC-93; unchanged). A resumed session is somebody's earlier work, whatever its count did.
    if (discardIfEmpty && h.createdHere && QFileInfo::exists(h.entry.folder)
        && SwingPaths::swingFolders(h.entry.folder).isEmpty()) {
        QString where;
        if (SwingPaths::trashPath(h.entry.folder, &where)) {
            ppInfo() << "[SwingExport] discarded session (to" << where << "):" << h.entry.folder;
            m_held.erase(it);
            return written();
        }
        ppWarn() << "[SwingExport] could not trash empty session:" << h.entry.folder;
    }
    h.createdHere = false;
    if (h.entry.readOnly)
        return readOnly(h.entry.folder);

    const SessionIndexEntry before = h.entry;
    h.entry.status     = SessionStatus::Ended;
    h.entry.endedAtUtc = nowUtc();
    const WriteResult r = write(h);
    if (!r.ok())
        h.entry = before;   // still in progress, as its file says (DEC-73)
    return r;
}

SessionStartResult SessionStore::restartInLibrary(const SessionId &running, const SessionOwner &owner,
                                                  const QString &namingPattern, QString *oldEndProblem)
{
    QString type;
    QString was;
    if (oldEndProblem)
        oldEndProblem->clear();
    if (const auto it = m_held.constFind(running); it != m_held.constEnd()) {
        type = it->entry.identity.type();
        was  = it->entry.folder;
        if (const WriteResult r = end(running, /*discardIfEmpty=*/true); !r.ok()) {
            ppWarn() << "[SessionStore] the session in the old library could not be ended (it stays in progress"
                        " until the athlete's next session there):" << was << "-" << r.error;
            // A folder still there (a held lock, a read-only file) is a failure to report; a library that
            // has gone (a dropped share) is the expected case, and the "not being saved" notices cover it.
            if (oldEndProblem && QFileInfo::exists(was))
                *oldEndProblem = r.error;
        }
    }
    SessionStartResult s = start(owner, type, namingPattern);
    ppWarn() << "[SwingExport] the library changed during a session: the session" << was
             << "ended, and a new one started in" << s.folder;
    return s;
}

SwingAllocation SessionStore::allocateSwing(const SessionId &id)
{
    SwingAllocation out;
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd()) {
        out.result = failed(QStringLiteral("No session is open to record the swing in."));
        return out;
    }
    if (it->entry.readOnly) {
        out.result = readOnly(it->entry.folder);
        return out;
    }
    const QString folder = it->entry.folder;

    // The allocation is one write (the number, its folder, the high-water), under the session's lock.
    // A folder that has gone (a share dropped) is not locked, and neither is one where no lock file can
    // be made (FolderLock's Failed): deliberately, as before the lock existed — the mkpath below decides,
    // and its failure is the one reported. Only a lock HELD by another writer refuses the allocation.
    FolderLock lock(folder);
    if (QFileInfo::exists(folder)
        && lock.lock(m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs) == FolderLock::Result::Locked) {
        ppWarn() << "[SwingExport] the session folder is locked by" << lock.holder() << ":" << folder;
        out.result = failed(QStringLiteral("The session %1 is being changed by %2.").arg(folder, lock.holder()));
        return out;
    }

    // One past the HIGHEST number this session has ever issued — the highest folder still here, or the
    // recorded high-water if a later one has since been deleted — then upward past any collision.
    // ⚠ NOT "COUNT THE FOLDERS + 1" (#56): a number is a swing's identity and is never reissued (DEC-91).
    int highest = SessionFiles::recordedSwingHighWater(folder);
    for (const QString &name : SwingPaths::swingFolders(folder))
        highest = std::max(highest, SwingPaths::swingNumberOf(name));
    int     index = highest + 1;
    QString name  = SwingPaths::swingFolderName(index);
    while (QDir(folder).exists(name))
        name = SwingPaths::swingFolderName(++index);

    const QString swingDir = folder + QLatin1Char('/') + name;
    if (!QDir().mkpath(swingDir)) {
        ppError() << "[SwingExport] failed to create" << swingDir;
        out.result = failed(QStringLiteral("Could not create the swing folder %1.").arg(swingDir));
        return out;
    }
    // Best effort, as before: a failed write costs only the never-reissued guarantee, and is logged.
    if (const AtomicWriteResult w = SessionFiles::raiseSwingHighWater(folder, index); !w.ok)
        ppWarn() << "[SwingExport] could not record the last swing number in" << folder << "-" << w.detail;

    out.shot              = ShotRef(id, index);
    out.swingFolder       = swingDir;
    out.swingName         = name;
    out.sessionFolderName = it->entry.identity.folderName();
    out.result            = written();
    return out;
}

} // namespace pinpoint
