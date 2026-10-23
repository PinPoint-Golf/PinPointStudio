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

// report_list_test — the golfer's session list (src/Gui/report/session_report_list_model) over a temp
// library of fake sessions, and the mail hand-off's platform-free half (report_mailer.cpp).
//
// The library: six folders for one golfer —
//   2026-10-12_G_01   3 swings, none indexed (no summary): dated from the folder name, no clubs
//   2026-10-13_G_01   1 swing: Open session only
//   2026-10-20_G_01   3 swings at 09:00, a CURRENT work-on record, a current report
//   2026-10-20_G_02   4 swings at 11:00, a record from an OLDER rule, a report gone out of date
//   2026-10-20_G_03   5 swings at 14:00, no record, a report opened in Mail
//   2026-10-20_G_04   no swings: not a session
// and the report state through report_meta.h, as SessionReportModel::state() will give it.

#include "report/session_report_list_model.h"
#include "report/report_mailer.h"
#include "report/mail_credentials.h"
#include "report/report_meta.h"
#include "../../Coaching/work_ons.h"
#include "../../Export/swing_doc.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

using namespace pinpoint::report;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// The platform half is the test's: it records what it was handed and "opens" nothing.
static QStringList g_platformTo;
static QString     g_platformFile;
namespace pinpoint::report {
QString platformCompose(const QStringList &to, const QString &, const QString &, const QString &, const QString &filePath)
{
    g_platformTo = to;
    g_platformFile = filePath;
    return {};
}
// The password store's platform half is the test's too, and refuses: nothing here may reach the keychain
// (PINPOINT_MAIL_CREDENTIALS=memory is set in main, so these are never called).
bool    platformStoreMailPassword(const QString &, const QString &, QString *e) { if (e) *e = QStringLiteral("test"); return false; }
QString platformReadMailPassword(const QString &) { return {}; }
bool    platformHasMailPassword(const QString &) { return false; }
bool    platformForgetMailPassword(const QString &) { return true; }
}

static void touch(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
}

static qint64 at(int month, int day, int h, int m)
{
    return QDateTime(QDate(2026, month, day), QTime(h, m)).toMSecsSinceEpoch();
}

// n swings, each a stub swing.json with (when indexed) a fresh summary sidecar saying when, which club
// and what recorded it.
static void makeSession(const QString &dir, int n, qint64 startMs, const QStringList &clubs,
                        const QStringList &devices, bool indexed = true)
{
    QDir().mkpath(dir);
    for (int i = 1; i <= n; ++i) {
        const QString sd = dir + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'));
        touch(sd + QStringLiteral("/swing.json"), "{}");
        if (!indexed) continue;
        pinpoint::PersistedShot ps;
        ps.ok          = true;
        ps.swingDir    = sd;
        ps.ordinal     = i;
        ps.wallclockMs = startMs + qint64(i - 1) * 60'000;
        ps.club        = clubs.at((i - 1) % clubs.size());
        ps.devices     = devices;
        pinpoint::SwingDocReader::writeSwingSummary(ps);
    }
}

static void writeRecord(const QString &dir, const QString &id, const QString &name, int ruleVersion)
{
    pinpoint::analysis::SessionWorkOns rec;
    rec.sessionId = QFileInfo(dir).fileName();
    pinpoint::analysis::WorkOnEntry e;
    e.id = QStringLiteral("other_one"); e.name = QStringLiteral("Second"); e.rank = 2;
    rec.entries.push_back(e);
    e.id = id; e.name = name; e.rank = 1;
    rec.entries.push_back(e);
    QJsonObject root = pinpoint::analysis::toJson(rec);
    root[QStringLiteral("ruleVersion")] = ruleVersion;
    touch(dir + QStringLiteral("/work_ons.json"), QJsonDocument(root).toJson());
}

static QString fp(const QString &dir) { return reportFingerprint(dir, 4, QStringLiteral("content"), QStringLiteral("inputs")); }

