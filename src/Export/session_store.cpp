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

#include "session_diagnostics_io.h"   // the diagnostics files' file work (J-42)
#include "session_report_io.h"        // the report's records and stills' file work (step 5.6a)
#include "session_files.h"
#include "athlete_files.h"   // athlete.json's name: whether `unknown/` is a golfer's (R-UNASSIGNED-2)
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
// Another writer holds the folder's advisory lock past the wait (J-22): the lock's own words.
WriteResult locked(const QString &folder, const QString &holder)
{
    return { WriteResult::Status::Locked,
             QStringLiteral("The session %1 is being changed by %2; it was not saved.")
                 .arg(folder, holder.isEmpty() ? QStringLiteral("another program") : holder) };
}
WriteResult written() { return { WriteResult::Status::Written, {} }; }
WriteResult unchanged() { return { WriteResult::Status::Unchanged, {} }; }
WriteResult readOnly(const QString &folder)
{
    return { WriteResult::Status::ReadOnly,
             QStringLiteral("The session %1 was saved by a newer PinPoint Studio and is not changed by this one.")
                 .arg(folder) };
}

SessionArchiveResult archiveRefused(const WriteResult &why)
{
    SessionArchiveResult r;
    r.status = why.status;
    r.error  = why.error;
    return r;
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
    // Cleaned (REVIEW-f R1-m10): a library path set with a trailing slash ("/mnt/swingdata/") gave "lib//<athlete>", so the
    // index's folders disagreed with their own SessionIdentity::folder() and with every other source's spelling (the
    // 2aa5bc72 double count). No identity moves: a legacy session's id is its athlete's and its folder NAME's.
    // The Unassigned owner's folder is its own fixed name, verbatim (R-UNASSIGNED-2).
    if (isUnassigned(owner.athlete))
        return QDir::cleanPath(root + QLatin1Char('/') + unassignedFolderName());
    return QDir::cleanPath(root + QLatin1Char('/') + athleteToken(owner));
}

QString SessionStore::legacyNoGolferFolder(const QString &libraryRoot)
{
    const QString root = libraryRoot.trimmed();
    if (root.isEmpty())
        return {};
    const QString dir = QDir::cleanPath(root + QStringLiteral("/unknown"));
    if (!QFileInfo(dir).isDir() || QFileInfo::exists(AthleteFiles::athleteFile(dir)))
        return {};   // none, or a golfer's (one whose name sanitised to "unknown")
    return dir;
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
    out.endInferred    = out.endedAtUtc.isValid() && o.value(QStringLiteral("endInferred")).toBool();
    out.hasSessionFile = true;
    out.readOnly       = version > 1;
    out.archived       = QFileInfo::exists(SessionFiles::archiveMarker(folder));
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
    e.archived       = QFileInfo::exists(SessionFiles::archiveMarker(folder));
    return e;
}

SessionStore::Held &SessionStore::hold(const SessionIndexEntry &entry, const QJsonObject &file)
{
    auto it = m_held.find(entry.id());
    if (it == m_held.end()) {
        publish(entry.id(), entry.folder);
        return *m_held.insert(entry.id(), Held{ entry, file, false });
    }
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
    publish(entry.id(), folder);
    return *it;
}

QList<SessionIndexEntry> SessionStore::enumerate(const SessionOwner &owner)
{
    QList<SessionIndexEntry> entries;
    if (isUnassigned(owner.athlete)) {
        // R-UNASSIGNED-4 (J-66): the no-golfer sessions an older build captured into `unknown/` are held where they
        // are — the ones whose file names no athlete (or this one), and the ones with no file (D3's id, named for
        // Unassigned: a read, nothing written). A session there naming a golfer stays that golfer's matter.
        const QString legacy = legacyNoGolferFolder(owner.libraryRoot);
        if (!legacy.isEmpty() && QDir::cleanPath(legacy) != QDir::cleanPath(athleteFolder(owner)))
            for (const SessionIndexEntry &e : enumerateFolder(legacy, owner.athlete))
                if (!e.identity.isNull() && (e.athlete.isNull() || e.athlete == owner.athlete))
                    entries.append(e);
    }
    for (const SessionIndexEntry &e : enumerateFolder(athleteFolder(owner), owner.athlete)) {
        if (e.identity.isNull())
            continue;   // a legacy session with no athlete to name it (no athlete selected): as before step 2.7a
        if (e.hasSessionFile && !owner.athlete.isNull() && !e.athlete.isNull() && e.athlete != owner.athlete)
            ppWarn() << "[SessionStore] the session file of" << e.folder << "names another athlete ("
                     << e.athlete.toString() << ") - listed, not re-owned";
        entries.append(e);
    }
    return entries;
}

