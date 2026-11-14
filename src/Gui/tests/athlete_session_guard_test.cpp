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
// Stage 0.8g). The contract is athlete_controller.h (setSessionRunningCheck) and
// athlete_switch_gate.h:
//
//   G — while the session check answers true, selectAthlete(another) and deleteAthlete(current)
//       answer false and change nothing; saveAthlete("", …) creates the athlete WITHOUT making it
//       current; editing a record still works. With no session, all of it behaves as before.
//   W — the gate a data-reloading listener of currentAthleteChanged runs through (the live
//       carousel's in main.cpp, SessionReviewController::onAthleteChanged): a record edit — a club
//       length, updateAthlete, the form's Save of an existing athlete — is not a switch; selecting
//       another athlete is.
//
// Against a scratch INI (QSettings::setPath) — never the real settings file.

#include "athlete_controller.h"
#include "athlete_switch_gate.h"
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
        AthleteController a;
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
        QVERIFY(!unset.sessionRunning());
        QVERIFY(unset.selectAthlete(jess));
        QCOMPARE(unset.currentUuid(), jess);
    }

    // G5–G12: a session running for Nick.
    void sessionRunningRefusesASwitch()
    {
        AthleteController a;
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

        // G6 selecting the athlete already current is not a switch: allowed.
        QVERIFY(a.selectAthlete(nick));
        QCOMPARE(a.currentUuid(), nick);

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

        // G12 the session ends: the switch is allowed again.
        running = false;
        QVERIFY(a.selectAthlete(paddy));
        QCOMPARE(a.currentUuid(), paddy);
    }

    // W1–W3: the gate on its own.
    void gateAnswersOnlyAChange()
    {
        AthleteSwitchGate unprimed;
        QVERIFY(unprimed.switched(QStringLiteral("a")));          // W1 unprimed: the first call is a switch
        QVERIFY(!unprimed.switched(QStringLiteral("a")));
        AthleteSwitchGate primed(QStringLiteral("a"));
        QVERIFY(!primed.switched(QStringLiteral("a")));           // W2 primed: the same uuid is not
        QVERIFY(primed.switched(QStringLiteral("b")));            // W3 another uuid is, once
        QVERIFY(!primed.switched(QStringLiteral("b")));
        QVERIFY(primed.switched(QString()));                      // …and so is "no athlete"
    }

    // W4–W8: the gate as the carousel's listener runs it, on a real controller's signal.
    void recordEditDoesNotReload()
    {
        AthleteController a;
        const QString nick  = a.saveAthlete(QString(), QStringLiteral("Nick Henn"));
        const QString chris = a.saveAthlete(QString(), QStringLiteral("Chris Marriott"));
        QVERIFY(a.selectAthlete(nick));

        int reloads = 0, emissions = 0;
        QObject::connect(&a, &AthleteController::currentAthleteChanged, &a,
                         [&a, &reloads, &emissions, gate = AthleteSwitchGate(a.currentUuid())]() mutable {
            ++emissions;
            if (gate.switched(a.currentUuid())) ++reloads;
        });

        // W4 a club length: the club form's commit.
        QVariantMap clubs = a.clubsFor(nick);
        QVERIFY(!clubs.isEmpty());
        const QString club = clubs.firstKey();
        QVariantMap rec = clubs.value(club).toMap();
        rec[QStringLiteral("lengthMm")] = rec.value(QStringLiteral("lengthMm")).toInt() + 3;
        QVERIFY(a.setClubRecord(nick, club, rec));
        // W5 the form's Save of the current athlete: saveAthlete + four updateAthlete + photos.
        QCOMPARE(a.saveAthlete(nick, QStringLiteral("Nick Henn")), nick);
        QVERIFY(a.updateAthlete(nick, QStringLiteral("dob"), QStringLiteral("1990-01-01")));
        QVERIFY(a.updateAthlete(nick, QStringLiteral("sex"), QStringLiteral("male")));
        a.setPhotosApproved(nick, true);
        // W6 an edit of ANOTHER athlete's record.
        QVERIFY(a.updateAthlete(chris, QStringLiteral("notes"), QStringLiteral("hi")));

        QVERIFY(emissions >= 6);                                   // the signal fired for each edit…
        QCOMPARE(reloads, 0);                                      // …W4–W6 none of them reloads

        QVERIFY(a.selectAthlete(chris));                           // W7 a real switch reloads, once
        QCOMPARE(reloads, 1);
        QVERIFY(a.deleteAthlete(chris));                           // W8 deleting the current one is a switch (to none)
        QCOMPARE(reloads, 2);
    }
};

QTEST_GUILESS_MAIN(AthleteSessionGuardTest)
#include "athlete_session_guard_test.moc"
