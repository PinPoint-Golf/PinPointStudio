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

#include "../../App/app.h"                // AthleteCallables (the composition root's, D18 (c)); Athlete
#include "../../DomainModel/athlete.h"   // Athlete, AthleteActivity, SessionOwner (domain, inward)
#include "../session/session_list_presenter.h"   // the one session list: each athlete's activity (step 5.7b)

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <limits>

#include <QJsonObject>

namespace pinpoint { class SessionRepository; }

// One choice the athlete form's sex picker offers: the stored token and its words (sexOptions).
struct SexOption {
    Q_GADGET
    Q_PROPERTY(QString value MEMBER value)
    Q_PROPERTY(QString label MEMBER label)
public:
    QString value, label;
};

// ── THE ATHLETE SCREENS' TYPED VALUES (refactor step 5.7b, lint A6) ─────────────────────────────────────────────────────
// What the controller hands QML, as Q_GADGETs named as the old maps' keys, so the QML reads them as it read the maps.
// Display forms of the record's facts (OBJ-ATHLETE-RECORD, OBJ-BAG) and the athlete level's distances
// (CLS-ATHLETE-DIAG); the record itself and its one writer are unchanged.

// One athlete of the list (athletes): the record's fields, its initials, and its activity, derived from its SessionIndex
// (DEC-89: the sessions with a swing and the latest one's start; 0 until its group of the one session list is read).
struct AthleteRow {
    Q_GADGET
    Q_PROPERTY(QString uuid                   MEMBER uuid                   FINAL)
    Q_PROPERTY(QString name                   MEMBER name                   FINAL)
    Q_PROPERTY(QString handedness             MEMBER handedness             FINAL)
    Q_PROPERTY(double  heightValue            MEMBER heightValue            FINAL)
    Q_PROPERTY(QString heightUnit             MEMBER heightUnit             FINAL)
    Q_PROPERTY(double  weightValue            MEMBER weightValue            FINAL)
    Q_PROPERTY(QString weightUnit             MEMBER weightUnit             FINAL)
    Q_PROPERTY(double  handicap               MEMBER handicap               FINAL)
    Q_PROPERTY(QString primaryClub            MEMBER primaryClub            FINAL)
    Q_PROPERTY(double  speedTarget            MEMBER speedTarget            FINAL)
    Q_PROPERTY(QString notes                  MEMBER notes                  FINAL)
    Q_PROPERTY(QString dob                    MEMBER dob                    FINAL)
    Q_PROPERTY(QString sex                    MEMBER sex                    FINAL)
    Q_PROPERTY(QString reportEmail            MEMBER reportEmail            FINAL)
    Q_PROPERTY(QString reportGreeting         MEMBER reportGreeting         FINAL)
    Q_PROPERTY(bool    reportPhotosApproved   MEMBER reportPhotosApproved   FINAL)
    Q_PROPERTY(QString reportPhotosApprovedOn MEMBER reportPhotosApprovedOn FINAL)
    Q_PROPERTY(qint64  createdAt              MEMBER createdAt              FINAL)
    Q_PROPERTY(qint64  lastSessionAt          MEMBER lastSessionAt          FINAL)
    Q_PROPERTY(int     sessionCount           MEMBER sessionCount           FINAL)
    Q_PROPERTY(QString initials               MEMBER initials               FINAL)
public:
    QString uuid, name, handedness, heightUnit, weightUnit, primaryClub, notes, dob, sex, reportEmail, reportGreeting,
            reportPhotosApprovedOn, initials;
    double  heightValue = 0.0, weightValue = 0.0, handicap = -999.0, speedTarget = 0.0;
    bool    reportPhotosApproved = false;
    qint64  createdAt = 0, lastSessionAt = 0;
    int     sessionCount = 0;
    friend bool operator==(const AthleteRow &, const AthleteRow &) = default;
};