QList<SessionIndexEntry> SessionStore::enumerateFolder(const QString &athleteDir, const AthleteId &legacyAthlete)
{
    QList<SessionIndexEntry> entries;
    QList<QJsonObject>       files;
    if (athleteDir.isEmpty())
        return entries;
    const QStringList names = QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        QJsonObject             file;
        SessionIndexEntry e = readFolder(athleteDir + QLatin1Char('/') + name, legacyAthlete, file, true);
        if (e.identity.isNull()) {
            // Not a session folder (readFolder gave nothing), or a legacy session no athlete can be named for:
            // with no owner it is still placed — its day and order — but has no id, and is never held.
            if (e.folder.isEmpty() || !legacyAthlete.isNull())
                continue;
            e.identity = SessionIdentity::placeOfLegacyFolder(e.folder, {}, m_zone);
            if (e.identity.folder().isEmpty())
                continue;
        }
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
        if (!entries.at(i).identity.isNull())      // an unnamed legacy session (no owner) is placed, never held
            entries[i] = hold(entries.at(i), files.at(i)).entry;
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

QString SessionStore::heldFolder(const SessionId &id) const
{
    const auto it = m_held.constFind(id);
    return it == m_held.constEnd() ? QString() : it->entry.folder;
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
    // A folder that is not there — trashed (an empty session's discard), moved, or a share gone away — is no session:
    // nothing is held for it, so no Session is made for it and nothing is ever written there (refactor 4-a review).
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
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

bool SessionStore::folderPresent(const SessionId &id) const
{
    const auto it = m_held.constFind(id);
    return it != m_held.constEnd() && QFileInfo(it->entry.folder).isDir();
}

ShotRef SessionStore::openSwingFolder(const QString &swingFolder, const AthleteId &legacyAthlete)
{
    if (swingFolder.isEmpty())
        return {};
    const QFileInfo swing(QDir::cleanPath(swingFolder));
    const int       number = SwingPaths::swingNumberOf(swing.fileName());
    if (number < 1)
        return {};   // not a swing folder: no swing number, so no shot
    const QString sessionFolder = swing.path();

    // A session already held at this folder keeps the id it is held under: a folder the athlete's list
    // read (enumerate, with the athlete as owner) names the same session for every later caller, and a
    // second read of its file is saved.
    const QString clean = QDir::cleanPath(sessionFolder);
    for (auto it = m_held.cbegin(); it != m_held.cend(); ++it)
        if (QDir::cleanPath(it->entry.folder) == clean)
            return ShotRef(it.key(), number);

    const SessionId id = openFolder(sessionFolder, legacyAthlete);
    if (id.isNull())
        ppDebug() << "[SessionStore] the session of" << swingFolder << "cannot be named (no session file,"
                  << "and no athlete given for it)";
    return ShotRef(id, number);
}

QString SessionStore::swingFolder(const ShotRef &shot) const
{
    if (shot.isNull())
        return {};
    // From the published folders, not the identity map: any thread may ask (a worker writing a shot).
    const QMutexLocker guard(&m_publishedMutex);
    const auto it = m_published.constFind(shot.session());
    if (it == m_published.constEnd())
        return {};
    return *it + QLatin1Char('/') + SwingPaths::swingFolderName(shot.swingNumber());
}

void SessionStore::publish(const SessionId &id, const QString &folder)
{
    const QMutexLocker guard(&m_publishedMutex);
    m_published.insert(id, folder);
}

void SessionStore::unpublish(const SessionId &id)
{
    const QMutexLocker guard(&m_publishedMutex);
    m_published.remove(id);
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
    if (h.entry.status == SessionStatus::Ended && h.entry.endedAtUtc.isValid() && h.entry.endInferred)
        o.insert(QStringLiteral("endInferred"), true);   // J-18: an inferred end says so (P1)
    else
        o.remove(QStringLiteral("endInferred"));
    if (!o.value(QStringLiteral("origin")).isObject())
        o.insert(QStringLiteral("origin"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("captured") } });

    // The advisory lock, for this one write only (R-PERS-6; J-11).
    FolderLock lock(h.entry.folder);
    switch (lock.lock(m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs)) {
    case FolderLock::Result::Acquired:
        break;
    case FolderLock::Result::Locked:
        ppWarn() << "[SessionStore] not written, the folder is locked by" << lock.holder() << ":" << h.entry.folder;
        return locked(h.entry.folder, lock.holder());
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

bool SessionStore::fileSaysInProgress(const QString &sessionFolder)
{
    if (sessionFolder.isEmpty())
        return false;
    const SessionStore reader;   // a read of the file as it stands now, through the one reader
    SessionIndexEntry  e;
    QJsonObject        file;
    return reader.readFile(QDir::cleanPath(sessionFolder), e, file) == FileState::Read
        && e.status == SessionStatus::InProgress;
}

QString SessionStore::inProgressRefusal(const QString &sessionFolder)
{
    return QStringLiteral("%1 is in progress (its session.json says so: it is being recorded here or on another "
                          "computer) - not written. A session a crash left open can be passed with "
                          "--allow-in-progress (D11).").arg(QDir::cleanPath(sessionFolder));
}

QString SessionStore::sessionFolderOfSwing(const QString &swingFolder)
{
    return QFileInfo(QDir::cleanPath(swingFolder)).path();
}

QDateTime SessionStore::inferredEnd(const QString &folder) const
{
    if (!m_captureEndOf)
        return {};
    QDateTime latest;   // the latest of every swing's: the rule M2 uses for a session older than step 2
    for (const QString &name : SwingPaths::swingFolders(folder)) {
        const QDateTime t = m_captureEndOf(folder + QLatin1Char('/') + name);
        if (t.isValid() && (!latest.isValid() || t > latest))
            latest = t.toUTC();
    }
    return latest;
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
        // Its real end is not known (a crash, or a session the app never ended). J-18 (Mark, 15 Nov 2026):
        // its end is its last swing's capture end, MARKED AS INFERRED (`endInferred`, P1); when no swing
        // records a capture time, no end time is written rather than a made-up one. One InProgress session
        // per athlete (DEC-78, DEC-73).
        h.entry.status      = SessionStatus::Ended;
        h.entry.endedAtUtc  = inferredEnd(h.entry.folder);
        h.entry.endInferred = h.entry.endedAtUtc.isValid();
        h.createdHere       = false;
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
    publish(id, folder);
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
    h.entry.status      = SessionStatus::InProgress;
    h.entry.endedAtUtc  = {};
    h.entry.endInferred = false;
    const WriteResult r = write(h);
    if (!r.ok()) {
        h.entry = before;
        h.file  = fileBefore;
        return r;
    }
    h.createdHere = false;   // somebody's earlier work, not the folder this run's start() made
    ppInfo() << "[SwingExport] begin session:" << h.entry.folder << "(extend)"
             << "holding" << SwingPaths::swingFolders(h.entry.folder).size() << "swing(s)";
    return r;
}

WriteResult SessionStore::end(const SessionId &id)
{
    const auto it = m_held.find(id);
    if (it == m_held.end())
        return unchanged();
    Held &h = *it;
    if (!h.entry.hasSessionFile && !h.entry.readOnly)
        return unchanged();   // a session older than step 2 that nothing resumed: nothing is in progress

    // ⚠ NO EMPTY DISCARD HERE (refactor step 3.4a-ii): the store ends the session and nothing more. Whether an
    // empty session is discarded instead is the Session's rule (Session::end, which then asks
    // Athlete::trashSession: DEC-143; the rule itself DEC-93's, unchanged).
    h.createdHere = false;
    if (h.entry.readOnly)
        return readOnly(h.entry.folder);

    const SessionIndexEntry before = h.entry;
    h.entry.status      = SessionStatus::Ended;
    h.entry.endedAtUtc  = nowUtc();
    h.entry.endInferred = false;   // observed: the End
    const WriteResult r = write(h);
    if (!r.ok())
        h.entry = before;   // still in progress, as its file says (DEC-73)
    return r;
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
        out.result = locked(folder, lock.holder());
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

// ── The archive and the trash (step 2.6) ─────────────────────────────────────────────────────────

WriteResult SessionStore::checkNotLive(const Held &h, bool refuseReadOnly) const
{
    const QString &folder = h.entry.folder;
    const auto live = [&folder] {
        return failed(QStringLiteral("The session %1 is still in progress (being recorded here or on another "
                                     "computer, or left open by a crash until its athlete's next session); "
                                     "it was not changed.").arg(folder));
    };
    if (h.createdHere && h.entry.status == SessionStatus::InProgress)
        return live();   // this run's own running session
    // Afresh: the held entry may be older than another writer's change (the other machine's Start).
    SessionIndexEntry fresh;
    QJsonObject       file;
    const FileState   state = readFile(folder, fresh, file);
    if (state == FileState::Read && fresh.status == SessionStatus::InProgress)
        return live();
    if (refuseReadOnly && (state == FileState::Unreadable || (state == FileState::Read && fresh.readOnly)))
        return readOnly(folder);
    return unchanged();
}

SessionArchiveResult SessionStore::archive(const SessionId &id, const SessionArchiveOptions &options,
                                           const SessionArchiveProgress &progress)
{
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd())
        return archiveRefused(failed(QStringLiteral("That session is not known.")));
    // A newer build's (or an unreadable) session file: the stubs and the marker are writes this build does
    // not make into it, like every other write (DEC-126 read-only rule).
    if (const WriteResult c = checkNotLive(*it, /*refuseReadOnly=*/true); !c.ok())
        return archiveRefused(c);
    if (!m_archiving)
        return archiveRefused(failed(QStringLiteral("Archiving is not available here.")));
    const QString folder = it->entry.folder;
    // Asked again before each swing is thinned (J-52): another machine may resume the session meanwhile. By
    // id, not by `it`: the run's own reads may grow the identity map under it.
    const auto stillArchivable = [this, id]() -> QString {
        const auto h = m_held.constFind(id);
        if (h == m_held.constEnd())
            return QStringLiteral("That session is not known.");
        const WriteResult c = checkNotLive(*h, /*refuseReadOnly=*/true);
        return c.ok() ? QString() : c.error;
    };
    SessionArchiveResult r = m_archiving->archiveFolder(folder, options, progress, stillArchivable);
    if (const auto h = m_held.find(id); h != m_held.end())   // found again: the run may have grown the map
        h->entry.archived = QFileInfo::exists(SessionFiles::archiveMarker(folder));
    return r;
}

SessionArchiveJob SessionStore::archiveJob(const SessionId &id, const SessionArchiveOptions &options)
{
    SessionArchiveJob job;
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd()) {
        job.refused = archiveRefused(failed(QStringLiteral("That session is not known.")));
        return job;
    }
    // The same refusals as archive(): in progress (here, or by its session file read afresh), or a newer build's file.
    if (const WriteResult c = checkNotLive(*it, /*refuseReadOnly=*/true); !c.ok()) {
        job.refused = archiveRefused(c);
        return job;
    }
    if (!m_archiveJobs) {
        job.refused = archiveRefused(failed(QStringLiteral("Archiving is not available here.")));
        return job;
    }
    // Values only: the folder, the options and the file work, which opens its own repositories (it re-checks the
    // session's status from its file before each swing is thinned, J-52).
    const QString folder = it->entry.folder;
    const ArchiveJobs work = m_archiveJobs;
    job.run = [work, folder, options](const SessionArchiveProgress &progress) { return work(folder, options, progress); };
    return job;
}

void SessionStore::archiveLanded(const SessionId &id)
{
    if (const auto h = m_held.find(id); h != m_held.end())
        h->entry.archived = QFileInfo::exists(SessionFiles::archiveMarker(h->entry.folder));
}

SessionArchiveResult SessionStore::restore(const SessionId &id, const SessionArchiveProgress &progress)
{
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd())
        return archiveRefused(failed(QStringLiteral("That session is not known.")));
    // Not refused for a newer build's session file: a restore puts the session's own bytes back as they
    // were archived and writes nothing of this build's into it (the review merge goes through the swing
    // door, which refuses a newer document by itself).
    if (const WriteResult c = checkNotLive(*it, /*refuseReadOnly=*/false); !c.ok())
        return archiveRefused(c);
    if (!m_archiving)
        return archiveRefused(failed(QStringLiteral("Restoring is not available here.")));
    const QString folder = it->entry.folder;
    SessionArchiveResult r = m_archiving->restoreFolder(folder, progress);
    if (const auto h = m_held.find(id); h != m_held.end())
        h->entry.archived = QFileInfo::exists(SessionFiles::archiveMarker(folder));
    return r;
}

WriteResult SessionStore::trash(const SessionId &id, QString *where)
{
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd())
        return failed(QStringLiteral("That session is not known."));
    const QString folder = it->entry.folder;
    if (!QFileInfo::exists(folder))
        return failed(QStringLiteral("The session %1 is no longer there.").arg(folder));
    // Not refused for a read-only session file: moving the folder writes nothing into it, and the trash
    // is recoverable.
    if (const WriteResult c = checkNotLive(*it, /*refuseReadOnly=*/false); !c.ok()) {
        ppWarn() << "[SessionStore] not moved to the trash:" << c.error;
        return c;
    }
    {
        // No other PinPoint writer is mid-write in it (a tool, the other machine): taken for a moment and
        // let go BEFORE the move — a lock file must not travel into the trash with the folder, and Windows
        // will not move a folder that holds an open file. A folder where no lock can be made at all is
        // left to the move to decide (allocateSwing's rule).
        FolderLock lock(folder);
        if (lock.lock(m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs) == FolderLock::Result::Locked) {
            ppWarn() << "[SessionStore] not moved to the trash, the folder is locked by" << lock.holder() << ":" << folder;
            return locked(folder, lock.holder());
        }
    }
    QString to;
    // ⚠ SwingPaths::trashPath, not QFile::moveToTrash: a network share has no OS trash (2 Sept 2026).
    if (!SwingPaths::trashPath(folder, &to, m_osTrash)) {
        ppWarn() << "[SessionStore] could not move the session to the trash:" << folder;
        return failed(QStringLiteral("The session %1 could not be moved to the trash.").arg(folder));
    }
    ppInfo() << "[SessionStore] session moved to the trash (" << to << "):" << folder;
    if (where)
        *where = to;
    unpublish(id);
    m_held.remove(id);
    return written();
}

