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
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "session_archiver.h"

#include <algorithm>

#include "athlete_files.h"   // the owner of a session older than step 2 (J-39)
#include "session_files.h"   // archived.json, spelled once
#include "session_report_io.h"   // the report's restamp rule (keepCurrent; step 5.6a)
#include "swing_doc.h"
#include "swing_paths.h"
#include "swing_store.h"
#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../Core/pp_debug.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <filesystem>
#include <system_error>

#if defined(Q_OS_WIN)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace pinpoint {

namespace {

const QString kMarkerPrefix = QStringLiteral("pinpoint.archive/");

// A swing document's review, read from its root chunk alone (the pose and the other tracks are
// never decoded): the archive's copy can be a ~28 MB document on an external drive.
QJsonObject reviewOf(const QString &swingDir)
{
    return SwingStore::loadParts(swingDir, QStringList()).value(QStringLiteral("review")).toObject();
}

// Regenerable caches: never archived, and removed with the rest when a swing is thinned.
bool isCache(const QString &name)
{
    return name == QLatin1String("swing_summary.json") || name == QLatin1String("swing_phasegrid.json");
}

// Every file of the session worth keeping, relative to it, sorted. Dot-files (the lock, the swing
// counter) and half-written temporaries are never part of a session.
QStringList sessionFiles(const QString &sessionDir, bool keepRaw)
{
    QStringList out;
    QDirIterator it(sessionDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(sessionDir);
    while (it.hasNext()) {
        const QString abs  = it.next();
        const QString rel  = base.relativeFilePath(abs);
        const QString name = it.fileName();
        if (rel.startsWith(QLatin1Char('.')) || rel.contains(QLatin1String("/.")))  continue;
        if (name.endsWith(QLatin1String(".partial")) || name.contains(QLatin1String(".tmp-"))) continue;
        if (isCache(name) || rel == SessionFiles::archiveMarkerFileName())             continue;
        if (!keepRaw && name.endsWith(QLatin1String(".raw"), Qt::CaseInsensitive))   continue;
        out << rel;
    }
    out.sort();
    return out;
}

bool sameBytes(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    if (fa.size() != fb.size()) return false;
    constexpr qint64 kChunk = 4 << 20;
    while (!fa.atEnd()) {
        const QByteArray x = fa.read(kChunk);
        const QByteArray y = fb.read(kChunk);
        if (x != y) return false;
    }
    return fb.atEnd();
}

// Ask the OS to put a file's bytes on the disk (as QSaveFile's commit and libppswing do) before it is
// renamed into place. Best effort: a volume that cannot sync still verified the bytes it reads back.
void syncFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return;
#if defined(Q_OS_WIN)
    (void)_commit(f.handle());
#else
    (void)::fsync(f.handle());
#endif
}

std::filesystem::path fsPath(const QString &p)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path(p.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(p).toStdString());
#endif
}

// What stays of a swing in the library: everything a list, a trend or a ledger reads, nothing a
// replay needs. The pose, club and ball tracks — the bulk of a document — go.
QJsonObject stubOf(const QJsonObject &doc, const QJsonObject &archive)
{
    QJsonObject stub = doc;

    static const QSet<QString> keepAnalysis = {
        QStringLiteral("schema"), QStringLiteral("tier"), QStringLiteral("score"),
        QStringLiteral("metrics"), QStringLiteral("phases"), QStringLiteral("segmentation"),
        QStringLiteral("versions") };
    QJsonObject an;
    const QJsonObject full = doc.value(QStringLiteral("analysis")).toObject();
    for (auto it = full.begin(); it != full.end(); ++it)
        if (keepAnalysis.contains(it.key())) an.insert(it.key(), it.value());
    if (!full.isEmpty()) stub.insert(QStringLiteral("analysis"), an);

    // Streams keep their identity (which camera, which file it was) and lose their per-sample data.
    static const QSet<QString> keepStream = {
        QStringLiteral("kind"), QStringLiteral("alias"), QStringLiteral("file"), QStringLiteral("setup"),
        QStringLiteral("device"), QStringLiteral("origin"), QStringLiteral("playback"),
        QStringLiteral("encoded"), QStringLiteral("clip") };
    QJsonArray streams;
    for (const QJsonValue &v : doc.value(QStringLiteral("streams")).toArray()) {
        QJsonObject el;
        const QJsonObject src = v.toObject();
        for (auto it = src.begin(); it != src.end(); ++it)
            if (keepStream.contains(it.key())) el.insert(it.key(), it.value());
        streams.append(el);
    }
    if (doc.contains(QStringLiteral("streams"))) stub.insert(QStringLiteral("streams"), streams);

    stub.insert(QStringLiteral("archive"), archive);
    return stub;
}