static void writeReport(const QString &dir, bool current, qint64 preparedMs, qint64 sendMs = 0)
{
    const QString pdf = QStringLiteral("2026-10-20_G_session-report.pdf");
    touch(reportDir(dir) + QLatin1Char('/') + pdf, QByteArray(2048, 'x'));
    ReportMeta m;
    m.pdfName     = pdf;
    m.preparedMs  = preparedMs;
    m.preparedHistory = { preparedMs };
    m.sha256      = fileSha256(reportDir(dir) + QLatin1Char('/') + pdf);
    m.sizeBytes   = 2048;
    m.fingerprint = current ? fp(dir) : QStringLiteral("0123456789abcdef");
    if (sendMs) m.sends.append({ sendMs, QStringLiteral("mail-app"), m.sha256 });
    writeReportMeta(dir, m);
}

// What SessionReportModel::state() gives, from report_meta.h.
static QVariantMap stateOf(const QString &dir)
{
    const auto m = readReportMeta(dir);
    const ReportState st = reportState(dir, m, fp(dir));
    return { {QStringLiteral("state"), st == ReportState::Current ? QStringLiteral("current")
                                     : st == ReportState::Stale   ? QStringLiteral("stale") : QStringLiteral("none")},
             {QStringLiteral("words"), reportStateWords(st, m)},
             {QStringLiteral("pdfPath"), m ? reportPdfPath(dir, *m) : QString()},
             {QStringLiteral("sizeBytes"), double(m ? m->sizeBytes : 0)},
             {QStringLiteral("preparedMs"), double(m ? m->preparedMs : 0)} };
}

static QVariant role(const SessionReportListModel &m, int row, int role) { return m.data(m.index(row), role); }

