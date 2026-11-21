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

// Athlete — the athlete aggregate's STEP-2 FORM (refactor step 2.3; build/refactor/step-2/BRIEF.md §4.5;
// docs/developer/information_model.md OBJ-ATHLETE, OBJ-ATHLETE-RECORD, OBJ-BAG, OBJ-SESSION-INDEX,
// CLS-ATHLETE, LC-ATHLETE; IM-S2-5: DEC-160).
//
//   id()          its AthleteId (ID-ATHLETE), minted once at creation, never derived from a name
//   folder()      the athlete's folder: where its athlete.json is found, with no `folder` key (D4:
//                 DEC-151). Chosen ONCE, at creation; a rename changes the name only (E-02)
//   record()      the record and the bag, exactly SET-01's keys as stored today, plus the Wrist
//                 chart's reference ★ as a ShotRef (D5: DEC-152)
//   index()       the SessionIndex: one typed entry per session, status included (MD-07: DEC-136),
//                 read through SessionRepository (DEC-53). lastSessionAt and sessionCount are derived
//                 from it (activityOf), never stored (DEC-89)
//   clubSetOn()   the standard club set on a day (club_standards.h's rule)
//   editRecord, editBag, removeClub    the commands; each goes through AthleteRepository, the one
//                 writer (IM-S2-4), which announces the stored result with recordChanged()
//   remove()      refused while one of its sessions is InProgress (DEC-99); else the whole folder goes
//                 to the recoverable trash (DEC-79), purged later by housekeeping (DEC-80, DEC-137)
//
// ITS SESSIONS' LIFECYCLE (refactor step 3.4a; build/refactor/step-3/BRIEF.md §4.8; LC-SESSION, DEC-71,
// DEC-73, DEC-77, DEC-78, DEC-134, DEC-143, DEC-164):
//   startSession(type)   ends the athlete's session running here first (EVT-09; never the empty discard,
//                 which only End itself makes, as today), then SessionRepository::start, which mints the
//                 SessionId, writes the session file and ends any session a crash left in progress, its end
//                 inferred (J-18) as today; EVT-17. A session started ON DEMAND (SessionStart::OnDemand: capture's
//                 session for a swing outside a session) is never the empty discard's (today's rule)
//   todaysSession(type), resumeSession(id, type)   today's session of that type, resumed with its own id
//                 (DEC-77; only one started today, of the same type: DEC-134); the session running here is
//                 ended first; EVT-23. Resuming the session already running here — when it is today's of that type
//                 (DEC-134) — changes nothing (Unchanged, no write, no event) but that it is no longer the empty
//                 discard's: it is resumed work; another type or day is refused, as any resume
//   trashSession(id)   the whole session to the recoverable trash, refused while it is in progress (J-46);
//                 EVT-20; its Session is let go and deleted later (deleteLater)
//   sessionAt(folder)  THE EDGE TRANSLATION for a view that still holds a folder (D1: DEC-164; DEC-90): that
//                 folder's Session, when it is one of this athlete's; null otherwise. app.sessionAt asks it
//   session(id), sessions()   the Session for an id of the index, and every one (loaded when first asked)
// ONE INSTANCE PER SessionId: the Athlete holds each Session it has loaded as its child, so the panel, the
// report and a sweep asking for one get the same object (LIVE-2; Mark, 15 Nov 2026: DEC-190); the session
// repository holds the records. A trash lets one go; so does releaseSession, for one nobody holds (step 3.5-i: Q-18).
// The events are Qt signals carrying the id, emitted after the repository write returned ok (DEC-69).
// ITS CALLERS (run 3.4a-ii): capture (ShotProcessor) starts, resumes and ends through these; AthleteController
// reaches the athletes through the composition root's callables (D18 (c)); the drawer's session delete still
// goes to the repository directly until step 3.4b (SessionReviewController).
//
// recordChanged() is THE EDIT SIGNAL — the interim for EVT-14 until step 3's events. A switch of the
// current athlete is not an Athlete's business: the holder replaces the Athlete (R-APP-1).
//
// ONE INSTANCE PER AthleteId (DEC-52): AthleteRepository::load hands out the instance it holds. The
// repository owns every Athlete it makes and outlives its holders.
//
// UNTIL MIGRATION M1 (step 2.8) an athlete created before step 2 has no athlete.json: its record is read
// from this machine's settings (source() == Settings), its folder is today's derivation from its name
// (so a rename of such an athlete still moves its folder: E-02's last case, which M1 closes), and its
// edits are written back there, never into an athlete.json (M1 writes that, with the reconciliation
// across machines H-09 needs). Once athlete.json exists it wins.
//
// Domain model ring (src/DomainModel): names no store type.