QStringList swingDirsOf(const QString &sessionDir)
{
    return SwingDocReader::findSwingDirs(sessionDir);
}

// A swing folder whose document is an archive STUB (it carries the `archive` block stubOf adds), read
// from its root chunk alone. The location its block names, when asked.
bool isStub(const QString &swingDir, QString *location = nullptr)
{
    const QJsonObject root = SwingStore::loadParts(swingDir, QStringList());
    if (!root.contains(QStringLiteral("archive")))
        return false;
    if (location)
        *location = root.value(QStringLiteral("archive")).toObject().value(QStringLiteral("location")).toString();
    return true;
}

// The session's lock for ONE write (J-11). Acquired → go on. Locked → stop: `r` says who holds it.
// Failed (no lock file can be made at all: a read-only or vanished folder) → go on, and the write
// itself decides and reports (SessionStore::allocateSwing's rule).
bool takeLock(FolderLock &lock, int waitMs, SessionArchiveResult &r)
{
    if (lock.lock(waitMs) != FolderLock::Result::Locked)
        return true;
    r.ok     = false;
    r.status = WriteResult::Status::Locked;
    r.error  = QStringLiteral("The session %1 is being changed by %2; try again in a moment.")
                   .arg(lock.folder(), lock.holder().isEmpty() ? QStringLiteral("another program") : lock.holder());
    ppWarn() << "[SessionArchiver]" << r.error;
    return false;
}

SessionArchiveResult failure(SessionArchiveResult r, const QString &error)
{
    r.ok     = false;
    r.status = WriteResult::Status::Failed;
    r.error  = error;
    return r;
}

QString jsonText(const QJsonValue &v)
{
    if (v.isUndefined()) return QStringLiteral("(absent)");
    const QByteArray t = QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(t.mid(1, t.size() - 2));
}

// Merge one restored swing's review (the file header of session_archiver.h). Called right after its
// document landed, under the session's lock: the restored document holds the ARCHIVED review, unless the
// golfer has edited it since — that is `now`, read here, at the moment of the merge.
bool putBackReview(RecordingRepository &repo, const QString &swingDir, const QString &archivedSwingDir,
                   const QJsonObject &stubReview, QString *error)
{
    const QJsonObject archived = reviewOf(archivedSwingDir);
    const QJsonObject now      = reviewOf(swingDir);
    const SessionArchiver::ReviewMerge m = SessionArchiver::mergeReview(archived, stubReview, now);
    for (const QString &f : m.conflicts)
        ppWarn() << "[SessionArchiver] restore:" << swingDir << "— review field" << f
                 << "was changed both while archived and during the restore; the later edit (made during the"
                 << "restore) is kept:" << jsonText(now.value(f)) << "over" << jsonText(stubReview.value(f));
    if (!m.any)
        return true;
    const WriteResult w = repo.editReview(repo.shotOfFolder(swingDir), m.edit);
    if (!w.ok()) {
        if (error)
            *error = QStringLiteral("restored, but the review made while archived could not be kept for %1: %2")
                         .arg(swingDir, w.error);
        return false;
    }
    return true;
}

} // namespace

// ── Names and reads ──────────────────────────────────────────────────────────────────────────────

QString SessionArchiver::markerPath(const QString &sessionDir) { return SessionFiles::archiveMarker(sessionDir); }

