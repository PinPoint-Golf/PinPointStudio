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

#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class AthleteController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool         hasCurrentAthlete READ hasCurrentAthlete NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentName       READ currentName       NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentInitials   READ currentInitials   NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentHandedness READ currentHandedness NOTIFY currentAthleteChanged)
    Q_PROPERTY(QString      currentUuid       READ currentUuid       NOTIFY currentAthleteChanged)
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
    Q_PROPERTY(QVariantList athletes          READ athletes          NOTIFY athletesChanged)
    Q_PROPERTY(QVariantList recentSessions   READ recentSessions    NOTIFY athletesChanged)

public:
    explicit AthleteController(QObject *parent = nullptr);
    ~AthleteController() override;   // waits for a running activity scan (it posts back to this)

    // ── Activity: each golfer's session count and last session, read from the library ──────────
    // Nothing writes the registry's sessionCount / lastSessionAt, so both come from the golfer's
    // folder, counted the way the Sessions list counts (a folder with at least one swing_* in it)
    // and dated the way it dates one whose swings are not read (the yyyy-MM-dd its name starts
    // with, else its mtime). They order `athletes` (newest first), so the picker's Recent cards
    // follow who has been in. The scan runs on a pool thread; the result patches the list.
    struct Activity { int sessions = 0; qint64 lastAtSec = 0; };
    static Activity scanActivity(const QString &athleteDir);   // touches no member; public for the test
    // name → that golfer's folder ("" = unknown). main.cpp supplies it (the library root and
    // SwingPaths::sanitise), so this class needs neither. Setting it scans.
    void setAthleteDirResolver(std::function<QString(const QString &name)> resolver);
    // Re-reads every golfer's activity. Also run by every reload(); the picker asks on opening.
    Q_INVOKABLE void refreshActivity();
    // Tests: scan on the calling thread.
    void setSynchronousActivity(bool on) { m_syncActivity = on; }

    bool         hasCurrentAthlete() const { return !m_currentUuid.isEmpty(); }
    QString      currentName()       const { return m_currentName; }
    QString      currentInitials()   const { return m_currentInitials; }
    QString      currentHandedness() const { return m_currentHandedness; }
    QString      currentUuid()       const { return m_currentUuid; }
    QString      currentDob()        const { return currentField(QStringLiteral("dob")); }
    QString      currentSex()        const { return currentField(QStringLiteral("sex")); }
    // Standing height in metres, 0 = unknown. Stored in feet whatever unit the form used.
    double       currentHeightM()    const { return currentField(QStringLiteral("heightValue")).toDouble() * 0.3048; }
    QVariantList athletes()          const { return m_athletes; }
    QVariantList recentSessions()    const { return {}; }
    QString      currentReportEmail()      const { return currentField(QStringLiteral("reportEmail")); }
    QString      currentReportGreeting()   const;
    bool         currentIsJunior()         const;
    bool         currentPhotosApproved()   const { return currentField(QStringLiteral("reportPhotosApproved")) == QLatin1String("true"); }
    QString      currentPhotosApprovedOn() const { return currentField(QStringLiteral("reportPhotosApprovedOn")); }

    // ── No athlete switch while a session runs (SWE audit E-01, Stage 0.8g) ──────────────────
    // A running session's swings are saved under the CURRENT athlete's folder and analysed with
    // their handedness, height and clubs, so the current athlete must not change until the
    // session ends. The rule lives HERE, not only in the picker's QML (which greys Select and
    // Delete and says "A session is running for … End it to switch or delete a golfer."):
    // while the check answers true,
    //   selectAthlete(another)   is refused — returns false, logs why (ppInfo), changes nothing;
    //   deleteAthlete(current)   is refused the same way (another athlete may still be deleted);
    //   clearCurrentAthlete()    is refused the same way;
    //   saveAthlete("", …)       still CREATES the athlete, but does not make it current.
    // Editing a record (saveAthlete with a uuid, updateAthlete, the club records) is not a switch
    // and is always allowed. main.cpp hands in SessionController::running; unset (the tests'
    // default) means no session is ever running.
    void setSessionRunningCheck(std::function<bool()> running) { m_sessionRunning = std::move(running); }
    bool sessionRunning() const { return m_sessionRunning && m_sessionRunning(); }

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

    Q_INVOKABLE bool updateAthlete(const QString &uuid, const QString &fieldName, const QVariant &value);

    // ── Demographics, for norm cohorts ──────────────────────────────────────
    //
    // `dob` ("YYYY-MM-DD") and `sex` ("male" | "female" | "declined" | "") ride on the record as two
    // more fields, written through updateAthlete rather than by growing saveAthlete's positional
    // signature to twelve arguments. BOTH ARE OPTIONAL and unset is a first-class answer: a norm
    // resolves against whatever axes are known, and a golfer who has told us nothing is graded by
    // the corridor that describes everyone. Never NotMeasured — "we don't know your age" and "we
    // could not assess this" are different statements.
    //
    // The COHORT for this athlete on a given day, spelled as the norm set spells it
    // ({ sex, age }, each key present only when known). `onIsoDate` is the SWING's date, and passing
    // it is the whole point: an athlete ages across their own history, so a swing from four years
    // ago must resolve the band they were in then. An empty date yields no age band at all rather
    // than silently substituting today — a caller that does not know when the swing happened must
    // not be answered as though it were now.
    Q_INVOKABLE QVariantMap cohortFor(const QString &uuid, const QString &onIsoDate) const;

    // The same answer in words — "women 55–64", or EMPTY when neither field is known — from raw
    // values rather than from a saved record, so the form can show it while it is being typed.
    //
    // The form is where these two fields are otherwise abstract: a date and a word, with no visible
    // consequence. Rendering what they resolve to says what they are FOR in the reader's own
    // numbers, and it makes the band boundaries discoverable instead of buried in a header comment.
    // Empty is a real answer and reads as one — nothing known, graded against everyone.
    Q_INVOKABLE QString cohortLabelFor(const QString &dobIsoDate, const QString &sex,
                                       const QString &onIsoDate) const;

    // The stable tokens the form's picker offers, so QML never spells them itself.
    Q_INVOKABLE QVariantList sexOptions() const;

    // Per-athlete club records (Settings live under athletes/<uuid>/clubs as a
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
    // They stay in the app INI beside the rest of the record — never in a swing document, a session
    // folder or the share — and deleteAthlete removes them with the group.
    //
    // reportDetails: { uuid, name, email, greeting, dob, photosApproved, photosApprovedOn } for an
    // athlete found by uuid, else by name (case-insensitive) — the report map names its golfer, and a
    // session folder knows only the name. Empty map when there is no such athlete.
    Q_INVOKABLE QVariantMap reportDetails(const QString &uuidOrName) const;
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
    // Tick or untick the photo approval. Ticking stamps today's date unless it was already approved
    // (the date records when consent was GIVEN, not when the box was last pressed); unticking clears it.
    Q_INVOKABLE void setPhotosApproved(const QString &uuid, bool approved);

    Q_INVOKABLE QVariantMap clubsFor(const QString &uuid) const;
    // "Your distances" for any golfer (shot_outcomes_design.md §7.5), from <athlete>/tendencies.json:
    // { items: [the stored per-club rows, keys as tendencies.h clubDistanceItemToJson writes them],
    //   stale: bool (no file, or older than the newest ledger), note: "" or why they are not current }.
    // clubDistancesChanged is the cue to read again after a catch-up lands.
    Q_INVOKABLE QVariantMap clubDistances(const QString &uuid) const;
    Q_INVOKABLE QVariantMap defaultClubRecord(const QString &clubId) const;  // factory spec defaults (loft/length/shaft)
    Q_INVOKABLE QStringList clubOptions() const;   // canonical vocabulary (club_vocabulary.h)
    // The athlete's bag: the club ids in their records, in vocabulary order (an id outside the
    // vocabulary sorts first). Empty for no uuid or an empty bag. Not a property, so a QML binding
    // that calls it reads athleteController.athletes to re-evaluate on every club commit.
    Q_INVOKABLE QStringList bagClubIds(const QString &uuid) const;
    // The athlete's default/preferred club, always resolved to a real bag club:
    // the stored primaryClub (legacy tokens normalised to canonical) if it is in
    // the bag, else "DRIVER" if present, else the first bag club, else "". Read on
    // both the QML side (Home / form pickers) and the C++ side (session seeding).
    Q_INVOKABLE QString effectivePrimaryClub(const QString &uuid) const;
    Q_INVOKABLE bool setClubRecord(const QString &uuid, const QString &clubId,
                                   const QVariantMap &record);
    Q_INVOKABLE bool removeClubRecord(const QString &uuid, const QString &clubId);
    // deleteAthlete / selectAthlete answer false when refused while a session runs (see
    // setSessionRunningCheck above); selectAthlete of the athlete already current is not a switch.
    Q_INVOKABLE bool deleteAthlete(const QString &uuid);
    Q_INVOKABLE bool selectAthlete(const QString &uuid);
    Q_INVOKABLE void clearCurrentAthlete();