// One club of the bag (clubRecords, clubRecord, defaultClubRecord, setClubRecord): the record's keys as stored, keyed by
// `club`, the canonical club-vocabulary id (the bag's key). A key a record does not hold reads as the default below —
// the defaults the club form has always shown for it (0 = unknown; untaped; 25 mm bands; steel). `club` is "" for a
// club not in the bag.
struct ClubRecord {
    Q_GADGET
    Q_PROPERTY(QString    club            MEMBER club            FINAL)
    Q_PROPERTY(QString    shaftType       MEMBER shaftType       FINAL)
    Q_PROPERTY(double     loftDeg         MEMBER loftDeg         FINAL)
    Q_PROPERTY(int        lengthMm        MEMBER lengthMm        FINAL)
    Q_PROPERTY(int        bandWidthMm     MEMBER bandWidthMm     FINAL)
    Q_PROPERTY(QList<int> bandCentersMm   MEMBER bandCentersMm   FINAL)
    Q_PROPERTY(int        hoselFromButtMm MEMBER hoselFromButtMm FINAL)
    Q_PROPERTY(bool       headPatch       MEMBER headPatch       FINAL)
    Q_PROPERTY(int        shaftLengthMm   MEMBER shaftLengthMm   FINAL)
    Q_PROPERTY(int        handsEndMm      MEMBER handsEndMm      FINAL)
    Q_PROPERTY(QString    tapedOn         MEMBER tapedOn         FINAL)
    Q_PROPERTY(QString    notes           MEMBER notes           FINAL)
    Q_PROPERTY(double     lieDeg          MEMBER lieDeg          FINAL)
public:
    QString    club;
    QString    shaftType = QStringLiteral("steel");
    double     loftDeg = 0.0;
    int        lengthMm = 0;
    int        bandWidthMm = 25;
    QList<int> bandCentersMm;              // retro-band CENTRES from the butt, mm; empty = untaped
    int        hoselFromButtMm = 0;
    bool       headPatch = false;
    int        shaftLengthMm = 0;          // exposed shaft, grip end → hosel top; 0 unknown
    int        handsEndMm = 0;             // bottom of the hands from the butt; 0 unknown
    QString    tapedOn;                    // "YYYY-MM-DD"
    QString    notes;
    double     lieDeg = 0.0;
    // The bag's form (AthleteRecord::bag's value for one club), and back. toMap holds every key above but `club`, the
    // band centres as a QVariantList (the JSON the store writes).
    static ClubRecord fromMap(const QString &club, const QVariantMap &record);
    QVariantMap       toMap() const;
    friend bool operator==(const ClubRecord &, const ClubRecord &) = default;
};

// One session's point of a club's distances (perSession): its day, the carry's mean ± SD (NaN: not measured), the shots.
struct ClubDistancePoint {
    Q_GADGET
    Q_PROPERTY(QString date MEMBER date FINAL)
    Q_PROPERTY(double  mean MEMBER mean FINAL)
    Q_PROPERTY(double  sd   MEMBER sd   FINAL)
    Q_PROPERTY(int     n    MEMBER n    FINAL)
public:
    QString date;
    double  mean = std::numeric_limits<double>::quiet_NaN();
    double  sd   = std::numeric_limits<double>::quiet_NaN();
    int     n    = 0;
    friend bool operator==(const ClubDistancePoint &a, const ClubDistancePoint &b)
    {
        const auto same = [](double x, double y) { return x == y || (x != x && y != y); };
        return a.date == b.date && same(a.mean, b.mean) && same(a.sd, b.sd) && a.n == b.n;
    }
};

// One club's row of "Your distances": the keys tendencies.h clubDistanceItemToJson writes (made from its output, so the
// words keep their one rule). A number not measured is NaN, as the JSON leaves it out.
struct ClubDistanceRow {
    Q_GADGET
    Q_PROPERTY(QString club           MEMBER club           FINAL)
    Q_PROPERTY(QString label          MEMBER label          FINAL)
    Q_PROPERTY(double  carryMean      MEMBER carryMean      FINAL)
    Q_PROPERTY(double  carrySd        MEMBER carrySd        FINAL)
    Q_PROPERTY(QString carryText      MEMBER carryText      FINAL)
    Q_PROPERTY(double  totalMean      MEMBER totalMean      FINAL)
    Q_PROPERTY(double  totalSd        MEMBER totalSd        FINAL)
    Q_PROPERTY(double  offlineSd      MEMBER offlineSd      FINAL)
    Q_PROPERTY(double  speedMean      MEMBER speedMean      FINAL)
    Q_PROPERTY(double  speedSd        MEMBER speedSd        FINAL)
    Q_PROPERTY(double  ballSpeedMean  MEMBER ballSpeedMean  FINAL)
    Q_PROPERTY(double  ballSpeedSd    MEMBER ballSpeedSd    FINAL)
    Q_PROPERTY(double  smashMean      MEMBER smashMean      FINAL)
    Q_PROPERTY(double  smashSd        MEMBER smashSd        FINAL)
    Q_PROPERTY(int     shots          MEMBER shots          FINAL)
    Q_PROPERTY(int     sessions       MEMBER sessions       FINAL)
    Q_PROPERTY(QString mishitsText    MEMBER mishitsText    FINAL)
    Q_PROPERTY(bool    fewShots       MEMBER fewShots       FINAL)
    Q_PROPERTY(QString fewShotsText   MEMBER fewShotsText   FINAL)
    Q_PROPERTY(QString trend          MEMBER trend          FINAL)
    Q_PROPERTY(QString trendWords     MEMBER trendWords     FINAL)
    Q_PROPERTY(QString gapWith        MEMBER gapWith        FINAL)
    Q_PROPERTY(double  gapPrev        MEMBER gapPrev        FINAL)
    Q_PROPERTY(double  gapNext        MEMBER gapNext        FINAL)
    Q_PROPERTY(QString gapsText       MEMBER gapsText       FINAL)
    Q_PROPERTY(double  shortBelow     MEMBER shortBelow     FINAL)
    Q_PROPERTY(QString shortBelowText MEMBER shortBelowText FINAL)
    Q_PROPERTY(QList<ClubDistancePoint> perSession MEMBER perSession FINAL)
public:
    static constexpr double kNA = std::numeric_limits<double>::quiet_NaN();
    QString club, label, carryText, mishitsText, fewShotsText, trend, trendWords, gapWith, gapsText, shortBelowText;
    double  carryMean = kNA, carrySd = kNA, totalMean = kNA, totalSd = kNA, offlineSd = kNA, speedMean = kNA,
            speedSd = kNA, ballSpeedMean = kNA, ballSpeedSd = kNA, smashMean = kNA, smashSd = kNA, gapPrev = kNA,
            gapNext = kNA, shortBelow = kNA;
    int     shots = 0, sessions = 0;
    bool    fewShots = true;
    QList<ClubDistancePoint> perSession;   // oldest first
    static ClubDistanceRow fromJson(const QJsonObject &item);
    friend bool operator==(const ClubDistanceRow &, const ClubDistanceRow &) = default;
};