bool SessionArchiver::isArchived(const QString &sessionDir) { return QFileInfo::exists(markerPath(sessionDir)); }

qint64 SessionArchiver::folderBytes(const QString &dir)
{
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

QString SessionArchiver::archivePathFor(const QString &sessionDir, const QString &archiveRoot)
{
    const QFileInfo s(QDir::cleanPath(sessionDir));
    const QString athlete = QFileInfo(s.absolutePath()).fileName();
    return QDir::cleanPath(archiveRoot) + QLatin1Char('/') + athlete + QLatin1Char('/') + s.fileName();
}

SessionArchiver::Marker SessionArchiver::readMarker(const QString &sessionDir)
{
    Marker m;
    QFile f(markerPath(sessionDir));
    if (!f.exists())
        return m;
    m.state = Marker::State::NotAMarker;
    if (!f.open(QIODevice::ReadOnly))
        return m;
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return m;
    m.json = doc.object();
    const QString schema  = m.json.value(QStringLiteral("schema")).toString();
    bool          numeric = false;
    const int version = schema.startsWith(kMarkerPrefix) ? schema.mid(kMarkerPrefix.size()).toInt(&numeric) : 0;
    if (!numeric || version < 1)
        return m;
    if (version > 1) {
        m.state = Marker::State::Newer;
        return m;
    }
    m.location = m.json.value(QStringLiteral("location")).toString();
    m.state    = m.location.isEmpty() ? Marker::State::NotAMarker : Marker::State::Read;
    return m;
}

// ── The file work ────────────────────────────────────────────────────────────────────────────────

int SessionArchiver::lockWait() const { return m_lockWaitMs < 0 ? FolderLock::kWaitMs : m_lockWaitMs; }

bool SessionArchiver::replace(const QString &from, const QString &to, QString *why) const
{
    if (m_replace)
        return m_replace(from, to, why);
    std::error_code ec;
    std::filesystem::rename(fsPath(from), fsPath(to), ec);   // over `to`, in one step (never remove first)
    if (ec && why)
        *why = QString::fromStdString(ec.message());
    return !ec;
}

// Copy `src` to `<dst>.partial`, prove it byte for byte, then — `beforeReplace` first, when given — rename it
// over `dst` in one step. `dst` is only ever its old self or a complete, proven copy. False (with `error`)
// at any step, the partial copy deleted; `replaced` says whether `dst` was replaced.
bool SessionArchiver::putInPlace(const QString &src, const QString &dst, QString *error,
                                 const std::function<bool()> &beforeReplace, bool *replaced) const
{
    if (replaced) *replaced = false;
    if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
        *error = QStringLiteral("cannot create %1").arg(QFileInfo(dst).absolutePath());
        return false;
    }
    const QString part = dst + QStringLiteral(".partial");
    QFile::remove(part);   // a leftover of an interrupted run: ours, never a session's file
    if (!QFile::copy(src, part)) {
        *error = QStringLiteral("cannot copy %1 to %2").arg(src, part);
        return false;
    }
    syncFile(part);
    if (!sameBytes(src, part)) {
        QFile::remove(part);
        *error = QStringLiteral("copy of %1 did not read back identical").arg(src);
        return false;
    }
    if (beforeReplace && !beforeReplace()) {
        QFile::remove(part);
        if (error->isEmpty())
            *error = QStringLiteral("cannot put %1 in place").arg(dst);
        return false;
    }
    QString why;
    if (!replace(part, dst, &why)) {
        QFile::remove(part);
        *error = QStringLiteral("cannot move %1 into place: %2").arg(dst, why);
        return false;
    }
    if (replaced) *replaced = true;
    return true;
}