signals:
    // ⚠ Emitted by every reload(): a SWITCH of athlete and an EDIT of any record alike (one form
    // Save emits it five or six times). A listener that reloads data must act only when
    // currentUuid() differs from the one it last acted on — athlete_switch_gate.h. (Splitting
    // this into a switch signal and an edit signal is refactor step 2, not done here.)
    void currentAthleteChanged();
    void athletesChanged();
    void clubDistancesChanged();

private:
    void reload();
    static QString computeInitials(const QString &name);
    static void sortAthletes(QVariantList &list);
    void applyActivity(int generation, const QHash<QString, Activity> &byUuid);

    // One field off the CURRENT athlete's record, empty when there is none. Read out of m_athletes
    // rather than mirrored into a member beside m_currentName: those four are set when the current
    // athlete CHANGES, and a demographic edited in the form changes the record without changing
    // which record is current — a mirror would go stale exactly there.
    QString currentField(const QString &key) const
    {
        if (m_currentUuid.isEmpty()) return {};
        for (const QVariant &v : m_athletes) {
            const QVariantMap a = v.toMap();
            if (a.value(QStringLiteral("uuid")).toString() == m_currentUuid)
                return a.value(key).toString();
        }
        return {};
    }

    QVariantList m_athletes;
    QString      m_currentUuid;
    QString      m_currentName;
    QString      m_currentInitials;
    QString      m_currentHandedness;

    std::function<QString(const QString &)> m_dirFor;
    std::function<bool()> m_sessionRunning;   // SessionController::running, from main.cpp
    QHash<QString, Activity> m_activity;      // uuid → last scan's result
    int  m_activityGeneration = 0;
    bool m_syncActivity = false;
    QThreadPool m_pool;
};