// ── A session to another athlete (step 5.1; §5.11 R-UNASSIGNED-4; J-67) ─────────────────────────────

WriteResult SessionStore::moveToAthlete(const SessionId &id, const SessionOwner &to, QString *newFolder)
{
    if (newFolder)
        newFolder->clear();
    const auto it = m_held.constFind(id);
    if (it == m_held.constEnd())
        return failed(QStringLiteral("That session is not known."));
    const QString folder = it->entry.folder;
    if (!QFileInfo::exists(folder))
        return failed(QStringLiteral("The session %1 is no longer there.").arg(folder));
    if (it->entry.archived)
        return failed(QStringLiteral("The session %1 is archived; restore it before moving it.").arg(folder));
    if (to.athlete.isNull() || to.athlete == it->entry.athlete)
        return failed(QStringLiteral("Choose another golfer to move the session to."));
    // P3: never change the athlete under a running session; a newer build's file is not rewritten either.
    if (const WriteResult c = checkNotLive(*it, /*refuseReadOnly=*/true); !c.ok()) {
        ppWarn() << "[SessionStore] not moved:" << c.error;
        return c;
    }
    {
        // As the trash: no other writer mid-write in it, the lock let go BEFORE the rename (a lock file must not
        // travel with the folder, and Windows will not rename a folder that holds an open file).
        FolderLock lock(folder);
        if (lock.lock(m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs) == FolderLock::Result::Locked) {
            ppWarn() << "[SessionStore] not moved, the folder is locked by" << lock.holder() << ":" << folder;
            return locked(folder, lock.holder());
        }
    }
    const QString destDir = athleteFolder(to);
    if (!QDir().mkpath(destDir))
        return failed(QStringLiteral("The golfer's folder %1 could not be made.").arg(destDir));
    const QString target = QDir::cleanPath(destDir + QLatin1Char('/') + QFileInfo(folder).fileName());
    if (QFileInfo::exists(target))
        return failed(QStringLiteral("The golfer's folder already holds a session called %1.").arg(QFileInfo(folder).fileName()));
    if (!QDir().rename(folder, target)) {
        ppWarn() << "[SessionStore] could not move the session folder" << folder << "to" << target;
        return failed(QStringLiteral("The session %1 could not be moved.").arg(folder));
    }

    Held &h = m_held[id];
    const Held before = h;
    if (!h.entry.hasSessionFile) {
        // The first write to a session older than step 2 (resume's rule): its file, with D3's id, its start inferred.
        const QDateTime start = inferredLegacyStart(target);
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
    h.entry.identity = SessionIdentity::fromSessionFile(id, target, before.entry.identity.type(), h.entry.startedAtUtc, m_zone);
    h.entry.folder   = target;
    h.entry.athlete  = to.athlete;
    const WriteResult w = write(h);
    if (!w.ok()) {
        // Put it back as it was: the folder where it was, the file unchanged (the write failed before replacing it).
        h = before;
        if (!QDir().rename(target, folder))
            ppError() << "[SessionStore] the moved session" << target << "could not be put back at" << folder;
        ppWarn() << "[SessionStore] the session was not moved, its file could not be written:" << w.error;
        return w;
    }
    h.createdHere = false;
    publish(id, target);
    ppInfo() << "[SessionStore] session moved to athlete" << to.athlete.toString() << ":" << folder << "->" << target;
    if (newFolder)
        *newFolder = target;
    return written();
}

// ── One shot to the trash (step 3.4a; J-24) ──────────────────────────────────────────────────────────

WriteResult SessionStore::trashShot(const ShotRef &shot, QString *where)
{
    const QString folder = heldFolder(shot.session());
    if (shot.isNull() || folder.isEmpty())
        return failed(QStringLiteral("That shot's session is not open."));
    const QString swingDir = folder + QLatin1Char('/') + SwingPaths::swingFolderName(shot.swingNumber());
    if (!QFileInfo::exists(swingDir)) {
        // GONE ALREADY COUNTS AS GONE (the carousel's rule since #56, moved here with N1 in step 5.1): someone else moved
        // it while the trash waited. Nothing to move; its number is still recorded (best effort), and the Session lets
        // the Shot go.
        if (QFileInfo::exists(folder))
            SessionFiles::raiseSwingHighWater(folder, shot.swingNumber());
        ppInfo() << "[SessionStore] swing already gone from the session:" << swingDir;
        return { WriteResult::Status::Unchanged, QStringLiteral("The swing %1 is no longer there.").arg(swingDir) };
    }
    // One write under the session's lock: the number recorded, then the folder moved (allocateSwing's rule
    // for a folder where no lock file can be made: the move decides).
    FolderLock lock(folder);
    if (lock.lock(m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs) == FolderLock::Result::Locked) {
        ppWarn() << "[SessionStore] swing not moved to the trash, the session folder is locked by" << lock.holder()
                 << ":" << swingDir;
        return locked(folder, lock.holder());
    }
    // ⚠ THE NUMBER FIRST (DEC-91): a swing leaving its session takes its number with it, never back into
    // circulation; a session recorded before the high-water existed has none, so this records it. Best
    // effort, as the allocation's: a failed write costs only the never-reissued guarantee, and is logged.
    if (const AtomicWriteResult w = SessionFiles::raiseSwingHighWater(folder, shot.swingNumber()); !w.ok)
        ppWarn() << "[SessionStore] could not record the last swing number in" << folder << "-" << w.detail;
    QString to;
    // ⚠ SwingPaths::trashPath, not QFile::moveToTrash: a network share has no OS trash (2 Sept 2026). Its own
    // high-water raise for a swing folder (kept for the carousel's trash until step 3.4b moves it here) finds
    // the number already recorded and writes nothing.
    if (!SwingPaths::trashPath(swingDir, &to, m_osTrash)) {
        ppWarn() << "[SessionStore] could not move the swing to the trash:" << swingDir;
        return failed(QStringLiteral("The swing %1 could not be moved to the trash.").arg(swingDir));
    }
    ppInfo() << "[SessionStore] swing moved to the trash (" << to << "):" << swingDir;
    if (where)
        *where = to;
    return written();
}

// ── The session's diagnostics files by its id (step 3.4a; J-42) ──────────────────────────────────────

namespace {
const QString kNotHeld = QStringLiteral("That session is not known.");
}

DiagnosticsRead SessionStore::readDiagnostics(const SessionId &id)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty()) {
        DiagnosticsRead r;
        r.error = kNotHeld;
        return r;
    }
    return session_diagnostics_io::readDiagnostics(folder);
}