SessionArchiveResult SessionArchiver::archiveFolder(const QString &sessionDirIn, const SessionArchiveOptions &opt,
                                                    const SessionArchiveProgress &progress,
                                                    const StillArchivable &stillArchivable)
{
    SessionArchiveResult r;
    const QString sessionDir = QDir::cleanPath(sessionDirIn);
    if (!QFileInfo(sessionDir).isDir()) return failure(r, QStringLiteral("no session at %1").arg(sessionDir));
    if (isArchived(sessionDir))          return failure(r, QStringLiteral("%1 is already archived").arg(sessionDir));
    if (opt.archiveRoot.isEmpty())       return failure(r, QStringLiteral("no archive location set"));

    const QString dest = archivePathFor(sessionDir, opt.archiveRoot);
    r.archivePath = dest;
    const QString destClean = QDir::cleanPath(QFileInfo(dest).absoluteFilePath());
    if ((destClean + QLatin1Char('/')).startsWith(sessionDir + QLatin1Char('/'))
        || (sessionDir + QLatin1Char('/')).startsWith(destClean + QLatin1Char('/')))
        return failure(r, QStringLiteral("the archive location overlaps the session"));

    // A JSON-era swing becomes swing.ppsw first (verified), so its stub can carry the summary the
    // document already holds rather than a second derivation of it. One conversion is one write.
    const QStringList swings = swingDirsOf(sessionDir);
    for (const QString &sd : swings) {
        if (SwingStore::info(sd).format != SwingStore::Format::Json) continue;
        FolderLock lock(sessionDir);
        if (!takeLock(lock, lockWait(), r)) return r;
        const ConversionResult c = m_swings.convert(m_swings.shotOfFolder(sd), /*deleteJson=*/true);
        if (c.outcome == ConversionResult::Outcome::Failed)
            return failure(r, QStringLiteral("cannot convert %1 before archiving: %2").arg(sd, c.error));
    }

    // ⚠ A STUB IS NEVER COPIED OVER A FULL DOCUMENT (round 2). A swing already thinned — by a run that
    //   stopped part-way before step 2.6 wrote its marker first, or a marker lost since — holds only its
    //   stub: its full document is in the archive the stub names. Copying the stub into the mirror would
    //   replace that full document for good (pose and tracks lost). So such a swing's document is not
    //   copied: the mirror's full copy is kept, and the swing is thinned again below (its stub is rewritten,
    //   nothing else is left to remove). A stub whose full copy is NOT in this mirror (a different archive
    //   location, or the mirror lost) refuses the archive, with the library untouched: restore it first.
    QSet<QString> stubDocs;   // rel paths of documents that are stubs, their full copy kept in the mirror
    for (const QString &sd : swings) {
        QString was;
        if (!isStub(sd, &was)) continue;
        const QString mirrored = dest + QLatin1Char('/') + QFileInfo(sd).fileName();
        if (!SwingStore::hasDocument(mirrored) || isStub(mirrored))
            return failure(r, QStringLiteral("%1 is already archived (a stub of %2) and its full document is not in %3 — "
                                             "restore it from there first; the library is unchanged")
                                  .arg(sd, was.isEmpty() ? QStringLiteral("an archive") : was, dest));
        stubDocs.insert(QFileInfo(sd).fileName() + QStringLiteral("/swing.ppsw"));
        ppWarn() << "[SessionArchiver]" << sd << "is already a stub; the archive's full document at" << mirrored << "is kept";
    }

    // 1–2. Copy and prove every file into the mirror. Nothing in the library changes until this loop
    //      completes, so it reads the library without its lock.
    const QStringList files = sessionFiles(sessionDir, opt.keepRaw);
    int done = 0;
    for (const QString &rel : files) {
        if (progress && !progress(done, int(files.size())))
            return failure(r, QStringLiteral("cancelled — the library is unchanged"));
        if (stubDocs.contains(rel)) { ++done; continue; }   // the mirror's full document stays
        const QString src = sessionDir + QLatin1Char('/') + rel;
        QString error;
        if (!putInPlace(src, dest + QLatin1Char('/') + rel, &error))
            return failure(r, error);
        r.bytesPacked += QFileInfo(src).size();
        ++done;
    }
    r.members = int(files.size());
    if (progress) progress(done, int(files.size()));

    // The marker, through the one atomic write, under the lock (one write). Written twice: before the first
    // swing is thinned, and at the end with the final counts.
    const QString archivedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    WriteResult::Status markerStatus = WriteResult::Status::Failed;   // why the last marker write failed
    const auto writeMarker = [&](QString *why) {
        FolderLock lock(sessionDir);
        SessionArchiveResult lr;
        markerStatus = WriteResult::Status::Failed;
        if (!takeLock(lock, lockWait(), lr)) { *why = lr.error; markerStatus = lr.status; return false; }
        const AtomicWriteResult w = atomicWrite(markerPath(sessionDir), QJsonDocument(QJsonObject{
            { QStringLiteral("schema"),     QString::fromLatin1(kMarkerSchema) },
            { QStringLiteral("location"),   dest },
            { QStringLiteral("archivedAt"), archivedAt },
            { QStringLiteral("swings"),     r.swings },
            { QStringLiteral("files"),      r.members },
            { QStringLiteral("bytes"),      double(r.bytesPacked) },
            { QStringLiteral("rawKept"),    opt.keepRaw },
        }).toJson(QJsonDocument::Indented), AtomicWriteOptions::intoExistingFolder());
        if (!w.ok) {
            ppWarn() << "[SessionArchiver]" << w.error << "-" << w.detail;
            *why = w.error;
        }
        return w.ok;
    };

    // 3. THE MARKER FIRST (round 2): written BEFORE the first swing is thinned, so the library can never
    //    hold a stub without a marker naming the archive that holds its full copy. From this write on the
    //    session reads as archived — a reader that sees the marker sees "the library may hold stubs; the
    //    archive holds the full copies", true from here on — and archiving it again is refused. Its counts
    //    (`swings`: thinned so far) are made final by the same write at the end. No new key: no reader of
    //    archived.json reads the counts (isArchived reads its presence, readMarker its schema and location).
    {
        QString why;
        if (!writeMarker(&why)) {
            SessionArchiveResult f = failure(r, QStringLiteral("cannot write the archive marker: %1 — the library is unchanged").arg(why));
            f.status = markerStatus;   // Locked when another writer holds the session
            return f;
        }
    }

    // 4. Thin each swing to its stub, one swing per hold of the lock. The stub is written before anything
    //    is deleted, so a failure part-way leaves every swing either untouched or stubbed, the archive
    //    complete, and the marker in place: restoring the session brings the thinned ones back (a swing
    //    that was not thinned is put back byte for byte over itself, its review merged as any other).
    //    The marker's count is brought up to date on the way out — with the loop's lock let go first.
    const auto partway = [&](SessionArchiveResult failed) {
        QString why;
        if (!writeMarker(&why))
            ppWarn() << "[SessionArchiver] the marker's count could not be brought up to date:" << why
                     << "(the marker written before thinning stays)";
        failed.error += QStringLiteral(" — %1 of %2 swings were archived and the session is marked archived; "
                                       "restore it to bring them back").arg(r.swings).arg(swings.size());
        return failed;
    };
    int index = 0;
    for (const QString &sd : swings) {
        if (m_beforeThin) m_beforeThin(index++, sd);
        FolderLock lock(sessionDir);
        SessionArchiveResult lr = r;
        if (!takeLock(lock, lockWait(), lr)) return partway(lr);
        // RE-CHECKED UNDER THIS SWING'S HOLD (step 2.8b; J-52). A restore run from another computer while
        // this one thins removes the marker when it ends: thinning on past that would leave stubs with no
        // marker. So the marker must still be this run's, and the session still not live (resumed here or
        // elsewhere since the start). A marker gone or naming another archive: stop at once, and never write
        // it again — the restore has put the library back whole (it removes the marker only when no stub is
        // left, under this same lock). The session live again: stop, the marker kept over the stubs made.
        if (const Marker now = readMarker(sessionDir); now.state != Marker::State::Read || now.location != dest) {
            ppWarn() << "[SessionArchiver] the archive marker of" << sessionDir << "is gone or names another archive:"
                     << "the session was restored while it was being archived; archiving stopped before" << sd;
            return failure(r, QStringLiteral("%1 was restored (or archived elsewhere) by another run while it was being "
                                             "archived; archiving stopped before %2 — the library holds the restored session")
                                  .arg(sessionDir, QFileInfo(sd).fileName()));
        }
        if (stillArchivable) {
            if (const QString why = stillArchivable(); !why.isEmpty()) {
                lock.unlock();   // the marker's write takes the lock itself
                return partway(failure(r, why));
            }
        }
        const QJsonObject doc = SwingStore::load(sd);
        if (doc.isEmpty()) continue;   // no document: nothing to stub; its files are archived
        const QString thumb = doc.value(QStringLiteral("thumbnail")).toObject()
                                 .value(QStringLiteral("file")).toString(QStringLiteral("thumb.jpg"));
        const QJsonObject block{
            { QStringLiteral("location"),   dest + QLatin1Char('/') + QFileInfo(sd).fileName() },
            { QStringLiteral("archivedAt"), archivedAt } };
        // Through the door (FILE-SHOT-10): a read-only (newer build's) document is refused, never thinned.
        const WriteResult w = m_swings.writeArchiveStub(m_swings.shotOfFolder(sd), stubOf(doc, block));
        if (!w.ok()) {
            lock.unlock();   // the marker's write takes the lock itself (lock(), not lockOrJoin: a second writer)
            return partway(failure(r, QStringLiteral("cannot write the stub for %1: %2").arg(sd, w.error)));
        }
        QDirIterator it(sd, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString name = it.fileName();
            if (QFileInfo(f).absolutePath() == QFileInfo(sd).absoluteFilePath()
                && (name == QLatin1String("swing.ppsw") || name == thumb))
                continue;
            r.bytesFreed += QFileInfo(f).size();
            QFile::remove(f);
        }
        ++r.swings;
    }

    // The marker made final (its counts). Its presence and location — all any reader reads — were right
    // from the first write, so a failure here is logged, not a failed archive.
    QString why;
    if (!writeMarker(&why))
        ppWarn() << "[SessionArchiver] archived, but the marker's counts could not be made final:" << why;
    r.ok     = true;
    r.status = WriteResult::Status::Written;
    return r;
}

