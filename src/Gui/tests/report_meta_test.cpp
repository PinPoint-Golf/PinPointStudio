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

// report_meta_test — the session report's record (src/Gui/report/report_meta.h): the JSON round trip, the
// file name, the fingerprint moving with exactly the inputs it should, the three states and their words,
// and the send log saying whether the PDF on disk is the one that went.

#include "report/report_meta.h"

#include <QCoreApplication>
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

static void touch(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString session = tmp.filePath(QStringLiteral("2026-10-20_Nick-Henn_Wrist_03"));
    touch(session + QStringLiteral("/swing_0001/swing.ppsw"), "one");
    touch(session + QStringLiteral("/swing_0002/swing.ppsw"), "two");

    check(reportFileName(QDate(2026, 10, 20), QStringLiteral("Nick Henn")) == QLatin1String("2026-10-20_Nick-Henn_session-report.pdf"),
          "file name carries the session date and the golfer");
    check(reportFileName(QDate(2026, 10, 20), QStringLiteral("  Siân O'Neil ")) == QStringLiteral("2026-10-20_Siân-ONeil_session-report.pdf"),
          "file name keeps letters, drops punctuation");

    // Fingerprint: rule, content, inputs and every swing document move it; nothing else does.
    const QString f0 = reportFingerprint(session, 4, QStringLiteral("c1"), QStringLiteral("s1"));
    check(f0 == reportFingerprint(session, 4, QStringLiteral("c1"), QStringLiteral("s1")), "fingerprint is stable");
    check(f0 != reportFingerprint(session, 3, QStringLiteral("c1"), QStringLiteral("s1")), "rule version moves it");
    check(f0 != reportFingerprint(session, 4, QStringLiteral("c2"), QStringLiteral("s1")), "content stamp moves it");
    check(f0 != reportFingerprint(session, 4, QStringLiteral("c1"), QStringLiteral("s2")), "studio/photo stamp moves it");
    touch(session + QStringLiteral("/diagnostics.json"), "{}");
    check(f0 == reportFingerprint(session, 4, QStringLiteral("c1"), QStringLiteral("s1")), "diagnostics.json does not move it");
    QDir(session + QStringLiteral("/swing_0002")).removeRecursively();
    check(f0 != reportFingerprint(session, 4, QStringLiteral("c1"), QStringLiteral("s1")), "a deleted swing moves it");
    const QString f1 = reportFingerprint(session, 4, QStringLiteral("c1"), QStringLiteral("s1"));

    // No record, then a record with no PDF: no report.
    check(!readReportMeta(session), "no record yet");
    check(reportState(session, std::nullopt, f1) == ReportState::None, "no record is no report");
    check(reportStateWords(ReportState::None, std::nullopt) == QLatin1String("No report"), "words: No report");

    ReportMeta m;
    m.note = QStringLiteral("Good work today, Nick.");
    m.options = { {QStringLiteral("earlier"), true} };
    m.photosIncluded = true;
    m.preparedMs = QDateTime(QDate(2026, 10, 20), QTime(18, 30)).toMSecsSinceEpoch();
    m.preparedHistory = { m.preparedMs };
    m.pdfName = reportFileName(QDate(2026, 10, 20), QStringLiteral("Nick Henn"));
    m.fingerprint = f1;
    touch(reportPdfPath(session, m), "%PDF-1.4 first");
    m.sha256 = fileSha256(reportPdfPath(session, m));
    m.sizeBytes = QFileInfo(reportPdfPath(session, m)).size();
    check(m.sha256.size() == 64, "sha-256 of the PDF");
    check(writeReportMeta(session, m), "record written");

    const auto back = readReportMeta(session);
    check(back && back->note == m.note && back->fingerprint == f1 && back->photosIncluded
              && back->preparedHistory.size() == 1 && back->options.value(QStringLiteral("earlier")).toBool(),
          "record round-trips");
    check(reportState(session, back, f1) == ReportState::Current, "same inputs: current");
    check(reportStateWords(ReportState::Current, back) == QLatin1String("Report prepared 20 Oct"), "words: prepared date");
    check(reportState(session, back, f0) == ReportState::Stale, "moved inputs: out of date");
    check(reportStateWords(ReportState::Stale, back).startsWith(QStringLiteral("Out of date")), "words: out of date");

    // Opened in Mail, then remade: the send log says the PDF differs from the one that went.
    ReportMeta sent = *back;
    sent.sends.append({ QDateTime(QDate(2026, 10, 20), QTime(18, 40)).toMSecsSinceEpoch(), QStringLiteral("mail-app"), sent.sha256 });
    check(!sentDiffers(sent), "sent copy is the one on disk");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Opened in Mail 20 Oct 18:40"), "words: opened in Mail");
    touch(reportPdfPath(session, sent), "%PDF-1.4 remade");
    sent.sha256 = fileSha256(reportPdfPath(session, sent));
    check(sentDiffers(sent), "remade after a send: differs from the one sent");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Report prepared 20 Oct"),
          "remade copy is not described as opened in Mail");

    // Sent by the app over SMTP: "Sent", the server's acceptance — and only for the PDF that went.
    sent.sends.append({ QDateTime(QDate(2026, 10, 21), QTime(18, 40)).toMSecsSinceEpoch(), QStringLiteral("smtp"), sent.sha256 });
    check(!sentDiffers(sent), "smtp: the sent copy is the one on disk");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Sent 21 Oct 18:40"), "words: sent over SMTP");
    {
        const std::optional<ReportMeta> rt = reportMetaFromJson(toJson(sent));
        check(rt && rt->sends.size() == sent.sends.size() && rt->sends.last().method == QLatin1String("smtp"),
              "smtp send round-trips in the record");
    }
    touch(reportPdfPath(session, sent), "%PDF-1.4 remade again");
    sent.sha256 = fileSha256(reportPdfPath(session, sent));
    check(sentDiffers(sent), "remade after an SMTP send: differs from the one sent");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Report prepared 20 Oct"),
          "remade copy is not described as sent");
    ReportMeta other2 = sent;
    other2.sends.append({ QDateTime(QDate(2026, 10, 21), QTime(19, 0)).toMSecsSinceEpoch(), QStringLiteral("copied"), sent.sha256 });
    check(reportStateWords(ReportState::Current, other2) == QLatin1String("Report prepared 20 Oct"),
          "a copied address is not a send");

    // The record never holds an address.
    const QByteArray raw = QJsonDocument(toJson(sent)).toJson();
    check(!raw.contains('@'), "no email address in the record");

    // A PDF that has gone is no report, whatever the record says.
    QFile::remove(reportPdfPath(session, sent));
    check(reportState(session, sent, f1) == ReportState::None, "missing PDF: no report");

    // A record of another schema is refused, not misread.
    QJsonObject other = toJson(sent);
    other[QStringLiteral("schema")] = 99;
    check(!reportMetaFromJson(other), "unknown schema refused");

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
