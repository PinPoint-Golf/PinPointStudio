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

// THE STUDIO, ITS COACH AND THE GOLFER'S REPORT DETAILS (session report design §7) — StudioProfile and
// the four report fields on AthleteController, against a scratch INI and a scratch studio folder.
//
//   defaults        — empty studio and coach, the mock's four practice steps, the §8.3 email templates,
//                     Folio's accent, the PS monogram, all three details missing
//   round trip      — every field written by one instance is read back by a fresh one, and an absent
//                     key (the shipped default) is not written at all
//   logo / photo    — import copies into the studio folder, replaces any earlier copy (another
//                     extension included), refuses what is not an image, survives the original going;
//                     clear removes the copy
//   stamp           — moves with every field the PDF prints and with the logo's bytes; the email
//                     templates do not move it
//   monogram        — initials of the first two significant words
//   accent          — normalised to #rrggbb, refused when not a colour, the 3:1 contrast test, presets
//   athlete         — the report fields round-trip through updateAthlete and reload(), reportDetails by
//                     uuid and by name, the greeting's fallback, isJuniorOn on the session date, the
//                     email shape check, the approval date, and deleteAthlete taking them all away

#include "studio_profile.h"
#include "athlete_controller.h"
#include "pp_settings.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class ReportStudioTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_cfg;
    QTemporaryDir m_studio;
    QTemporaryDir m_files;

    QString iniPath() const { return m_cfg.path() + QStringLiteral("/PinPointStudio/PinPointStudio.ini"); }

    QString makePng(const QString &name, QColor colour)
    {
        QImage img(24, 16, QImage::Format_RGB32);
        img.fill(colour);
        const QString p = m_files.path() + QLatin1Char('/') + name;
        if (!img.save(p, "PNG")) return {};
        return p;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_cfg.isValid() && m_studio.isValid() && m_files.isValid());
        // ppSettings() is IniFormat/UserScope: point the whole scope at the scratch folder before the
        // first QSettings is made, so nothing here can reach the real settings file.
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_cfg.path());
        qputenv("PINPOINT_STUDIO_DIR", m_studio.path().toUtf8());
        QCOMPARE(QFileInfo(ppSettings().fileName()).absoluteFilePath(), QFileInfo(iniPath()).absoluteFilePath());
    }

    void init()
    {
        QSettings s = ppSettings();
        s.clear();
        s.sync();
        QDir(m_studio.path()).removeRecursively();
        QDir().mkpath(m_studio.path());
    }

    void defaults()
    {
        StudioProfile p;
        QVERIFY(p.studioName().isEmpty());
        QVERIFY(p.coachName().isEmpty());
        QVERIFY(p.coachEmail().isEmpty());
        QVERIFY(p.logoPath().isEmpty());
        QVERIFY(p.coachPhotoPath().isEmpty());
        QCOMPARE(p.accent(), QStringLiteral("#2573d1"));
        QCOMPARE(p.monogram(), QStringLiteral("PS"));

        const QVariantList steps = p.practiceSteps();
        QCOMPARE(steps.size(), 4);
        QCOMPARE(steps[0].toMap().value("head").toString(), QStringLiteral("Rehearse without a ball"));
        QCOMPARE(steps[3].toMap().value("body").toString(),
                 QStringLiteral("Stay with work-on 1 until your next session. The rest of the list waits."));

        QCOMPARE(p.emailSubject(), QStringLiteral("Your session report — {date}"));
        QVERIFY(p.emailBody().startsWith(QStringLiteral("Hi {first},\n\nHere is your report from today's session.")));
        QVERIFY(p.emailBody().endsWith(QStringLiteral("{coach}\n{studio}")));

        const QVariantList missing = p.missingDetails();
        QCOMPARE(missing.size(), 3);
        for (const QVariant &m : missing) QCOMPARE(m.toMap().value("where").toString(), QStringLiteral("studio"));
        QCOMPARE(missing[0].toMap().value("text").toString(),
                 QStringLiteral("No studio name yet — add it in Settings → Studio"));
        QVERIFY(!p.stamp().isEmpty());

        // Nothing is written until something is set.
        QVERIFY(!QFileInfo::exists(iniPath()) || ppSettings().childGroups().isEmpty());
    }

    void roundTrip()
    {
        QVariantList steps;
        steps << QVariantMap{{"head", "Step one"}, {"body", "Do this."}}
              << QVariantMap{{"head", "Step two"}, {"body", "Then this."}, {"junk", 7}};
        {
            StudioProfile p;
            QSignalSpy spy(&p, &StudioProfile::changed);
            p.setStudioName(QStringLiteral("  Northfield Golf Studio  "));
            p.setAccent(QStringLiteral("1F5A4C"));
            p.setAddress(QStringLiteral("1 High Street, Northfield"));
            p.setPhone(QStringLiteral("01234 567890"));
            p.setEmail(QStringLiteral("hello@northfieldgolf.example"));
            p.setWebsite(QStringLiteral("northfieldgolf.example"));
            p.setBooking(QStringLiteral("Book your next session at northfieldgolf.example."));
            p.setPracticeSteps(steps);
            p.setEmailSubject(QStringLiteral("Your report, {first}"));
            p.setEmailBody(QStringLiteral("Hello {first},\n\nAttached.\n\n{coach}"));
            p.setCoachName(QStringLiteral("Alex Morgan"));
            p.setCoachTitle(QStringLiteral("Head coach"));
            p.setCoachEmail(QStringLiteral("alex@northfieldgolf.example"));
            p.setCoachPhone(QStringLiteral("07700 900123"));
            QCOMPARE(spy.count(), 14);
            p.setStudioName(QStringLiteral("Northfield Golf Studio"));    // unchanged once trimmed
            QCOMPARE(spy.count(), 14);
            QVERIFY(p.missingDetails().isEmpty());
        }
        StudioProfile q;
        QCOMPARE(q.studioName(), QStringLiteral("Northfield Golf Studio"));
        QCOMPARE(q.accent(), QStringLiteral("#1f5a4c"));
        QCOMPARE(q.address(), QStringLiteral("1 High Street, Northfield"));
        QCOMPARE(q.phone(), QStringLiteral("01234 567890"));
        QCOMPARE(q.email(), QStringLiteral("hello@northfieldgolf.example"));
        QCOMPARE(q.website(), QStringLiteral("northfieldgolf.example"));
        QCOMPARE(q.booking(), QStringLiteral("Book your next session at northfieldgolf.example."));
        QCOMPARE(q.practiceSteps().size(), 2);
        QCOMPARE(q.practiceSteps()[1].toMap().value("body").toString(), QStringLiteral("Then this."));
        QVERIFY(!q.practiceSteps()[1].toMap().contains("junk"));
        QCOMPARE(q.emailSubject(), QStringLiteral("Your report, {first}"));
        QCOMPARE(q.emailBody(), QStringLiteral("Hello {first},\n\nAttached.\n\n{coach}"));
        QCOMPARE(q.coachName(), QStringLiteral("Alex Morgan"));
        QCOMPARE(q.coachTitle(), QStringLiteral("Head coach"));
        QCOMPARE(q.coachEmail(), QStringLiteral("alex@northfieldgolf.example"));
        QCOMPARE(q.coachPhone(), QStringLiteral("07700 900123"));
        QCOMPARE(q.monogram(), QStringLiteral("NG"));

        // Back to the defaults: the override keys go, so a later build's default reaches this studio.
        q.setPracticeSteps(q.defaultPracticeSteps());
        q.setEmailSubject(QString());
        q.setEmailBody(q.defaultEmailBody());
        q.setAccent(QString());
        QSettings s = ppSettings();
        s.beginGroup("Studio");
        QVERIFY(!s.contains("practiceSteps"));
        QVERIFY(!s.contains("emailSubject"));
        QVERIFY(!s.contains("emailBody"));
        QVERIFY(!s.contains("accent"));
        QVERIFY(s.contains("name"));
        QCOMPARE(q.practiceSteps().size(), 4);
        QCOMPARE(q.accent(), QStringLiteral("#2573d1"));
    }

    void fill()
    {
        StudioProfile p;
        const QVariantMap v{{"first", "Nick"}, {"date", "20 October 2026"}, {"coach", "Alex"}, {"studio", "Northfield"}};
        QCOMPARE(p.fill(p.emailSubject(), v), QStringLiteral("Your session report — 20 October 2026"));
        QVERIFY(p.fill(p.emailBody(), v).startsWith(QStringLiteral("Hi Nick,")));
        QVERIFY(p.fill(p.emailBody(), v).endsWith(QStringLiteral("Alex\nNorthfield")));
        QCOMPARE(p.fill(QStringLiteral("{frist}"), v), QStringLiteral("{frist}"));
    }

    void logoImport()
    {
        const QString red  = makePng(QStringLiteral("red.png"), Qt::red);
        const QString blue = makePng(QStringLiteral("blue.png"), Qt::blue);
        QVERIFY(!red.isEmpty() && !blue.isEmpty());

        StudioProfile p;
        const QString before = p.stamp();
        QVERIFY(p.importLogo(red));
        const QString dest = m_studio.path() + QStringLiteral("/logo.png");
        QCOMPARE(QFileInfo(p.logoPath()).absoluteFilePath(), QFileInfo(dest).absoluteFilePath());
        QVERIFY(QFile::exists(dest));
        QCOMPARE(QImage(dest).pixelColor(0, 0), QColor(Qt::red));
        const QString afterRed = p.stamp();
        QVERIFY(afterRed != before);

        // Same file name, different bytes: the stamp still moves.
        QVERIFY(p.importLogo(blue));
        QCOMPARE(QImage(dest).pixelColor(0, 0), QColor(Qt::blue));
        QVERIFY(p.stamp() != afterRed);

        // Re-importing the copy itself must not delete its own source.
        QVERIFY(p.importLogo(p.logoPath()));
        QVERIFY(QFile::exists(dest));

        // Another extension replaces the PNG; the folder holds one logo.
        const QString svg = m_files.path() + QStringLiteral("/mark.svg");
        { QFile f(svg); QVERIFY(f.open(QIODevice::WriteOnly));
          f.write("<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"><rect width=\"10\" height=\"10\" fill=\"#123\"/></svg>"); }
        QVERIFY(p.importLogo(QUrl::fromLocalFile(svg).toString()));   // a file:// URL from QML works too
        QVERIFY(p.logoPath().endsWith(QStringLiteral("/logo.svg")));
        QVERIFY(!QFile::exists(dest));

        // Refusals change nothing.
        const QString keep = p.logoPath(), keepStamp = p.stamp();
        const QString notImage = m_files.path() + QStringLiteral("/fake.png");
        { QFile f(notImage); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("not a png at all"); }
        QVERIFY(!p.importLogo(notImage));
        QVERIFY(!p.importLogo(m_files.path() + QStringLiteral("/missing.png")));
        const QString gif = m_files.path() + QStringLiteral("/x.gif");
        QVERIFY(QFile::copy(red, gif));
        QVERIFY(!p.importLogo(gif));
        QCOMPARE(p.logoPath(), keep);
        QCOMPARE(p.stamp(), keepStamp);

        // The original going does not matter: the report reads the copy.
        QVERIFY(QFile::remove(svg));
        { StudioProfile q; QCOMPARE(q.logoPath(), keep); }

        p.clearLogo();
        QVERIFY(p.logoPath().isEmpty());
        QVERIFY(!QFile::exists(keep));
        { StudioProfile q; QVERIFY(q.logoPath().isEmpty()); }

        // The coach's photo, the same way, under its own name.
        QVERIFY(p.importCoachPhoto(red));
        QVERIFY(p.coachPhotoPath().endsWith(QStringLiteral("/coach.png")));
        { StudioProfile q; QCOMPARE(q.coachPhotoPath(), p.coachPhotoPath()); }
        const QString jpg = m_files.path() + QStringLiteral("/coach.jpeg");
        QVERIFY(QImage(red).save(jpg, "JPEG"));
        QVERIFY(p.importCoachPhoto(jpg));
        QVERIFY(p.coachPhotoPath().endsWith(QStringLiteral("/coach.jpg")));
        QVERIFY(!QFile::exists(m_studio.path() + QStringLiteral("/coach.png")));
        p.clearCoachPhoto();
        QVERIFY(p.coachPhotoPath().isEmpty());
    }

    void stampMovesWithEachField()
    {
        StudioProfile p;
        QString last = p.stamp();
        auto moved = [&](const char *what) {
            const QString now = p.stamp();
            if (now == last) qWarning("stamp did not move for %s", what);
            const bool ok = now != last;
            last = now;
            return ok;
        };
        p.setStudioName("A");                       QVERIFY(moved("studioName"));
        p.setAccent("#8a2433");                     QVERIFY(moved("accent"));
        p.setAddress("addr");                       QVERIFY(moved("address"));
        p.setPhone("1");                            QVERIFY(moved("phone"));
        p.setEmail("a@b.co");                       QVERIFY(moved("email"));
        p.setWebsite("b.co");                       QVERIFY(moved("website"));
        p.setBooking("Book");                       QVERIFY(moved("booking"));
        p.setPracticeSteps({ QVariantMap{{"head", "H"}, {"body", "B"}} });  QVERIFY(moved("practiceSteps"));
        p.setCoachName("C");                        QVERIFY(moved("coachName"));
        p.setCoachTitle("T");                       QVERIFY(moved("coachTitle"));
        p.setCoachEmail("c@b.co");                  QVERIFY(moved("coachEmail"));
        p.setCoachPhone("2");                       QVERIFY(moved("coachPhone"));
        QVERIFY(p.importLogo(makePng("s1.png", Qt::green)));          QVERIFY(moved("logo"));
        QVERIFY(p.importCoachPhoto(makePng("s2.png", Qt::yellow)));   QVERIFY(moved("coachPhoto"));

        // The covering email is not in the PDF: editing it leaves every report current.
        p.setEmailSubject("Subject {date}");
        p.setEmailBody("Body {first}");
        QCOMPARE(p.stamp(), last);

        // Deterministic: a fresh instance over the same INI and files computes the same stamp.
        StudioProfile q;
        QCOMPARE(q.stamp(), p.stamp());
    }

    void monogram_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("mono");
        QTest::newRow("empty")        << ""                        << "PS";
        QTest::newRow("pinpoint")     << "PinPoint Studio"         << "PS";
        QTest::newRow("three words")  << "Northfield Golf Studio"  << "NG";
        QTest::newRow("leading the")  << "The Golf Lab"            << "GL";
        QTest::newRow("one word")     << "fairway"                 << "F";
        QTest::newRow("punctuation")  << "St. Andrews Golf"        << "SA";
        QTest::newRow("ampersand")    << "Smith & Jones Golf"      << "SJ";
        QTest::newRow("hyphen")       << "Lake-side academy"       << "LS";
        QTest::newRow("all stop")     << "The A"                   << "TA";
        QTest::newRow("accented")     << "école Golf"              << "ÉG";
    }
    void monogram()
    {
        QFETCH(QString, name);
        QFETCH(QString, mono);
        StudioProfile p;
        p.setStudioName(name);
        QCOMPARE(p.monogram(), mono);
    }

    void accent()
    {
        StudioProfile p;
        QVERIFY(p.isColour("#1F5A4C"));
        QVERIFY(p.isColour("1f5a4c"));
        QVERIFY(p.isColour("darkgreen"));
        QVERIFY(!p.isColour("#12"));
        QVERIFY(!p.isColour("blurple"));
        QVERIFY(!p.isColour(""));

        p.setAccent("DarkGreen");
        QCOMPARE(p.accent(), QStringLiteral("#006400"));
        p.setAccent("#12");                         // refused: unchanged
        QCOMPARE(p.accent(), QStringLiteral("#006400"));

        QVERIFY(p.accentTooLight("#ffffff"));
        QVERIFY(p.accentTooLight("#ffd700"));       // gold on white
        QVERIFY(p.accentTooLight("#a0c4ff"));       // pale blue
        QVERIFY(!p.accentTooLight("#000000"));
        QVERIFY(!p.accentTooLight("#1f5a4c"));
        QVERIFY(!p.accentTooLight("#767676"));      // 4.5:1, comfortably over
        QVERIFY(p.accentTooLight("#a0a0a0"));       // 2.6:1
        QVERIFY(!p.accentTooLight("nonsense"));

        const QStringList presets = p.accentPresets();
        QVERIFY(presets.size() >= 6);
        QCOMPARE(presets.first(), QStringLiteral("#2573d1"));
        for (const QString &c : presets) {
            QVERIFY2(p.isColour(c), qPrintable(c));
            QVERIFY2(!p.accentTooLight(c), qPrintable(c));
        }
    }

    // ── the golfer's report details on AthleteController ─────────────────────────────────────────

    void athleteReportFields()
    {
        QString uuid;
        {
            AthleteController a;
            uuid = a.saveAthlete(QString(), QStringLiteral("Nick Henn"));
            QVERIFY(!uuid.isEmpty());
            QVERIFY(a.updateAthlete(uuid, "reportEmail", QStringLiteral("nick@example.com, parent@example.co.uk")));
            QVERIFY(a.updateAthlete(uuid, "dob", QStringLiteral("2012-03-04")));
            const QVariantMap d = a.reportDetails(uuid);
            QCOMPARE(d.value("uuid").toString(), uuid);
            QCOMPARE(d.value("name").toString(), QStringLiteral("Nick Henn"));
            QCOMPARE(d.value("email").toString(), QStringLiteral("nick@example.com, parent@example.co.uk"));
            QCOMPARE(d.value("greeting").toString(), QStringLiteral("Nick"));      // first word of the name
            QCOMPARE(d.value("dob").toString(), QStringLiteral("2012-03-04"));
            QCOMPARE(d.value("photosApproved").toBool(), false);
            QVERIFY(d.value("photosApprovedOn").toString().isEmpty());
            // The current athlete (saveAthlete selects a new one) mirrors them.
            QCOMPARE(a.currentReportEmail(), d.value("email").toString());
            QCOMPARE(a.currentReportGreeting(), QStringLiteral("Nick"));
            QVERIFY(a.currentIsJunior());
            QVERIFY(a.updateAthlete(uuid, "reportGreeting", QStringLiteral("Nicky")));
        }
        AthleteController b;                                   // a fresh reload() sees all of it
        QVariantMap d = b.reportDetails(QStringLiteral("  nick henn "));
        QCOMPARE(d.value("uuid").toString(), uuid);
        QCOMPARE(d.value("greeting").toString(), QStringLiteral("Nicky"));
        QCOMPARE(b.currentReportGreeting(), QStringLiteral("Nicky"));
        QVERIFY(b.reportDetails(QStringLiteral("Nobody")).isEmpty());
        QVERIFY(b.reportDetails(QString()).isEmpty());

        // Approval: ticking stamps today; ticking again keeps the first date; unticking clears it.
        QSignalSpy spy(&b, &AthleteController::athletesChanged);
        b.setPhotosApproved(uuid, true);
        QVERIFY(spy.count() > 0);
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        d = b.reportDetails(uuid);
        QCOMPARE(d.value("photosApproved").toBool(), true);
        QCOMPARE(d.value("photosApprovedOn").toString(), today);
        QVERIFY(b.currentPhotosApproved());
        QCOMPARE(b.currentPhotosApprovedOn(), today);
        {   // backdate the stored date, then tick again: the date the consent was given survives
            QSettings s = ppSettings();
            s.setValue(QStringLiteral("athletes/%1/reportPhotosApprovedOn").arg(uuid), QStringLiteral("2026-01-02"));
            s.sync();
        }
        b.setPhotosApproved(uuid, true);
        QCOMPARE(b.reportDetails(uuid).value("photosApprovedOn").toString(), QStringLiteral("2026-01-02"));
        b.setPhotosApproved(uuid, false);
        d = b.reportDetails(uuid);
        QCOMPARE(d.value("photosApproved").toBool(), false);
        QVERIFY(d.value("photosApprovedOn").toString().isEmpty());
        b.setPhotosApproved(QStringLiteral("no-such-uuid"), true);    // no record is created
        QVERIFY(!ppSettings().childGroups().isEmpty());
        { QSettings s = ppSettings(); s.beginGroup("athletes"); QVERIFY(!s.childGroups().contains("no-such-uuid")); }

        // Deleting the athlete deletes the report details with the rest of the group.
        b.setPhotosApproved(uuid, true);
        QVERIFY(b.deleteAthlete(uuid));
        QSettings s = ppSettings();
        QVERIFY(!s.contains(QStringLiteral("athletes/%1/reportEmail").arg(uuid)));
        QVERIFY(!s.contains(QStringLiteral("athletes/%1/reportPhotosApprovedOn").arg(uuid)));
        QVERIFY(b.reportDetails(uuid).isEmpty());
    }

    void juniorOnTheSessionDate()
    {
        AthleteController a;
        auto ms = [](int y, int m, int d) { return QDateTime(QDate(y, m, d), QTime(12, 0)).toMSecsSinceEpoch(); };
        QVERIFY(a.isJuniorOn("2008-10-21", ms(2026, 10, 20)));    // turns 18 tomorrow
        QVERIFY(!a.isJuniorOn("2008-10-20", ms(2026, 10, 20)));   // 18 today
        QVERIFY(!a.isJuniorOn("2008-10-19", ms(2026, 10, 20)));
        QVERIFY(a.isJuniorOn("2008-10-20", ms(2025, 6, 1)));      // an old session keeps its answer
        QVERIFY(!a.isJuniorOn("", ms(2026, 10, 20)));             // no date of birth: an adult
        QVERIFY(!a.isJuniorOn("20/10/2012", ms(2026, 10, 20)));   // unparseable: an adult
        const QString tenYearsAgo = QDate::currentDate().addYears(-10).toString(Qt::ISODate);
        QVERIFY(a.isJuniorOn(tenYearsAgo, 0));                    // 0 = today
    }

    void emailShape_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::newRow("one")               << "nick@example.com"                      << true;
        QTest::newRow("subdomain")         << "nick@mail.example.co.uk"               << true;
        QTest::newRow("plus")              << "nick+golf@example.com"                 << true;
        QTest::newRow("two")               << "nick@example.com, mum@example.org"     << true;
        QTest::newRow("two no space")      << "nick@example.com,mum@example.org"      << true;
        QTest::newRow("typing the next")   << "nick@example.com, "                    << true;
        QTest::newRow("empty")             << ""                                      << false;
        QTest::newRow("no at")             << "nick.example.com"                      << false;
        QTest::newRow("no tld")            << "nick@example"                          << false;
        QTest::newRow("short tld")         << "nick@example.c"                        << false;
        QTest::newRow("space inside")      << "nick henn@example.com"                 << false;
        QTest::newRow("two ats")           << "nick@@example.com"                     << false;
        QTest::newRow("one bad of two")    << "nick@example.com, mum@"                << false;
        QTest::newRow("leading comma")     << ", nick@example.com"                    << false;
        QTest::newRow("double comma")      << "a@b.com,, c@d.com"                     << false;
        QTest::newRow("semicolon")         << "a@b.com; c@d.com"                      << false;
    }
    void emailShape()
    {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        AthleteController a;
        QCOMPARE(a.validReportEmail(text), ok);
    }
};

QTEST_GUILESS_MAIN(ReportStudioTest)
#include "report_studio_test.moc"
