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

// The FILE WORK of a session's diagnostics files — diagnostics.json (FILE-SESS-03, its backups FILE-SESS-12
// and the set-aside copy), session_input.json (FILE-SESS-02), club_baseline.json and work_ons.json
// (FILE-SESS-04, 05): the reads and writes themselves, by session folder (refactor step 3.4a, J-42). Adapters
// ring, beside the session store. Moved here, unchanged, out of session_diagnostics_files.h (step 2.5), so the
// session store can serve them by SessionId (SessionRepository's id-keyed methods) without that header's
// golfer-input resolver and its rule includes, which the many targets that compile session_store.cpp do not
// carry. SessionDiagnosticsFiles' folder-keyed functions forward here, so there is still ONE reader and ONE
// writer of each file; their rules (a newer file read-only, unknown keys kept, the session's lock for each
// write, an atomic write, the older golfer keys carried: J-33) are written there.
//
// HEADER-ONLY, like SessionFiles and atomicWrite: a caller links only what it calls (a writer needs
// folder_lock.cpp). Stateless, so safe from any thread.

#include "session_files.h"
#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../Core/pp_debug.h"
#include "../DomainModel/recording_repository.h"   // WriteResult
#include "../DomainModel/session_repository.h"     // DiagnosticsRead, SessionInputRead, JsonFileRead
#include "../DomainModel/session_input.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pinpoint {

namespace session_diagnostics_detail {

inline WriteResult failed(const QString &why) { return { WriteResult::Status::Failed, why }; }
inline WriteResult written() { return { WriteResult::Status::Written, {} }; }
inline WriteResult readOnly(const QString &file, const QString &why)
{
    return { WriteResult::Status::ReadOnly,
             QStringLiteral("%1 is not changed by this build: %2").arg(QFileInfo(file).fileName(), why) };
}

// The session folder's advisory lock, for one write (R-PERS-6; J-11). Empty = acquired.
inline WriteResult lockFor(FolderLock &lock, const QString &sessionDir, bool *acquired)
{
    *acquired = false;
    switch (lock.lock(FolderLock::kWaitMs)) {
    case FolderLock::Result::Acquired:
        *acquired = true;
        return written();
    case FolderLock::Result::Locked:
        return { WriteResult::Status::Locked,
                 QStringLiteral("The session %1 is being changed by %2; it was not saved.")
                     .arg(sessionDir, lock.holder().isEmpty() ? QStringLiteral("another program") : lock.holder()) };
    case FolderLock::Result::Failed:
        break;
    }
    return failed(lock.errorString().isEmpty() ? QStringLiteral("The session folder %1 could not be locked.").arg(sessionDir)
                                               : lock.errorString());
}

// One JSON file into a session folder that must already be there: locked, atomic.
inline WriteResult writeJson(const QString &sessionDir, const QString &file, const QJsonObject &root,
                      QJsonDocument::JsonFormat format)
{
    if (sessionDir.isEmpty())
        return failed(QStringLiteral("No session folder was given."));
    FolderLock lock(sessionDir);
    bool acquired = false;
    const WriteResult l = lockFor(lock, sessionDir, &acquired);
    if (!acquired) {
        ppWarn() << "[SessionFiles] not written:" << qPrintable(file) << "—" << qPrintable(l.error);
        return l;
    }
    const AtomicWriteResult w = atomicWrite(file, QJsonDocument(root).toJson(format),
                                            AtomicWriteOptions::intoExistingFolder());
    if (!w.ok) {
        ppWarn() << "[SessionFiles]" << qPrintable(w.error) << "—" << qPrintable(w.detail);
        return failed(w.error);
    }
    return written();
}

inline JsonFileRead readJsonFile(const QString &file)
{
    JsonFileRead r;
    if (!QFileInfo::exists(file)) return r;
    r.exists = true;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = f.errorString();
        return r;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        r.error = QStringLiteral("%1 at byte %2").arg(err.errorString()).arg(err.offset);
        return r;
    }
    if (!doc.isObject()) {
        r.error = QStringLiteral("not a JSON object");
        return r;
    }
    r.ok   = true;
    r.root = doc.object();
    return r;
}

// "pinpoint.session-input/1" → 1; 0 for anything else.
inline int sessionInputSchemaNumber(const QString &schema)
{
    static const QString prefix = QStringLiteral("pinpoint.session-input/");
    if (!schema.startsWith(prefix)) return 0;
    bool ok = false;
    const int n = schema.mid(prefix.size()).toInt(&ok);
    return ok ? n : 0;
}