// "Your distances" for a golfer (clubDistances): the per-club rows; stale while no summary is read yet, or a session with
// shots is not summed up yet; the note says why they are not current ("" when they are).
struct ClubDistances {
    Q_GADGET
    Q_PROPERTY(QList<ClubDistanceRow> items MEMBER items FINAL)
    Q_PROPERTY(bool                   stale MEMBER stale FINAL)
    Q_PROPERTY(QString                note  MEMBER note  FINAL)
public:
    QList<ClubDistanceRow> items;
    bool                   stale = true;
    QString                note;
    friend bool operator==(const ClubDistances &, const ClubDistances &) = default;
};

// The session report's details of one athlete (reportDetails, reportDetailsForSession; design §7.3). Empty (no uuid)
// when there is no such athlete.
struct ReportDetails {
    Q_GADGET
    Q_PROPERTY(QString uuid             MEMBER uuid             FINAL)
    Q_PROPERTY(QString name             MEMBER name             FINAL)
    Q_PROPERTY(QString email            MEMBER email            FINAL)
    Q_PROPERTY(QString greeting         MEMBER greeting         FINAL)
    Q_PROPERTY(QString dob              MEMBER dob              FINAL)
    Q_PROPERTY(bool    photosApproved   MEMBER photosApproved   FINAL)
    Q_PROPERTY(QString photosApprovedOn MEMBER photosApprovedOn FINAL)
public:
    QString uuid, name, email, greeting, dob, photosApprovedOn;
    bool    photosApproved = false;
    bool        isEmpty() const { return uuid.isEmpty(); }
    // The report's own form (SessionReportModel's athlete map): the keys above; {} when empty.
    QVariantMap toMap() const;
    friend bool operator==(const ReportDetails &, const ReportDetails &) = default;
};

// AthleteController — THE QML ADAPTER OVER THE CURRENT ATHLETE (refactor step 2.3; brief §4.5). The
// record lives on a pinpoint::Athlete, read and written through pinpoint::AthleteRepository (the one
// writer, IM-S2-4); this class keeps today's QML surface — the same 14 properties and invokables — and
// SET-02 (the current athlete at launch, which stays in settings: DEC-05). It reaches the athletes through
// the composition root's callables (D18 (c): DEC-167; refactor step 3.4a-ii), never the athlete repository
// itself: list, load and create them, and read each one's activity; every edit is the Athlete's command.
//
// SWITCH vs EDIT (E-03, folds in Stage 0.7). currentAthleteChanged fires ONLY ON A SWITCH: another
// athlete (or none) becomes current. An edit of any record — the form's Save, a club, the photo consent,
// the Wrist ★ — is announced by the Athlete's recordChanged() and, here, by athletesChanged, which the
// record-valued properties notify on. So a listener that reloads data on currentAthleteChanged needs no
// gate (AthleteSwitchGate is gone).
//
// A LIST OVER THE ATHLETES (refactor step 5.7b). Each row is an Athlete's record (it follows recordChanged) and its
// ACTIVITY (the session count and last session: the picker's rows and its Recent order), derived from that Athlete's
// SessionIndex (DEC-89) as the one session list (SessionListPresenter) holds it: every listed athlete is followed there,
// so a session started, resumed, archived, restored, trashed or moved, or a first swing in one (EVT-17/18/19/20/23,
// EVT-01/03), changes the picker at once — no reopen. The list's rows are recomputed when a group's rows change, and the
// list is announced (athletesChanged) only when an activity moved. refreshActivity() re-lists the library's athletes.
namespace pinpoint { class AthleteLevel; class Athlete; }

