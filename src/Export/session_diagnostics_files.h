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
// THE FILE WORK ITSELF (the reads and writes of FILE-SESS-02…05 and 12) lives in session_diagnostics_io.h since
// step 3.4a, unchanged, so the session store can serve the same files by SessionId (J-42) without this header's
// golfer-input resolver; the folder-keyed functions below forward to it.
//
// INTERIM (IM-S2-4: DEC-160; step 2.5's J1). Keyed by the session FOLDER, like SessionFiles (2.2's J14) and
// AthleteFiles (2.3's J7), because every caller today holds a folder — the diagnostics model (made by QML),
// the work-ons catch-up, Charts, the report and the tools. Step 3's `Session` reaches the same functions
// through SessionRepository by its id. Stateless, so safe from any thread (each worker passes its own
// RecordingRepository to golferInput()).

#include "session_diagnostics_io.h"   // the file work itself (step 3.4a)
#include "session_files.h"
#include "session_store.h"
#include "swing_paths.h"
#include "swing_store_repository.h"
#include "../Coaching/board_annotations.h"    // BoardAnnotation (step 3.6)
#include "../Coaching/club_distances.h"       // ShotCorrection
#include "../Coaching/tendencies.h"           // shotCorrectionsFromJson, shotIntentsFromJson
#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../Core/pp_debug.h"
#include "../DomainModel/golfer_input_rules.h"     // the J-33 resolution, one rule (step 3.5-i)
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

// DiagnosticsEnvelope, DiagnosticsRead, SessionInputRead and JsonFileRead are SessionRepository's values now
// (src/DomainModel/session_repository.h; refactor step 3.4a, J-42), so its id-keyed methods can return them.

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
    // The launch monitor board's annotations (review.excluded / review.clubOverride), by swing number, only where the
    // board said something (refactor step 3.6; Stage 0.5's follow-up). Their only home is the review: no older copy.
    QHash<int, analysis::BoardAnnotation>                board;
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
    static constexpr int kEnvelopeVersion = session_diagnostics_io::kEnvelopeVersion;
    static constexpr const char *kSessionInputSchema = session_diagnostics_io::kSessionInputSchema;

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

// ── The older shape's keys: the one rule, src/DomainModel/golfer_input_rules.h (moved there in step 3.5-i so
//    SessionDiagnostics resolves the golfer's input by the same rule) ───────────────────────────────────────

using golfer_input::legacyFocus;
using golfer_input::legacyMiss;
using golfer_input::legacyScreens;
using golfer_input::legacyRun;
using golfer_input::toShotCorrections;

} // namespace session_diagnostics_detail


// ── The older shape, FILE-SESS-03, FILE-SESS-02: the file work is session_diagnostics_io.h's (step 3.4a) ───

inline const QStringList &SessionDiagnosticsFiles::legacyGolferKeys()
{
    return session_diagnostics_io::legacyGolferKeys();
}

inline QJsonObject SessionDiagnosticsFiles::legacyGolferInputOf(const QJsonObject &envelopeRoot)
{
    return session_diagnostics_io::legacyGolferInputOf(envelopeRoot);
}

inline DiagnosticsRead SessionDiagnosticsFiles::readDiagnostics(const QString &sessionDir)
{
    return session_diagnostics_io::readDiagnostics(sessionDir);
}

inline WriteResult SessionDiagnosticsFiles::writeDiagnostics(const QString &sessionDir, const QJsonObject &session,
                                                      const QJsonObject &ledger, const QJsonObject &legacyFallback,
                                                      DiagnosticsRead::State *state)
{
    return session_diagnostics_io::writeDiagnostics(sessionDir, session, ledger, legacyFallback, state);
}

inline WriteResult SessionDiagnosticsFiles::backupDiagnostics(const QString &sessionDir, const QString &tag, QString *backupPath)
{
    return session_diagnostics_io::backupDiagnostics(sessionDir, tag, backupPath);
}

inline WriteResult SessionDiagnosticsFiles::setAsideDiagnostics(const QString &sessionDir, QString *asidePath)
{
    return session_diagnostics_io::setAsideDiagnostics(sessionDir, asidePath);
}

inline SessionInputRead SessionDiagnosticsFiles::readSessionInput(const QString &sessionDir)
{
    return session_diagnostics_io::readSessionInput(sessionDir);
}

inline WriteResult SessionDiagnosticsFiles::writeSessionInput(const QString &sessionDir, const SessionInput &input)
{
    return session_diagnostics_io::writeSessionInput(sessionDir, input);
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
    g.intentRun = golfer_input::resolveIntentRun(in, legacy);   // one run today: the latest

    // Shot level: the older copy first, then every shot's review over it, field by field.
    g.shotIntent  = golfer_input::legacyShotIntents(legacy);
    g.corrections = golfer_input::legacyShotCorrections(legacy);
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
        // No document: nothing to say about it. Else the review over the older copy, field by field (the one rule).
        if (golfer_input::applyReview(w.review, w.number, g.shotIntent, g.corrections)) ++g.shotsFromReview;
        if (w.review.ok) {
            const analysis::BoardAnnotation a{ w.review.lmExcluded, w.review.lmClubOverride };
            if (a.any()) g.board.insert(w.number, a);
        }
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
    return session_diagnostics_io::clubBaselineExists(sessionDir);
}

inline JsonFileRead SessionDiagnosticsFiles::readClubBaseline(const QString &sessionDir)
{
    return session_diagnostics_io::readClubBaseline(sessionDir);
}

inline WriteResult SessionDiagnosticsFiles::writeClubBaseline(const QString &sessionDir, const QJsonObject &root)
{
    return session_diagnostics_io::writeClubBaseline(sessionDir, root);
}

inline JsonFileRead SessionDiagnosticsFiles::readWorkOns(const QString &sessionDir)
{
    return session_diagnostics_io::readWorkOns(sessionDir);
}

inline WriteResult SessionDiagnosticsFiles::writeWorkOns(const QString &sessionDir, const QJsonObject &root)
{
    return session_diagnostics_io::writeWorkOns(sessionDir, root);
}

} // namespace pinpoint