inline constexpr int kSessionInputVersion = 1;

} // namespace session_diagnostics_detail

namespace session_diagnostics_io {

// The envelope's own schema tag (SessionDiagnosticsFiles::kEnvelopeVersion) and session_input.json's.
inline constexpr int         kEnvelopeVersion    = 1;
inline constexpr const char *kSessionInputSchema = "pinpoint.session-input/1";

// ── The older shape ──────────────────────────────────────────────────────────────────────────────────

inline const QStringList &legacyGolferKeys()
{
    static const QStringList keys{ QStringLiteral("intent"),     QStringLiteral("screens"),
                                   QStringLiteral("shotIntent"), QStringLiteral("intentFrom"),
                                   QStringLiteral("corrections") };
    return keys;
}

inline QJsonObject legacyGolferInputOf(const QJsonObject &envelopeRoot)
{
    QJsonObject out;
    for (const QString &k : legacyGolferKeys())
        if (envelopeRoot.contains(k)) out.insert(k, envelopeRoot.value(k));
    return out;
}

// ── FILE-SESS-03 ─────────────────────────────────────────────────────────────────────────────────────

inline DiagnosticsRead readDiagnostics(const QString &sessionDir)
{
    DiagnosticsRead r;
    if (sessionDir.isEmpty()) return r;
    const QString path = SessionFiles::diagnosticsFile(sessionDir);
    if (!QFileInfo::exists(path)) return r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.state = DiagnosticsRead::State::Unopenable;
        r.error = f.errorString();
        return r;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.state = DiagnosticsRead::State::Unparseable;
        r.error = err.error != QJsonParseError::NoError
                      ? QStringLiteral("%1 at byte %2").arg(err.errorString()).arg(err.offset)
                      : QStringLiteral("not a JSON object");
        return r;
    }
    const QJsonObject root = doc.object();
    DiagnosticsEnvelope &e = r.envelope;
    e.schemaVersion     = root.value(QStringLiteral("schemaVersion")).toInt(0);
    e.session           = root.value(QStringLiteral("session")).toObject();
    e.ledger            = root.value(QStringLiteral("ledger")).toObject();
    e.legacyGolferInput = legacyGolferInputOf(root);
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString &k = it.key();
        if (k == QLatin1String("schemaVersion") || k == QLatin1String("session") || k == QLatin1String("ledger")
            || legacyGolferKeys().contains(k))
            continue;
        e.other.insert(k, it.value());
    }
    if (e.schemaVersion > kEnvelopeVersion) {
        r.state = DiagnosticsRead::State::Newer;
        r.error = QStringLiteral("saved by a newer PinPoint Studio (envelope version %1; this build reads up to %2)")
                      .arg(e.schemaVersion).arg(kEnvelopeVersion);
    } else {
        r.state = DiagnosticsRead::State::Read;
    }
    return r;
}

inline WriteResult writeDiagnostics(const QString &sessionDir, const QJsonObject &session,
                                                      const QJsonObject &ledger, const QJsonObject &legacyFallback,
                                                      DiagnosticsRead::State *state)
{
    const QString path = SessionFiles::diagnosticsFile(sessionDir);
    // What is on disk NOW: its older golfer keys are carried as they stand (J-33), and it decides whether
    // this write may be made at all.
    const DiagnosticsRead now = readDiagnostics(sessionDir);
    if (state) *state = now.state;
    switch (now.state) {
    case DiagnosticsRead::State::Newer:
        return session_diagnostics_detail::readOnly(path, now.error);
    case DiagnosticsRead::State::Unparseable:
    case DiagnosticsRead::State::Unopenable:
        return session_diagnostics_detail::failed(QStringLiteral("%1 could not be read (%2), so it was not written over.")
                          .arg(QFileInfo(path).fileName(), now.error));
    case DiagnosticsRead::State::Missing:
    case DiagnosticsRead::State::Read:
        break;
    }
    QJsonObject root = now.parsed() ? now.envelope.other : QJsonObject();
    const QJsonObject carried = now.parsed() ? now.envelope.legacyGolferInput : legacyGolferInputOf(legacyFallback);
    for (auto it = carried.constBegin(); it != carried.constEnd(); ++it) root.insert(it.key(), it.value());
    root[QStringLiteral("schemaVersion")] = kEnvelopeVersion;
    root[QStringLiteral("session")]       = session;
    // Nested, so diagnostic_ledger.h's reader takes exactly the object its writer produced.
    root[QStringLiteral("ledger")]        = ledger;
    return session_diagnostics_detail::writeJson(sessionDir, path, root, QJsonDocument::Compact);
}