class AthleteController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool         hasCurrentAthlete READ hasCurrentAthlete NOTIFY currentAthleteChanged)
    // The name and handedness change on a switch AND on an edit, so they notify on athletesChanged (which a
    // switch emits too); the uuid changes on a switch only.
    Q_PROPERTY(QString      currentName       READ currentName       NOTIFY athletesChanged)
    Q_PROPERTY(QString      currentInitials   READ currentInitials   NOTIFY athletesChanged)
    Q_PROPERTY(QString      currentHandedness READ currentHandedness NOTIFY athletesChanged)
    Q_PROPERTY(QString      currentUuid       READ currentUuid       NOTIFY currentAthleteChanged)
    // THE UNASSIGNED OWNER is the athlete on screen (information_model.md §5.11; DEC-198; J-67): its sessions list, open
    // and play, but it shows no athlete-level figures (pooling different golfers' shots is wrong data) and its record is
    // never edited or deleted. The fixed id itself, for the views that compare a row's uuid.
    Q_PROPERTY(bool         currentIsUnassigned READ currentIsUnassigned NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      unassignedUuid      READ unassignedUuid      CONSTANT)
    // The two demographic fields, RAW — an ISO date and a token, not a resolved cohort.
    //
    // Raw because the age band depends on the DAY, and the day belongs to whatever is being graded,
    // not to this object. A `currentCohort` property would have to pick a date, and the only one it
    // could pick is today — which is the single mistake norm.h's ageBandFor() exists to prevent, and
    // it would be baked into a property nobody could override. A consumer holding a swing hands
    // these two to cohortFor() with that swing's own date.
    //
    // Empty is a first-class answer: unset means the corridor that describes everyone, never
    // NotMeasured. NOTIFY athletesChanged as well, because editing the current athlete's date of
    // birth changes these without changing WHICH athlete is current.
    Q_PROPERTY(QString      currentDob        READ currentDob        NOTIFY athletesChanged)
    Q_PROPERTY(QString      currentSex        READ currentSex        NOTIFY athletesChanged)
    // The session report's details for the CURRENT athlete (docs/design/session_report_design.md §7.3).
    // currentReportGreeting is what the email and page 1 call them: the stored greeting, else the first
    // word of the name. currentIsJunior is under 18 TODAY — a preview for the form; a report asks
    // isJuniorOn() with its own session's date.
    Q_PROPERTY(QString      currentReportEmail      READ currentReportEmail      NOTIFY athletesChanged)
    Q_PROPERTY(QString      currentReportGreeting   READ currentReportGreeting   NOTIFY athletesChanged)
    Q_PROPERTY(bool         currentIsJunior         READ currentIsJunior         NOTIFY athletesChanged)
    Q_PROPERTY(bool         currentPhotosApproved   READ currentPhotosApproved   NOTIFY athletesChanged)
    Q_PROPERTY(QString      currentPhotosApprovedOn READ currentPhotosApprovedOn NOTIFY athletesChanged)
    Q_PROPERTY(QList<AthleteRow> athletes     READ athletes          NOTIFY athletesChanged)