SessionArchiveResult SessionArchiver::restoreFolder(const QString &sessionDirIn, const SessionArchiveProgress &progress)
{
    SessionArchiveResult r;
    const QString sessionDir = QDir::cleanPath(sessionDirIn);
    const Marker  marker     = readMarker(sessionDir);
    switch (marker.state) {
    case Marker::State::Missing:
        return failure(r, QStringLiteral("%1 is not archived").arg(sessionDir));
    case Marker::State::NotAMarker:
        return failure(r, QStringLiteral("the archive marker of %1 cannot be read").arg(sessionDir));
    case Marker::State::Newer:
        r.status = WriteResult::Status::ReadOnly;
        r.error  = QStringLiteral("%1 was archived by a newer PinPoint Studio and is restored by that one").arg(sessionDir);
        return r;
    case Marker::State::Read:
        break;
    }
    const QString src = marker.location;
    r.archivePath = src;
    if (!QFileInfo(src).isDir())
        return failure(r, QStringLiteral("the archive %1 cannot be found — is its drive connected?").arg(src));

    QStringList files;
    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(src);
    while (it.hasNext()) {
        const QString abs = it.next();
        if (it.fileName().endsWith(QLatin1String(".partial"))) continue;
        const QString rel = base.relativeFilePath(abs);
        // Never thinned, so the library's copy is the newer: the report (a send recorded while archived is
        // in its log only) and the session file. The archive's is put back only where the library has none.
        const bool keptInLibrary = rel.startsWith(QLatin1String("report/")) || rel == SessionFiles::sessionFileName();
        if (keptInLibrary && QFileInfo::exists(sessionDir + QLatin1Char('/') + rel))
            continue;
        files << rel;
    }
    files.sort();

    int done = 0;
    for (const QString &rel : files) {
        if (progress && !progress(done, int(files.size())))
            return failure(r, QStringLiteral("cancelled part-way — the session is still marked archived; restore again to finish"));
        // A swing's document: its review is merged three ways once it lands (the file header). The stub's
        // review is read under the lock, just before the copy — already made and proven — replaces the stub.
        const bool isDocument = QFileInfo(rel).fileName() == QLatin1String("swing.ppsw")
                                && QFileInfo(rel).path() != QLatin1String(".");
        const QString swingDir = sessionDir + QLatin1Char('/') + QFileInfo(rel).path();
        const QString dst      = sessionDir + QLatin1Char('/') + rel;
        FolderLock  lock(sessionDir);
        QJsonObject stubReview;
        QString     error;
        bool        locked = false;
        const bool  placed = putInPlace(src + QLatin1Char('/') + rel, dst, &error, [&] {
            if (!takeLock(lock, lockWait(), r)) { locked = true; error = r.error; return false; }
            if (isDocument) stubReview = reviewOf(swingDir);
            return true;
        });
        if (!placed)
            return locked ? r : failure(r, error);
        r.bytesFreed += QFileInfo(dst).size();
        ++done;
        if (isDocument && !putBackReview(m_swings, swingDir, src + QLatin1Char('/') + QFileInfo(rel).path(),
                                         stubReview, &error))
            return failure(r, error);
        lock.unlock();   // one write: this file
    }
    r.members = int(files.size());
    r.swings  = int(swingDirsOf(sessionDir).size());
    if (progress) progress(done, int(files.size()));

    FolderLock lock(sessionDir);
    if (!takeLock(lock, lockWait(), r)) return r;
    // THE MARKER GOES LAST, AND ONLY OVER A WHOLE LIBRARY (step 2.8b; J-52). Under the lock, so no archive
    // run can thin a swing between this check and the removal (it re-checks the marker under the same lock
    // before each swing). A swing that is a stub again — another computer archiving the session while this
    // restore ran — keeps the marker, which names where its full copy is.
    for (const QString &sd : swingDirsOf(sessionDir)) {
        if (!isStub(sd)) continue;
        ppWarn() << "[SessionArchiver] restore:" << sd << "is a stub again (archived by another run meanwhile); the marker is kept";
        return failure(r, QStringLiteral("%1 was archived again by another run while it was being restored (%2 is a stub); "
                                         "it is still marked archived — restore it again").arg(sessionDir, QFileInfo(sd).fileName()));
    }
    if (!QFile::remove(markerPath(sessionDir)))
        return failure(r, QStringLiteral("restored, but cannot remove the archive marker"));
    r.ok     = true;
    r.status = WriteResult::Status::Written;
    return r;
}

