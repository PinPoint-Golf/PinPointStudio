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

// No athlete switch while a session runs, and an edit is not a switch (SWE audit E-01 / E-03,
// Stage 0.8g; refactor step 2.3; the rule is AppState's since step 4-a). The contract is athlete_controller.h (setAppState, and
// currentAthleteChanged on a SWITCH only):
//
//   G — while the session check answers true, selectAthlete(another) and deleteAthlete(current)
//       answer false and change nothing; saveAthlete("", …) creates the athlete WITHOUT making it
//       current; editing a record still works. With no session, all of it behaves as before.
//   W — a record edit — a club length, updateAthlete, the form's Save of an existing athlete, the
//       photo consent, another athlete's record — emits no currentAthleteChanged (so the live carousel's
//       reload in main.cpp and SessionReviewController::onAthleteChanged, which run on it, never run
//       for an edit); selecting another athlete emits it once, and so does deleting the current one.
//
// Against a scratch INI (QSettings::setPath) and a scratch library — never the real ones.

#include "athlete_controller.h"
#include "athlete_test_library.h"
#include "pp_settings.h"

#include "../../Diagnostics/derived/tendencies.h"   // clubDistanceItemToJson (the typed distances, step 5.7b)

#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

class AthleteSessionGuardTest : public QObject
{
    Q_OBJECT
    pinpoint::test::IsolatedStores m_stores;   // #187: the first member, so every store is scratch before any other

    QTemporaryDir m_cfg;

    QString iniPath() const { return m_cfg.path() + QStringLiteral("/PinPointStudio/PinPointStudio.ini"); }

