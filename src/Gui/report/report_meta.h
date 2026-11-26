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

// A SESSION REPORT'S RECORD (docs/design/session_report_design.md §8.1–§8.4): the report's rules over its two records
// (refactor step 5.6a; FILE-SESS-07, FILE-SESS-13), which this file no longer reads or writes. The records are the
// Session's: src/DomainModel/report_record.h holds them as values, SessionRepository reads and writes them
// (src/Stores/session_report_io.h), and the Session's commands write them (recordReportMake, recordReportSend,
// saveReport). The session list reads them through Athlete::readReportAt without opening the session.
//
// ReportMeta is the report's FLAT VIEW of the two, as read: the source facts (the coach's note and options, every
// prepared time, the send log: report_input.json) and the derived facts (whether swing photos went in, the latest
// prepared time, the PDF's name, SHA-256 and size, the FINGERPRINT of what it was made from: report_meta.json). It
// holds NO ADDRESS: the session folder lives on the share, and a golfer's email belongs on this machine only (§7.3).
//
// THE FINGERPRINT is the work-on record's own recipe (WorkOnsController::scan: the rule version, the
// content stamp, each swing document's name, size and mtime) plus a stamp of the report's other inputs
// (the studio's details and the golfer's photo approval). Any of those moving makes the report "out of
// date"; nothing else does — re-opening the session or a panel rewriting diagnostics.json changes no
// sentence in the report.
//
// THE SEND LOG says what really happened. With the mail-app hand-off the app cannot know the coach
// pressed Send, so an entry is "Opened in Mail", never "Sent" (§8.3). Sent by the app itself over SMTP
// (method "smtp", option B) it says "Sent" — and that means the coach's mail server ACCEPTED it, not
// that it was delivered; a bounce comes back to the coach's inbox. Each entry carries the SHA-256 of
// the PDF that went, so a remake after a send can say the new PDF differs from the one sent.
//
// Pure apart from the swing documents' identity (swingsIdentity); header-only; report_meta_test.

#include "../../DomainModel/report_record.h"   // the two records, as values (step 5.6a)
#include "../../Stores/session_files.h"   // the report documents' names (D9)
#include "../../Stores/swing_store.h"
#include "../../Stores/swing_paths.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <optional>

namespace pinpoint::report {

using ReportSend = pinpoint::ReportSend;

// The flat view of the two records, as read (above).
struct ReportMeta {
    QString          note;              // the coach's note, as typed (FILE-SESS-13)
    QVariantMap      options;           // FILE-SESS-13
    bool             photosIncluded = false;
    qint64           preparedMs = 0;    // the latest make
    QVector<qint64>  preparedHistory;   // every make, oldest first (§8.4; FILE-SESS-13)
    QString          pdfName;           // file name inside <session>/report/
    QString          sha256;
    qint64           sizeBytes = 0;
    QString          fingerprint;
    QVector<ReportSend> sends;          // FILE-SESS-13
    bool             pdfPresent = false;   // the PDF it names is on disk (read with the records)
};

// The flat view of a read; none when neither file is there.
inline std::optional<ReportMeta> reportMetaOf(const pinpoint::ReportRead &r)
{
    if (!r.any()) return std::nullopt;
    ReportMeta m;
    m.note            = r.input.note;
    m.options         = r.input.options;
    m.preparedHistory = r.input.preparedHistory;
    m.sends           = r.input.sends;
    m.photosIncluded  = r.record.photosIncluded;
    m.preparedMs      = r.record.preparedMs;
    m.pdfName         = r.record.pdfName;
    m.sha256          = r.record.sha256;
    m.sizeBytes       = r.record.sizeBytes;
    m.fingerprint     = r.record.fingerprint;
    m.pdfPresent      = r.pdfPresent;
    return m;
}

enum class ReportState { None, Current, Stale };

// The names are SessionFiles' (D9; lint A4). Path text only.
inline QString reportDir(const QString &sessionDir) { return QDir(sessionDir).filePath(pinpoint::SessionFiles::reportFolderName()); }

// "2026-10-20_Nick-Henn_session-report.pdf": the SESSION date, so a coach's folder sorts by session (§8.4).
inline QString reportFileName(const QDate &sessionDate, const QString &golferName)
{
    QString name = golferName.simplified();
    name.replace(QLatin1Char(' '), QLatin1Char('-'));
    QString safe;
    for (const QChar c : name)
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) safe += c;
    if (safe.isEmpty()) safe = QStringLiteral("Golfer");
    return sessionDate.toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char('_') + safe
           + pinpoint::SessionFiles::reportPdfSuffix();
}

