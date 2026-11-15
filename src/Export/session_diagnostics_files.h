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

// SessionDiagnosticsFiles — THE ONE READER AND WRITER of a session's diagnostics files and of the golfer's
// input (refactor step 2.5, audit H-04 and D-08; build/refactor/step-2/BRIEF.md §3.3, §4.4, §4.8, §4.11;
// docs/developer/information_model.md FILE-SESS-02…05, FILE-SESS-12, FILE-SHOT-02). Adapters ring, beside
// the session store (session_store.h) whose files these are; every name is SessionFiles' (session_files.h).
//
//   FILE-SESS-03  diagnostics.json — A CACHE that can be deleted at any time (DEC-160: IM-S2-3): the ledger
//                 (diagnostic_ledger.h's rows, every shot's findings and derived data, kept by the session as
//                 its collection: MD-06, DEC-135) and the session block. Read in ONE place (readDiagnostics,
//                 replacing the fifteen parsers of D-08), its `schemaVersion` checked (H-08): one newer than
//                 this build's is READ-ONLY, every write refuses it
//   FILE-SESS-02  session_input.json — the golfer's session-level input (SessionInput,
//                 src/DomainModel/session_input.h), `schema "pinpoint.session-input/1"`, checked on read; a
//                 newer or unparseable file is never overwritten; keys this build does not know are kept
//   FILE-SHOT-02  the shot's `review.shotIntent` and `review.labelCorrections` — through RecordingRepository
//                 (readReview / editReview), never here; golferInput() below reads them back
//   FILE-SESS-04, 05  club_baseline.json, work_ons.json — their bytes; the rules that compute them stay
//                 where they are (Coaching), their writers are today's (the model, the work-ons controller)
//   FILE-SESS-12  diagnostics.json.<tag> — backupDiagnostics, a COPY (the live file stays where it is)
//
// THE OLDER SHAPE, AND WHY NO GOLFER INPUT IS LOST BEFORE MIGRATION M3 (the lead's call J-33). Before step 2.5
// the golfer's input lived inside diagnostics.json under the keys legacyGolferKeys() names (`intent`,
// `screens`, `shotIntent`, `intentFrom`, `corrections`). Until migration M3 (step 2.8) has moved a session's
// copy, and checked it field by field:
//   · golferInput() reads each field from its NEW home first — session_input.json for the session level, the
//     shot's review for the shot level — and from diagnostics.json's older keys only when the new home does
//     not hold that field (in memory; nothing is written by a read);
//   · writeDiagnostics() CARRIES the older keys over unchanged: the ones in the file as it stands when the
//     write is made, or — when there is no file any more — the ones the caller read from it. It never drops
//     one; removing them is M3's job;
//   · new edits go only to the new homes. A shot whose older copy says something the golfer has since
//     cleared holds the cleared answer explicitly in its review (ReviewEdit::keepEmptyGolferInput), so the
//     older copy cannot come back.
//
// A WRITE THAT FAILS SAYS SO (DEC-126; §4.11): every write returns WriteResult (Written, ReadOnly, Locked,
// Failed with the reason); the caller surfaces it through the existing notifications. Each write takes the
// session folder's advisory lock for that one write (FolderLock, R-PERS-6; J-11) and is atomic (atomicWrite,
// R-PERS-2).
//
// INTERIM (IM-S2-4: DEC-160; step 2.5's J1). Keyed by the session FOLDER, like SessionFiles (2.2's J14) and
// AthleteFiles (2.3's J7), because every caller today holds a folder — the diagnostics model (made by QML),
// the work-ons catch-up, Charts, the report and the tools. Step 3's `Session` reaches the same functions
// through SessionRepository by its id. Stateless, so safe from any thread (each worker passes its own
// RecordingRepository to golferInput()).