WriteResult SessionStore::writeDiagnostics(const SessionId &id, const QJsonObject &session, const QJsonObject &ledger,
                                           const QJsonObject &legacyFallback, DiagnosticsRead::State *state)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_diagnostics_io::writeDiagnostics(folder, session, ledger, legacyFallback, state);
}

WriteResult SessionStore::backupDiagnostics(const SessionId &id, const QString &tag, QString *backupPath)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_diagnostics_io::backupDiagnostics(folder, tag, backupPath);
}

WriteResult SessionStore::setAsideDiagnostics(const SessionId &id, QString *asidePath)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_diagnostics_io::setAsideDiagnostics(folder, asidePath);
}

SessionInputRead SessionStore::readSessionInput(const SessionId &id)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty()) {
        SessionInputRead r;
        r.error = kNotHeld;
        return r;
    }
    return session_diagnostics_io::readSessionInput(folder);
}

WriteResult SessionStore::writeSessionInput(const SessionId &id, const SessionInput &input)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_diagnostics_io::writeSessionInput(folder, input);
}

JsonFileRead SessionStore::readClubBaseline(const SessionId &id)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty()) {
        JsonFileRead r;
        r.error = kNotHeld;
        return r;
    }
    return session_diagnostics_io::readClubBaseline(folder);
}