inline WriteResult backupDiagnostics(const QString &sessionDir, const QString &tag, QString *backupPath)
{
    const QString path   = SessionFiles::diagnosticsFile(sessionDir);
    const QString backup = SessionFiles::diagnosticsBackupFile(sessionDir, tag);
    if (backupPath) *backupPath = backup;
    if (tag.isEmpty() || tag.contains(QLatin1Char('/')) || tag.contains(QLatin1Char('\\')))
        return session_diagnostics_detail::failed(QStringLiteral("The backup's tag \"%1\" is not a plain name.").arg(tag));
    if (QFileInfo::exists(backup))
        return session_diagnostics_detail::failed(QStringLiteral("%1 already exists.").arg(QFileInfo(backup).fileName()));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return session_diagnostics_detail::failed(QStringLiteral("%1 could not be read: %2").arg(QFileInfo(path).fileName(), f.errorString()));
    const QByteArray bytes = f.readAll();
    f.close();
    FolderLock lock(sessionDir);
    bool acquired = false;
    const WriteResult l = session_diagnostics_detail::lockFor(lock, sessionDir, &acquired);
    if (!acquired) return l;
    // A copy, byte for byte: the live file stays where it is, so there is no moment without one.
    const AtomicWriteResult w = atomicWrite(backup, bytes, AtomicWriteOptions::intoExistingFolder());
    if (!w.ok) return session_diagnostics_detail::failed(w.error);
    return session_diagnostics_detail::written();
}