SessionArchiver::ReviewMerge SessionArchiver::mergeReview(const QJsonObject &archived, const QJsonObject &stub,
                                                          const QJsonObject &now)
{
    ReviewMerge m;
    static const char *const kFields[] = { "rating", "note", "club", "excluded", "clubOverride" };
    for (const char *fk : kFields) {
        const QString f = QString::fromLatin1(fk);
        const QJsonValue base = archived.value(f), theirs = stub.value(f), mine = now.value(f);
        if (theirs == base || mine == theirs)
            continue;                         // the stub changed nothing here, or both agree
        if (mine != base) {
            m.conflicts << f;                 // both changed it: the later edit (mine) stays
            continue;
        }
        // Changed on the stub only: take it. The popover's fields are never removed by an edit,
        // so a stub without one has nothing to give; an annotation absent on the stub was cleared.
        if (f == QLatin1String("rating")) {
            if (theirs.isUndefined()) continue;
            m.edit.rating = theirs.toInt();
        } else if (f == QLatin1String("note")) {
            if (theirs.isUndefined()) continue;
            m.edit.note = theirs.toString();
        } else if (f == QLatin1String("club")) {
            if (theirs.isUndefined()) continue;
            m.edit.club = theirs.toString();
        } else if (f == QLatin1String("excluded")) {
            m.edit.excluded = theirs.toBool();
        } else {
            m.edit.clubOverride = theirs.toString();
        }
        m.any = true;
    }
    return m;
}