    static bool listed(const AthleteController &a, const QString &uuid)
    {
        for (const AthleteRow &r : a.athletes())
            if (r.uuid == uuid) return true;
        return false;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_cfg.isValid());
        // ppSettings() is IniFormat/UserScope: point the whole scope at the scratch folder before the
        // first QSettings is made, so nothing here can reach the real settings file.
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_cfg.path());
        QCOMPARE(QFileInfo(ppSettings().fileName()).absoluteFilePath(), QFileInfo(iniPath()).absoluteFilePath());
    }

    void init()
    {
        QSettings s = ppSettings();
        s.clear();
        s.sync();
    }

    // G1–G4: no session — create selects, select and delete work (the behaviour before the guard).
    void noSessionBehavesAsBefore()
    {
        AthleteTestLibrary lib;
        AthleteController &a = lib.makeController();
        bool running = false;
        pinpoint::AppState state;
        pinpoint::AppStateDoors doors;
        doors.sessionRunning = [&running] { return running; };
        state.setDoors(doors);
        a.setAppState(&state);

        const QString nick = a.saveAthlete(QString(), QStringLiteral("Nick Henn"));
        QVERIFY(!nick.isEmpty());
        QCOMPARE(a.currentUuid(), nick);                          // G1 a new athlete becomes current
        const QString jess = a.saveAthlete(QString(), QStringLiteral("Jess Ratcliffe"));
        QCOMPARE(a.currentUuid(), jess);
        QVERIFY(a.selectAthlete(nick));                           // G2 select works
        QCOMPARE(a.currentUuid(), nick);
        QVERIFY(a.deleteAthlete(nick));                           // G3 delete of the current works
        QVERIFY(a.currentUuid().isEmpty());
        QVERIFY(!listed(a, nick));

        AthleteController unset;                                  // G4 no AppState given = never running
        unset.setSynchronousActivity(true);
        unset.setCallables(callablesOver(*lib.athletes), &lib.sessions);
        QVERIFY(!unset.sessionRunning());
        QVERIFY(unset.selectAthlete(jess));
        QCOMPARE(unset.currentUuid(), jess);
    }

    // G5–G12: a session running for Nick.
    void sessionRunningRefusesASwitch()
    {
        AthleteTestLibrary lib;
        AthleteController &a = lib.makeController();
        bool running = false;
        pinpoint::AppState state;
        pinpoint::AppStateDoors doors;
        doors.sessionRunning = [&running] { return running; };
        state.setDoors(doors);
        a.setAppState(&state);
        const QString nick  = a.saveAthlete(QString(), QStringLiteral("Nick Henn"));
        const QString chris = a.saveAthlete(QString(), QStringLiteral("Chris Marriott"));
        QVERIFY(a.selectAthlete(nick));
        running = true;
        QVERIFY(a.sessionRunning());

        QSignalSpy changed(&a, &AthleteController::currentAthleteChanged);

        // G5 select of another athlete is refused, and nothing is re-announced.
        QVERIFY(!a.selectAthlete(chris));
        QCOMPARE(a.currentUuid(), nick);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(ppSettings().value(QStringLiteral("currentAthleteUuid")).toString(), nick);

        // G6 selecting the athlete already current is not a switch: allowed, and not announced.
        QVERIFY(a.selectAthlete(nick));
        QCOMPARE(a.currentUuid(), nick);
        QCOMPARE(changed.count(), 0);

        // G7 delete of the current athlete is refused; the record stays.
        QVERIFY(!a.deleteAthlete(nick));
        QCOMPARE(a.currentUuid(), nick);
        QVERIFY(listed(a, nick));

        // G8 clearing the current athlete is refused.
        a.clearCurrentAthlete();
        QCOMPARE(a.currentUuid(), nick);

        // G9 "＋ New athlete" mid-session: saved and listed, NOT current.
        const QString paddy = a.saveAthlete(QString(), QStringLiteral("Paddy Marriott"));
        QVERIFY(!paddy.isEmpty());
        QVERIFY(listed(a, paddy));
        QCOMPARE(a.currentUuid(), nick);
        QCOMPARE(a.currentName(), QStringLiteral("Nick Henn"));
        QCOMPARE(ppSettings().value(QStringLiteral("currentAthleteUuid")).toString(), nick);

        // G10 another athlete can still be deleted (the rule is about the CURRENT one).
        QVERIFY(a.deleteAthlete(chris));
        QVERIFY(!listed(a, chris));
        QCOMPARE(a.currentUuid(), nick);

        // G11 editing the current record is not a switch: allowed.
        QVERIFY(a.updateAthlete(nick, QStringLiteral("dob"), QStringLiteral("1990-01-01")));
        QCOMPARE(a.currentDob(), QStringLiteral("1990-01-01"));
        QCOMPARE(a.saveAthlete(nick, QStringLiteral("Nick Henn")), nick);
        QCOMPARE(a.currentUuid(), nick);
        QCOMPARE(changed.count(), 0);

        // G12 the session ends: the switch is allowed again.
        running = false;
        QVERIFY(a.selectAthlete(paddy));
        QCOMPARE(a.currentUuid(), paddy);
        QCOMPARE(changed.count(), 1);
    }

    // W1–W6: an edit is not a switch — currentAthleteChanged stays silent; the record-valued
    // properties follow the edit through athletesChanged.
    void recordEditIsNotASwitch()
    {
        AthleteTestLibrary lib;
        AthleteController &a = lib.makeController();
        const QString nick  = a.saveAthlete(QString(), QStringLiteral("Nick Henn"));
        const QString chris = a.saveAthlete(QString(), QStringLiteral("Chris Marriott"));
        QVERIFY(a.selectAthlete(nick));

        QSignalSpy switched(&a, &AthleteController::currentAthleteChanged);
        QSignalSpy edited(&a, &AthleteController::athletesChanged);

        // W1 a club length: the club form's commit.
        const QList<ClubRecord> clubs = a.clubRecords(nick);
        QVERIFY(!clubs.isEmpty());
        const QString club = clubs.first().club;
        ClubRecord rec = a.clubRecord(nick, club);
        QVERIFY(rec == clubs.first());
        rec.lengthMm += 3;
        QVERIFY(a.setClubRecord(nick, club, rec));
        QCOMPARE(a.clubRecord(nick, club).lengthMm, rec.lengthMm);
        QCOMPARE(a.clubsFor(nick).value(club).toMap().value(QStringLiteral("lengthMm")).toInt(), rec.lengthMm);   // the bag's form
        // W2 the form's Save of the current athlete — a rename — and the fields written after it.
        QCOMPARE(a.saveAthlete(nick, QStringLiteral("Nick Henn-Smith"), QStringLiteral("Left")), nick);
        QCOMPARE(a.currentName(), QStringLiteral("Nick Henn-Smith"));      // the property follows the edit
        QCOMPARE(a.currentHandedness(), QStringLiteral("Left"));
        QVERIFY(a.updateAthlete(nick, QStringLiteral("dob"), QStringLiteral("1990-01-01")));
        QVERIFY(a.updateAthlete(nick, QStringLiteral("sex"), QStringLiteral("male")));
        a.setPhotosApproved(nick, true);
        // W3 an edit of ANOTHER athlete's record.
        QVERIFY(a.updateAthlete(chris, QStringLiteral("notes"), QStringLiteral("hi")));

        QVERIFY(edited.count() >= 6);                              // every edit was announced…
        QCOMPARE(switched.count(), 0);                             // …W1–W3 none of them as a switch

        QVERIFY(a.selectAthlete(chris));                           // W4 a real switch, once
        QCOMPARE(switched.count(), 1);
        QVERIFY(a.selectAthlete(chris));                           // W5 the same athlete again: not one
        QCOMPARE(switched.count(), 1);
        QVERIFY(a.deleteAthlete(chris));                           // W6 deleting the current one is a switch (to none)
        QCOMPARE(switched.count(), 2);
        QVERIFY(!a.hasCurrentAthlete());
    }

    // T1–T4 (refactor step 5.7b): the typed values QML reads. "Your distances" made from clubDistanceItemToJson's output
    // (one rule for the words), a number not measured NaN as the JSON leaves it out; a club record's defaults are the
    // form's; a stored key this build does not know survives a typed commit.
    void typedValues()
    {
        pinpoint::analysis::ClubDistanceItem it;
        it.club = QStringLiteral("7 IRON");  it.label = QStringLiteral("7 iron");
        it.carryMean = 150.0;                it.carrySd = 6.0;   it.carryText = QStringLiteral("150 ± 6 yd");
        it.shots = 12;                       it.sessions = 3;    it.fewShots = false;
        it.trend = QStringLiteral("up");     it.trendWords = QStringLiteral("↑ 4 yd");
        it.gapPrev = 12.0;                   it.gapPrevClub = QStringLiteral("6 iron");
        it.shortBelow = 141.0;
        it.perSession.push_back({ QStringLiteral("2026-11-08"), 149.0, std::numeric_limits<double>::quiet_NaN(), 4 });
        const QJsonObject json = pinpoint::analysis::clubDistanceItemToJson(it);
        const ClubDistanceRow r = ClubDistanceRow::fromJson(json);
        QCOMPARE(r.club, QStringLiteral("7 IRON"));                                                 // T1 the keys
        QCOMPARE(r.carryMean, 150.0);
        QCOMPARE(r.carryText, QStringLiteral("150 ± 6 yd"));
        QVERIFY(std::isnan(r.totalMean) && std::isnan(r.speedSd) && std::isnan(r.gapNext));        // T2 not measured: NaN
        QCOMPARE(r.shots, 12);
        QCOMPARE(r.fewShots, false);
        QVERIFY(r.fewShotsText.isEmpty());
        QCOMPARE(r.gapsText, json.value(QStringLiteral("gapsText")).toString());                     // T3 the words, one rule
        QVERIFY(!r.gapsText.isEmpty());
        QCOMPARE(r.shortBelowText, json.value(QStringLiteral("shortBelowText")).toString());
        QCOMPARE(r.perSession.size(), 1);
        QCOMPARE(r.perSession.first().mean, 149.0);
        QVERIFY(std::isnan(r.perSession.first().sd));
        QCOMPARE(r.perSession.first().n, 4);

        const ClubRecord none = ClubRecord::fromMap(QStringLiteral("7 IRON"), {});                 // T4 the form's defaults
        QCOMPARE(none.shaftType, QStringLiteral("steel"));
        QCOMPARE(none.bandWidthMm, 25);
        QVERIFY(none.bandCentersMm.isEmpty() && none.lengthMm == 0 && !none.headPatch);
        AthleteTestLibrary lib;
        AthleteController &c = lib.makeController();
        const QString uuid = c.saveAthlete(QString(), QStringLiteral("Typed Golfer"));
        QVariantMap bag = c.clubsFor(uuid);
        QVariantMap seven = bag.value(QStringLiteral("7 IRON")).toMap();
        seven.insert(QStringLiteral("futureKey"), QStringLiteral("kept"));
        bag.insert(QStringLiteral("7 IRON"), seven);
        pinpoint::Athlete *held = c.currentAthlete();
        QVERIFY(held && held->editBag(bag).ok());
        ClubRecord rec = c.clubRecord(uuid, QStringLiteral("7 IRON"));
        rec.bandCentersMm = { 270, 320 };
        QVERIFY(c.setClubRecord(uuid, QStringLiteral("7 IRON"), rec));
        const QVariantMap stored = c.clubsFor(uuid).value(QStringLiteral("7 IRON")).toMap();
        QCOMPARE(stored.value(QStringLiteral("futureKey")).toString(), QStringLiteral("kept"));
        QCOMPARE(stored.value(QStringLiteral("bandCentersMm")).toList().size(), 2);
        QCOMPARE(c.clubRecord(uuid, QStringLiteral("7 IRON")).bandCentersMm, (QList<int>{ 270, 320 }));
        QVERIFY(c.clubRecord(uuid, QStringLiteral("NO SUCH CLUB")).club.isEmpty());
        QCOMPARE(c.defaultClubRecord(QStringLiteral("3 WOOD")).loftDeg, 15.0);
    }
};

QTEST_GUILESS_MAIN(AthleteSessionGuardTest)
#include "athlete_session_guard_test.moc"
