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
// Stage 0.8g; refactor step 2.3). The contract is athlete_controller.h (setSessionRunningCheck, and
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

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class AthleteSessionGuardTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_cfg;

    QString iniPath() const { return m_cfg.path() + QStringLiteral("/PinPointStudio/PinPointStudio.ini"); }

    static bool listed(const AthleteController &a, const QString &uuid)
    {
        for (const QVariant &v : a.athletes())
            if (v.toMap().value(QStringLiteral("uuid")).toString() == uuid) return true;
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
        a.setSessionRunningCheck([&running] { return running; });

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

        AthleteController unset;                                  // G4 no check given = never running
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
        a.setSessionRunningCheck([&running] { return running; });
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
        QVariantMap clubs = a.clubsFor(nick);
        QVERIFY(!clubs.isEmpty());
        const QString club = clubs.firstKey();
        QVariantMap rec = clubs.value(club).toMap();
        rec[QStringLiteral("lengthMm")] = rec.value(QStringLiteral("lengthMm")).toInt() + 3;
        QVERIFY(a.setClubRecord(nick, club, rec));
        QCOMPARE(a.clubsFor(nick).value(club).toMap().value(QStringLiteral("lengthMm")).toInt(),
                 rec.value(QStringLiteral("lengthMm")).toInt());
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
};

QTEST_GUILESS_MAIN(AthleteSessionGuardTest)
#include "athlete_session_guard_test.moc"
