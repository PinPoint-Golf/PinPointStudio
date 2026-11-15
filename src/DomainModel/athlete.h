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
#include "../Core/club_standards.h"
#include "../Identity/identity.h"

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace pinpoint {

class AthleteRepository;

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

signals:
    void recordChanged();

private:
    AthleteRepository *m_repository = nullptr;
    SessionRepository *m_sessions   = nullptr;
    AthleteId          m_id;
    QString            m_folder;
    SessionOwner       m_owner;
    AthleteRecord      m_record;
    Source             m_source   = Source::Library;
    bool               m_readOnly = false;
};

} // namespace pinpoint
