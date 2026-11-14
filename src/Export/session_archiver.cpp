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

#include "swing_doc.h"
#include "swing_store.h"
#include "swing_store_repository.h"   // the one door to the swing files (step 1.4)
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
#include <QSaveFile>
#include <QSet>

namespace pinpoint::SessionArchiver {

namespace {

constexpr auto kMarkerSchema = "pinpoint.archive/1";

// The swing files are written through RecordingRepository like every other writer's: the
// conversion before archiving, and the review restored after. The archiver is part of the store
// (src/Export) and the adapter is stateless, so it keeps its own. The one exception is the stub
// (SwingStore::save below), which moves with the archive into step 2's SessionRepository.
RecordingRepository &recordings()
{
    static SwingStoreRecordingRepository repo;
    return repo;
}

// A swing document's review, read from its root chunk alone (the pose and the other tracks are
// never decoded): the archive's copy can be a ~28 MB document on an external drive.
QJsonObject reviewOf(const QString &swingDir)
{
    return SwingStore::loadParts(swingDir, QStringList()).value(QStringLiteral("review")).toObject();
}

bool putBackReview(const QString &swingDir, const QString &archivedSwingDir, const QJsonObject &stubReview,
                   QString *error);

// Regenerable caches: never archived, and removed with the rest when a swing is thinned.
bool isCache(const QString &name)
{
    return name == QLatin1String("swing_summary.json") || name == QLatin1String("swing_phasegrid.json");
}

// Every file of the session worth keeping, relative to it, sorted. Dot-files and half-written
// temporaries are never part of a session.
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
        if (isCache(name) || rel == QLatin1String("archived.json"))                  continue;
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

// Copy, prove, then put in place. The destination is only ever a complete, verified file.
bool copyVerified(const QString &src, const QString &dst, QString *error)
{
    if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
        *error = QStringLiteral("cannot create %1").arg(QFileInfo(dst).absolutePath());
        return false;
    }
    const QString part = dst + QStringLiteral(".partial");
    QFile::remove(part);
    if (!QFile::copy(src, part)) {
        *error = QStringLiteral("cannot copy %1 to %2").arg(src, part);
        return false;
    }
    if (!sameBytes(src, part)) {
        QFile::remove(part);
        *error = QStringLiteral("copy of %1 did not read back identical").arg(src);
        return false;
    }
    QFile::remove(dst);
    if (!QFile::rename(part, dst)) {
        QFile::remove(part);
        *error = QStringLiteral("cannot move %1 into place").arg(dst);
        return false;
    }
    return true;
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

} // namespace

QString markerPath(const QString &sessionDir) { return sessionDir + QStringLiteral("/archived.json"); }

bool isArchived(const QString &sessionDir) { return QFileInfo::exists(markerPath(sessionDir)); }

qint64 folderBytes(const QString &dir)
{
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

QString archivePathFor(const QString &sessionDir, const QString &archiveRoot)
{
    const QFileInfo s(QDir::cleanPath(sessionDir));
    const QString athlete = QFileInfo(s.absolutePath()).fileName();
    return QDir::cleanPath(archiveRoot) + QLatin1Char('/') + athlete + QLatin1Char('/') + s.fileName();
}

Result archive(const QString &sessionDirIn, const QString &archiveRoot, const Options &opt,
               const Progress &progress)
{
    Result r;
    const QString sessionDir = QDir::cleanPath(sessionDirIn);
    if (!QFileInfo(sessionDir).isDir()) { r.error = QStringLiteral("no session at %1").arg(sessionDir); return r; }
    if (isArchived(sessionDir))          { r.error = QStringLiteral("%1 is already archived").arg(sessionDir); return r; }
    if (archiveRoot.isEmpty())           { r.error = QStringLiteral("no archive location set"); return r; }

    const QString dest = archivePathFor(sessionDir, archiveRoot);
    r.archivePath = dest;
    const QString destClean = QDir::cleanPath(QFileInfo(dest).absoluteFilePath());
    if ((destClean + QLatin1Char('/')).startsWith(sessionDir + QLatin1Char('/'))
        || (sessionDir + QLatin1Char('/')).startsWith(destClean + QLatin1Char('/'))) {
        r.error = QStringLiteral("the archive location overlaps the session");
        return r;
    }

    // A JSON-era swing becomes swing.ppsw first (verified), so its stub can carry the summary the
    // document already holds rather than a second derivation of it.
    const QStringList swings = swingDirsOf(sessionDir);
    for (const QString &sd : swings) {
        if (SwingStore::info(sd).format != SwingStore::Format::Json) continue;
        const ConversionResult c = recordings().convert(sd, /*deleteJson=*/true);
        if (c.outcome == ConversionResult::Outcome::Failed) {
            r.error = QStringLiteral("cannot convert %1 before archiving: %2").arg(sd, c.error);
            return r;
        }
    }

    // 1–2. Copy and prove every file. Nothing in the library changes until this loop completes.
    const QStringList files = sessionFiles(sessionDir, opt.keepRaw);
    int done = 0;
    for (const QString &rel : files) {
        if (progress && !progress(done, int(files.size()))) {
            r.error = QStringLiteral("cancelled — the library is unchanged");
            return r;
        }
        const QString src = sessionDir + QLatin1Char('/') + rel;
        if (!copyVerified(src, dest + QLatin1Char('/') + rel, &r.error))
            return r;
        r.bytesPacked += QFileInfo(src).size();
        ++done;
    }
    r.members = int(files.size());
    if (progress) progress(done, int(files.size()));

    // 3. Thin each swing to its stub. The stub is written before anything is deleted, so a failure
    //    part-way leaves every swing either untouched or stubbed — and the archive complete.
    const QString archivedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QString &sd : swings) {
        const QJsonObject doc = SwingStore::load(sd);
        if (doc.isEmpty()) continue;   // no document: nothing to stub; its files are archived
        const QString thumb = doc.value(QStringLiteral("thumbnail")).toObject()
                                 .value(QStringLiteral("file")).toString(QStringLiteral("thumb.jpg"));
        const QJsonObject block{
            { QStringLiteral("location"),   dest + QLatin1Char('/') + QFileInfo(sd).fileName() },
            { QStringLiteral("archivedAt"), archivedAt } };
        QString err;
        if (!SwingStore::save(sd, stubOf(doc, block), &err)) {
            r.error = QStringLiteral("cannot write the stub for %1: %2").arg(sd, err);
            return r;
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

    // 4. The marker.
    QSaveFile m(markerPath(sessionDir));
    if (!m.open(QIODevice::WriteOnly)) { r.error = QStringLiteral("cannot write the archive marker"); return r; }
    m.write(QJsonDocument(QJsonObject{
        { QStringLiteral("schema"),     QString::fromLatin1(kMarkerSchema) },
        { QStringLiteral("location"),   dest },
        { QStringLiteral("archivedAt"), archivedAt },
        { QStringLiteral("swings"),     r.swings },
        { QStringLiteral("files"),      r.members },
        { QStringLiteral("bytes"),      double(r.bytesPacked) },
        { QStringLiteral("rawKept"),    opt.keepRaw },
    }).toJson(QJsonDocument::Indented));
    if (!m.commit()) { r.error = QStringLiteral("cannot write the archive marker"); return r; }

    r.ok = true;
    return r;
}

Result restore(const QString &sessionDirIn, const Progress &progress)
{
    Result r;
    const QString sessionDir = QDir::cleanPath(sessionDirIn);
    QFile mf(markerPath(sessionDir));
    if (!mf.open(QIODevice::ReadOnly)) { r.error = QStringLiteral("%1 is not archived").arg(sessionDir); return r; }
    const QJsonObject marker = QJsonDocument::fromJson(mf.readAll()).object();
    mf.close();
    const QString src = marker.value(QStringLiteral("location")).toString();
    r.archivePath = src;
    if (src.isEmpty() || !QFileInfo(src).isDir()) {
        r.error = QStringLiteral("the archive %1 cannot be found — is its drive connected?").arg(src);
        return r;
    }

    QStringList files;
    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(src);
    while (it.hasNext()) {
        const QString abs = it.next();
        if (it.fileName().endsWith(QLatin1String(".partial"))) continue;
        const QString rel = base.relativeFilePath(abs);
        // The report was never thinned: the library's copy is the newer (a send recorded while
        // archived is in its log only), so the archive's never overwrites it.
        if (rel.startsWith(QLatin1String("report/")) && QFileInfo::exists(sessionDir + QLatin1Char('/') + rel))
            continue;
        files << rel;
    }
    files.sort();

    int done = 0;
    for (const QString &rel : files) {
        if (progress && !progress(done, int(files.size()))) {
            r.error = QStringLiteral("cancelled part-way — the session is still marked archived; restore again to finish");
            return r;
        }
        // A swing's document: its review is merged three ways once it lands (the file header).
        // The stub's review is read here, just before the copy replaces the stub.
        const bool isDocument = QFileInfo(rel).fileName() == QLatin1String("swing.ppsw")
                                && QFileInfo(rel).path() != QLatin1String(".");
        const QString swingDir = sessionDir + QLatin1Char('/') + QFileInfo(rel).path();
        const QJsonObject stubReview = isDocument ? reviewOf(swingDir) : QJsonObject();
        if (!copyVerified(src + QLatin1Char('/') + rel, sessionDir + QLatin1Char('/') + rel, &r.error))
            return r;
        r.bytesFreed += QFileInfo(sessionDir + QLatin1Char('/') + rel).size();
        ++done;
        if (isDocument && !putBackReview(swingDir, src + QLatin1Char('/') + QFileInfo(rel).path(),
                                         stubReview, &r.error))
            return r;
    }
    r.members = int(files.size());
    r.swings  = int(swingDirsOf(sessionDir).size());
    if (progress) progress(done, int(files.size()));

    if (!QFile::remove(markerPath(sessionDir))) {
        r.error = QStringLiteral("restored, but cannot remove the archive marker");
        return r;
    }
    r.ok = true;
    return r;
}

namespace {

// Merge one restored swing's review (the file header of session_archiver.h). Called right after its
// document landed: the restored document holds the ARCHIVED review, unless the golfer has edited it
// since — that is `now`, read here, at the moment of the merge.
QString jsonText(const QJsonValue &v)
{
    if (v.isUndefined()) return QStringLiteral("(absent)");
    const QByteArray t = QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(t.mid(1, t.size() - 2));
}

bool putBackReview(const QString &swingDir, const QString &archivedSwingDir, const QJsonObject &stubReview,
                   QString *error)
{
    const QJsonObject archived = reviewOf(archivedSwingDir);
    const QJsonObject now      = reviewOf(swingDir);
    const ReviewMerge m = mergeReview(archived, stubReview, now);
    for (const QString &f : m.conflicts)
        ppWarn() << "[SessionArchiver] restore:" << swingDir << "— review field" << f
                 << "was changed both while archived and during the restore; the later edit (made during the"
                 << "restore) is kept:" << jsonText(now.value(f)) << "over" << jsonText(stubReview.value(f));
    if (!m.any)
        return true;
    const WriteResult w = recordings().editReview(swingDir, m.edit);
    if (!w.ok()) {
        if (error)
            *error = QStringLiteral("restored, but the review made while archived could not be kept for %1: %2")
                         .arg(swingDir, w.error);
        return false;
    }
    return true;
}

} // namespace

ReviewMerge mergeReview(const QJsonObject &archived, const QJsonObject &stub, const QJsonObject &now)
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

} // namespace pinpoint::SessionArchiver