inline WriteResult setAsideDiagnostics(const QString &sessionDir, QString *asidePath)
{
    const QString path = SessionFiles::diagnosticsFile(sessionDir);
    // A name of its own, never reused: a second unreadable file in the same second is a second copy.
    const QString stem =
        SessionFiles::diagnosticsAsideStem(sessionDir, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString aside = stem;
    for (int n = 2; QFileInfo::exists(aside); ++n) aside = stem + QLatin1Char('-') + QString::number(n);
    if (asidePath) *asidePath = aside;
    FolderLock lock(sessionDir);
    bool acquired = false;
    const WriteResult l = session_diagnostics_detail::lockFor(lock, sessionDir, &acquired);
    if (!acquired) return l;
    if (!QFile::rename(path, aside))
        return session_diagnostics_detail::failed(QStringLiteral("%1 could not be moved aside.").arg(QFileInfo(path).fileName()));
    return session_diagnostics_detail::written();
}

// ── FILE-SESS-02 ─────────────────────────────────────────────────────────────────────────────────────

inline SessionInputRead readSessionInput(const QString &sessionDir)
{
    SessionInputRead r;
    if (sessionDir.isEmpty()) return r;
    const JsonFileRead f = session_diagnostics_detail::readJsonFile(SessionFiles::sessionInputFile(sessionDir));
    if (!f.exists) return r;
    if (!f.ok) {
        // Never overwritten, so it can be looked at; its fields fall back as if it were not there.
        r.state = SessionInputRead::State::Unparseable;
        r.error = f.error;
        return r;
    }
    const int version = session_diagnostics_detail::sessionInputSchemaNumber(f.root.value(QStringLiteral("schema")).toString());
    if (version > session_diagnostics_detail::kSessionInputVersion) {
        r.state = SessionInputRead::State::Newer;
        r.error = QStringLiteral("saved by a newer PinPoint Studio (%1)").arg(f.root.value(QStringLiteral("schema")).toString());
    } else {
        r.state = SessionInputRead::State::Read;
    }
    const QJsonObject &o = f.root;
    SessionInput &in = r.input;
    if (o.contains(QStringLiteral("declaredFocus"))) {
        const QJsonObject d = o.value(QStringLiteral("declaredFocus")).toObject();
        in.declaredFocus = DeclaredFocus{ d.value(QStringLiteral("conditionId")).toString(),
                                          d.value(QStringLiteral("fromShot")).toInt(-1) };
    }
    if (o.contains(QStringLiteral("declaredMiss")))
        in.declaredMiss = o.value(QStringLiteral("declaredMiss")).toString();
    if (o.contains(QStringLiteral("screenAnswers"))) {
        QMap<QString, bool> answers;
        const QJsonObject a = o.value(QStringLiteral("screenAnswers")).toObject();
        for (auto it = a.constBegin(); it != a.constEnd(); ++it) answers.insert(it.key(), it.value().toBool());
        in.screenAnswers = answers;
    }
    if (o.contains(QStringLiteral("intentRuns"))) {
        QVector<IntentRun> runs;
        for (const QJsonValue &v : o.value(QStringLiteral("intentRuns")).toArray()) {
            const QJsonObject run = v.toObject();
            runs.append(IntentRun{ run.value(QStringLiteral("fromShot")).toInt(-1), run.value(QStringLiteral("shape")).toString() });
        }
        in.intentRuns = runs;
    }
    return r;
}

inline WriteResult writeSessionInput(const QString &sessionDir, const SessionInput &input)
{
    const QString path = SessionFiles::sessionInputFile(sessionDir);
    const JsonFileRead now = session_diagnostics_detail::readJsonFile(path);
    if (now.exists && !now.ok)
        return session_diagnostics_detail::readOnly(path, QStringLiteral("it could not be read (%1)").arg(now.error));
    if (now.ok && session_diagnostics_detail::sessionInputSchemaNumber(now.root.value(QStringLiteral("schema")).toString()) > session_diagnostics_detail::kSessionInputVersion)
        return session_diagnostics_detail::readOnly(path, QStringLiteral("it was saved by a newer PinPoint Studio"));

    QJsonObject o = now.ok ? now.root : QJsonObject();     // keys this build does not know are kept
    o.insert(QStringLiteral("schema"), QLatin1String(kSessionInputSchema));
    if (input.declaredFocus)
        o.insert(QStringLiteral("declaredFocus"),
                 QJsonObject{ { QStringLiteral("conditionId"), input.declaredFocus->conditionId },
                              { QStringLiteral("fromShot"),    input.declaredFocus->fromShot } });
    if (input.declaredMiss)
        o.insert(QStringLiteral("declaredMiss"), *input.declaredMiss);
    if (input.screenAnswers) {
        QJsonObject a;
        for (auto it = input.screenAnswers->constBegin(); it != input.screenAnswers->constEnd(); ++it)
            a.insert(it.key(), it.value());
        o.insert(QStringLiteral("screenAnswers"), a);
    }
    if (input.intentRuns) {
        QJsonArray runs;
        for (const IntentRun &run : *input.intentRuns)
            runs.append(QJsonObject{ { QStringLiteral("fromShot"), run.fromShot }, { QStringLiteral("shape"), run.shape } });
        o.insert(QStringLiteral("intentRuns"), runs);
    }
    return session_diagnostics_detail::writeJson(sessionDir, path, o, QJsonDocument::Indented);
}

// ── FILE-SESS-04, 05 ─────────────────────────────────────────────────────────────────────────────────

inline bool clubBaselineExists(const QString &sessionDir)
{
    return !sessionDir.isEmpty() && QFileInfo::exists(SessionFiles::clubBaselineFile(sessionDir));
}

inline JsonFileRead readClubBaseline(const QString &sessionDir)
{
    return sessionDir.isEmpty() ? JsonFileRead() : session_diagnostics_detail::readJsonFile(SessionFiles::clubBaselineFile(sessionDir));
}

inline WriteResult writeClubBaseline(const QString &sessionDir, const QJsonObject &root)
{
    return session_diagnostics_detail::writeJson(sessionDir, SessionFiles::clubBaselineFile(sessionDir), root, QJsonDocument::Compact);
}

inline JsonFileRead readWorkOns(const QString &sessionDir)
{
    return sessionDir.isEmpty() ? JsonFileRead() : session_diagnostics_detail::readJsonFile(SessionFiles::workOnsFile(sessionDir));
}

inline WriteResult writeWorkOns(const QString &sessionDir, const QJsonObject &root)
{
    return session_diagnostics_detail::writeJson(sessionDir, SessionFiles::workOnsFile(sessionDir), root, QJsonDocument::Compact);
}

} // namespace session_diagnostics_io

} // namespace pinpoint
