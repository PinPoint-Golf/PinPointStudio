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

#pragma once

// THE FILE WORK OF A SESSION'S REPORT RECORDS (refactor step 5.6a; information_model.md FILE-SESS-07, FILE-SESS-09,
// FILE-SESS-13), by session folder, for SessionStore's id-keyed methods (SessionRepository::readReport …
// reportStillsWriter). Adapters ring, beside the session store, like session_diagnostics_io.h. Header-only: a writer
// links folder_lock.cpp.
//
//   report/report_input.json   FILE-SESS-13, the source facts (ReportInput):
//       { "schema": 1, "note": "…", "options": {…}, "prepared": [ms…], "sends": [{ "ms", "method", "sha256" }…] }
//   report/report_meta.json    FILE-SESS-07, the derived facts (ReportRecord):
//       { "schema": 1, "fingerprint", "pdf", "preparedMs", "sha256", "sizeBytes", "photosIncluded" }
//     THE OLDER SHAPE (before 5.6a) is the same file holding the source keys too ("note", "options", "prepared",
//     "sends"). Its schema is the same: the shape is told by its keys, so an older build still reads the new file.
//
// THE RULES.
//   - A newer file (schema above this build's) is read for what this build knows and never written (R-PERS-3).
//     An unparseable report_input.json is never written either (the source facts may be recoverable by hand); an
//     unparseable report_meta.json is a cache with nothing readable in it, and the next make replaces it.
//   - While report_input.json is missing, an older report_meta.json's source facts are read as the input (in memory:
//     a read never writes).
//   - THE ONE INVARIANT (J-66: "nothing is lost"): report_meta.json loses the source keys only when report_input.json
//     is there, reads, and holds them. writeRecord keeps them otherwise, so a restamp of a session never opened since
//     5.6a (an archive's) keeps its note and send log.
//   - THE FOLD (review 5.6a E1): a build older than 5.6a writes the source keys into report_meta.json again after the
//     split, on every write. Before they are dropped, writeRecord folds them into report_input.json: the make history
//     and the send log as a union in time order, exact duplicates dropped; the note and the options are report_meta.json's
//     only when its history holds a make later than report_input.json's latest (an older build's send alone writes the
//     blank note it read from the split file, which must not replace the golfer's). The fold is written, read back and
//     compared; if any of that fails, the keys stay in report_meta.json and the next write tries again.
//   - migrate: write report_input.json from the older report_meta.json, read it back, and only then rewrite
//     report_meta.json as derived only. A crash between the two leaves both readable; the next migrate finishes.
//   - Each write takes the session folder's advisory lock for that one write (R-PERS-6) and replaces the file
//     atomically (src/Core/atomic_write.h). Keys this build does not know are kept on a rewrite.
//   - The stills (FILE-SESS-09) live outside the library, at <Cache>/report-stills/<sha1(folder + key), 12 hex>/:
//     today's location and recipe (the key is the make's fingerprint).

#include "session_files.h"
#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../DomainModel/recording_repository.h"   // WriteResult
#include "../DomainModel/report_record.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <functional>
#include <utility>