#include "session_repository.h"   // SessionIndexEntry, SessionOwner, WriteResult
#include "shot_command_values.h"   // TuningFlagNote
#include "../Core/club_standards.h"
#include "../Identity/identity.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace pinpoint {

class AthleteRepository;
class FiringReader;
class RecordingReader;
class RuleData;
class Session;
class SessionDiagnostics;
class Shot;
class WorkPool;
class WorkReceiver;

// The doors a Session's shots read and (from step 3.4b) write through (refactor step 3.4a): the composition
// root's one RecordingRepository and RecordingReader, handed down Athlete → Session → Shot by the athlete
// repository that makes the Athlete. Not owned here (the reader is shared: every RecordingSource keeps it
// alive). Null doors: the shots know no kind and read nothing (a tool or test that wires none).
// How a session is started (Athlete::startSession): by the golfer (the wizard, the toolbar), or on demand by
// capture for a swing outside a session. Only the first is ever the empty discard's (DEC-93, as today).
enum class SessionStart { Deliberate, OnDemand };

struct ShotDoors {
    RecordingRepository             *recordings = nullptr;
    std::shared_ptr<RecordingReader> reader;
};

// The doors a Session's SessionDiagnostics works through (refactor step 3.5-i; build/refactor/step-3/BRIEF.md §4.4),
// handed down Athlete → Session → SessionDiagnostics by the composition root, as ShotDoors are: the one pool and its
// receiver (D15), the machine's RuleData (D5), the firing reader (firing_reader.h). None is owned here; each outlives
// every Athlete. `attach` is SessionDiagnostics::attach, set by the composition root that links it: the Athlete calls
// it for every Session it makes, so every loaded session's diagnostics run (DEC-02) — and with no `attach` (the app in
// run 3.5-i, every tool and test that sets none) no Session has one. `persist`: it writes diagnostics.json — false in
// the app until the cut-over (step 3.7: the old model is the file's one writer until then). `synchronous`: headless
// (R-HL-2: the jobs run on the calling thread). `writeDebounceMs`: how long after a batch settles the write waits.
// `rebuild` (refactor step 3.5-ii; regrade_ledger --engine=new): a regrade — the file's rows, derived data and
// diagnosis state are not kept, so every shot fires and the stage ratchet replays from Cold; the older golfer keys
// are still read and carried (J-33), as the old model's setRebuildCache does. With `persist` the session summary
// (work_ons.json, FILE-SESS-05; DEC-55, DEC-192) is written too, with what it is of: SessionDiagnostics is that file's
// one writer (refactor step 3.6; in the app from the catch-up's cut-over, 3.7). With `persist` the club baseline is
// also frozen (Session::freezeClubBaseline) when the session has none or an older rule's (DEC-46; 3.6).
struct DiagnosticsDoors {
    WorkPool                           *pool     = nullptr;
    WorkReceiver                       *receiver = nullptr;
    RuleData                           *ruleData = nullptr;
    std::shared_ptr<const FiringReader> reader;
    bool                                persist         = false;
    bool                                synchronous     = false;
    int                                 writeDebounceMs = 1500;
    bool                                rebuild         = false;
    // With `persist`, the session summary (work_ons.json) is written too (refactor 3.7-a): false in the app until the
    // catch-up's cut-over (3.7-e), where WorkOnsController stays that file's one writer; true in every tool and test.
    bool                                writeSummary    = true;
    std::function<SessionDiagnostics *(Session *, const DiagnosticsDoors &)> attach;
};

