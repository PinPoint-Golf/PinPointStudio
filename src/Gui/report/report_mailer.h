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

// HANDING A REPORT TO THE COACH'S MAIL APP (docs/design/session_report_design.md §8.3, option A, with
// option C beside it). A mailto: link cannot carry an attachment, so each platform opens a compose
// window its own way: NSSharingService on macOS, Simple MAPI on Windows, xdg-email on Linux. The coach
// reads it and presses Send; the app never knows whether they did, and says so ("Opened in Mail").
// Context property `reportMailer`.
//
// THE ADDRESSES COME FROM THE CALLER, every time, and are never stored here: a golfer's email belongs
// in this machine's settings (§7.3), and nothing about a send that names a person goes into the
// session folder, which lives on the share. The send log (report_meta.json) is written by
// SessionReportModel::recordSend, and holds the method and the PDF's hash only.
//
// PINPOINT_MAIL_DRYRUN=1 (probes and tests; safe_probe sets it) logs the composed message — to,
// reply-to, subject, body, the attachment's path and size — and returns method "dry-run" instead of
// opening anything. Under it reveal() logs and opens nothing, and copyText() logs and leaves the
// system clipboard alone, so a probe on Mark's desktop neither pops a Finder window nor overwrites
// what he copied. `lastAction` says what the last call did, for the probe to read back.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class ReportMailer : public QObject
{
    Q_OBJECT
    // "compose:<method>" | "copy:<text>" | "reveal:<path>" — the last call, dry-run or not.
    Q_PROPERTY(QString lastAction READ lastAction NOTIFY lastActionChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)
public:
    explicit ReportMailer(QObject *parent = nullptr);

    // {ok, method: "mail-app" | "dry-run", error}. `error` is plain words for the coach, shown in place.
    Q_INVOKABLE QVariantMap compose(const QStringList &to, const QString &replyTo, const QString &subject,
                                    const QString &body, const QString &filePath);
    Q_INVOKABLE void copyText(const QString &text);
    // Shows the file selected in Finder / Explorer; elsewhere opens its folder.
    Q_INVOKABLE void reveal(const QString &path);

    QString lastAction() const { return m_lastAction; }
    bool    dryRun() const;

signals:
    void lastActionChanged();

private:
    void setLastAction(const QString &a);
    QString m_lastAction;
};

namespace pinpoint::report {
// The platform half, one file each (report_mailer_mac.mm / _win.cpp / _linux.cpp). Returns an empty
// string on success, else the reason in plain words. Called on the GUI (main) thread.
QString platformCompose(const QStringList &to, const QString &replyTo, const QString &subject,
                        const QString &body, const QString &filePath);
}