public:
    explicit AthleteController(QObject *parent = nullptr);
    ~AthleteController() override;

    // The composition root hands in its callables over the athletes (D18 (c); main.cpp; a test, its own over
    // a scratch library) and the session repository (the Wrist reference's edge translation, and the drawer's
    // session delete until step 3.4b). Until then there are no athletes. Reads the library at once.
    void setCallables(const pinpoint::AthleteCallables &athletes, pinpoint::SessionRepository *sessions);

    // THE ONE SESSION LIST (main.cpp's; a test's own): every listed athlete is followed in it and read once, and the
    // activity is its rows'. Null (a test of the record alone): every activity is 0.
    void setSessionList(pinpoint::SessionListPresenter *list);
    // Re-lists the library's athletes; a new one is followed and read (the others follow their events). The picker asks
    // on opening; a library change asks too.
    Q_INVOKABLE void refreshActivity();
    // Tests: the activity recomputed at once on every change of the list (no coalescing on the event loop).
    void setSynchronousActivity(bool on) { m_syncActivity = on; }

    // ── C++ only (not QML) ───────────────────────────────────────────────────────────────────────
    pinpoint::Athlete     *currentAthlete() const { return m_current; }
    // The session repository the composition root handed in (the app's one SessionStore), for a view
    // controller that acts on one of the current athlete's sessions by its SessionId — the session trash
    // (step 2.6) — until step 3's Athlete::trashSession takes it over.
    pinpoint::SessionRepository *sessionRepository() const { return m_sessions; }
    // The current athlete's folder (D4: where its athlete.json is found; a settings athlete's: today's
    // derivation), "" when none. What the work-ons, the report list and the carousel follow.
    QString                currentFolder() const { return m_current ? m_current->folder() : QString(); }
    // Any athlete's folder, by its uuid, through its Athlete (D4) — so a RENAMED athlete is still found in the
    // folder it has always had, never in one derived from its name (J-29; step 2.7b: the session report). "" when
    // there is no such athlete.
    QString                folderOf(const QString &uuid) const;
    // reportDetails() of the athlete a SESSION belongs to: the athlete whose folder (D4) holds the session's
    // folder — and only that. Never by the name a folder spells, nor by the uuid a swing recorded (a swing's
    // uuid may be the capturing machine's, H-09, or a moved session's previous athlete): E-02, J-66 (step
    // 2.8b). The first athlete listed for the folder wins (two can resolve to one before M1: J-30). {} when no
    // athlete's folder holds it. C++ only (the session report).
    ReportDetails          reportDetailsForSession(const QString &sessionDir) const;
    // Who the current athlete's sessions belong to, as SessionRepository takes it (capture's session
    // folder, through capture::athleteIdentity).
    pinpoint::SessionOwner currentSessionOwner() const;
    // THE WRIST REFERENCE ★ ON THE RECORD (D5: DEC-152), as a ShotRef, translated at this edge from and
    // to the swing folder path the Charts views carry (appSettings.wristReferenceSwingDir). "" when the
    // current athlete's record names none, or its swing is gone.
    QString wristReferenceSwingDir() const;
    // Records `swingDir` (a swing of the current athlete's) on its record; "" clears it. False when it is
    // not one of the current athlete's swings, or the write failed.
    bool    setWristReferenceSwingDir(const QString &swingDir);

    bool         hasCurrentAthlete() const { return !m_currentUuid.isEmpty(); }
    QString      currentName()       const { return m_currentName; }
    QString      currentInitials()   const { return m_currentInitials; }
    QString      currentHandedness() const { return m_currentHandedness; }
    QString      currentUuid()       const { return m_currentUuid; }
    QString      currentDob()        const { return currentField(QStringLiteral("dob")); }
    QString      currentSex()        const { return currentField(QStringLiteral("sex")); }
    // Standing height in metres, 0 = unknown. Stored in feet whatever unit the form used.
    double       currentHeightM()    const { return currentField(QStringLiteral("heightValue")).toDouble() * 0.3048; }
    QList<AthleteRow> athletes()     const { return m_athletes; }
    QString      currentReportEmail()      const { return currentField(QStringLiteral("reportEmail")); }
    QString      currentReportGreeting()   const;
    bool         currentIsJunior()         const;
    bool         currentPhotosApproved()   const { return currentField(QStringLiteral("reportPhotosApproved")) == QLatin1String("true"); }
    QString      currentPhotosApprovedOn() const { return currentField(QStringLiteral("reportPhotosApprovedOn")); }

    // ── No athlete switch while a session runs (R-APP-1: the AppState rule, which replaced Stage 0.8g's check) ──
    // A running session's swings are saved under the CURRENT athlete's folder and analysed with their handedness,
    // height and clubs, so the current athlete must not change until it ends. AppState::mayChangeAthlete answers;
    // while it answers false,
    //   selectAthlete(another)   is refused — returns false, logs why (ppInfo), changes nothing;
    //   deleteAthlete(current)   is refused the same way (another athlete may still be deleted);
    //   clearCurrentAthlete()    is refused the same way;
    //   saveAthlete("", …)       still CREATES the athlete, but does not make it current.
    // Editing a record is not a switch and is always allowed. Unset (the tests' default): never locked.
    void setAppState(const pinpoint::AppState *state) { m_appState = state; }
    bool sessionRunning() const { return m_appState && !m_appState->mayChangeAthlete(); }

    // Upsert: empty uuid creates a new athlete (returns its new uuid); a non-empty,
    // existing uuid updates in place (returns that uuid). Returns "" on failure
    // (empty name, or unknown uuid on update). Performs cm->ft / kg->lb conversion.
    // A new athlete becomes the current one — unless a session is running (above), when it
    // is saved and listed but the session's athlete stays current.
    Q_INVOKABLE QString saveAthlete(
        const QString &uuid,                                    // "" = create
        const QString &name,
        const QString &handedness  = QStringLiteral("Right"),
        double         heightValue = 0.0,
        const QString &heightUnit  = QStringLiteral("ft"),
        double         weightValue = 0.0,
        const QString &weightUnit  = QStringLiteral("lb"),
        double         handicap    = -999.0,
        const QString &primaryClub = QStringLiteral("DRIVER"),
        double         speedTarget = 0.0,
        const QString &notes       = QString()
    );

    Q_INVOKABLE QString createAthlete(
        const QString &name,
        const QString &handedness  = QStringLiteral("Right"),
        double         heightValue = 0.0,
        const QString &heightUnit  = QStringLiteral("ft"),
        double         weightValue = 0.0,
        const QString &weightUnit  = QStringLiteral("lb"),
        double         handicap    = -999.0,
        const QString &primaryClub = QStringLiteral("DRIVER"),
        double         speedTarget = 0.0,
        const QString &notes       = QString()
    );

    // One field of the record, by its stored key, as text (AthleteRecord::setValue reads a number or a flag from it).
    Q_INVOKABLE bool updateAthlete(const QString &uuid, const QString &fieldName, const QString &value);

    // ── Demographics, for norm cohorts ──────────────────────────────────────
    //
    // `dob` ("YYYY-MM-DD") and `sex` ("male" | "female" | "declined" | "") ride on the record as two
    // more fields, written through updateAthlete rather than by growing saveAthlete's positional
    // signature to twelve arguments. BOTH ARE OPTIONAL and unset is a first-class answer: a norm
    // resolves against whatever axes are known, and a golfer who has told us nothing is graded by
    // the corridor that describes everyone. Never NotMeasured — "we don't know your age" and "we
    // could not assess this" are different statements.
    //
    // The COHORT for this athlete on a given day (pinpoint::analysis::cohortFor, with the swing's own date:
    // an athlete ages across their own history), in words — "women 55–64", or EMPTY when neither field is known — from raw
    // values rather than from a saved record, so the form can show it while it is being typed.
    //
    // The form is where these two fields are otherwise abstract: a date and a word, with no visible
    // consequence. Rendering what they resolve to says what they are FOR in the reader's own
    // numbers, and it makes the band boundaries discoverable instead of buried in a header comment.
    // Empty is a real answer and reads as one — nothing known, graded against everyone.
    Q_INVOKABLE QString cohortLabelFor(const QString &dobIsoDate, const QString &sex,
                                       const QString &onIsoDate) const;

    // The stable tokens the form's picker offers, so QML never spells them itself.
    Q_INVOKABLE QList<SexOption> sexOptions() const;

    // Per-athlete club records (the record's bag, `clubs`: a
    // QVariantMap keyed by the canonical club-vocabulary name — the same id
    // markup writes to truth.json meta.club). Record shape:
    //   { shaftType: "steel"|"graphite", loftDeg: double, lengthMm: int, bandWidthMm: int,
    //     bandCentersMm: list<int> (retro-band CENTRES from the butt, mm;
    //     empty = untaped), hoselFromButtMm: int, headPatch: bool,
    //     shaftLengthMm: int (exposed shaft, grip end → hosel top; 0 unknown),
    //     handsEndMm: int (bottom of the hands from the butt; 0 unknown),
    //     tapedOn: "YYYY-MM-DD", notes: string }
    // Consumers: shaft-tracker search radius (lengthMm) and the
    // instrumented-club pipeline (docs/validation/instrumented_club_protocol.md).
    // ── The session report's details (design §7.3) ──────────────────────────
    //
    // Four more fields on the record, written through updateAthlete like dob and sex:
    //   reportEmail             one address or several, comma-separated (a junior's parent or guardian)
    //   reportGreeting          what the email and page 1 call them; empty = the first word of the name
    //   reportPhotosApproved    a junior's parent or guardian approved swing photos (bool)
    //   reportPhotosApprovedOn  the ISO date the box was ticked; cleared when it is unticked
    // They stay on the record (athlete.json, or the settings record until M1) — never in a swing
    // document or a session folder — and go with the athlete when it is deleted.
    //
    // reportDetails: { uuid, name, email, greeting, dob, photosApproved, photosApprovedOn } for an
    // athlete found by uuid, else by name (case-insensitive) — the report map names its golfer, and a
    // session folder knows only the name. Empty (no uuid) when there is no such athlete.
    Q_INVOKABLE ReportDetails reportDetails(const QString &uuidOrName) const;
    // Under 18 on the day `sessionMs` (ms since the epoch, local date) falls on. Age is taken on the
    // SESSION's date, so an old session's report does not change when the golfer turns 18. An empty
    // or unparseable dob is NOT a junior (design §7.3: no date of birth is treated as an adult). A
    // sessionMs of 0 or less means "today", for the form's live preview.
    Q_INVOKABLE bool isJuniorOn(const QString &dobIsoDate, qint64 sessionMs) const;
    // One address or several, comma-separated, each shaped something@something.tld. A shape check,
    // never a verification. Empty is false — whether a blank is allowed is the caller's decision.
    Q_INVOKABLE bool validReportEmail(const QString &text) const;
    // "a@b.com, , c@d.org," → "a@b.com, c@d.org": the one spelling the form stores, whatever was typed
    // between the addresses. Each address trimmed, blanks dropped, joined by ", ".
    Q_INVOKABLE QString normaliseReportEmails(const QString &text) const;
    // What a blank greeting falls back to — the first word of the name, as reportDetails resolves it —
    // so the form's placeholder previews exactly what the report will say. "" for a blank name.
    Q_INVOKABLE QString greetingDefault(const QString &name) const;
    // The form's date-of-birth check. Blank is VALID (the field is optional); otherwise exactly
    // YYYY-MM-DD and a real calendar date ("2026-02-31" is refused).
    Q_INVOKABLE bool validDob(const QString &text) const;
    // The form's Height field, read into a number in `unit` ("cm", else "ft") for saveAthlete:
    // 5'11", 5ft 11in, 71 in, 180 cm, 1.80 m, or a bare number in the field's unit (height_text.h has
    // every spelling). 0 for blank or not a height; the form refuses a non-blank 0 (heightError).
    Q_INVOKABLE double parseHeight(const QString &text, const QString &unit) const;
    // Tick or untick the photo approval. Ticking stamps today's date unless it was already approved
    // (the date records when consent was GIVEN, not when the box was last pressed); unticking clears it.
    Q_INVOKABLE void setPhotosApproved(const QString &uuid, bool approved);

    // The bag in the record's own form (club id → its stored record), for the C++ readers (capture's context, the LM
    // board). Not QML's: it reads clubRecords / clubRecord.
    QVariantMap clubsFor(const QString &uuid) const;
    // The bag, typed, in vocabulary order (bagClubIds' order), and one club of it ("" club: not in the bag).
    Q_INVOKABLE QList<ClubRecord> clubRecords(const QString &uuid) const;
    Q_INVOKABLE ClubRecord        clubRecord(const QString &uuid, const QString &clubId) const;
    // "Your distances" for any golfer (shot_outcomes_design.md §7.5), from that athlete's AthleteDiagnostics (the session
    // summaries' club windows; refactor 3.7-f):
    // { items: [the per-club rows, keys as tendencies.h clubDistanceItemToJson writes them],
    //   stale: bool (no summary read yet, or a session with shots not summed up yet), note: "" or why they are not current }.
    // clubDistancesChanged is the cue to read again after a catch-up lands.
    Q_INVOKABLE ClubDistances clubDistances(const QString &uuid) const;
    // The athlete level "Your distances" reads (refactor 3.7-f; main.cpp: the Athlete's AthleteDiagnostics). None: "being
    // worked out".
    void setAthleteLevelDoor(std::function<pinpoint::AthleteLevel *(pinpoint::Athlete *)> door) { m_athleteLevelDoor = std::move(door); }
    Q_INVOKABLE ClubRecord  defaultClubRecord(const QString &clubId) const;  // factory spec defaults (loft/length/shaft)
    Q_INVOKABLE QStringList clubOptions() const;   // canonical vocabulary (club_vocabulary.h)
    // The athlete's bag: the club ids in their records, in vocabulary order (an id outside the
    // vocabulary sorts first). Empty for no uuid or an empty bag. Not a property, so a QML binding
    // that calls it reads athleteController.athletes to re-evaluate on every club commit (a record edit announces it).
    Q_INVOKABLE QStringList bagClubIds(const QString &uuid) const;
    // The athlete's default/preferred club, always resolved to a real bag club:
    // the stored primaryClub (legacy tokens normalised to canonical) if it is in
    // the bag, else "DRIVER" if present, else the first bag club, else "". Read on
    // both the QML side (Home / form pickers) and the C++ side (session seeding).
    Q_INVOKABLE QString effectivePrimaryClub(const QString &uuid) const;
    // The typed fields laid over the club's stored record, so a key this build does not know is kept.
    Q_INVOKABLE bool setClubRecord(const QString &uuid, const QString &clubId,
                                   const ClubRecord &record);
    Q_INVOKABLE bool removeClubRecord(const QString &uuid, const QString &clubId);
    // deleteAthlete / selectAthlete answer false when refused while a session runs (see
    // the AppState rule above). deleteAthlete moves the athlete's WHOLE folder, sessions and all, to
    // the recoverable trash (DEC-79), and is refused while one of its sessions is InProgress (DEC-99).
    // selectAthlete of the athlete already current is not a switch.
    Q_INVOKABLE bool deleteAthlete(const QString &uuid);
    Q_INVOKABLE bool selectAthlete(const QString &uuid);
    Q_INVOKABLE void clearCurrentAthlete();

    bool    currentIsUnassigned() const { return m_current && m_current->isUnassigned(); }
    QString unassignedUuid() const { return pinpoint::unassignedAthleteId().toString(); }
    // Whether the session at `sessionDir` is the Unassigned owner's (the drawer offers "Move to golfer…" for it).
    Q_INVOKABLE bool isUnassignedSession(const QString &sessionDir) const;
    // THE MOVE TO A GOLFER (information_model.md §5.11, R-UNASSIGNED-4; DEC-198; J-67): the Unassigned session at
    // `sessionDir` to the golfer `toUuid` — Athlete::moveSessionTo (the folder into the golfer's, its name and SessionId
    // kept; only session.json's athleteId rewritten). "" when moved (sessionMoved follows); else the golfer's words
    // for why not (in progress, archived, locked, a name already there).
    Q_INVOKABLE QString moveSessionToAthlete(const QString &sessionDir, const QString &toUuid);