#include "session_files.h"
#include "session_store.h"
#include "swing_paths.h"
#include "swing_store_repository.h"
#include "../Coaching/club_distances.h"       // ShotCorrection
#include "../Coaching/tendencies.h"           // shotCorrectionsFromJson, shotIntentsFromJson
#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../Core/pp_debug.h"
#include "../DomainModel/recording_repository.h"   // WriteResult, RecordingRepository
#include "../DomainModel/session_input.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSemaphore>
#include <QThreadPool>

#include <atomic>
#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

// HEADER-ONLY (as SessionFiles, AthleteFiles and atomicWrite): a caller links only what it calls. A reader of
// diagnostics.json alone (Charts) needs no new source; a writer needs folder_lock.cpp; golferInput() needs the
// session store and the swing door (session_store.cpp, session_identity.cpp, swing_store_repository.cpp).

namespace pinpoint {

// diagnostics.json as read: the cache half, and the older shape's golfer keys kept apart.
struct DiagnosticsEnvelope {
    int         schemaVersion = 0;   // the envelope's own version (kEnvelopeVersion), not the ledger's
    QJsonObject session;             // the session block (stage, shot count, the swing folders, the stamps)
    QJsonObject ledger;              // diagnostic_ledger.h's object, nested, with ITS schemaVersion
    QJsonObject legacyGolferInput;   // the older shape's golfer keys, verbatim (legacyGolferKeys())
    QJsonObject other;               // any other top-level key this build does not know: kept on rewrite
};

struct DiagnosticsRead {
    enum class State {
        Missing,      // no file: nothing reduced yet (or the cache was deleted)
        Read,         // parsed and of this build's schema or older
        Newer,        // parsed, but a newer build's: read what can be read, write nothing (READ-ONLY)
        Unparseable,  // it opens but is not a JSON object: the file itself is bad (set it aside)
        Unopenable,   // it is there and cannot be opened: met at a bad moment (a share, a permission)
    };
    State               state = State::Missing;
    DiagnosticsEnvelope envelope;
    QString             error;     // why, for Unparseable / Unopenable / Newer
    bool exists() const { return state != State::Missing; }
    bool parsed() const { return state == State::Read || state == State::Newer; }
};

struct SessionInputRead {
    enum class State { Missing, Read, Newer, Unparseable };
    State        state = State::Missing;
    SessionInput input;            // each field present only when the file holds it
    QString      error;
    bool readOnly() const { return state == State::Newer || state == State::Unparseable; }
};

// A small JSON file read whole (club_baseline.json, work_ons.json).
struct JsonFileRead {
    bool        exists = false;   // the file is there
    bool        ok = false;       // it opened and is a JSON object
    QJsonObject root;
    QString     error;
};

// THE GOLFER'S INPUT FOR ONE SESSION, RESOLVED: each field from its new home, else the older copy (J-33).
struct GolferInput {
    // Session level, every field filled (the defaults are "none").
    DeclaredFocus       focus;
    QString             declaredMiss;
    QHash<QString, bool> screens;
    IntentRun           intentRun;
    // Shot level, by swing number. As the model has always held them: an entry only where the golfer said
    // something ("straight" kept, since it overrides a run; an empty intent or list is no entry).
    QHash<int, QString>                                  shotIntent;
    QHash<int, std::vector<analysis::ShotCorrection>>    corrections;
    // Where each half came from, for the logs and the tests.
    bool sessionFromNewHome = false;   // session_input.json held at least one field
    int  shotsFromReview = 0;          // shots whose review held a shot-level key
    int  shotsUnnamed = 0;             // swing folders that could not be named as a shot (their older copy
                                       // stands; logged)
};

class SessionDiagnosticsFiles
{
public:
    // The envelope's own schema tag (`schemaVersion` at its top level), separate from the ledger's
    // kDiagSchemaVersion: two contracts on two schedules. 1: every envelope written so far (the golfer keys
    // inside it before step 2.5 are the "older shape", told by the keys, not the number).
    static constexpr int kEnvelopeVersion = 1;
    static constexpr const char *kSessionInputSchema = "pinpoint.session-input/1";