// OBJ-ATHLETE-RECORD and OBJ-BAG: SET-01's keys exactly as stored today, so M1's move is lossless.
// lastSessionAt and sessionCount are NOT here (DEC-89): they come from the index.
struct AthleteRecord {
    QString     name;
    QString     handedness  = QStringLiteral("Right");
    double      heightValue = 0.0;                       // FEET, whatever unit the form showed
    QString     heightUnit  = QStringLiteral("ft");      // the unit the form shows it in
    double      weightValue = 0.0;                       // pounds
    QString     weightUnit  = QStringLiteral("lb");
    double      handicap    = -999.0;                    // -999: not given
    QString     primaryClub = QStringLiteral("DRIVER");  // as stored (an old record may hold a legacy token)
    double      speedTarget = 0.0;
    QString     notes;
    QString     dob;                                     // "YYYY-MM-DD", or "" (optional)
    QString     sex;                                     // "male" | "female" | "declined" | ""
    QString     reportEmail;
    QString     reportGreeting;
    bool        reportPhotosApproved = false;
    QString     reportPhotosApprovedOn;
    qint64      createdAt = 0;                           // seconds since the epoch
    QVariantMap bag;                                     // `clubs`: club id → club record (today's map)
    bool        hasBag = false;   // false: no bag stored yet; whoever shows it seeds the standard set
                                  // (today's rule), and nothing is written by reading it
    ShotRef     wristReference;   // the Wrist chart's reference ★ (D5: DEC-152); null: none chosen
    QVariantMap extra;            // keys this build does not know: kept, and written back unchanged

    double heightM() const { return heightValue * 0.3048; }

    // By SET-01's key. Setting a key this build does not know keeps it in `extra`; setting
    // lastSessionAt or sessionCount is ignored (derived, DEC-89).
    QVariant value(const QString &key) const;
    void     setValue(const QString &key, const QVariant &v);

    // Every key, as SET-01 spells it ("clubs" only when hasBag; "wristReference" only when set), and
    // the inverse. The two derived keys are dropped on the way in.
    QVariantMap          toMap() const;
    static AthleteRecord fromMap(const QVariantMap &m);
    // SET-01's keys this build knows (the record's, without the two derived ones).
    static const QStringList &knownKeys();

    bool operator==(const AthleteRecord &o) const { return toMap() == o.toMap(); }
    bool operator!=(const AthleteRecord &o) const { return !(*this == o); }
};

// The two figures the picker shows and orders by, DERIVED from the index (DEC-89): the sessions that
// hold at least one swing (the Sessions list's rule: a folder nothing was hit in is not a session),
// and the latest one's start — its local day's midnight for a session read from its name (DEC-150).
// What a delete did (refactor step 2.3, round 2: J-30). The whole folder moves to the trash (DEC-79)
// unless it is not this athlete's alone: when ANOTHER athlete resolves to the same folder (two settings
// athletes whose names sanitise alike, or one renamed onto another's folder), only this athlete's record
// goes — exactly the delete before step 2 — and the folder stays (`folderMoved` false, `note` says why).
// When the folder's athlete.json names a DIFFERENT athlete, the delete is refused (`result` Failed).
struct AthleteRemoval {
    WriteResult result;
    bool        folderMoved = false;
    QString     note;          // for the user, when the folder stayed or the delete was refused; else ""
    bool ok() const { return result.ok(); }
};

struct AthleteActivity {
    int    sessions  = 0;
    qint64 lastAtSec = 0;
};

