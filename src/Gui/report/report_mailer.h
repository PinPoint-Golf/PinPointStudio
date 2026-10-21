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
// SENDING IT ITSELF (§8.3, option B, decided 21 Oct): once the coach sets up their own mail account in
// Settings → Studio → Sending reports, send() puts the report straight onto their server over SMTP
// (smtp_client.h), From their login, Reply-To their studio email when it differs, a Bcc copy to
// themselves (copyToSelf, on by default — SMTP leaves nothing in Sent except on Gmail). compose() stays
// as the fallback until then. The settings live in ppSettings() group "ReportSending" (provider, host,
// port, security, username, copyToSelf); the password never does — it goes to the system's secure store
// (mail_credentials.h) under "username@host", and any change of user name or server forgets it, so
// `hasPassword` is always about the login on screen. The result arrives as sendFinished / testFinished;
// the method is "smtp", and the send log then says "Sent", meaning the server accepted it.
//
// PINPOINT_MAIL_DRYRUN=1 (probes and tests; safe_probe sets it) logs the composed message — to,
// reply-to, subject, body, the attachment's path and size — and returns method "dry-run" instead of
// opening anything. Under it reveal() logs and opens nothing, and copyText() logs and leaves the
// system clipboard alone, so a probe on Mark's desktop neither pops a Finder window nor overwrites
// what he copied. send() and sendTest() log the message they would have sent (to, bcc, from, reply-to,
// subject, attachment, encoded size), read no password, open no socket, and finish on the next event-loop
// turn with method "dry-run"; the password store is the in-memory one (mail_credentials.h), so a probe
// never touches the keychain. `lastAction` says what the last call did, for the probe to read back.

#include "smtp_client.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class ReportMailer : public QObject
{
    Q_OBJECT
    // "compose:<method>" | "copy:<text>" | "reveal:<path>" | "send:smtp|dry-run|failed" |
    // "test:smtp|dry-run|failed" — the last call, dry-run or not.
    Q_PROPERTY(QString lastAction READ lastAction NOTIFY lastActionChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)

    // Sending reports (Settings → Studio). provider: "" (not set up) | "gmail" | "icloud" | "microsoft" |
    // "other"; security: "starttls" | "tls" | "none".
    Q_PROPERTY(QString provider READ provider NOTIFY sendingSettingsChanged)
    Q_PROPERTY(QString host READ host NOTIFY sendingSettingsChanged)
    Q_PROPERTY(int port READ port NOTIFY sendingSettingsChanged)
    Q_PROPERTY(QString security READ security NOTIFY sendingSettingsChanged)
    Q_PROPERTY(QString username READ username NOTIFY sendingSettingsChanged)
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY sendingSettingsChanged)
    Q_PROPERTY(bool copyToSelf READ copyToSelf NOTIFY sendingSettingsChanged)
    // A provider, a server and port, and either a login with its password or (security "none" only) no login.
    Q_PROPERTY(bool sendConfigured READ sendConfigured NOTIFY sendingSettingsChanged)
    Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
    Q_PROPERTY(double sendProgress READ sendProgress NOTIFY sendProgressChanged)   // 0..1 of the upload
