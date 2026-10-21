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

// A SESSION REPORT'S RECORD (docs/design/session_report_design.md §8.1–§8.4) — <session>/report/
// report_meta.json, written beside the PDF every time a report is made, and read by the session list to
// say what state each session's report is in without opening the session.
//
// It holds the coach's note and the options, whether swing photos went in, every prepared time, the PDF's
// SHA-256, the FINGERPRINT of what the report was made from, and the send log. It holds NO ADDRESS: the
// session folder lives on the share, and a golfer's email belongs on this machine only (§7.3).
//
// THE FINGERPRINT is the work-on record's own recipe (WorkOnsController::scan: the rule version, the
// content stamp, each swing document's name, size and mtime) plus a stamp of the report's other inputs
// (the studio's details and the golfer's photo approval). Any of those moving makes the report "out of
// date"; nothing else does — re-opening the session or a panel rewriting diagnostics.json changes no
// sentence in the report.
//
// THE SEND LOG says what really happened. With the mail-app hand-off the app cannot know the coach
// pressed Send, so an entry is "Opened in Mail", never "Sent" (§8.3). Each entry carries the SHA-256 of
// the PDF that went, so a remake after a send can say the new PDF differs from the one sent.
//
// Pure apart from the file system; header-only; report_meta_test.

#include "../../Export/swing_store.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pinpoint::report {

inline constexpr int kReportMetaSchema = 1;

struct ReportSend {
    qint64  ms = 0;
    QString method;          // "mail-app" | "copied" | "revealed" — never an address
    QString sha256;          // of the PDF that went
};

struct ReportMeta {
    int              schema = kReportMetaSchema;
    QString          note;              // the coach's note, as typed
    QVariantMap      options;
    bool             photosIncluded = false;
    qint64           preparedMs = 0;    // the latest make
    QVector<qint64>  preparedHistory;   // every make, oldest first (§8.4)
    QString          pdfName;           // file name inside <session>/report/
    QString          sha256;
    qint64           sizeBytes = 0;
    QString          fingerprint;
    QVector<ReportSend> sends;
};

enum class ReportState { None, Current, Stale };

inline QString reportDir(const QString &sessionDir) { return QDir(sessionDir).filePath(QStringLiteral("report")); }
inline QString reportMetaPath(const QString &sessionDir)
{
    return QDir(reportDir(sessionDir)).filePath(QStringLiteral("report_meta.json"));
}

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
           + QStringLiteral("_session-report.pdf");
}

// Each swing document's identity, as WorkOnsController::scan hashes it.
inline QByteArray swingsIdentity(const QString &sessionDir)
{
    const QDir d(sessionDir);
    const QStringList swings = d.entryList(QStringList{ QStringLiteral("swing_*") },
                                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QByteArray out;
    for (const QString &name : swings) {
        const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(d.filePath(name));
        out += name.toUtf8() + '|' + QByteArray::number(doc.size) + '|' + QByteArray::number(doc.mtimeMs) + ';';
    }
    return out;
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

inline QString fileSha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

inline QJsonObject toJson(const ReportMeta &m)
{
    QJsonArray prepared, sends;
    for (qint64 t : m.preparedHistory) prepared.append(double(t));
    for (const ReportSend &s : m.sends)
        sends.append(QJsonObject{ {QStringLiteral("ms"), double(s.ms)}, {QStringLiteral("method"), s.method},
                                  {QStringLiteral("sha256"), s.sha256} });
    return QJsonObject{
        {QStringLiteral("schema"), m.schema},
        {QStringLiteral("note"), m.note},
        {QStringLiteral("options"), QJsonObject::fromVariantMap(m.options)},
        {QStringLiteral("photosIncluded"), m.photosIncluded},
        {QStringLiteral("preparedMs"), double(m.preparedMs)},
        {QStringLiteral("prepared"), prepared},
        {QStringLiteral("pdf"), m.pdfName},
        {QStringLiteral("sha256"), m.sha256},
        {QStringLiteral("sizeBytes"), double(m.sizeBytes)},
        {QStringLiteral("fingerprint"), m.fingerprint},
        {QStringLiteral("sends"), sends},
    };
}

inline std::optional<ReportMeta> reportMetaFromJson(const QJsonObject &o)
{
    if (o.value(QStringLiteral("schema")).toInt() != kReportMetaSchema) return std::nullopt;
    ReportMeta m;
    m.note           = o.value(QStringLiteral("note")).toString();
    m.options        = o.value(QStringLiteral("options")).toObject().toVariantMap();
    m.photosIncluded = o.value(QStringLiteral("photosIncluded")).toBool();
    m.preparedMs     = qint64(o.value(QStringLiteral("preparedMs")).toDouble());
    for (const QJsonValue v : o.value(QStringLiteral("prepared")).toArray()) m.preparedHistory.append(qint64(v.toDouble()));
    m.pdfName        = o.value(QStringLiteral("pdf")).toString();
    m.sha256         = o.value(QStringLiteral("sha256")).toString();
    m.sizeBytes      = qint64(o.value(QStringLiteral("sizeBytes")).toDouble());
    m.fingerprint    = o.value(QStringLiteral("fingerprint")).toString();
    for (const QJsonValue v : o.value(QStringLiteral("sends")).toArray()) {
        const QJsonObject s = v.toObject();
        m.sends.append({ qint64(s.value(QStringLiteral("ms")).toDouble()), s.value(QStringLiteral("method")).toString(),
                         s.value(QStringLiteral("sha256")).toString() });
    }
    return m;
}

inline std::optional<ReportMeta> readReportMeta(const QString &sessionDir)
{
    QFile f(reportMetaPath(sessionDir));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    return reportMetaFromJson(doc.object());
}

// Whole-file replace through QSaveFile, like the PDF itself.
inline bool writeReportMeta(const QString &sessionDir, const ReportMeta &m)
{
    if (!QDir().mkpath(reportDir(sessionDir))) return false;
    QSaveFile f(reportMetaPath(sessionDir));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented));
    return f.commit();
}

inline QString reportPdfPath(const QString &sessionDir, const ReportMeta &m)
{
    return m.pdfName.isEmpty() ? QString() : QDir(reportDir(sessionDir)).filePath(m.pdfName);
}

// Current only when the PDF is still there and was made from what the session holds now.
inline ReportState reportState(const QString &sessionDir, const std::optional<ReportMeta> &m,
                               const QString &currentFingerprint)
{
    if (!m || m->pdfName.isEmpty() || !QFileInfo::exists(reportPdfPath(sessionDir, *m))) return ReportState::None;
    return m->fingerprint == currentFingerprint ? ReportState::Current : ReportState::Stale;
}

// The PDF on disk is not the one that last went out.
inline bool sentDiffers(const ReportMeta &m)
{
    return !m.sends.isEmpty() && m.sends.last().sha256 != m.sha256;
}

// The row's state words (§8.0), plain text, never a badge: "No report" · "Report prepared 20 Oct" ·
// "Opened in Mail 20 Oct 18:40" · "Out of date — remake". Dates in the C locale's English month names.
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
    if (m && !m->sends.isEmpty() && m->sends.last().sha256 == m->sha256 && m->sends.last().method == QLatin1String("mail-app")) {
        const QDateTime t = QDateTime::fromMSecsSinceEpoch(m->sends.last().ms);
        return QStringLiteral("Opened in Mail ") + c.toString(t, QStringLiteral("d MMM HH:mm"));
    }
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(m ? m->preparedMs : 0);
    return QStringLiteral("Report prepared ") + c.toString(t.date(), QStringLiteral("d MMM"));
}

} // namespace pinpoint::report