signals:
    // A SWITCH only: another athlete, or none, became current (R-APP-1). Never on an edit.
    void currentAthleteChanged();
    void athletesChanged();
    // A delete that did not do what the picker's prompt said (J-30): refused, or the athlete removed
    // but its folder kept because another athlete shares it. main.cpp posts it as a notice.
    void deleteNotice(const QString &title, const QString &detail);
    void clubDistancesChanged();
    // A session moved to a golfer: where it was and where it now is (the views that showed it reload).
    void sessionMoved(const QString &fromDir, const QString &toDir);

private:
    std::function<pinpoint::AthleteLevel *(pinpoint::Athlete *)> m_athleteLevelDoor;
    void reload();                       // re-list the library's athletes, then rebuild()
    void rebuild();                      // the list and the current athlete, from the athletes held
    pinpoint::Athlete *athleteFor(const QString &uuid) const;
    AthleteRow         rowOf(const pinpoint::Athlete &a) const;
    static QString computeInitials(const QString &name);
    static void sortAthletes(QList<AthleteRow> &list);
    // The activity (step 5.7b): every listed athlete followed in the one session list and read once; its rows' change
    // recomputes the activity (coalesced on the event loop unless synchronous), and the list is rebuilt when one moved.
    void followAll();
    void activityMoved();
    void applyActivity();

    // One field off the CURRENT athlete's record, empty when there is none. Read off the current Athlete's
    // record rather than mirrored into a member beside m_currentName: those four are set when the current
    // athlete CHANGES, and a demographic edited in the form changes the record without changing
    // which record is current — a mirror would go stale exactly there.
    QString currentField(const QString &key) const
    {
        if (m_currentUuid.isEmpty() || !m_current) return {};
        return m_current->record().value(key).toString();
    }

    QList<AthleteRow> m_athletes;
    QString      m_currentUuid;
    QString      m_currentName;
    QString      m_currentInitials;
    QString      m_currentHandedness;

    pinpoint::AthleteCallables   m_athletesDoor;   // the composition root's (D18 (c)); empty: no athletes
    pinpoint::SessionRepository *m_sessions = nullptr;
    QList<pinpoint::AthleteId>   m_ids;     // the last list(), in its order
    QPointer<pinpoint::Athlete>  m_current;
    const pinpoint::AppState *m_appState = nullptr;   // the switch rule (R-APP-1), from main.cpp
    QPointer<pinpoint::SessionListPresenter> m_list;     // the one session list (main.cpp's), not owned
    QHash<QString, pinpoint::AthleteActivity> m_activity;   // uuid → as the list's rows last gave it
    bool m_activityArmed = false;
    bool m_syncActivity = false;
};