    // The older shape's golfer keys, in the order they are reported.
    static const QStringList &legacyGolferKeys();
    // The older keys present in a parsed envelope object, verbatim.
    static QJsonObject legacyGolferInputOf(const QJsonObject &envelopeRoot);

    // ── FILE-SESS-03 ─────────────────────────────────────────────────────────────────────────────────
    static DiagnosticsRead readDiagnostics(const QString &sessionDir);
    // The cache half written; the older golfer keys CARRIED: from the file as it stands now, else (no file)
    // `legacyFallback`. Refused (nothing written): a newer file (ReadOnly); one that cannot be opened or does
    // not parse (Failed — the caller decides: set it aside, or block); a held lock (Locked). `*state`, when
    // given, says what was found on disk.
    static WriteResult writeDiagnostics(const QString &sessionDir, const QJsonObject &session,
                                        const QJsonObject &ledger, const QJsonObject &legacyFallback,
                                        DiagnosticsRead::State *state = nullptr);
    // FILE-SESS-12: a copy to diagnostics.json.<tag>; refused when that exists. `*backupPath` names it.
    static WriteResult backupDiagnostics(const QString &sessionDir, const QString &tag, QString *backupPath = nullptr);
    // An unreadable diagnostics.json moved aside to diagnostics.json.unreadable-<stamp>[-n]: kept, never
    // written over. `*asidePath` names where it went.
    static WriteResult setAsideDiagnostics(const QString &sessionDir, QString *asidePath = nullptr);

    // ── FILE-SESS-02 ─────────────────────────────────────────────────────────────────────────────────
    static SessionInputRead readSessionInput(const QString &sessionDir);
    // Every field of `input` that is set is written (the model always sets all four); keys this build does
    // not know are kept. Refused: a newer or unparseable file (ReadOnly), a held lock (Locked).
    static WriteResult writeSessionInput(const QString &sessionDir, const SessionInput &input);

    // ── The golfer's input, resolved (J-33) ──────────────────────────────────────────────────────────
    // `legacy` is the envelope's older keys (DiagnosticsEnvelope::legacyGolferInput; {} when none). The shot
    // level is read through `recordings` (readReview), each swing folder translated once (shotOfFolder), so
    // `recordings` must belong to the calling thread. `input`, when given, is session_input.json already read.
    static GolferInput golferInput(const QString &sessionDir, const QJsonObject &legacy,
                                   RecordingRepository &recordings, const SessionInputRead *input = nullptr);
    // The same with a reader of its own (a SessionStore and a swing door made for this call): for a worker
    // thread, which may not touch the app's (2.3's J6, 2.4's J4).
    static GolferInput golferInput(const QString &sessionDir, const QJsonObject &legacy);

    // ── Where a session sits in its athlete's history (step 2.7a; audit F-06/C-11) ─────────────────────
    // The day and the place in the day are SessionIdentity's — by start time, and for a session without a
    // session file (every session before migration M2) from its folder's name, in SessionIdentity and nowhere
    // else — never parsed out of a folder name by a caller. A legacy session's D3 id is the folder OWNER's (J-39:
    // the resolver, athlete.json, else the athlete the first swing records). `sessions` and `recordings` belong
    // to the calling thread; the forms without them make a pair of their own (a worker).
    //
    // The athlete folder's sessions as SessionStore::enumerateFolder lists them, each with its n-of-day.
    static QList<SessionIndexEntry> athleteSessions(const QString &athleteDir, SessionStore &sessions,
                                                    SwingStoreRecordingRepository &recordings);
    static QList<SessionIndexEntry> athleteSessions(const QString &athleteDir);
    // One session's identity (its day; n-of-day is not set: that needs the whole folder, athleteSessions).
    // A legacy session no athlete can be named for is placed by its name with a null id; empty for no folder.
    static SessionIdentity identityOf(const QString &sessionDir, SessionStore &sessions,
                                      SwingStoreRecordingRepository &recordings);
    // ── Where a session sits, for a caller that needs only its DAY and its PLACE (step 2.7b) ─────────────
    // Charts, the report, the Sessions row: the day, the start time and n-of-day, not the id. A SessionStore of
    // the call's own, so any thread, and no swing door: no owner is looked up, so a session older than step 2
    // is placed by its folder's name with a NULL id (SessionIdentity::placeOfLegacyFolder; J2 of step 2.7a) —
    // its day and order are the same as with its id. A session with a session file reads as it is.
    //
    // One session (n-of-day not set: that needs the whole folder, sessionPlaces).
    static SessionIdentity identityOf(const QString &sessionDir);
    // The athlete folder's sessions as SessionStore::enumerateFolder lists them with no owner, each with its
    // n-of-day.
    static QList<SessionIndexEntry> sessionPlaces(const QString &athleteDir);
    // `sessionDir`'s place in `index`, matched by its cleaned absolute folder; just the folder's name (no day,
    // ordered nowhere) when it is not listed.
    static analysis::SessionPlace placeOf(const QList<SessionIndexEntry> &index, const QString &sessionDir);