// ── The run ──────────────────────────────────────────────────────────────────────────────────────

SessionArchiveResult SessionArchiveRun::archiveInOwnRun(const QString &sessionDir, const SessionArchiveOptions &options,
                                                        const SessionArchiveProgress &progress)
{
    SessionArchiveRun run;
    const SessionId id = run.openSession(sessionDir);
    if (id.isNull())
        ppWarn() << "[Archive] the session of" << sessionDir << "cannot be named (no session file, and no"
                 << "athlete recorded for it)";
    return run.sessions().archive(id, options, progress);
}

SessionArchiveResult SessionArchiveRun::restoreInOwnRun(const QString &sessionDir, const SessionArchiveProgress &progress)
{
    SessionArchiveRun run;
    const SessionId id = run.openSession(sessionDir);
    if (id.isNull())
        ppWarn() << "[Archive] the session of" << sessionDir << "cannot be named (no session file, and no"
                 << "athlete recorded for it)";
    return run.sessions().restore(id, progress);
}

SessionArchiveResult SessionArchiveRun::keepReportCurrent(const QString &sessionDir, const ReportFingerprintFn &fingerprint,
                                                          const std::function<SessionArchiveResult()> &rewrite)
{
    if (!fingerprint)
        return rewrite();
    SessionArchiveRun run;   // this job's own (a worker thread: never the app's identity map)
    const SessionId id = run.openSession(sessionDir);
    if (id.isNull())
        return rewrite();
    SessionRepository &repo = run.sessions();
    return session_report_io::keepCurrent([&] { return repo.readReport(id); },
                                          [&](const ReportRecord &rec) { return repo.writeReportRecord(id, rec); },
                                          [&] { return fingerprint(sessionDir); }, rewrite);
}

