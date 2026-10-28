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
//
// The Sessions screen's half (sessions_screen_redesign.md, Phase 1) runs over a second golfer, H:
// folder sizes, the archived marker, the club / report / archived filters and the search alone and
// together, the filter labels' counts against what clicking them shows, the short report words — and a
// real archive and restore (SessionArchiver into a temp archive location) keeping a current report
// current (S-D6). The live guard on SessionReviewController::trashSession is not here: that controller
// needs the shot model, the settings and the athlete controller.

#include "report/session_report_list_model.h"
#include "report/report_mailer.h"
#include "report/mail_credentials.h"
#include "report/report_meta.h"
#include "../../Coaching/work_ons.h"
#include "../../Export/session_archiver.h"
#include "../../Export/swing_doc.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

static void writeReport(const QString &dir, bool current, qint64 preparedMs, qint64 sendMs = 0,
                        const QString &sendMethod = QStringLiteral("mail-app"))
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
    if (sendMs) m.sends.append({ sendMs, sendMethod, m.sha256 });
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
             {QStringLiteral("preparedMs"), double(m ? m->preparedMs : 0)},
             {QStringLiteral("lastSend"), m && !m->sends.isEmpty()
                  ? QVariantMap{ {QStringLiteral("ms"), double(m->sends.last().ms)},
                                 {QStringLiteral("method"), m->sends.last().method} } : QVariantMap{}},
             {QStringLiteral("sentDiffers"), m ? sentDiffers(*m) : false} };
}