    // ── FILE-SESS-04, 05 ─────────────────────────────────────────────────────────────────────────────
    static bool         clubBaselineExists(const QString &sessionDir);
    static JsonFileRead readClubBaseline(const QString &sessionDir);
    static WriteResult  writeClubBaseline(const QString &sessionDir, const QJsonObject &root);
    static JsonFileRead readWorkOns(const QString &sessionDir);
    static WriteResult  writeWorkOns(const QString &sessionDir, const QJsonObject &root);
};

// ── The definitions (header-only, like SessionFiles and atomic_write.h: a reader that calls only
//    readDiagnostics links nothing new) ──────────────────────────────────────────────────────────────

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

// The reviews' readers (golferInput): I/O-bound, so more than the cores; their own pool, so a caller waiting on
// them never waits behind the global pool's work, and a worker calling golferInput (the tendencies) cannot
// starve them. Never a task that itself calls golferInput.
inline constexpr int kReviewReadThreads = 8;
inline QThreadPool &reviewReadPool()
{
    static QThreadPool pool;
    static const bool sized = (pool.setMaxThreadCount(kReviewReadThreads), pool.setExpiryTimeout(30000), true);
    (void)sized;
    return pool;
}

// ── The older shape's keys, read as the model always read them ────────────────────────────────────────

inline DeclaredFocus legacyFocus(const QJsonObject &legacy)
{
    const QJsonObject intent = legacy.value(QStringLiteral("intent")).toObject();
    return { intent.value(QStringLiteral("focusConditionId")).toString(),
             intent.value(QStringLiteral("focusFromShot")).toInt(-1) };
}

inline QString legacyMiss(const QJsonObject &legacy)
{
    return legacy.value(QStringLiteral("intent")).toObject().value(QStringLiteral("declaredMiss")).toString();
}

inline QHash<QString, bool> legacyScreens(const QJsonObject &legacy)
{
    QHash<QString, bool> out;
    const QJsonObject screens = legacy.value(QStringLiteral("screens")).toObject();
    for (auto it = screens.constBegin(); it != screens.constEnd(); ++it) out.insert(it.key(), it.value().toBool());
    return out;
}

inline IntentRun legacyRun(const QJsonObject &legacy)
{
    const QJsonObject run = legacy.value(QStringLiteral("intentFrom")).toObject();
    return { run.value(QStringLiteral("fromShot")).toInt(-1), run.value(QStringLiteral("shape")).toString() };
}

inline std::vector<analysis::ShotCorrection> toShotCorrections(const QVector<LabelCorrection> &list)
{
    std::vector<analysis::ShotCorrection> out;
    out.reserve(size_t(list.size()));
    for (const LabelCorrection &c : list) out.push_back(analysis::ShotCorrection{ c.family, c.from, c.to });
    return out;
}

} // namespace session_diagnostics_detail


// ── The older shape ──────────────────────────────────────────────────────────────────────────────────