namespace pinpoint::session_report_io {

inline constexpr int kReportInputSchema = 1;
inline constexpr int kReportMetaSchema  = 1;

inline QString reportDir(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + SessionFiles::reportFolderName(); }
inline QString metaFile(const QString &sessionDir)  { return reportDir(sessionDir) + QLatin1Char('/') + SessionFiles::reportMetaFileName(); }
inline QString inputFile(const QString &sessionDir) { return reportDir(sessionDir) + QLatin1Char('/') + SessionFiles::reportInputFileName(); }

// The keys of the older shape that belong to the input (FILE-SESS-13).
inline const QStringList &legacyInputKeys()
{
    static const QStringList k{ QStringLiteral("note"), QStringLiteral("options"), QStringLiteral("prepared"), QStringLiteral("sends") };
    return k;
}

// ── JSON ───────────────────────────────────────────────────────────────────────────────────────────────────────────

inline QJsonObject inputToJson(const ReportInput &in)
{
    QJsonArray prepared, sends;
    for (qint64 t : in.preparedHistory) prepared.append(double(t));
    for (const ReportSend &s : in.sends)
        sends.append(QJsonObject{ { QStringLiteral("ms"), double(s.ms) }, { QStringLiteral("method"), s.method },
                                  { QStringLiteral("sha256"), s.sha256 } });
    return QJsonObject{ { QStringLiteral("note"), in.note },
                        { QStringLiteral("options"), QJsonObject::fromVariantMap(in.options) },
                        { QStringLiteral("prepared"), prepared },
                        { QStringLiteral("sends"), sends } };
}

inline ReportInput inputFromJson(const QJsonObject &o)
{
    ReportInput in;
    in.note    = o.value(QStringLiteral("note")).toString();
    in.options = o.value(QStringLiteral("options")).toObject().toVariantMap();
    for (const QJsonValue v : o.value(QStringLiteral("prepared")).toArray()) in.preparedHistory.append(qint64(v.toDouble()));
    for (const QJsonValue v : o.value(QStringLiteral("sends")).toArray()) {
        const QJsonObject s = v.toObject();
        in.sends.append({ qint64(s.value(QStringLiteral("ms")).toDouble()), s.value(QStringLiteral("method")).toString(),
                          s.value(QStringLiteral("sha256")).toString() });
    }
    return in;
}

inline QJsonObject recordToJson(const ReportRecord &r)
{
    return QJsonObject{ { QStringLiteral("fingerprint"), r.fingerprint },
                        { QStringLiteral("pdf"), r.pdfName },
                        { QStringLiteral("preparedMs"), double(r.preparedMs) },
                        { QStringLiteral("sha256"), r.sha256 },
                        { QStringLiteral("sizeBytes"), double(r.sizeBytes) },
                        { QStringLiteral("photosIncluded"), r.photosIncluded } };
}

inline ReportRecord recordFromJson(const QJsonObject &o)
{
    ReportRecord r;
    r.fingerprint    = o.value(QStringLiteral("fingerprint")).toString();
    r.pdfName        = o.value(QStringLiteral("pdf")).toString();
    r.preparedMs     = qint64(o.value(QStringLiteral("preparedMs")).toDouble());
    r.sha256         = o.value(QStringLiteral("sha256")).toString();
    r.sizeBytes      = qint64(o.value(QStringLiteral("sizeBytes")).toDouble());
    r.photosIncluded = o.value(QStringLiteral("photosIncluded")).toBool();
    return r;
}

// ── Reads ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace detail {

struct RawRead {
    ReportRead::State state = ReportRead::State::Missing;
    QJsonObject       root;
    QString           error;
};

inline RawRead readRaw(const QString &path, int schema)
{
    RawRead r;
    if (!QFileInfo::exists(path)) return r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.state = ReportRead::State::Unparseable;
        r.error = QStringLiteral("%1 could not be read: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return r;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    const int s = doc.isObject() ? doc.object().value(QStringLiteral("schema")).toInt() : 0;
    if (err.error != QJsonParseError::NoError || !doc.isObject() || s < 1) {
        r.state = ReportRead::State::Unparseable;
        r.error = QStringLiteral("%1 is not a report record this build can read").arg(QFileInfo(path).fileName());
        return r;
    }
    r.root  = doc.object();
    r.state = s > schema ? ReportRead::State::Newer : ReportRead::State::Read;
    if (s > schema) r.error = QStringLiteral("%1 was written by a newer build").arg(QFileInfo(path).fileName());
    return r;
}

inline bool holdsLegacyKeys(const QJsonObject &meta)
{
    for (const QString &k : legacyInputKeys())
        if (meta.contains(k)) return true;
    return false;
}

inline WriteResult failed(const QString &why) { return { WriteResult::Status::Failed, why }; }

// The session folder's advisory lock for one write; the report folder made. Empty error: acquired.
inline WriteResult lockFor(FolderLock &lock, const QString &sessionDir, bool *acquired)
{
    *acquired = false;
    if (sessionDir.isEmpty() || !QFileInfo(sessionDir).isDir()) return failed(QStringLiteral("The session folder is not there."));
    switch (lock.lock(FolderLock::kWaitMs)) {
    case FolderLock::Result::Acquired:
        *acquired = true;
        if (!QDir().mkpath(reportDir(sessionDir))) return failed(QStringLiteral("The report folder could not be made."));
        return { WriteResult::Status::Written, {} };
    case FolderLock::Result::Locked:
        return { WriteResult::Status::Locked,
                 QStringLiteral("The session %1 is being changed by %2; the report was not saved.")
                     .arg(sessionDir, lock.holder().isEmpty() ? QStringLiteral("another program") : lock.holder()) };
    case FolderLock::Result::Failed:
        break;
    }
    return failed(lock.errorString().isEmpty() ? QStringLiteral("The session folder %1 could not be locked.").arg(sessionDir)
                                               : lock.errorString());
}

inline WriteResult writeJson(const QString &sessionDir, const QString &path, const QJsonObject &root)
{
    FolderLock lock(sessionDir);
    bool acquired = false;
    const WriteResult l = lockFor(lock, sessionDir, &acquired);
    if (!acquired || !l.ok()) return l;
    const AtomicWriteResult w = atomicWrite(path, QJsonDocument(root).toJson(QJsonDocument::Indented), AtomicWriteOptions::intoExistingFolder());
    if (!w.ok) return failed(w.error);
    return { WriteResult::Status::Written, {} };
}

inline QString sha256Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

} // namespace detail

// Both files, as read. Reads only.
inline ReportRead read(const QString &sessionDir)
{
    ReportRead r;
    if (sessionDir.isEmpty()) return r;
    const detail::RawRead meta  = detail::readRaw(metaFile(sessionDir), kReportMetaSchema);
    const detail::RawRead input = detail::readRaw(inputFile(sessionDir), kReportInputSchema);
    r.recordState = meta.state;
    r.inputState  = input.state;
    r.error = !input.error.isEmpty() ? input.error : meta.error;
    if (r.hasRecord()) {
        r.record = recordFromJson(meta.root);
        r.legacy = detail::holdsLegacyKeys(meta.root);
        r.pdfPresent = !r.record.pdfName.isEmpty() && QFileInfo::exists(reportDir(sessionDir) + QLatin1Char('/') + r.record.pdfName);
    }
    if (input.state == ReportRead::State::Read || input.state == ReportRead::State::Newer)
        r.input = inputFromJson(input.root);
    else if (input.state == ReportRead::State::Missing && r.legacy)
        r.input = inputFromJson(meta.root);   // the older shape's source facts, in memory
    return r;
}

// ── Writes ─────────────────────────────────────────────────────────────────────────────────────────────────────────

// FILE-SESS-13, whole. Refused (ReadOnly) for a newer or unparseable file. Keys this build does not know are kept.
inline WriteResult writeInput(const QString &sessionDir, const ReportInput &in)
{
    const detail::RawRead now = detail::readRaw(inputFile(sessionDir), kReportInputSchema);
    if (now.state == ReportRead::State::Newer || now.state == ReportRead::State::Unparseable)
        return { WriteResult::Status::ReadOnly, now.error };
    QJsonObject o = now.root;
    const QJsonObject j = inputToJson(in);
    for (auto it = j.begin(); it != j.end(); ++it) o.insert(it.key(), it.value());
    o.insert(QStringLiteral("schema"), kReportInputSchema);
    return detail::writeJson(sessionDir, inputFile(sessionDir), o);
}

namespace detail {

inline bool sameSend(const ReportSend &a, const ReportSend &b) { return a.ms == b.ms && a.method == b.method && a.sha256 == b.sha256; }

inline qint64 latestMake(const ReportInput &in)
{
    return in.preparedHistory.isEmpty() ? 0 : *std::max_element(in.preparedHistory.cbegin(), in.preparedHistory.cend());
}

// THE FOLD (above): `in` (report_input.json, as read) with the source keys an older build wrote into `meta`.
inline ReportInput foldedInput(const ReportInput &in, const QJsonObject &meta)
{
    const ReportInput old = inputFromJson(meta);
    ReportInput out = in;
    for (qint64 t : old.preparedHistory)
        if (!out.preparedHistory.contains(t)) out.preparedHistory.append(t);
    std::stable_sort(out.preparedHistory.begin(), out.preparedHistory.end());
    for (const ReportSend &s : old.sends)
        if (std::none_of(out.sends.cbegin(), out.sends.cend(), [&](const ReportSend &o) { return sameSend(o, s); }))
            out.sends.append(s);
    std::stable_sort(out.sends.begin(), out.sends.end(), [](const ReportSend &a, const ReportSend &b) { return a.ms < b.ms; });
    if (latestMake(old) > latestMake(in)) {   // the older build made the latest report: its note and options are the latest
        if (meta.contains(QStringLiteral("note")))    out.note    = old.note;
        if (meta.contains(QStringLiteral("options"))) out.options = old.options;
    }
    return out;
}

// Fold `meta`'s source keys into report_input.json (which reads). True: report_input.json now holds them, written
// and read back (or held them already), so they may be dropped from report_meta.json.
inline bool foldIntoInput(const QString &sessionDir, const QJsonObject &meta)
{
    const RawRead now = readRaw(inputFile(sessionDir), kReportInputSchema);
    if (now.state != ReportRead::State::Read) return false;
    const ReportInput in = inputFromJson(now.root);
    const ReportInput want = foldedInput(in, meta);
    if (want == in) return true;
    if (!writeInput(sessionDir, want).ok()) return false;
    const RawRead back = readRaw(inputFile(sessionDir), kReportInputSchema);
    return back.state == ReportRead::State::Read && inputFromJson(back.root) == want;
}

} // namespace detail

// FILE-SESS-07, the derived facts. The PDF's SHA-256 and size are read from the PDF the record names when it is there.
// Refused (ReadOnly) for a newer file. Writes report_input.json only to fold an older build's source keys into it (THE
// FOLD, above); never deletes it. `removeReplacedPdf`: the previous record's PDF, when it names another file (a golfer
// renamed since the last make), is removed after the write — the make's own documents saved through its one command.
// The source keys of an older file are dropped only when report_input.json reads and holds them (THE ONE INVARIANT).
inline WriteResult writeRecord(const QString &sessionDir, ReportRecord rec, bool removeReplacedPdf = false)
{
    const detail::RawRead now = detail::readRaw(metaFile(sessionDir), kReportMetaSchema);
    if (now.state == ReportRead::State::Newer)
        return { WriteResult::Status::ReadOnly, now.error };
    const QString pdf = rec.pdfName.isEmpty() ? QString() : reportDir(sessionDir) + QLatin1Char('/') + rec.pdfName;
    if (!pdf.isEmpty() && QFileInfo::exists(pdf)) {
        rec.sha256    = detail::sha256Of(pdf);
        rec.sizeBytes = QFileInfo(pdf).size();
    }
    QJsonObject o = now.state == ReportRead::State::Read ? now.root : QJsonObject();
    const QString oldPdf = o.value(QStringLiteral("pdf")).toString();
    const QJsonObject j = recordToJson(rec);
    for (auto it = j.begin(); it != j.end(); ++it) o.insert(it.key(), it.value());
    o.insert(QStringLiteral("schema"), kReportMetaSchema);
    if (detail::holdsLegacyKeys(o) && detail::foldIntoInput(sessionDir, now.root))
        for (const QString &k : legacyInputKeys()) o.remove(k);
    const WriteResult w = detail::writeJson(sessionDir, metaFile(sessionDir), o);
    if (w.ok() && removeReplacedPdf && !oldPdf.isEmpty() && oldPdf != rec.pdfName) {
        const QString old = reportDir(sessionDir) + QLatin1Char('/') + oldPdf;
        if (QFileInfo::exists(old)) QFile::remove(old);
    }
    return w;
}

// THE LAZY MIGRATION (FILE-SESS-13's row; J-66). Unchanged for a session with no older file (fresh, or done already),
// for a newer file of either kind, and for an unparseable report_input.json. Otherwise report_input.json is written
// from the older file (unless it is already there, from a migration a crash cut short), read back and compared, and only
// then report_meta.json rewritten as derived only. `migrated`: whether anything was written.
inline WriteResult migrate(const QString &sessionDir, bool *migrated = nullptr)
{
    if (migrated) *migrated = false;
    const ReportRead r = read(sessionDir);
    if (!r.legacy || r.recordState != ReportRead::State::Read) return { WriteResult::Status::Unchanged, {} };
    if (r.inputState == ReportRead::State::Newer || r.inputState == ReportRead::State::Unparseable)
        return { WriteResult::Status::Unchanged, {} };
    if (r.inputState == ReportRead::State::Missing) {
        const WriteResult w = writeInput(sessionDir, r.input);
        if (!w.ok()) return w;
        if (migrated) *migrated = true;
        const ReportRead back = read(sessionDir);
        if (back.inputState != ReportRead::State::Read || !(back.input == r.input))
            return detail::failed(QStringLiteral("report_input.json did not read back as written; report_meta.json is kept as it was."));
    }
    const WriteResult w = writeRecord(sessionDir, r.record);
    if (w.ok() && migrated) *migrated = true;
    return w;
}

// ── A REWRITE THAT CHANGES HOW THE SWINGS ARE STORED, NOT WHAT THEY SAY (sessions_screen_redesign.md S-D6) ──────────
// Archiving thins each swing to a stub and restoring brings the full document back; both move every document's size and
// mtime, so the fingerprint moves while every sentence of the report still holds. A report that was current before
// `rewrite` is re-stamped after it succeeds (FILE-SESS-07's fingerprint only, through `write`: the derived facts, never
// FILE-SESS-13); one already out of date stays so, and a failed rewrite re-stamps nothing. `rewrite` returns something
// with `ok` (SessionArchiveResult). `read` and `write` are the session's records' read and FILE-SESS-07's write (the
// repository's, by id: SessionArchiveRun::keepReportCurrent; or this file's, by folder: keepReportCurrentAt).
template <typename Read, typename Write, typename Rewrite>
auto keepCurrent(Read &&read, Write &&write, const std::function<QString()> &fingerprintNow, Rewrite &&rewrite)
{
    const bool current = fingerprintNow && reportIsCurrent(read(), fingerprintNow());
    auto r = rewrite();
    if (r.ok && current) {
        const ReportRead now = read();
        const QString fp = fingerprintNow();
        if (now.hasRecord() && now.recordWritable() && now.record.fingerprint != fp) {
            ReportRecord rec = now.record;
            rec.fingerprint = fp;
            write(rec);
        }
    }
    return r;
}

// The same by session folder: the file work's own read and write (a tool, a test).
template <typename Rewrite>
auto keepReportCurrentAt(const QString &sessionDir, const ReportFingerprintFn &fingerprint, Rewrite &&rewrite)
{
    return keepCurrent([&] { return read(sessionDir); }, [&](const ReportRecord &rec) { return writeRecord(sessionDir, rec); },
                       fingerprint ? std::function<QString()>([&] { return fingerprint(sessionDir); }) : std::function<QString()>(),
                       std::forward<Rewrite>(rewrite));
}

// ── FILE-SESS-09: the stills ───────────────────────────────────────────────────────────────────────────────────────

// <Cache>/report-stills/<sha1(folder + key), 12 hex>/ (pure path text; today's recipe, byte for byte).
inline QString stillsFolder(const QString &sessionDir, const QString &key)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash((QDir::cleanPath(sessionDir) + key).toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    return (base.isEmpty() ? QDir::tempPath() : base) + QLatin1Char('/') + SessionFiles::reportStillsFolderName()
           + QLatin1Char('/') + h;
}

// The still writer for one make's folder: any thread (file work only).
inline ReportStillWriter stillsWriter(const QString &folder)
{
    return [folder](const QString &fileName, const QByteArray &bytes, QString *why) -> QString {
        if (folder.isEmpty() || fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))) {
            if (why) *why = QStringLiteral("no still name");
            return {};
        }
        const QString path = folder + QLatin1Char('/') + fileName;
        const AtomicWriteResult w = atomicWrite(path, bytes);   // makes the folder
        if (!w.ok) {
            if (why) *why = QStringLiteral("could not write %1").arg(path);
            return {};
        }
        return path;
    };
}

} // namespace pinpoint::session_report_io