public:
    explicit ReportMailer(QObject *parent = nullptr);
    ~ReportMailer() override;

    // {ok, method: "mail-app" | "dry-run", error}. `error` is plain words for the coach, shown in place.
    Q_INVOKABLE QVariantMap compose(const QStringList &to, const QString &replyTo, const QString &subject,
                                    const QString &body, const QString &filePath);
    Q_INVOKABLE void copyText(const QString &text);
    // Shows the file selected in Finder / Explorer; elsewhere opens its folder.
    Q_INVOKABLE void reveal(const QString &path);

    // ── Sending reports: the settings ──
    // A preset fills host, port and security (gmail smtp.gmail.com 587 starttls; icloud smtp.mail.me.com
    // 587 starttls; microsoft smtp.office365.com 587 starttls); "other" leaves them as they are; "" = not
    // set up (the rest is kept).
    Q_INVOKABLE void setProvider(const QString &provider);
    Q_INVOKABLE void setServer(const QString &host, int port, const QString &security);
    Q_INVOKABLE void setUsername(const QString &username);
    Q_INVOKABLE void setCopyToSelf(bool on);
    // {ok, error}. Empty forgets it. For gmail and icloud every space is taken out first (Google shows an
    // app password as four groups of four).
    Q_INVOKABLE QVariantMap setPassword(const QString &password);
    // Clears the settings and the password.
    Q_INVOKABLE void forgetSending();

    // ── Sending reports: sending ──
    // {ok, error} at once — the addresses, the file, the set-up, one send at a time; then sendFinished
    // (method "smtp" | "dry-run"). From = the user name when it is an address, else `replyTo`.
    Q_INVOKABLE QVariantMap send(const QString &sessionDir, const QStringList &to, const QString &replyTo,
                                 const QString &fromName, const QString &subject, const QString &body,
                                 const QString &filePath);
    // A short message with no attachment to the From address; then testFinished. `fromAddress` is used only
    // when the user name is not an address (an open relay).
    Q_INVOKABLE QVariantMap sendTest(const QString &fromName, const QString &fromAddress = QString());

    QString lastAction() const { return m_lastAction; }
    bool    dryRun() const;

    QString provider() const;
    QString host() const;
    int     port() const;
    QString security() const;
    QString username() const;
    bool    hasPassword() const;
    bool    copyToSelf() const;
    bool    sendConfigured() const;
    bool    sending() const { return m_sending; }
    double  sendProgress() const { return m_progress; }

signals:
    void lastActionChanged();
    void sendingSettingsChanged();
    void sendingChanged();
    void sendProgressChanged();
    void sendFinished(const QString &sessionDir, bool ok, const QString &method, const QString &error);
    void testFinished(bool ok, const QString &error, qint64 atMs);

private:
    struct Sending {
        QString provider, host, security, username;
        int     port = 0;
        bool    copyToSelf = true;
        bool    hasPassword = false;
    };
    enum class Job { None, Report, Test };

    void setLastAction(const QString &a);
    void ensureLoaded() const;
    void save(const QString &key, const QVariant &value);
    void accountChanging(const QString &newUsername, const QString &newHost);
    QString fromAddressFor(const QString &replyTo) const;
    QVariantMap startSend(Job job, const QString &sessionDir, const pinpoint::report::MailMessage &m,
                          const QString &attachmentPath);
    void onSmtpFinished(bool ok, pinpoint::report::SmtpError code, const QString &serverReply);
    void endSend(bool ok, const QString &method, const QString &error);
    void setSending(bool on);
    void setProgress(double p);

    QString m_lastAction;
    mutable bool    m_loaded = false;
    mutable Sending m_s;
    pinpoint::report::SmtpClient *m_smtp = nullptr;
    bool    m_sending = false;
    double  m_progress = 0.0;
    Job     m_job = Job::None;
    QString m_jobDir;
    qint64  m_jobBytes = 0;   // the PDF (or, for the test, the message), for "too big" words
};

namespace pinpoint::report {
// The platform half, one file each (report_mailer_mac.mm / _win.cpp / _linux.cpp). Returns an empty
// string on success, else the reason in plain words. Called on the GUI (main) thread.
QString platformCompose(const QStringList &to, const QString &replyTo, const QString &subject,
                        const QString &body, const QString &filePath);

// The coach's words for a failed send, provider-aware ("gmail" | "icloud" | "microsoft" | other).
// `rejected` is the refused recipient; `sizeBytes` the report's size.
QString sendErrorWords(SmtpError code, const QString &provider, const QString &host, int port,
                       const QString &serverReply, const QString &rejected, qint64 sizeBytes);
}