int main(int argc, char **argv)
{
    qputenv("PINPOINT_MAIL_DRYRUN", "0");
    qputenv("PINPOINT_MAIL_CREDENTIALS", "memory");
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString golfer = tmp.filePath(QStringLiteral("G"));
    const QString s12 = golfer + QStringLiteral("/2026-10-12_G_01");
    const QString s13 = golfer + QStringLiteral("/2026-10-13_G_01");
    const QString b1  = golfer + QStringLiteral("/2026-10-20_G_01");
    const QString b2  = golfer + QStringLiteral("/2026-10-20_G_02");
    const QString b3  = golfer + QStringLiteral("/2026-10-20_G_03");
    makeSession(s12, 3, 0, { QStringLiteral("7 IRON") }, {}, /*indexed=*/false);
    makeSession(s13, 1, at(10, 13, 10, 0), { QStringLiteral("7 IRON") }, { QStringLiteral("faceOn") });
    makeSession(b1, 3, at(10, 20, 9, 0), { QStringLiteral("7 IRON") },
                { QStringLiteral("faceOn"), QStringLiteral("dtl"), QStringLiteral("lm:gcquad") });
    makeSession(b2, 4, at(10, 20, 11, 0), { QStringLiteral("DRIVER"), QStringLiteral("7 IRON") }, { QStringLiteral("faceOn") });
    makeSession(b3, 5, at(10, 20, 14, 0), { QStringLiteral("7 IRON") }, { QStringLiteral("dtl"), QStringLiteral("faceOn") });
    QDir().mkpath(golfer + QStringLiteral("/2026-10-20_G_04"));
    writeRecord(b1, QStringLiteral("trail_knee_straighten"), QStringLiteral("Trail knee straightens"), pinpoint::analysis::kWorkOnRuleVersion);
    writeRecord(b2, QStringLiteral("early_extension"), QStringLiteral("Early extension"), pinpoint::analysis::kWorkOnRuleVersion - 1);
    writeReport(b1, /*current=*/true,  at(10, 22, 9, 30));
    writeReport(b2, /*current=*/false, at(10, 20, 12, 0));
    writeReport(b3, /*current=*/true,  at(10, 20, 15, 0), at(10, 20, 18, 40));

    std::printf("scan, order and session of the day\n");
    SessionReportListModel list;
    list.setSynchronous(true);
    list.setStateFunction(stateOf);
    list.setGoalResolver([](const QString &id) {
        return id == QLatin1String("trail_knee_straighten") ? QStringLiteral("Keep the flex in your trail knee") : QString();
    });
    list.setAthleteDir(golfer);
    using R = SessionReportListModel;
    check(list.count() == 5, "five sessions (the empty folder is not one)");
    const QStringList want{ b3, b2, b1, s13, s12 };
    bool order = list.count() == 5;
    for (int i = 0; order && i < 5; ++i) order = role(list, i, R::DirRole).toString() == QFileInfo(want[i]).absoluteFilePath();
    check(order, "newest first, by the first swing's time (the folder date when no swing says)");
    check(role(list, 0, R::SessionOfDayRole).toInt() == 3 && role(list, 1, R::SessionOfDayRole).toInt() == 2
          && role(list, 2, R::SessionOfDayRole).toInt() == 1, "20 Oct: sessions 3, 2, 1, counted in start order");
    check(role(list, 3, R::SessionOfDayRole).toInt() == 1 && role(list, 4, R::SessionOfDayRole).toInt() == 1,
          "a day with one session: Session 1");
    check(role(list, 0, R::DateLabelRole).toString() == QLatin1String("Tue 20 Oct 2026"), "date label: Tue 20 Oct 2026");
    check(role(list, 4, R::DateLabelRole).toString() == QLatin1String("Mon 12 Oct 2026"), "un-indexed session dated from its folder");

    std::printf("summary\n");
    check(role(list, 0, R::SwingsRole).toInt() == 5 && role(list, 3, R::SwingsRole).toInt() == 1, "swing counts");
    check(role(list, 4, R::SwingsRole).toInt() == 3, "un-indexed swings still count");
    check(role(list, 1, R::ClubsRole).toString() == QLatin1String("Driver, 7 iron"), "clubs, first-seen order, comma-joined");
    check(role(list, 2, R::DevicesRole).toStringList() == QStringList({ "Face-on", "DTL", "GC Quad" }),
          "devices: the drawer's labels in its order");
    check(role(list, 0, R::DevicesRole).toStringList() == QStringList({ "Face-on", "DTL" }), "devices sorted, not as recorded");
    check(role(list, 2, R::DeviceTokensRole).toStringList() == QStringList({ "faceOn", "dtl", "lm:gcquad" }),
          "device tokens beside the labels, same order");

    std::printf("device filter\n");
    auto ranks = [&] {
        QList<int> out;
        for (int i = 0; i < list.count(); ++i) out << role(list, i, R::ShownRankRole).toInt();
        return out;
    };
    check(ranks() == QList<int>({ 0, 1, 2, 3, 4 }) && list.shownCount() == 5, "no filter: every row shown, in order");
    check(list.savedCount() == 5 && list.shownSavedCount() == 5, "five saved sessions");
    {
        const QVariantList chips = list.availableDevices();
        QStringList ids; QList<int> counts;
        for (const QVariant &c : chips) { ids << c.toMap().value("id").toString(); counts << c.toMap().value("count").toInt(); }
        check(ids == QStringList({ "faceOn", "dtl", "lm:gcquad" }) && counts == QList<int>({ 4, 2, 1 }),
              "chips: every device a session used, drawer order, with how many sessions each would leave");
    }
    list.toggleDevice(QStringLiteral("dtl"));
    check(list.count() == 5, "filtering leaves the rows in place");
    check(ranks() == QList<int>({ 0, -1, 1, -1, -1 }) && list.shownCount() == 2 && list.shownSavedCount() == 2,
          "DTL: the two sessions that used it, ranked in order");
    list.toggleDevice(QStringLiteral("lm:gcquad"));
    check(ranks() == QList<int>({ -1, -1, 0, -1, -1 }), "DTL and GC Quad: AND, one session");
    list.setLiveSessionDir(b2);
    check(role(list, 0, R::IsLiveRole).toBool() && role(list, 0, R::ShownRankRole).toInt() == 0,
          "the live session is never filtered out");
    list.setLiveSessionDir(QString());
    list.clearDeviceFilter();
    check(ranks() == QList<int>({ 0, 1, 2, 3, 4 }) && list.deviceFilter().isEmpty(), "Clear: every row back");

    std::printf("work-on 1\n");
    check(role(list, 2, R::WorkOn1Role).toString() == QLatin1String("Keep the flex in your trail knee"),
          "the rank-1 entry's goal through the resolver");
    check(role(list, 2, R::WorkOnKnownRole).toBool(), "a current record is known");
    check(!role(list, 1, R::WorkOnKnownRole).toBool() && role(list, 1, R::WorkOn1Role).toString().isEmpty(),
          "a record from an older rule is not quoted");
    check(!role(list, 0, R::WorkOnKnownRole).toBool(), "no record: not known");
    {
        SessionReportListModel noGoal;
        noGoal.setSynchronous(true);
        noGoal.setAthleteDir(golfer);
        check(noGoal.data(noGoal.index(2), R::WorkOn1Role).toString() == QLatin1String("Trail knee straightens"),
              "no resolver: the record's own name");
    }

    std::printf("canReport and live\n");
    check(role(list, 0, R::CanReportRole).toBool() && role(list, 2, R::CanReportRole).toBool(), "three or more swings: reportable");
    check(!role(list, 3, R::CanReportRole).toBool(), "one swing: not reportable");
    list.setLiveSessionDir(b1);
    check(role(list, 0, R::DirRole).toString() == QFileInfo(b1).absoluteFilePath(), "the live session goes to the top");
    check(role(list, 0, R::IsLiveRole).toBool() && !role(list, 0, R::CanReportRole).toBool(), "live: not reportable");
    check(!role(list, 1, R::IsLiveRole).toBool(), "only one live row");
    list.setLiveSessionDir(QString());
    check(role(list, 2, R::DirRole).toString() == QFileInfo(b1).absoluteFilePath() && role(list, 2, R::CanReportRole).toBool(),
          "the session ended: back in its place, reportable");

    std::printf("report state\n");
    const QLocale c = QLocale::c();
    const QString openedWords = QStringLiteral("Opened in Mail ")
                              + c.toString(QDateTime::fromMSecsSinceEpoch(at(10, 20, 18, 40)), QStringLiteral("d MMM HH:mm"));
    check(role(list, 0, R::StateWordsRole).toString() == openedWords, "a report handed to Mail: Opened in Mail 20 Oct 18:40");
    check(role(list, 1, R::StateRole).toString() == QLatin1String("stale")
          && role(list, 1, R::StateWordsRole).toString() == QStringLiteral("Out of date — remake"), "out of date — remake");
    check(role(list, 2, R::StateRole).toString() == QLatin1String("current")
          && role(list, 2, R::StateWordsRole).toString() == QLatin1String("Report prepared 22 Oct"),
          "current: the PREPARED date, not the session's");
    check(role(list, 2, R::SizeBytesRole).toDouble() == 2048 && role(list, 2, R::PdfPathRole).toString().endsWith(".pdf"),
          "size and path from the record");
    check(role(list, 3, R::StateRole).toString() == QLatin1String("none")
          && role(list, 3, R::StateWordsRole).toString() == QLatin1String("No report"), "no report");
    // A swing re-analysed: the report goes out of date, and one row is re-asked without a re-scan.
    touch(b1 + QStringLiteral("/swing_0002/swing.json"), "{ }");
    list.refreshState(b1);
    check(role(list, 2, R::StateRole).toString() == QLatin1String("stale"), "a swing changed: refreshState makes it stale");
    {
        // The wave-0 model says only {state}; the words are report_meta.h's.
        SessionReportListModel bare;
        bare.setSynchronous(true);
        bare.setStateFunction([](const QString &d) {
            return QVariantMap{ {QStringLiteral("state"), d.endsWith(QLatin1String("_02")) ? QStringLiteral("stale") : QStringLiteral("none")} };
        });
        bare.setAthleteDir(golfer);
        check(bare.data(bare.index(1), R::StateWordsRole).toString() == QStringLiteral("Out of date — remake")
              && bare.data(bare.index(0), R::StateWordsRole).toString() == QLatin1String("No report"),
              "a state without words takes report_meta's");
    }
    const QVariantMap row = list.rowFor(b3);
    check(row.value(QStringLiteral("swings")).toInt() == 5 && row.value(QStringLiteral("sessionOfDay")).toInt() == 3,
          "rowFor(dir) gives the row as a map");

    std::printf("asynchronous scan\n");
    {
        SessionReportListModel async;
        async.setStateFunction(stateOf);
        async.setAthleteDir(golfer);
        check(async.scanning(), "scanning while the pool works");
        QElapsedTimer t; t.start();
        while ((async.scanning() || async.data(async.index(4), R::StateRole).toString().isEmpty()) && t.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(5);
        }
        check(async.count() == 5 && !async.scanning(), "the scan lands off the GUI thread");
        check(async.data(async.index(4), R::StateKnownRole).toBool(), "every row's state is asked, turn by turn");
        // A golfer changed mid-scan: the first golfer's rows never land under the second's name.
        async.setAthleteDir(golfer);           // same: no-op
        async.setAthleteDir(tmp.filePath(QStringLiteral("nobody")));
        check(async.count() == 0, "a new golfer starts empty");
        t.restart();
        while (async.scanning() && t.elapsed() < 10000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); QThread::msleep(5); }
        check(async.count() == 0, "...and stays empty when they have no sessions");
    }

    std::printf("mail hand-off\n");
    {
        const QString pdf = reportDir(b1) + QStringLiteral("/2026-10-20_G_session-report.pdf");
        ReportMailer mailer;
        QVariantMap r = mailer.compose({ QStringLiteral("  ") }, QString(), QStringLiteral("S"), QStringLiteral("B"), pdf);
        check(!r.value(QStringLiteral("ok")).toBool() && !r.value(QStringLiteral("error")).toString().isEmpty(),
              "no address: refused, with words");
        r = mailer.compose({ QStringLiteral("nick@example.com") }, QString(), QStringLiteral("S"), QStringLiteral("B"),
                           pdf + QStringLiteral(".gone"));
        check(!r.value(QStringLiteral("ok")).toBool(), "no file: refused");
        r = mailer.compose({ QStringLiteral(" nick@example.com ") }, QStringLiteral("coach@example.com"), QStringLiteral("S"),
                           QStringLiteral("B"), pdf);
        check(r.value(QStringLiteral("ok")).toBool() && r.value(QStringLiteral("method")).toString() == QLatin1String("mail-app")
              && g_platformTo == QStringList{ QStringLiteral("nick@example.com") } && g_platformFile == QFileInfo(pdf).absoluteFilePath(),
              "not a dry run: the platform half gets the trimmed address and the file");
        qputenv("PINPOINT_MAIL_DRYRUN", "1");
        g_platformTo.clear();
        r = mailer.compose({ QStringLiteral("nick@example.com") }, QString(), QStringLiteral("S"), QStringLiteral("B"), pdf);
        check(r.value(QStringLiteral("method")).toString() == QLatin1String("dry-run") && g_platformTo.isEmpty(),
              "PINPOINT_MAIL_DRYRUN=1: logged, nothing opened");
        mailer.copyText(QStringLiteral("nick@example.com"));
        check(mailer.lastAction() == QLatin1String("copy:nick@example.com"), "copyText recorded (dry run: clipboard untouched)");
        mailer.reveal(pdf);
        check(mailer.lastAction().startsWith(QLatin1String("reveal:")), "reveal recorded (dry run: no Finder)");
    }

    std::printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