inline const QStringList &SessionDiagnosticsFiles::legacyGolferKeys()
{
    static const QStringList keys{ QStringLiteral("intent"),     QStringLiteral("screens"),
                                   QStringLiteral("shotIntent"), QStringLiteral("intentFrom"),
                                   QStringLiteral("corrections") };
    return keys;
}

inline QJsonObject SessionDiagnosticsFiles::legacyGolferInputOf(const QJsonObject &envelopeRoot)
{
    QJsonObject out;
    for (const QString &k : legacyGolferKeys())
        if (envelopeRoot.contains(k)) out.insert(k, envelopeRoot.value(k));
    return out;
}

// ── FILE-SESS-03 ─────────────────────────────────────────────────────────────────────────────────────

inline DiagnosticsRead SessionDiagnosticsFiles::readDiagnostics(const QString &sessionDir)
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

inline WriteResult SessionDiagnosticsFiles::writeDiagnostics(const QString &sessionDir, const QJsonObject &session,
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
        return session_diagnostics_detail::failed(QStringLiteral("%1 could not be read (%2), so it was not session_diagnostics_detail::written over.")
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

inline WriteResult SessionDiagnosticsFiles::backupDiagnostics(const QString &sessionDir, const QString &tag, QString *backupPath)
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

inline WriteResult SessionDiagnosticsFiles::setAsideDiagnostics(const QString &sessionDir, QString *asidePath)
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

inline SessionInputRead SessionDiagnosticsFiles::readSessionInput(const QString &sessionDir)
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

inline WriteResult SessionDiagnosticsFiles::writeSessionInput(const QString &sessionDir, const SessionInput &input)
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

// ── The golfer's input, resolved (J-33) ─────────────────────────────────────────────────────────────────

inline GolferInput SessionDiagnosticsFiles::golferInput(const QString &sessionDir, const QJsonObject &legacy,
                                                 RecordingRepository &recordings, const SessionInputRead *already)
{
    GolferInput g;
    const SessionInputRead read = already ? *already : readSessionInput(sessionDir);
    const SessionInput &in = read.input;
    g.sessionFromNewHome = !in.isEmpty();

    // Session level: the new home's field, else the older copy's (as the model read it before step 2.5).
    g.focus        = in.declaredFocus ? *in.declaredFocus : session_diagnostics_detail::legacyFocus(legacy);
    g.declaredMiss = in.declaredMiss ? *in.declaredMiss : session_diagnostics_detail::legacyMiss(legacy);
    if (in.screenAnswers) {
        for (auto it = in.screenAnswers->constBegin(); it != in.screenAnswers->constEnd(); ++it)
            g.screens.insert(it.key(), it.value());
    } else {
        g.screens = session_diagnostics_detail::legacyScreens(legacy);
    }
    if (in.intentRuns) {
        if (!in.intentRuns->isEmpty()) g.intentRun = in.intentRuns->last();   // one run today: the latest
    } else {
        g.intentRun = session_diagnostics_detail::legacyRun(legacy);
    }

    // Shot level: the older copy first, then every shot's review over it, field by field.
    g.shotIntent  = analysis::shotIntentsFromJson(legacy.value(QStringLiteral("shotIntent")).toObject());
    g.corrections = analysis::shotCorrectionsFromJson(legacy.value(QStringLiteral("corrections")).toObject());
    if (sessionDir.isEmpty()) return g;
    // Each folder named on this thread (shotOfFolder is the owner thread's; after the first swing of a
    // session it is a lookup in the identity map) ...
    struct Swing { int number = 0; ShotRef shot; ShotReview review; };
    std::vector<Swing> swings;
    for (const QString &name : SwingPaths::swingFolders(sessionDir)) {
        const QString swingDir = sessionDir + QLatin1Char('/') + name;
        const ShotRef shot = recordings.shotOfFolder(swingDir);
        if (shot.isNull()) {
            ++g.shotsUnnamed;
            ppWarn() << "[GolferInput] a swing folder could not be named as a shot; its intent and corrections"
                     << "are read from diagnostics.json only:" << qPrintable(swingDir);
            continue;
        }
        swings.push_back(Swing{ shot.swingNumber(), shot, {} });
    }
    // ... and the reviews read ALL AT ONCE, on reviewReadPool() (readReview is safe from any thread). Each read
    // is the document's header, table of contents and root chunk — never its pose tracks — and on the share
    // it is a handful of round trips, so fifty in a row were ~200 ms on the calling thread (the GUI thread at
    // activation and at a baseline freeze: step 2.5's J-44 timing). Side by side they cost a few round trips.
    // The caller still waits for them: what it reads next must include them.
    const int n = int(swings.size());
    if (n <= 2) {
        for (Swing &w : swings) w.review = recordings.readReview(w.shot);
    } else {
        std::atomic<int> next{ 0 };
        QSemaphore       finished;
        const int        workers = std::min(n, session_diagnostics_detail::kReviewReadThreads);
        RecordingRepository *repo = &recordings;
        for (int t = 0; t < workers; ++t)
            session_diagnostics_detail::reviewReadPool().start([&swings, &next, &finished, repo, n] {
                for (int i = next.fetch_add(1); i < n; i = next.fetch_add(1))
                    swings[size_t(i)].review = repo->readReview(swings[size_t(i)].shot);
                finished.release();
            });
        finished.acquire(workers);
    }
    for (const Swing &w : swings) {
        const ShotReview &review = w.review;
        if (!review.ok) continue;                         // no document: nothing to say about it
        const int num = w.number;
        bool fromReview = false;
        if (review.hasShotIntent) {
            fromReview = true;
            if (review.shotIntent.isEmpty()) g.shotIntent.remove(num);          // cleared, explicitly
            else                             g.shotIntent.insert(num, review.shotIntent);
        }
        if (review.hasLabelCorrections) {
            fromReview = true;
            if (review.labelCorrections.isEmpty()) g.corrections.remove(num);   // cleared, explicitly
            else                                   g.corrections.insert(num, session_diagnostics_detail::toShotCorrections(review.labelCorrections));
        }
        if (fromReview) ++g.shotsFromReview;
    }
    return g;
}

inline GolferInput SessionDiagnosticsFiles::golferInput(const QString &sessionDir, const QJsonObject &legacy)
{
    SessionStore                  sessions;
    SwingStoreRecordingRepository recordings(sessions);
    return golferInput(sessionDir, legacy, recordings);
}

// ── Where a session sits (step 2.7a) ───────────────────────────────────────────────────────────────

namespace session_diagnostics_detail {
// The first swing folder under `sessionDir`, or under any session of `athleteDir` when `sessionDir` is empty or
// has none: what the owner chain's last step reads (the athlete the swing records). "" when there is none.
inline QString firstSwingFolder(const QString &athleteDir, const QString &sessionDir)
{
    if (!sessionDir.isEmpty())
        if (const QStringList own = SwingPaths::swingFolders(sessionDir); !own.isEmpty())
            return QDir(sessionDir).filePath(own.first());
    const QDir athlete(athleteDir);
    for (const QString &session : athlete.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString dir = athlete.filePath(session);
        if (const QStringList swings = SwingPaths::swingFolders(dir); !swings.isEmpty())
            return QDir(dir).filePath(swings.first());
    }
    return {};
}

inline QString cleanAbsolute(const QString &folder)
{
    return QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
}
} // namespace session_diagnostics_detail

inline QList<SessionIndexEntry> SessionDiagnosticsFiles::athleteSessions(const QString &athleteDir, SessionStore &sessions,
                                                                         SwingStoreRecordingRepository &recordings)
{
    if (athleteDir.isEmpty()) return {};
    const AthleteId owner =
        recordings.legacyOwnerOf(athleteDir, session_diagnostics_detail::firstSwingFolder(athleteDir, QString()));
    return sessions.enumerateFolder(athleteDir, owner);
}

inline QList<SessionIndexEntry> SessionDiagnosticsFiles::athleteSessions(const QString &athleteDir)
{
    SessionStore                  sessions;
    SwingStoreRecordingRepository recordings(sessions);
    return athleteSessions(athleteDir, sessions, recordings);
}

inline SessionIdentity SessionDiagnosticsFiles::identityOf(const QString &sessionDir, SessionStore &sessions,
                                                          SwingStoreRecordingRepository &recordings)
{
    if (sessionDir.isEmpty()) return {};
    const QString athleteDir = QFileInfo(QDir::cleanPath(sessionDir)).absolutePath();
    const AthleteId owner =
        recordings.legacyOwnerOf(athleteDir, session_diagnostics_detail::firstSwingFolder(athleteDir, sessionDir));
    const SessionId id = sessions.openFolder(sessionDir, owner);
    if (id.isNull())
        // No athlete to name a legacy session by: still placed by its name, with no id (as enumerateFolder lists it).
        return owner.isNull() ? SessionIdentity::placeOfLegacyFolder(sessionDir) : SessionIdentity();
    const std::optional<SessionIndexEntry> e = sessions.open(id);
    return e ? e->identity : SessionIdentity();
}

inline SessionIdentity SessionDiagnosticsFiles::identityOf(const QString &sessionDir)
{
    if (sessionDir.isEmpty()) return {};
    SessionStore    sessions;
    const SessionId id = sessions.openFolder(sessionDir, AthleteId());   // a session file names its own id
    if (!id.isNull())
        if (const std::optional<SessionIndexEntry> e = sessions.open(id)) return e->identity;
    return SessionIdentity::placeOfLegacyFolder(sessionDir);
}

inline QList<SessionIndexEntry> SessionDiagnosticsFiles::sessionPlaces(const QString &athleteDir)
{
    if (athleteDir.isEmpty()) return {};
    SessionStore sessions;
    return sessions.enumerateFolder(athleteDir, AthleteId());
}

inline analysis::SessionPlace SessionDiagnosticsFiles::placeOf(const QList<SessionIndexEntry> &index, const QString &sessionDir)
{
    const QString want = session_diagnostics_detail::cleanAbsolute(sessionDir);
    for (const SessionIndexEntry &e : index)
        if (session_diagnostics_detail::cleanAbsolute(e.folder) == want)
            return analysis::SessionPlace{ e.identity.folderName(), e.identity.localDay(), e.identity.nOfDay() };
    return analysis::SessionPlace{ QFileInfo(want).fileName(), QDate(), 0 };
}

// ── FILE-SESS-04, 05 ─────────────────────────────────────────────────────────────────────────────────

inline bool SessionDiagnosticsFiles::clubBaselineExists(const QString &sessionDir)
{
    return !sessionDir.isEmpty() && QFileInfo::exists(SessionFiles::clubBaselineFile(sessionDir));
}

inline JsonFileRead SessionDiagnosticsFiles::readClubBaseline(const QString &sessionDir)
{
    return sessionDir.isEmpty() ? JsonFileRead() : session_diagnostics_detail::readJsonFile(SessionFiles::clubBaselineFile(sessionDir));
}

inline WriteResult SessionDiagnosticsFiles::writeClubBaseline(const QString &sessionDir, const QJsonObject &root)
{
    return session_diagnostics_detail::writeJson(sessionDir, SessionFiles::clubBaselineFile(sessionDir), root, QJsonDocument::Compact);
}

inline JsonFileRead SessionDiagnosticsFiles::readWorkOns(const QString &sessionDir)
{
    return sessionDir.isEmpty() ? JsonFileRead() : session_diagnostics_detail::readJsonFile(SessionFiles::workOnsFile(sessionDir));
}

inline WriteResult SessionDiagnosticsFiles::writeWorkOns(const QString &sessionDir, const QJsonObject &root)
{
    return session_diagnostics_detail::writeJson(sessionDir, SessionFiles::workOnsFile(sessionDir), root, QJsonDocument::Compact);
}

} // namespace pinpoint