class Athlete : public QObject
{
    Q_OBJECT
public:
    enum class Source {
        Library,    // <folder>/athlete.json (FILE-ATH-01)
        Settings    // today's per-machine settings record (SET-01), until M1 moves it
    };

    // Made by an AthleteRepository only.
    Athlete(AthleteRepository *repository, SessionRepository *sessions, const AthleteId &id,
            QObject *parent = nullptr);

    AthleteId            id() const { return m_id; }
    QString              folder() const { return m_folder; }
    const AthleteRecord &record() const { return m_record; }
    Source               source() const { return m_source; }
    // A newer build's athlete.json: every edit refuses it (WriteResult::Status::ReadOnly).
    bool                 readOnly() const { return m_readOnly; }
    // Who the athlete's sessions belong to and where they are found, as SessionRepository takes it.
    // Its name is the token the session store composes the athlete folder from: the folder's own name
    // for an athlete with athlete.json (so a rename leaves its sessions where they are), today's name
    // for a settings athlete.
    SessionOwner         sessionOwner() const { return m_owner; }

    // OBJ-SESSION-INDEX (DEC-136): every session of the athlete, through SessionRepository. Reads only.
    QList<SessionIndexEntry> index() const;
    bool                     hasSessionInProgress() const;
    // From an index: sessions with a swing, and the latest one's start. Pure but for `sessions`'
    // shots() listing, so any thread may call it with a SessionRepository of its own.
    static AthleteActivity activityOf(const QList<SessionIndexEntry> &index, const SessionRepository &sessions);

    ClubSet clubSetOn(const QDate &on) const;

    // The commands (through the repository; recordChanged() follows a stored change).
    WriteResult editRecord(const AthleteRecord &record);
    WriteResult editBag(const QVariantMap &bag);
    WriteResult removeClub(const QString &clubId);
    AthleteRemoval remove();

    // THE REPOSITORY'S SIDE: what is stored now. Emits recordChanged() when the record differs.
    void setStored(const AthleteRecord &record, const QString &folder, const SessionOwner &owner,
                   Source source, bool readOnly);
    // THE REPOSITORY'S SIDE: the doors its sessions' shots use (the sessions loaded before keep theirs).
    void setShotDoors(const ShotDoors &doors) { m_shotDoors = doors; }
    // THE COMPOSITION ROOT'S SIDE: the doors its sessions' diagnostics use (refactor step 3.5-i). Sessions loaded
    // before keep what they have (none, or theirs).
    void setDiagnosticsDoors(const DiagnosticsDoors &doors) { m_diagnosticsDoors = doors; }
    const DiagnosticsDoors &diagnosticsDoors() const { return m_diagnosticsDoors; }

    // ── Its sessions (step 3.4a) ─────────────────────────────────────────────────────────────────────
    // `type` by name (DEC-88: "swing", "wrist", "grf", "coach", "" for none); `namingPattern` is SET-04,
    // which the caller reads. Null on failure, with `*result` saying why (DEC-126); else `*result` the write's.
    Session    *startSession(const QString &type, const QString &namingPattern, WriteResult *result = nullptr,
                             SessionStart how = SessionStart::Deliberate);
    SessionId   todaysSession(const QString &type) const;
    Session    *resumeSession(const SessionId &id, const QString &type, WriteResult *result = nullptr);
    WriteResult trashSession(const SessionId &id, QString *where = nullptr);
    Session    *sessionAt(const QString &folder);
    Session    *session(const SessionId &id);
    QList<Session *> sessions();
    // The Session for `id` if it is loaded now, never loading one (the composition root's identity-map lookup for
    // the work receiver: §4.4 step 6), and every one loaded now.
    Session    *loadedSession(const SessionId &id) const { return m_loaded.value(id); }
    QList<Session *> loadedSessions() const { return m_loaded.values(); }