// Each swing document's identity, as WorkOnsController::scan hashes it.
inline QByteArray swingsIdentity(const QString &sessionDir)
{
    const QDir d(sessionDir);
    const QStringList swings = pinpoint::SwingPaths::swingFolders(sessionDir);   // THE one listing (step 2.7b)
    QByteArray out;
    for (const QString &name : swings) {
        const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(d.filePath(name));
        out += name.toUtf8() + '|' + QByteArray::number(doc.size) + '|' + QByteArray::number(doc.mtimeMs) + ';';
    }
    return out;
}

// THE REPORT'S OWN RULES IN ITS STAMP (refactor step 3.7-d; R-STAMP-1: "the version of every rule that produced the
// result"; session_report_design.md §8.2). A report is out of date when a rule that made it changed, not only its inputs:
// the version of where the report reads its diagnosis from, and the launch monitor board's rule
// (board_annotations.h kBoardAnnotationRuleVersion, passed in by the report).
//   1 (to 21 Nov 2026): the report's own SessionDiagnosticsModel (no version was stamped: the content stamp alone).
//   2 (21 Nov 2026, refactor 3.7-d): the Session's owner through its thin adapter — every page reads the shots as the
//     golfer annotated them on the board, and the appendix's carry cost is "-" until a club's usual is earned (G-08).
//   3 (23 Nov 2026, refactor 6.3b): the shot shape on the Delivery page is the pack's direction rule (the corridors, F-05)
//     and a left-hander's is glossed (G-02); a skied shot is labelled Sky on every page (G-04).
// A report made before 6.3b therefore reads "out of date" once: a remake says something else.
inline constexpr int kReportDiagnosisVersion = 3;

// The content stamp a report's fingerprint carries: the diagnostics' content stamp plus the report's rule versions.
inline QString reportRulesStamp(const QString &contentStamp, int boardRuleVersion)
{
    return contentStamp + QStringLiteral("|report%1|board%2").arg(kReportDiagnosisVersion).arg(boardRuleVersion);
}

inline QString reportFingerprint(const QString &sessionDir, int workOnRuleVersion, const QString &contentStamp,
                                 const QString &inputsStamp)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(workOnRuleVersion));
    h.addData(contentStamp.toUtf8());
    h.addData(inputsStamp.toUtf8());
    h.addData(swingsIdentity(sessionDir));
    return QString::fromLatin1(h.result().toHex().left(16));
}

inline QString reportPdfPath(const QString &sessionDir, const ReportMeta &m)
{
    return m.pdfName.isEmpty() ? QString() : QDir(reportDir(sessionDir)).filePath(m.pdfName);
}

// Current only when the PDF is still there and was made from what the session holds now (report_record.h's rule).
inline ReportState reportState(const std::optional<ReportMeta> &m, const QString &currentFingerprint)
{
    if (!m || m->pdfName.isEmpty() || !m->pdfPresent) return ReportState::None;
    return m->fingerprint == currentFingerprint ? ReportState::Current : ReportState::Stale;
}

// The PDF on disk is not the one that last went out.
inline bool sentDiffers(const ReportMeta &m)
{
    return !m.sends.isEmpty() && m.sends.last().sha256 != m.sha256;
}

// A session's fingerprint as the report model computes it now (report_record.h).
using ReportFingerprintFn = pinpoint::ReportFingerprintFn;

// The row's state words (§8.0), plain text, never a badge: "No report" · "Report prepared 20 Oct" ·
// "Sent 21 Oct 18:40" (the server accepted it) · "Opened in Mail 20 Oct 18:40" · "Out of date — remake".
// Dates in the C locale's English month names.
inline QString reportStateWords(ReportState state, const std::optional<ReportMeta> &m)
{
    const QLocale c = QLocale::c();
    switch (state) {
    case ReportState::None:
        return QStringLiteral("No report");
    case ReportState::Stale:
        return QStringLiteral("Out of date — remake");
    case ReportState::Current:
        break;
    }
    if (m && !m->sends.isEmpty() && m->sends.last().sha256 == m->sha256) {
        const ReportSend &last = m->sends.last();
        const QString when = c.toString(QDateTime::fromMSecsSinceEpoch(last.ms), QStringLiteral("d MMM HH:mm"));
        if (last.method == QLatin1String("smtp"))     return QStringLiteral("Sent ") + when;
        if (last.method == QLatin1String("mail-app")) return QStringLiteral("Opened in Mail ") + when;
    }
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(m ? m->preparedMs : 0);
    return QStringLiteral("Report prepared ") + c.toString(t.date(), QStringLiteral("d MMM"));
}

} // namespace pinpoint::report
