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

// report_meta_test — the session report's records: the rules (src/Gui/report/report_meta.h) — the file name, the
// fingerprint moving with exactly the inputs it should, the three states and their words, and the send log saying
// whether the PDF on disk is the one that went — and their two files (refactor step 5.6a; FILE-SESS-07, FILE-SESS-13:
// src/Export/session_report_io.h, SessionRepository's file work): the round trip, the derived write never touching the
// source file, a newer file read-only, and the lazy migration of an older report_meta.json — a fresh session, a legacy
// one, a crash between the two writes, a newer file, and a second open (a no-op) — and an archive's restamp of a
// legacy record keeping its note and send log.

#include "report/report_meta.h"
#include "../../Export/session_report_io.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

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
    pinpoint::test::IsolatedStores stores;   // #187: the stills' cache folder is a test location, never the user's cache
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

    // The report's own rule versions in its stamp (refactor 3.7-d, R-STAMP-1; review 3.7-d B2): the diagnosis source and
    // the board's rule each move the fingerprint, so a report made by an earlier rule reads "out of date".
    check(reportRulesStamp(QStringLiteral("c1"), 1) != QLatin1String("c1")
              && reportRulesStamp(QStringLiteral("c1"), 1).startsWith(QLatin1String("c1|")),
          "the report's stamp is the diagnostics' content stamp plus its rule versions");
    check(reportRulesStamp(QStringLiteral("c1"), 1) != reportRulesStamp(QStringLiteral("c1"), 2), "the board rule's version moves it");
    check(reportRulesStamp(QStringLiteral("c1"), 1).contains(QStringLiteral("report%1").arg(kReportDiagnosisVersion))
              && kReportDiagnosisVersion >= 2, "the diagnosis source's version is in it (2: the session's owner, 3.7-d)");
    check(reportFingerprint(session, 4, reportRulesStamp(QStringLiteral("c1"), 1), QStringLiteral("s1")) != f1,
          "a report made before 3.7-d (content stamp alone) is out of date after it");

    namespace io = pinpoint::session_report_io;
    using RS = pinpoint::ReportRead::State;
    const auto readMeta = [](const QString &dir) { return reportMetaOf(io::read(dir)); };
    const auto writeBoth = [](const QString &dir, const ReportMeta &m) {
        const bool a = io::writeInput(dir, pinpoint::ReportInput{ m.note, m.options, m.preparedHistory, m.sends }).ok();
        const bool b = io::writeRecord(dir, pinpoint::ReportRecord{ m.fingerprint, m.pdfName, m.preparedMs, m.sha256, m.sizeBytes, m.photosIncluded }).ok();
        return a && b;
    };
    const auto rawOf = [](const QString &path) { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); };
    const auto objOf = [&](const QString &path) { return QJsonDocument::fromJson(rawOf(path)).object(); };

    // No record, then a record with no PDF: no report.
    check(!readMeta(session), "no record yet");
    check(reportState(std::nullopt, f1) == ReportState::None, "no record is no report");
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
    check(writeBoth(session, m), "both records written (5.6a: report_input.json and report_meta.json)");

    const auto back = readMeta(session);
    check(back && back->note == m.note && back->fingerprint == f1 && back->photosIncluded && back->pdfPresent
              && back->preparedHistory.size() == 1 && back->options.value(QStringLiteral("earlier")).toBool(),
          "records round-trip");
    check(back && back->sha256.size() == 64 && back->sizeBytes == QFileInfo(reportPdfPath(session, m)).size(),
          "the record's SHA-256 and size are read off the PDF by the write");
    {
        const QJsonObject meta = objOf(io::metaFile(session)), in = objOf(io::inputFile(session));
        check(!meta.contains(QStringLiteral("note")) && !meta.contains(QStringLiteral("sends")) && !meta.contains(QStringLiteral("prepared"))
                  && !meta.contains(QStringLiteral("options")) && meta.value(QStringLiteral("fingerprint")).toString() == f1,
              "report_meta.json holds the derived facts only (FILE-SESS-07)");
        check(in.value(QStringLiteral("note")).toString() == m.note && in.contains(QStringLiteral("prepared"))
                  && in.contains(QStringLiteral("options")) && in.contains(QStringLiteral("sends")),
              "report_input.json holds the note, options, history and send log (FILE-SESS-13)");
    }
    check(reportState(back, f1) == ReportState::Current, "same inputs: current");
    check(reportStateWords(ReportState::Current, back) == QLatin1String("Report prepared 20 Oct"), "words: prepared date");
    check(reportState(back, f0) == ReportState::Stale, "moved inputs: out of date");
    check(reportStateWords(ReportState::Stale, back).startsWith(QStringLiteral("Out of date")), "words: out of date");

    // A remake (the derived write) never writes or deletes report_input.json.
    {
        const QByteArray before = rawOf(io::inputFile(session));
        const qint64 mtime = QFileInfo(io::inputFile(session)).lastModified().toMSecsSinceEpoch();
        QThread::msleep(20);
        pinpoint::ReportRecord rec{ f0, m.pdfName, m.preparedMs + 1, {}, 0, true };
        check(io::writeRecord(session, rec).ok() && rawOf(io::inputFile(session)) == before
                  && QFileInfo(io::inputFile(session)).lastModified().toMSecsSinceEpoch() == mtime,
              "a remake rewrites report_meta.json and leaves report_input.json byte for byte, untouched");
        rec.fingerprint = f1;
        io::writeRecord(session, rec);
    }

    // Opened in Mail, then remade: the send log says the PDF differs from the one that went.
    ReportMeta sent = *readMeta(session);
    sent.sends.append({ QDateTime(QDate(2026, 10, 20), QTime(18, 40)).toMSecsSinceEpoch(), QStringLiteral("mail-app"), sent.sha256 });
    check(!sentDiffers(sent), "sent copy is the one on disk");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Opened in Mail 20 Oct 18:40"), "words: opened in Mail");
    touch(reportPdfPath(session, sent), "%PDF-1.4 remade");
    sent.sha256 = io::detail::sha256Of(reportPdfPath(session, sent));
    check(sentDiffers(sent), "remade after a send: differs from the one sent");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Report prepared 20 Oct"),
          "remade copy is not described as opened in Mail");

    // Sent by the app over SMTP: "Sent", the server's acceptance — and only for the PDF that went.
    sent.sends.append({ QDateTime(QDate(2026, 10, 21), QTime(18, 40)).toMSecsSinceEpoch(), QStringLiteral("smtp"), sent.sha256 });
    check(!sentDiffers(sent), "smtp: the sent copy is the one on disk");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Sent 21 Oct 18:40"), "words: sent over SMTP");
    {
        check(writeBoth(session, sent), "the send log written");
        const auto rt = readMeta(session);
        check(rt && rt->sends.size() == sent.sends.size() && rt->sends.last().method == QLatin1String("smtp"),
              "smtp send round-trips in report_input.json");
    }
    touch(reportPdfPath(session, sent), "%PDF-1.4 remade again");
    sent.sha256 = io::detail::sha256Of(reportPdfPath(session, sent));
    check(sentDiffers(sent), "remade after an SMTP send: differs from the one sent");
    check(reportStateWords(ReportState::Current, sent) == QLatin1String("Report prepared 20 Oct"),
          "remade copy is not described as sent");
    ReportMeta other2 = sent;
    other2.sends.append({ QDateTime(QDate(2026, 10, 21), QTime(19, 0)).toMSecsSinceEpoch(), QStringLiteral("copied"), sent.sha256 });
    check(reportStateWords(ReportState::Current, other2) == QLatin1String("Report prepared 20 Oct"),
          "a copied address is not a send");

    // The records never hold an address.
    check(!rawOf(io::metaFile(session)).contains('@') && !rawOf(io::inputFile(session)).contains('@'), "no email address in the records");

    // A PDF that has gone is no report, whatever the record says.
    QFile::remove(reportPdfPath(session, sent));
    check(reportState(readMeta(session), f1) == ReportState::None, "missing PDF: no report");

    // A NEWER FILE (R-PERS-3): read for what this build knows, never written.
    {
        const QString d = tmp.filePath(QStringLiteral("newer"));
        touch(d + QStringLiteral("/swing_0001/swing.ppsw"), "one");
        touch(io::metaFile(d), R"({"schema":99,"pdf":"x.pdf","fingerprint":"f","note":"old"})");
        touch(io::inputFile(d), R"({"schema":99,"note":"from a newer build"})");
        const pinpoint::ReportRead r = io::read(d);
        check(r.recordState == RS::Newer && r.inputState == RS::Newer && r.input.note == QLatin1String("from a newer build")
                  && r.record.fingerprint == QLatin1String("f"),
              "newer files: read for what this build knows");
        const QByteArray mb = rawOf(io::metaFile(d)), ib = rawOf(io::inputFile(d));
        check(io::writeRecord(d, pinpoint::ReportRecord{}).status == pinpoint::WriteResult::Status::ReadOnly
                  && io::writeInput(d, pinpoint::ReportInput{}).status == pinpoint::WriteResult::Status::ReadOnly,
              "newer files: both writes refused (ReadOnly)");
        bool migrated = true;
        check(io::migrate(d, &migrated).ok() && !migrated && rawOf(io::metaFile(d)) == mb && rawOf(io::inputFile(d)) == ib,
              "MIGRATION, a newer file: nothing written");
        // A newer report_meta.json beside no input: still never written by the migration.
        QFile::remove(io::inputFile(d));
        check(io::migrate(d, &migrated).ok() && !migrated && !QFileInfo::exists(io::inputFile(d)) && rawOf(io::metaFile(d)) == mb,
              "MIGRATION, a newer report_meta.json in the older shape: nothing written");
    }

    // THE LAZY MIGRATION (FILE-SESS-13's row; J-66).
    const QByteArray legacyJson = R"({"schema":1,"note":"Legacy note","options":{"appendix":true},"photosIncluded":true,
        "preparedMs":2000,"prepared":[1000,2000],"pdf":"r_session-report.pdf","sha256":"abc","sizeBytes":3,"fingerprint":"fpL",
        "sends":[{"ms":3000,"method":"mail-app","sha256":"abc"}],"future":"kept"})";
    const auto legacySession = [&](const QString &name) {
        const QString d = tmp.filePath(name);
        touch(d + QStringLiteral("/swing_0001/swing.ppsw"), "one");
        touch(io::metaFile(d), legacyJson);
        touch(io::reportDir(d) + QStringLiteral("/r_session-report.pdf"), "pdf");
        return d;
    };
    {
        // A fresh session: no report, nothing written, no folder made.
        const QString d = tmp.filePath(QStringLiteral("fresh"));
        touch(d + QStringLiteral("/swing_0001/swing.ppsw"), "one");
        bool migrated = true;
        check(io::migrate(d, &migrated).ok() && !migrated && !QFileInfo::exists(io::reportDir(d)),
              "MIGRATION, a fresh session: nothing written");
    }
    {
        // A legacy session, read before it is opened: the older shape in memory, nothing written.
        const QString d = legacySession(QStringLiteral("legacy"));
        const pinpoint::ReportRead r = io::read(d);
        check(r.legacy && r.inputState == RS::Missing && r.hasInput() && r.input.note == QLatin1String("Legacy note")
                  && r.input.sends.size() == 1 && r.input.preparedHistory.size() == 2 && r.record.fingerprint == QLatin1String("fpL")
                  && r.pdfPresent && !QFileInfo::exists(io::inputFile(d)),
              "a legacy record read unopened: its source facts as the input, and no file written by the read");
        // Opened: report_input.json first, read back, then report_meta.json derived only.
        bool migrated = false;
        check(io::migrate(d, &migrated).ok() && migrated, "MIGRATION, a legacy session: migrated");
        const QJsonObject meta = objOf(io::metaFile(d)), in = objOf(io::inputFile(d));
        check(in.value(QStringLiteral("note")).toString() == QLatin1String("Legacy note")
                  && in.value(QStringLiteral("sends")).toArray().size() == 1 && in.value(QStringLiteral("prepared")).toArray().size() == 2
                  && in.value(QStringLiteral("options")).toObject().value(QStringLiteral("appendix")).toBool()
                  && in.value(QStringLiteral("schema")).toInt() == 1,
              "MIGRATION: report_input.json holds the note, the send log, the options and the history");
        bool noSource = true;
        for (const QString &k : io::legacyInputKeys()) noSource = noSource && !meta.contains(k);
        check(noSource && meta.value(QStringLiteral("fingerprint")).toString() == QLatin1String("fpL")
                  && meta.value(QStringLiteral("pdf")).toString() == QLatin1String("r_session-report.pdf")
                  && meta.value(QStringLiteral("photosIncluded")).toBool() && meta.value(QStringLiteral("future")).toString() == QLatin1String("kept"),
              "MIGRATION: report_meta.json rewritten as derived only (a key this build does not know kept)");
        const pinpoint::ReportRead after = io::read(d);
        check(!after.legacy && after.inputState == RS::Read && after.input == r.input && after.record.fingerprint == r.record.fingerprint,
              "MIGRATION: nothing lost — the same facts read back from the two files");
        // A second open: a no-op, both files byte for byte.
        const QByteArray mb = rawOf(io::metaFile(d)), ib = rawOf(io::inputFile(d));
        check(io::migrate(d, &migrated).ok() && !migrated && rawOf(io::metaFile(d)) == mb && rawOf(io::inputFile(d)) == ib,
              "MIGRATION, a second open: nothing written");
    }
    {
        // A crash between the two writes (simulated: report_input.json written, report_meta.json still the older shape).
        const QString d = legacySession(QStringLiteral("crash"));
        const pinpoint::ReportRead r0 = io::read(d);
        check(io::writeInput(d, r0.input).ok(), "crash: the first write landed");
        const pinpoint::ReportRead mid = io::read(d);
        check(mid.legacy && mid.inputState == RS::Read && mid.input == r0.input && mid.record.fingerprint == QLatin1String("fpL")
                  && objOf(io::metaFile(d)).value(QStringLiteral("note")).toString() == QLatin1String("Legacy note"),
              "MIGRATION, a crash between the writes: both files readable, nothing lost");
        bool migrated = false;
        check(io::migrate(d, &migrated).ok() && migrated && !io::read(d).legacy && io::read(d).input == r0.input,
              "MIGRATION after the crash: the next open finishes it");
    }
    {
        // An unparseable report_input.json beside a legacy record: never overwritten, the legacy file kept whole.
        const QString d = legacySession(QStringLiteral("badinput"));
        touch(io::inputFile(d), "not json");
        bool migrated = true;
        check(io::migrate(d, &migrated).ok() && !migrated && rawOf(io::inputFile(d)) == QByteArray("not json")
                  && rawOf(io::metaFile(d)) == legacyJson,
              "MIGRATION, an unparseable report_input.json: nothing written");
    }
    {
        // THE ONE INVARIANT: the derived write keeps the source keys while report_input.json is missing.
        const QString d = legacySession(QStringLiteral("restamp"));
        const auto res = io::keepReportCurrentAt(d, [](const QString &) { return QStringLiteral("fpL"); }, [] {
            struct { bool ok = true; } r;
            return r;
        });
        check(res.ok && rawOf(io::metaFile(d)) == legacyJson, "restamp: the rewrite ran; an unmoved fingerprint writes nothing");
        // a current report (fingerprint fpL, PDF there) re-stamped to fpL2 by a rewrite that moved the swings
        int calls = 0;
        const pinpoint::ReportFingerprintFn moving = [&](const QString &) { return ++calls == 1 ? QStringLiteral("fpL") : QStringLiteral("fpL2"); };
        io::keepReportCurrentAt(d, moving, [] { struct { bool ok = true; } r; return r; });
        const QJsonObject meta = objOf(io::metaFile(d));
        check(meta.value(QStringLiteral("fingerprint")).toString() == QLatin1String("fpL2")
                  && meta.value(QStringLiteral("note")).toString() == QLatin1String("Legacy note")
                  && meta.value(QStringLiteral("sends")).toArray().size() == 1 && !QFileInfo::exists(io::inputFile(d)),
              "an archive's restamp of an unopened legacy record: re-stamped, its note and send log kept, no report_input.json");
    }
    {
        // A make's derived write removes the previous PDF of another name; a restamp removes nothing.
        const QString d = legacySession(QStringLiteral("rename"));
        io::migrate(d);
        touch(io::reportDir(d) + QStringLiteral("/new_session-report.pdf"), "new");
        pinpoint::ReportRecord rec = io::read(d).record;
        rec.pdfName = QStringLiteral("new_session-report.pdf");
        check(io::writeRecord(d, rec, /*removeReplacedPdf=*/true).ok()
                  && !QFileInfo::exists(io::reportDir(d) + QStringLiteral("/r_session-report.pdf"))
                  && QFileInfo::exists(io::reportDir(d) + QStringLiteral("/new_session-report.pdf"))
                  && QFileInfo::exists(io::inputFile(d)),
              "a remake under a new name removes the old PDF, and never report_input.json");
    }
    // THE FOLD (review 5.6a E1): a build older than 5.6a writes report_meta.json whole, source keys and all, from what it
    // read of the split file (a blank note, no history, no sends) plus its own make or send.
    const auto olderBuildWrites = [&](const QString &d, const QByteArray &sourceKeys) {
        QJsonObject o = objOf(io::metaFile(d));
        const QJsonObject k = QJsonDocument::fromJson(sourceKeys).object();
        for (auto it = k.begin(); it != k.end(); ++it) o.insert(it.key(), it.value());
        touch(io::metaFile(d), QJsonDocument(o).toJson());
    };
    {
        const QString d = legacySession(QStringLiteral("older-after-split"));
        io::migrate(d);
        olderBuildWrites(d, R"({"note":"Typed in the older build","options":{"appendix":false},"prepared":[5000],
            "sends":[{"ms":6000,"method":"smtp","sha256":"def"}]})");
        pinpoint::ReportRecord rec = io::read(d).record;
        rec.fingerprint = QStringLiteral("fpRemade");
        check(io::writeRecord(d, rec).ok(), "an older build's note and send, then a remake: the remake written");
        const pinpoint::ReportRead r = io::read(d);
        const QJsonObject meta = objOf(io::metaFile(d));
        bool noSource = true;
        for (const QString &k : io::legacyInputKeys()) noSource = noSource && !meta.contains(k);
        check(r.inputState == RS::Read && r.input.note == QLatin1String("Typed in the older build")
                  && !r.input.options.value(QStringLiteral("appendix")).toBool()
                  && r.input.preparedHistory == QVector<qint64>({ 1000, 2000, 5000 }) && r.input.sends.size() == 2
                  && r.input.sends.first().ms == 3000 && r.input.sends.last().ms == 6000
                  && r.input.sends.last().method == QLatin1String("smtp"),
              "FOLD: the older build's note and send kept in report_input.json, the history and send log a union in order");
        check(noSource && meta.value(QStringLiteral("fingerprint")).toString() == QLatin1String("fpRemade"),
              "FOLD: then, and only then, report_meta.json derived only");
    }
    {
        // A send alone by the older build writes the blank note it read: never the golfer's note.
        const QString d = legacySession(QStringLiteral("older-send-only"));
        io::migrate(d);
        olderBuildWrites(d, R"({"note":"","options":{},"prepared":[],"sends":[{"ms":3000,"method":"mail-app","sha256":"abc"},
            {"ms":7000,"method":"copied","sha256":"abc"}]})");
        io::writeRecord(d, io::read(d).record);
        const pinpoint::ReportRead r = io::read(d);
        check(r.input.note == QLatin1String("Legacy note") && r.input.options.value(QStringLiteral("appendix")).toBool()
                  && r.input.preparedHistory.size() == 2 && r.input.sends.size() == 2 && r.input.sends.last().ms == 7000,
              "FOLD: an older build's send alone keeps the note and options; the duplicate send dropped, the new one kept");
    }
    {
        // A fold that cannot be written: report_meta.json keeps the older build's keys.
        const QString d = legacySession(QStringLiteral("fold-fails"));
        io::migrate(d);
        olderBuildWrites(d, R"({"note":"Typed in the older build","prepared":[5000],"sends":[{"ms":6000,"method":"smtp","sha256":"def"}]})");
        const QByteArray inBefore = rawOf(io::inputFile(d));
        QFile::setPermissions(io::inputFile(d), QFileDevice::ReadOwner);
        pinpoint::ReportRecord rec = io::read(d).record;
        rec.fingerprint = QStringLiteral("fpRemade");
        const bool wrote = io::writeRecord(d, rec).ok();
        QFile::setPermissions(io::inputFile(d), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const QJsonObject meta = objOf(io::metaFile(d));
        check(wrote && rawOf(io::inputFile(d)) == inBefore && meta.value(QStringLiteral("fingerprint")).toString() == QLatin1String("fpRemade")
                  && meta.value(QStringLiteral("note")).toString() == QLatin1String("Typed in the older build")
                  && meta.value(QStringLiteral("sends")).toArray().size() == 1 && meta.contains(QStringLiteral("prepared")),
              "FOLD failed (report_input.json not writable): the derived facts written, the older build's keys kept");
        check(io::writeRecord(d, rec).ok() && io::read(d).input.note == QLatin1String("Typed in the older build")
                  && !objOf(io::metaFile(d)).contains(QStringLiteral("note")),
              "FOLD: the next write, once it can, folds them and only then drops them");
    }

    // The stills' folder: today's location and recipe, byte for byte.
    {
        const QString key = QStringLiteral("fp123");
        const QString want = QString::fromLatin1(QCryptographicHash::hash((QDir::cleanPath(session) + key).toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
        const QString folder = io::stillsFolder(session, key);
        check(folder.endsWith(QStringLiteral("/report-stills/") + want), "the stills' folder: <Cache>/report-stills/<sha1(folder + fingerprint), 12>");
        QString why;
        const QString p = io::stillsWriter(folder)(QStringLiteral("s0001.jpg"), QByteArray("jpeg"), &why);
        check(p == folder + QStringLiteral("/s0001.jpg") && rawOf(p) == QByteArray("jpeg"), "a still written through the writer, its folder made");
        check(io::stillsWriter(folder)(QStringLiteral("../x.jpg"), QByteArray("x"), &why).isEmpty(), "a still name with a path refused");
        QDir(folder).removeRecursively();
    }

    // 5.6b: FILE-SESS-08 through the report command's file work — the print's target outside the library, the PDF moved
    // into the session atomically, and a placement that fails leaving the PDF there untouched.
    {
        const QString d = tmp.filePath(QStringLiteral("2026-10-21_Nick-Henn_Wrist_01"));
        touch(d + QStringLiteral("/swing_0001/swing.ppsw"), "one");
        const QString name = QStringLiteral("2026-10-21_Nick-Henn_session-report.pdf");
        const QString target = io::printTarget(d, name);
        check(!target.isEmpty() && !target.startsWith(QDir::cleanPath(d)) && target.contains(QStringLiteral("/report-print/"))
                  && QFileInfo(QFileInfo(target).absolutePath()).isDir(),
              "5.6b the print's target: outside the library, <Cache>/report-print/<hash>/, its folder made");
        check(io::printTarget(d, QStringLiteral("../x.pdf")).isEmpty(), "5.6b a print name with a path refused");
        touch(target, "%PDF-1.4 the new make");
        const pinpoint::WriteResult w = io::placePdf(d, target, name);
        const QString placed = io::reportDir(d) + QLatin1Char('/') + name;
        check(w.ok() && rawOf(placed) == QByteArray("%PDF-1.4 the new make") && !QFileInfo::exists(target),
              "5.6b the PDF moved into <session>/report/ (FILE-SESS-08), the printed file gone");
        const pinpoint::WriteResult gone = io::placePdf(d, target, name);   // nothing printed: the make fails
        check(!gone.ok() && rawOf(placed) == QByteArray("%PDF-1.4 the new make"),
              "5.6b a placement with no printed PDF fails, the PDF there untouched");
        touch(target, "");
        check(!io::placePdf(d, target, name).ok() && rawOf(placed) == QByteArray("%PDF-1.4 the new make"),
              "5.6b an empty print fails, the PDF there untouched");
        QDir(QFileInfo(target).absolutePath()).removeRecursively();
    }

    // 5.6b: the restamp's one rule (report_record.h restampedRecord), for the Session's commands and the unheld path.
    {
        pinpoint::ReportRead now;
        now.recordState = pinpoint::ReportRead::State::Read;
        now.record.fingerprint = QStringLiteral("before");
        now.record.pdfName = QStringLiteral("a.pdf");
        now.record.sha256 = QStringLiteral("abc");
        const std::optional<pinpoint::ReportRecord> r = pinpoint::restampedRecord(now, QStringLiteral("after"));
        check(r && r->fingerprint == QLatin1String("after") && r->pdfName == QLatin1String("a.pdf") && r->sha256 == QLatin1String("abc"),
              "5.6b restamp: the fingerprint only, every other derived fact kept");
        check(!pinpoint::restampedRecord(now, QStringLiteral("before")), "5.6b restamp: nothing when the fingerprint did not move");
        now.recordState = pinpoint::ReportRead::State::Newer;
        check(!pinpoint::restampedRecord(now, QStringLiteral("after")), "5.6b restamp: never a newer build's file");
        now.recordState = pinpoint::ReportRead::State::Missing;
        check(!pinpoint::restampedRecord(now, QStringLiteral("after")), "5.6b restamp: nothing without a record");
    }

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