    // RELEASING A SESSION NOBODY HOLDS (refactor step 3.5-i; DEC-190's follow-up, Q-18): a sweep or a catch-up
    // must not keep every session it touched. The Session's diagnostics write what they kept FIRST and cancel their
    // waiting jobs (Session::releasing; §4.4 steps 8–9); then it leaves the identity map and is deleted later
    // (deleteLater), so a view still bound to it reads a live object until the event loop runs. A later
    // session(id) loads a new instance. Refused (false, `*whyNot`) for a session not loaded, the one running here,
    // or one a caller holds (Session::addUse). releaseUnused releases every loaded session it may; returns how many.
    bool        releaseSession(const SessionId &id, QString *whyNot = nullptr);
    int         releaseUnused();

    // ── Its tuning log (refactor step 3.4b-i; OBJ-TUNING-LOG, FILE-ATH-04; DEC-43) ────────────────────────────
    // Each Session hands it every Shot it holds; the Athlete then hears that Shot's EVT-06 (LabelCorrected) and
    // appends the correction the Shot saved (Shot::lastLabelCorrection) to its tuning log, one line, through
    // its repository (the athlete folder's lock for that one append). Never the Session, never AthleteDiagnostics.
    void        hearLabelCorrections(Shot *shot);
    // The tuning log's "flag" line for `shot` (the panel's "flag for review"; review 1 of 3.4b-i): appended by the
    // Athlete too, so the log has one writer. No event (the reference names none). A failed append is logged and
    // returned (the golfer is not told, as today: §11, DEC-126's row).
    WriteResult flagForTuning(const ShotRef &shot, const TuningFlagNote &note);

signals:
    void recordChanged();
    void sessionStarted(const pinpoint::SessionId &session);   // EVT-17
    void sessionResumed(const pinpoint::SessionId &session);   // EVT-23
    void sessionTrashed(const pinpoint::SessionId &session);   // EVT-20
    // Not a domain event (refactor step 3.6): a Session is now held (loaded), its diagnostics attached — for its
    // AthleteDiagnostics, which hears the session's events so the athlete level is marked stale (DEC-60), and so this
    // file links none of it.
    void sessionHeld(pinpoint::Session *session);
    // Not a domain event (refactor step 3.7-a; DEC-126): one of this athlete's writes failed — today the tuning log's
    // append (FILE-ATH-04) — so the composition root can show it to the golfer (it was logged only). `what` names the
    // fact in the golfer's words; `why` is the repository's error.
    void writeFailed(const QString &what, const QString &why);

private:
    friend class AthleteDiagnostics;   // reads its sessions' summaries through the repository it was made with
    AthleteRepository *m_repository = nullptr;
    SessionRepository *m_sessions   = nullptr;
    AthleteId          m_id;
    QString            m_folder;
    SessionOwner       m_owner;
    AthleteRecord      m_record;
    Source             m_source   = Source::Library;
    bool               m_readOnly = false;
    ShotDoors          m_shotDoors;
    DiagnosticsDoors   m_diagnosticsDoors;
    QHash<SessionId, Session *> m_loaded;   // each Session loaded, a child of this Athlete (one per id)

    // The Session object for a session the repository holds, made once; `startedHere` marks the one this
    // run's startSession made. Null when the repository does not hold it or it is not this athlete's.
    Session *hold(const SessionId &id, bool startedHere, bool onDemand = false);
    // The session running here (started or resumed by this process and not ended), ended first by a start
    // or a resume (DEC-78, DEC-134); `except` is left alone.
    void endRunningSessions(const SessionId &except);
    // Brings `id` into the repository's identity map if it is not there yet (the athlete's index).
    bool ensureHeld(const SessionId &id);
    // EVT-06 heard: the tuning log's line for `shot`'s last correction (DEC-43).
    void appendLabelCorrection(const Shot &shot);
    WriteResult appendTuningLine(const QJsonObject &line, const ShotRef &shot);   // through the repository
};

} // namespace pinpoint