QList<SessionArchiveRun::LibrarySession> SessionArchiveRun::scanLibrary(const QString &root, bool withBytes)
{
    QList<LibrarySession> out;
    SessionArchiveRun run;   // this scan's own (a worker thread: never the app's identity map)
    const QDir r(root);
    for (const QString &athlete : r.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir a(r.filePath(athlete));
        for (const QString &session : a.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString dir = a.filePath(session);
            const int swings = int(SwingPaths::swingFolders(dir).size());
            if (swings == 0) continue;
            LibrarySession s;
            s.dir = QDir::cleanPath(dir);
            s.name = session;
            s.athlete = athlete;
            s.dateMs   = QFileInfo(dir).lastModified().toMSecsSinceEpoch();
            s.archived = SessionArchiver::isArchived(dir);
            if (const auto e = run.sessions().open(run.openSession(dir))) {
                if (e->identity.localDay().isValid())
                    s.dateMs = e->identity.localDay().startOfDay().toMSecsSinceEpoch();
                s.archived = e->archived;
            }
            if (withBytes)
                s.bytes = SessionArchiver::folderBytes(dir);
            s.swings = swings;
            out << s;
        }
    }
    std::sort(out.begin(), out.end(), [](const LibrarySession &x, const LibrarySession &y) {
        return x.dateMs != y.dateMs ? x.dateMs > y.dateMs : x.dir > y.dir;
    });
    return out;
}

SessionId SessionArchiveRun::openSession(const QString &sessionDirIn)
{
    if (sessionDirIn.isEmpty())
        return {};
    const QString sessionDir = QDir::cleanPath(sessionDirIn);
    SessionId id = m_sessions.openFolder(sessionDir, AthleteFiles::read(QFileInfo(sessionDir).path()).id);
    if (!id.isNull())
        return id;
    // No session file and no athlete.json: the athlete its swings record, as the swing door names them —
    // so the stubs the archive writes by ShotRef find this same session held.
    for (const QString &name : SwingPaths::swingFolders(sessionDir))
        if (const ShotRef shot = m_swings.shotOfFolder(sessionDir + QLatin1Char('/') + name); !shot.isNull())
            return shot.session();
    return {};
}

} // namespace pinpoint
