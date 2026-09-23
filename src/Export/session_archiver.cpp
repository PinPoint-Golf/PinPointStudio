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
        const SwingStore::ConvertResult c = SwingDocWriter::convertToPpsw(sd, /*deleteJson=*/true);
        if (c.outcome == SwingStore::ConvertResult::Outcome::Failed) {
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

    // A review made while the session was archived (stars, a note, a corrected club) lives only on
    // the stub. Remembered here and put back after the full documents return, or restoring would
    // silently undo it.
    QHash<QString, QJsonObject> stubReviews;
    for (const QString &sd : swingDirsOf(sessionDir))
        stubReviews.insert(QFileInfo(sd).fileName(),
                           SwingStore::load(sd).value(QStringLiteral("review")).toObject());

    QStringList files;
    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(src);
    while (it.hasNext()) {
        const QString abs = it.next();
        if (it.fileName().endsWith(QLatin1String(".partial"))) continue;
        files << base.relativeFilePath(abs);
    }
    files.sort();

    int done = 0;
    for (const QString &rel : files) {
        if (progress && !progress(done, int(files.size()))) {
            r.error = QStringLiteral("cancelled part-way — the session is still marked archived; restore again to finish");
            return r;
        }
        if (!copyVerified(src + QLatin1Char('/') + rel, sessionDir + QLatin1Char('/') + rel, &r.error))
            return r;
        r.bytesFreed += QFileInfo(sessionDir + QLatin1Char('/') + rel).size();
        ++done;
    }
    r.members = int(files.size());
    r.swings  = int(swingDirsOf(sessionDir).size());
    if (progress) progress(done, int(files.size()));

    for (const QString &sd : swingDirsOf(sessionDir)) {
        const QJsonObject stubReview = stubReviews.value(QFileInfo(sd).fileName());
        if (stubReview.isEmpty()) continue;
        if (SwingStore::load(sd).value(QStringLiteral("review")).toObject() == stubReview) continue;
        QString err;
        if (!SwingDocWriter::updateReview(sd, stubReview.value(QStringLiteral("rating")).toInt(),
                                          stubReview.value(QStringLiteral("note")).toString(),
                                          stubReview.value(QStringLiteral("club")).toString(), &err)) {
            r.error = QStringLiteral("restored, but the review made while archived could not be kept for %1: %2")
                          .arg(sd, err);
            return r;
        }
    }

    if (!QFile::remove(markerPath(sessionDir))) {
        r.error = QStringLiteral("restored, but cannot remove the archive marker");
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace pinpoint::SessionArchiver