WriteResult SessionStore::writeClubBaseline(const SessionId &id, const QJsonObject &root)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_diagnostics_io::writeClubBaseline(folder, root);
}

JsonFileRead SessionStore::readWorkOns(const SessionId &id)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty()) {
        JsonFileRead r;
        r.error = kNotHeld;
        return r;
    }
    return session_diagnostics_io::readWorkOns(folder);
}

std::function<JsonFileRead()> SessionStore::deferredReadWorkOns(const SessionId &id)
{
    const QString folder = heldFolder(id);   // this (the owner's) thread: the identity map
    if (folder.isEmpty()) {
        JsonFileRead r;
        r.error = kNotHeld;
        return [r] { return r; };
    }
    return [folder] { return session_diagnostics_io::readWorkOns(folder); };   // any thread: a file read, no member
}

WriteResult SessionStore::writeWorkOns(const SessionId &id, const QJsonObject &root)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_diagnostics_io::writeWorkOns(folder, root);
}

// ── The session report's records and stills (refactor step 5.6a) ───────────────────────────────────────────────────

ReportRead SessionStore::readReport(const SessionId &id)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty()) {
        ReportRead r;
        r.error = kNotHeld;
        return r;
    }
    return session_report_io::read(folder);
}

WriteResult SessionStore::writeReportInput(const SessionId &id, const ReportInput &input)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_report_io::writeInput(folder, input);
}

WriteResult SessionStore::writeReportRecord(const SessionId &id, const ReportRecord &record, bool removeReplacedPdf)
{
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_report_io::writeRecord(folder, record, removeReplacedPdf);
}

WriteResult SessionStore::migrateReport(const SessionId &id, bool *migrated)
{
    if (migrated) *migrated = false;
    const QString folder = heldFolder(id);
    if (folder.isEmpty())
        return failed(kNotHeld);
    return session_report_io::migrate(folder, migrated);
}

QString SessionStore::reportStillsFolder(const SessionId &id, const QString &key)
{
    const QString folder = heldFolder(id);
    return folder.isEmpty() ? QString() : session_report_io::stillsFolder(folder, key);
}

ReportStillWriter SessionStore::reportStillsWriter(const SessionId &id, const QString &key)
{
    const QString stills = reportStillsFolder(id, key);   // this (the owner's) thread: the identity map
    return stills.isEmpty() ? ReportStillWriter() : session_report_io::stillsWriter(stills);
}

} // namespace pinpoint