// n swings as the app writes them (a real document, a summary, a thumbnail), for the archiver.
static void makeDocSession(const QString &dir, int n, qint64 startMs)
{
    for (int i = 1; i <= n; ++i) {
        const QString id = QStringLiteral("swing_%1").arg(i, 4, 10, QLatin1Char('0'));
        const QString sd = dir + QLatin1Char('/') + id;
        QDir().mkpath(sd);
        const QJsonObject manifest{
            { "schema", "pinpoint.swing/2" },
            { "clock", QJsonObject{ { "t0_us", 0 }, { "wallclock",
                QDateTime::fromMSecsSinceEpoch(startMs + qint64(i) * 60'000).toUTC().toString(Qt::ISODateWithMs) } } },
            { "swing", QJsonObject{ { "index", i }, { "id", id } } },
            { "streams", QJsonArray{ QJsonObject{ { "kind", "video" }, { "alias", "Face-On" }, { "file", "Face-On.mp4" } } } },
            { "thumbnail", QJsonObject{ { "file", "thumb.jpg" } } } };
        pinpoint::SwingDocWriter::writeSwingJson(sd, manifest, nullptr);
        pinpoint::SwingDocWriter::updateReview(sd, 3, QString(), QStringLiteral("7 IRON"));
        touch(sd + QStringLiteral("/Face-On.mp4"), QByteArray(64 * 1024, char('a' + i)));
        touch(sd + QStringLiteral("/thumb.jpg"), QByteArray(1024, 't'));
    }
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

    // ── The Sessions screen ─────────────────────────────────────────────────────────────────
    const QString hLib = tmp.filePath(QStringLiteral("H"));
    const QString h1 = hLib + QStringLiteral("/2026-10-01_H_01");   // 7 iron · current, sent by the app
    const QString h2 = hLib + QStringLiteral("/2026-10-02_H_01");   // Driver, 7 iron · DTL · out of date
    const QString h3 = hLib + QStringLiteral("/2026-10-03_H_01");   // Driver · no report · work-on goal
    const QString h4 = hLib + QStringLiteral("/2026-10-04_H_01");   // 7 iron · current · ARCHIVED
    const QString h5 = hLib + QStringLiteral("/2026-10-05_H_01");   // 7 iron · current, handed to Mail
    makeSession(h1, 3, at(10, 1, 9, 0), { QStringLiteral("7 IRON") }, { QStringLiteral("faceOn") });
    makeSession(h2, 4, at(10, 2, 9, 0), { QStringLiteral("DRIVER"), QStringLiteral("7 IRON") },
                { QStringLiteral("faceOn"), QStringLiteral("dtl") });
    makeSession(h3, 3, at(10, 3, 9, 0), { QStringLiteral("DRIVER") }, { QStringLiteral("faceOn") });
    makeSession(h4, 5, at(10, 4, 9, 0), { QStringLiteral("7 IRON") }, { QStringLiteral("faceOn") });
    makeSession(h5, 3, at(10, 5, 9, 0), { QStringLiteral("7 IRON") }, { QStringLiteral("faceOn") });
    writeRecord(h3, QStringLiteral("early_extension"), QStringLiteral("Early extension"), pinpoint::analysis::kWorkOnRuleVersion);
    writeReport(h1, /*current=*/true,  at(10, 1, 12, 0), at(10, 21, 18, 40), QStringLiteral("smtp"));
    writeReport(h2, /*current=*/false, at(10, 2, 12, 0));
    writeReport(h4, /*current=*/true,  at(10, 4, 12, 0));
    writeReport(h5, /*current=*/true,  at(10, 5, 12, 0), at(10, 5, 18, 0));
    touch(pinpoint::SessionArchiver::markerPath(h4), "{}");

    SessionReportListModel sl;
    sl.setSynchronous(true);
    sl.setStateFunction(stateOf);
    sl.setGoalResolver([](const QString &id) {
        return id == QLatin1String("early_extension") ? QStringLiteral("Stop the early extension") : QString();
    });
    sl.setAthleteDir(hLib);
    const auto rowOf = [&](const QString &dir) {
        for (int i = 0; i < sl.count(); ++i)
            if (role(sl, i, R::DirRole).toString() == QFileInfo(dir).absoluteFilePath()) return i;
        return -1;
    };
    // The shown rows' dirs in rank order — and the ranks must run 0, 1, 2… down the list.
    const auto shown = [&] {
        QStringList out;
        int expect = 0;
        for (int i = 0; i < sl.count(); ++i) {
            const int rank = role(sl, i, R::ShownRankRole).toInt();
            if (rank < 0) continue;
            if (rank != expect++) return QStringList{ QStringLiteral("ranks out of order") };
            out << QFileInfo(role(sl, i, R::DirRole).toString()).fileName().left(10);
        }
        return out;
    };
    const auto days = [](std::initializer_list<int> ds) {
        QStringList out;
        for (int d : ds) out << QStringLiteral("2026-10-%1").arg(d, 2, 10, QLatin1Char('0'));
        return out;
    };

    std::printf("sessions screen: rows\n");
    check(sl.count() == 5, "five sessions for H");
    {
        bool sizes = true;
        for (int i = 0; i < sl.count(); ++i)
            sizes = sizes && role(sl, i, R::SessionBytesRole).toDouble() > 0
                    && qint64(role(sl, i, R::SessionBytesRole).toDouble())
                           == pinpoint::SessionArchiver::folderBytes(role(sl, i, R::DirRole).toString());
        check(sizes, "sessionBytes: every row > 0, the folder's every file");
        const double before = role(sl, rowOf(h1), R::SessionBytesRole).toDouble();
        touch(h1 + QStringLiteral("/swing_0001/Face-On.mp4"), QByteArray(10000, 'v'));
        sl.refresh();
        check(role(sl, rowOf(h1), R::SessionBytesRole).toDouble() == before + 10000, "sessionBytes grows with the content");
    }
    check(role(sl, rowOf(h4), R::ArchivedRole).toBool() && !role(sl, rowOf(h1), R::ArchivedRole).toBool(),
          "archived: from the archived.json marker");
    check(sl.rowFor(h2).value(QStringLiteral("clubs")).toString() == QLatin1String("Driver, 7 iron"),
          "the joined clubs string is unchanged");
    check(role(sl, rowOf(h1), R::StateShortRole).toString() == QLatin1String("Sent 21 Oct"),
          "stateShort: sent by the app → Sent 21 Oct (the day, no time)");
    check(role(sl, rowOf(h2), R::StateShortRole).toString() == QLatin1String("Out of date"), "stateShort: Out of date");
    check(role(sl, rowOf(h3), R::StateShortRole).toString() == QLatin1String("No report"), "stateShort: No report");
    check(role(sl, rowOf(h4), R::StateShortRole).toString() == QLatin1String("Ready"), "stateShort: Ready");
    check(role(sl, rowOf(h5), R::StateShortRole).toString() == QLatin1String("Ready")
          && role(sl, rowOf(h5), R::StateWordsRole).toString().startsWith(QLatin1String("Opened in Mail")),
          "stateShort: handed to Mail is not a send → Ready (the full words keep Opened in Mail)");
    {
        // Remade after the send: the PDF on disk is not the one that went.
        auto m = readReportMeta(h1);
        m->sha256 = QStringLiteral("remade");
        writeReportMeta(h1, *m);
        sl.refreshState(h1);
        check(role(sl, rowOf(h1), R::StateShortRole).toString() == QLatin1String("Ready"), "stateShort: remade after a send → Ready");
        m->sha256 = m->sends.last().sha256;
        writeReportMeta(h1, *m);
        sl.refreshState(h1);
    }

    std::printf("sessions screen: filters\n");
    check(shown() == days({ 5, 3, 2, 1 }) && !sl.showArchived(), "default: archived hidden, the rest newest first");
    sl.setShowArchived(true);
    check(shown() == days({ 5, 4, 3, 2, 1 }), "showArchived: the archived session back in its place");
    sl.setShowArchived(false);
    sl.setClubFilter(QStringLiteral("Driver"));
    check(shown() == days({ 3, 2 }), "club Driver");
    sl.setClubFilter(QStringLiteral("7 iron"));
    check(shown() == days({ 5, 2, 1 }), "club 7 iron");
    sl.setClubFilter(QString());
    sl.setReportFilter(QStringLiteral("ready"));
    check(shown() == days({ 5, 1 }), "report ready: current, sent or not");
    sl.setReportFilter(QStringLiteral("stale"));
    check(shown() == days({ 2 }), "report stale");
    sl.setReportFilter(QStringLiteral("none"));
    check(shown() == days({ 3 }), "report none");
    sl.setReportFilter(QString());
    sl.setSearch(QStringLiteral("  EARLY "));
    check(shown() == days({ 3 }) && sl.search() == QLatin1String("EARLY"), "search: work-on goal, case-insensitive, trimmed");
    sl.setSearch(QStringLiteral("driver"));
    check(shown() == days({ 3, 2 }), "search: clubs");
    sl.setSearch(QStringLiteral("nothing like it"));
    check(shown().isEmpty() && sl.shownCount() == 0, "search: no match, nothing shown");
    sl.setSearch(QString());
    sl.setClubFilter(QStringLiteral("7 iron"));
    sl.setReportFilter(QStringLiteral("ready"));
    check(shown() == days({ 5, 1 }), "7 iron AND ready");
    sl.setShowArchived(true);
    check(shown() == days({ 5, 4, 1 }), "7 iron AND ready AND archived shown");
    sl.toggleDevice(QStringLiteral("dtl"));
    check(shown().isEmpty(), "…AND DTL: none");
    sl.setReportFilter(QStringLiteral("stale"));
    check(shown() == days({ 2 }), "7 iron AND stale AND DTL");
    sl.setSearch(QStringLiteral("driver"));
    check(shown() == days({ 2 }), "…AND search driver");
    sl.setSearch(QStringLiteral("early"));
    check(shown().isEmpty(), "…AND search early: none");
    sl.clearDeviceFilter(); sl.setClubFilter(QString()); sl.setReportFilter(QString()); sl.setSearch(QString());
    sl.setShowArchived(false);
    check(shown() == days({ 5, 3, 2, 1 }), "every filter cleared");

    std::printf("sessions screen: counts\n");
    // Each label against what clicking it shows, under a few states of the other filters.
    const auto countsHold = [&](const char *label) {
        bool ok = true;
        const QVariantMap rc = sl.reportCounts();
        const QString keepReport = sl.reportFilter();
        for (const QString &b : { QStringLiteral("none"), QStringLiteral("stale"), QStringLiteral("ready") }) {
            sl.setReportFilter(b);
            ok = ok && rc.value(b).toInt() == sl.shownSavedCount();
        }
        sl.setReportFilter(keepReport);
        const QVariantList cc = sl.clubCounts();
        const QString keepClub = sl.clubFilter();
        for (const QVariant &v : cc) {
            sl.setClubFilter(v.toMap().value(QStringLiteral("club")).toString());
            ok = ok && v.toMap().value(QStringLiteral("count")).toInt() == sl.shownSavedCount();
        }
        sl.setClubFilter(keepClub);
        const int ac = sl.archivedCount();
        const bool keepArch = sl.showArchived();
        sl.setShowArchived(false);
        const int without = sl.shownSavedCount();
        sl.setShowArchived(true);
        ok = ok && ac == sl.shownSavedCount() - without;
        sl.setShowArchived(keepArch);
        for (const QVariant &v : sl.availableDevices()) {
            const QString id = v.toMap().value(QStringLiteral("id")).toString();
            if (sl.deviceFilter().contains(id)) continue;
            sl.toggleDevice(id);
            ok = ok && v.toMap().value(QStringLiteral("count")).toInt() == sl.shownSavedCount();
            sl.toggleDevice(id);
        }
        check(ok, label);
    };
    {
        const QVariantMap rc = sl.reportCounts();
        check(rc.value("none").toInt() == 1 && rc.value("stale").toInt() == 1 && rc.value("ready").toInt() == 2,
              "reportCounts: none 1, stale 1, ready 2 (the archived one not counted while hidden)");
        const QVariantList cc = sl.clubCounts();
        check(cc.size() == 2 && cc[0].toMap().value("club") == "7 iron" && cc[0].toMap().value("count").toInt() == 3
              && cc[1].toMap().value("club") == "Driver" && cc[1].toMap().value("count").toInt() == 2,
              "clubCounts: most sessions first");
        check(sl.archivedCount() == 1, "archivedCount: 1");
    }
    countsHold("no filter: every label equals what clicking it shows");
    sl.setClubFilter(QStringLiteral("Driver"));
    countsHold("club Driver: labels hold");
    {
        const QVariantMap rc = sl.reportCounts();
        check(rc.value("none").toInt() == 1 && rc.value("stale").toInt() == 1 && rc.value("ready").toInt() == 0,
              "club Driver: report counts follow it");
    }
    sl.setReportFilter(QStringLiteral("ready"));
    {
        const QVariantList cc = sl.clubCounts();
        bool hasDriver = false;
        for (const QVariant &v : cc)
            if (v.toMap().value("club") == "Driver") hasDriver = v.toMap().value("count").toInt() == 0;
        check(hasDriver, "the chosen club stays listed at 0, so it can be switched off");
    }
    countsHold("club Driver AND ready: labels hold");
    sl.setClubFilter(QString());
    sl.setShowArchived(true);
    countsHold("ready AND archived shown: labels hold");
    check(sl.archivedCount() == 1, "archivedCount counts under the other filters");
    sl.setReportFilter(QString());
    sl.setSearch(QStringLiteral("iron"));
    sl.toggleDevice(QStringLiteral("faceOn"));
    countsHold("search iron AND Face-on AND archived shown: labels hold");
    sl.clearDeviceFilter(); sl.setSearch(QString()); sl.setShowArchived(false);
    {
        int swings = 0; double bytes = 0;
        for (int i = 0; i < sl.count(); ++i)
            if (role(sl, i, R::ShownRankRole).toInt() >= 0) {
                swings += role(sl, i, R::SwingsRole).toInt();
                bytes  += role(sl, i, R::SessionBytesRole).toDouble();
            }
        check(sl.shownSwings() == 13 && swings == 13 && sl.shownBytes() == bytes && bytes > 0,
              "shownSwings / shownBytes: totals over the shown rows (3+4+3+3)");
    }
    {
        // The live row: shown under every filter, counted in none.
        sl.setLiveSessionDir(h3);
        sl.setReportFilter(QStringLiteral("ready"));
        sl.setSearch(QStringLiteral("nothing like it"));
        check(shown() == days({ 3 }) && role(sl, 0, R::IsLiveRole).toBool(), "the live row is always shown");
        sl.setSearch(QString());
        check(sl.reportCounts().value("none").toInt() == 0, "the live row is not counted");
        countsHold("live row present: labels hold over the saved sessions");
        sl.setReportFilter(QString());
        sl.setLiveSessionDir(QString());
    }

    std::printf("sessions screen: archive and restore keep a current report current (S-D6)\n");
    {
        const QString archiveRoot = tmp.filePath(QStringLiteral("archive"));
        QDir().mkpath(archiveRoot);
        const QString a = tmp.filePath(QStringLiteral("I/2026-10-06_I_01"));   // current: kept current
        const QString b = tmp.filePath(QStringLiteral("I/2026-10-07_I_01"));   // stale: stays stale
        const QString c = tmp.filePath(QStringLiteral("I/2026-10-08_I_01"));   // control: plain archive
        makeDocSession(a, 3, at(10, 6, 9, 0));
        makeDocSession(b, 3, at(10, 7, 9, 0));
        makeDocSession(c, 3, at(10, 8, 9, 0));
        writeReport(a, true, at(10, 6, 12, 0));
        writeReport(b, false, at(10, 7, 12, 0));
        writeReport(c, true, at(10, 8, 12, 0));
        const ReportFingerprintFn fpFn = [](const QString &d) { return fp(d); };
        const auto stateIs = [](const QString &d) { return stateOf(d).value(QStringLiteral("state")).toString(); };
        check(stateIs(a) == QLatin1String("current") && stateIs(b) == QLatin1String("stale"), "before: current and stale");

        const QByteArray identityBefore = swingsIdentity(a);
        const auto ra = keepReportCurrent(a, fpFn, [&] { return pinpoint::SessionArchiver::archive(a, archiveRoot); });
        check(ra.ok && pinpoint::SessionArchiver::isArchived(a), "archived for real (verified copy, stubs, marker)");
        check(swingsIdentity(a) != identityBefore, "the stubs moved the swings' identity");
        check(stateIs(a) == QLatin1String("current"), "…and the report still reads current");
        const auto rb = keepReportCurrent(b, fpFn, [&] { return pinpoint::SessionArchiver::archive(b, archiveRoot); });
        check(rb.ok && stateIs(b) == QLatin1String("stale"), "an out-of-date report stays out of date");
        const auto rc = pinpoint::SessionArchiver::archive(c, archiveRoot);
        check(rc.ok && stateIs(c) == QLatin1String("stale"), "control: without the re-stamp, archiving reads out of date");

        SessionReportListModel il;
        il.setSynchronous(true);
        il.setStateFunction(stateOf);
        il.setShowArchived(true);
        il.setAthleteDir(tmp.filePath(QStringLiteral("I")));
        bool allArchived = il.count() == 3;
        for (int i = 0; i < il.count(); ++i) allArchived = allArchived && il.data(il.index(i), R::ArchivedRole).toBool();
        check(allArchived, "the list sees the archiver's own marker");
        check(il.rowFor(a).value(QStringLiteral("stateShort")).toString() == QLatin1String("Ready"), "the list reads it Ready");

        // A send made while archived lives only in the library's record; restore must not undo it.
        auto m = readReportMeta(a);
        m->sends.append({ at(10, 23, 9, 0), QStringLiteral("smtp"), m->sha256 });
        writeReportMeta(a, *m);
        const auto r2 = keepReportCurrent(a, fpFn, [&] { return pinpoint::SessionArchiver::restore(a); });
        check(r2.ok && !pinpoint::SessionArchiver::isArchived(a), "restored for real");
        check(stateIs(a) == QLatin1String("current"), "…and the report still reads current");
        const auto after = readReportMeta(a);
        check(after && after->sends.size() == 1 && after->sends.last().ms == at(10, 23, 9, 0),
              "the send recorded while archived survives the restore (report/ is not copied back over)");
        il.refresh();
        check(!il.rowFor(a).value(QStringLiteral("archived")).toBool()
              && il.rowFor(a).value(QStringLiteral("stateShort")).toString() == QLatin1String("Sent 23 Oct"),
              "the list: not archived, Sent 23 Oct");
    }

    std::printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
