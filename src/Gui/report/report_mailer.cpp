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

#include "report_mailer.h"

#include "mail_credentials.h"
#include "../../Core/pp_debug.h"
#include "../../Core/pp_settings.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace R = pinpoint::report;

ReportMailer::ReportMailer(QObject *parent) : QObject(parent) {}
ReportMailer::~ReportMailer() = default;

bool ReportMailer::dryRun() const
{
    return qEnvironmentVariableIntValue("PINPOINT_MAIL_DRYRUN") == 1;
}

void ReportMailer::setLastAction(const QString &a)
{
    m_lastAction = a;
    emit lastActionChanged();
}

QVariantMap ReportMailer::compose(const QStringList &to, const QString &replyTo, const QString &subject,
                                  const QString &body, const QString &filePath)
{
    const auto fail = [this](const QString &why) {
        ppWarn() << qPrintable(QStringLiteral("[ReportMailer] not composed: %1").arg(why));
        setLastAction(QStringLiteral("compose:failed"));
        return QVariantMap{ {QStringLiteral("ok"), false}, {QStringLiteral("method"), QString()},
                            {QStringLiteral("error"), why} };
    };

    QStringList addresses;
    for (const QString &a : to) {
        const QString t = a.trimmed();
        if (!t.isEmpty()) addresses << t;
    }
    if (addresses.isEmpty())
        return fail(tr("There is no email address to send it to."));
    const QFileInfo file(filePath);
    if (filePath.isEmpty() || !file.isFile())
        return fail(tr("The report's PDF is not there any more. Make the report again."));

    if (dryRun()) {
        // The whole message, so a probe can read back exactly what Mail would have been handed.
        ppInfo() << qPrintable(QStringLiteral("[ReportMailer] DRY RUN compose to=%1 replyTo=%2 subject=\"%3\" "
                                              "attachment=%4 size=%5 body=\"%6\"")
                                   .arg(addresses.join(QLatin1Char(',')), replyTo, subject,
                                        file.absoluteFilePath(), QString::number(file.size()),
                                        QString(body).replace(QLatin1Char('\n'), QStringLiteral("\\n"))));
        setLastAction(QStringLiteral("compose:dry-run"));
        return { {QStringLiteral("ok"), true}, {QStringLiteral("method"), QStringLiteral("dry-run")},
                 {QStringLiteral("error"), QString()} };
    }

    const QString err = pinpoint::report::platformCompose(addresses, replyTo, subject, body, file.absoluteFilePath());
    if (!err.isEmpty()) return fail(err);
    ppInfo() << qPrintable(QStringLiteral("[ReportMailer] handed to the mail app: %1 (%2 bytes)")
                               .arg(file.fileName()).arg(file.size()));
    setLastAction(QStringLiteral("compose:mail-app"));
    return { {QStringLiteral("ok"), true}, {QStringLiteral("method"), QStringLiteral("mail-app")},
             {QStringLiteral("error"), QString()} };
}

void ReportMailer::copyText(const QString &text)
{
    if (dryRun()) {
        ppInfo() << qPrintable(QStringLiteral("[ReportMailer] DRY RUN copy \"%1\"").arg(text));
    } else if (QClipboard *cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
    setLastAction(QStringLiteral("copy:") + text);
}

void ReportMailer::reveal(const QString &path)
{
    const QFileInfo fi(path);
    if (dryRun()) {
        ppInfo() << qPrintable(QStringLiteral("[ReportMailer] DRY RUN reveal %1 (exists=%2)")
                                   .arg(fi.absoluteFilePath(), fi.exists() ? QStringLiteral("yes") : QStringLiteral("no")));
        setLastAction(QStringLiteral("reveal:") + fi.absoluteFilePath());
        return;
    }
    setLastAction(QStringLiteral("reveal:") + fi.absoluteFilePath());
#if defined(Q_OS_MACOS)
    if (fi.exists()) {
        QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), fi.absoluteFilePath() });
        return;
    }
#elif defined(Q_OS_WIN)
    if (fi.exists()) {
        // explorer wants `/select,"C:\path"` as ONE argument; QProcess's own quoting would wrap the
        // whole thing in quotes and Explorer would open Documents instead.
        QProcess p;
        p.setProgram(QStringLiteral("explorer.exe"));
        p.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(fi.absoluteFilePath())));
        p.startDetached();
        return;
    }
#endif
    // Linux, or a file that has gone: open the folder it lives (lived) in.
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.exists() && fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
}

// ── Sending reports: the settings ───────────────────────────────────────────────────────────────

namespace {

const QString kGroup = QStringLiteral("ReportSending/");

bool knownProvider(const QString &p)
{
    return p.isEmpty() || p == QLatin1String("gmail") || p == QLatin1String("icloud")
           || p == QLatin1String("microsoft") || p == QLatin1String("other");
}

QString normalisedSecurity(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == QLatin1String("tls") || t == QLatin1String("none")) return t;
    return QStringLiteral("starttls");
}

R::SmtpConfig::Security securityOf(const QString &s)
{
    if (s == QLatin1String("tls"))  return R::SmtpConfig::Tls;
    if (s == QLatin1String("none")) return R::SmtpConfig::None;
    return R::SmtpConfig::StartTls;
}

QString providerName(const QString &p)
{
    if (p == QLatin1String("gmail"))     return QStringLiteral("Gmail");
    if (p == QLatin1String("icloud"))    return QStringLiteral("iCloud");
    if (p == QLatin1String("microsoft")) return QStringLiteral("Microsoft 365");
    return QStringLiteral("Your mail server");
}

// The server's own words without the codes: "535 5.7.8 Username and Password not accepted." →
// "Username and Password not accepted.", at most 160 characters.
QString shortServerText(const QString &reply)
{
    QString t = reply;
    t.remove(QRegularExpression(QStringLiteral("^\\s*\\d{3}[ -]?")));
    t.remove(QRegularExpression(QStringLiteral("\\b[245]\\.\\d{1,3}\\.\\d{1,3}\\b")));
    t = t.simplified();
    if (t.size() > 160) t = t.left(159).trimmed() + QChar(0x2026);
    return t;
}

} // namespace

QString R::sendErrorWords(SmtpError code, const QString &provider, const QString &host, int port,
                          const QString &serverReply, const QString &rejected, qint64 sizeBytes)
{
    const auto tr = [](const char *s) { return QCoreApplication::translate("ReportMailer", s); };
    const bool gmail = provider == QLatin1String("gmail");
    const bool icloud = provider == QLatin1String("icloud");
    switch (code) {
    case SmtpError::None:
        return {};
    case SmtpError::AppPasswordRequired:
        if (gmail)
            return tr("Gmail needs an app password here, not your normal password. Make one at "
                      "myaccount.google.com/apppasswords (it needs 2-Step Verification on) and paste it in "
                      "Settings → Studio.");
        if (icloud)
            return tr("iCloud needs an app-specific password here, not your Apple Account password. Make one at "
                      "account.apple.com (Sign-In and Security → App-Specific Passwords) and paste it in "
                      "Settings → Studio.");
        return tr("Your mail server needs an app password here, not your normal password. Make one in your mail "
                  "account's security settings and paste it in Settings → Studio.");
    case SmtpError::AuthRejected:
        if (gmail)
            return tr("Gmail didn't accept that app password. Make a new one at myaccount.google.com/apppasswords "
                      "and paste it in Settings → Studio.");
        if (icloud)
            return tr("iCloud didn't accept that app-specific password. Make a new one at account.apple.com and "
                      "paste it in Settings → Studio.");
        if (provider == QLatin1String("microsoft"))
            return tr("Microsoft 365 didn't accept the user name or password. Many Microsoft 365 accounts have "
                      "password sign-in for sending switched off; whoever runs the account can turn it on.");
        return tr("Your mail server didn't accept the user name or password.");
    case SmtpError::TooBig:
        return tr("The report is too big for your mail server (%1 MB).")
            .arg(double(sizeBytes) / (1024.0 * 1024.0), 0, 'f', 1);
    case SmtpError::Connect:
    case SmtpError::Timeout:
        return tr("Couldn't reach %1. Check the network — some networks block port %2.").arg(host).arg(port);
    case SmtpError::Resolve:
        return tr("Couldn't find the mail server %1. Check its name in Settings → Studio.").arg(host);
    case SmtpError::Tls:
        return tr("Couldn't make a secure connection to %1.").arg(host);
    case SmtpError::NoEncryption:
        return tr("%1 doesn't offer a secure connection, so the password wasn't sent.").arg(host);
    case SmtpError::RecipientRejected:
        return tr("The mail server refused the address %1.").arg(rejected);
    case SmtpError::BadConfig:
        return tr("Sending isn't set up properly. Check it in Settings → Studio.");
    case SmtpError::Disconnected:
        if (serverReply == QLatin1String("cancelled")) return tr("Sending was cancelled.");
        return tr("%1 closed the connection part-way through. Try again.").arg(host);
    case SmtpError::ServerRejected:
        break;
    }
    const QString why = shortServerText(serverReply);
    return why.isEmpty() ? tr("The mail server refused the message.")
                         : tr("The mail server refused the message: %1").arg(why);
}

void ReportMailer::ensureLoaded() const
{
    if (m_loaded) return;
    m_loaded = true;
    const QSettings s = ppSettings();
    m_s.provider   = s.value(kGroup + QStringLiteral("provider")).toString();
    if (!knownProvider(m_s.provider)) m_s.provider.clear();
    m_s.host       = s.value(kGroup + QStringLiteral("host")).toString().trimmed();
    m_s.port       = s.value(kGroup + QStringLiteral("port"), 0).toInt();
    m_s.security   = normalisedSecurity(s.value(kGroup + QStringLiteral("security")).toString());
    m_s.username   = s.value(kGroup + QStringLiteral("username")).toString().trimmed();
    m_s.copyToSelf = s.value(kGroup + QStringLiteral("copyToSelf"), true).toBool();
    m_s.hasPassword = R::hasMailPassword(R::mailAccount(m_s.username, m_s.host));
}

QString ReportMailer::provider() const { ensureLoaded(); return m_s.provider; }
QString ReportMailer::host() const { ensureLoaded(); return m_s.host; }
int     ReportMailer::port() const { ensureLoaded(); return m_s.port; }
QString ReportMailer::security() const { ensureLoaded(); return m_s.security; }
QString ReportMailer::username() const { ensureLoaded(); return m_s.username; }
bool    ReportMailer::hasPassword() const { ensureLoaded(); return m_s.hasPassword; }
bool    ReportMailer::copyToSelf() const { ensureLoaded(); return m_s.copyToSelf; }

bool ReportMailer::sendConfigured() const
{
    ensureLoaded();
    if (m_s.provider.isEmpty() || m_s.host.isEmpty() || m_s.port <= 0) return false;
    if (!m_s.username.isEmpty()) return m_s.hasPassword;
    return m_s.security == QLatin1String("none");   // an open relay: no login, and so nothing to protect
}

void ReportMailer::save(const QString &key, const QVariant &value)
{
    QSettings s = ppSettings();
    s.setValue(kGroup + key, value);
}

// A password belongs to one login on one server: a new user name or server forgets the old one rather
// than offer it to a server it was not typed for, and hasPassword is then about the new account.
void ReportMailer::accountChanging(const QString &newUsername, const QString &newHost)
{
    const QString before = R::mailAccount(m_s.username, m_s.host);
    const QString after  = R::mailAccount(newUsername, newHost);
    if (before == after) return;
    if (!before.isEmpty() && m_s.hasPassword) {
        R::forgetMailPassword(before);
        ppInfo() << "[ReportMailer] the login or server changed: its saved password is forgotten";
    }
    m_s.hasPassword = R::hasMailPassword(after);
}

void ReportMailer::setProvider(const QString &providerIn)
{
    ensureLoaded();
    const QString p = providerIn.trimmed().toLower();
    if (!knownProvider(p)) {
        ppWarn() << "[ReportMailer] unknown provider ignored:" << providerIn;
        return;
    }
    QString host = m_s.host;
    int port = m_s.port;
    QString security = m_s.security;
    if (p == QLatin1String("gmail"))          { host = QStringLiteral("smtp.gmail.com");     port = 587; security = QStringLiteral("starttls"); }
    else if (p == QLatin1String("icloud"))    { host = QStringLiteral("smtp.mail.me.com");   port = 587; security = QStringLiteral("starttls"); }
    else if (p == QLatin1String("microsoft")) { host = QStringLiteral("smtp.office365.com"); port = 587; security = QStringLiteral("starttls"); }
    accountChanging(m_s.username, host);
    m_s.provider = p;
    m_s.host = host;
    m_s.port = port;
    m_s.security = security;
    save(QStringLiteral("provider"), p);
    save(QStringLiteral("host"), host);
    save(QStringLiteral("port"), port);
    save(QStringLiteral("security"), security);
    emit sendingSettingsChanged();
}

void ReportMailer::setServer(const QString &hostIn, int portIn, const QString &securityIn)
{
    ensureLoaded();
    const QString host = hostIn.trimmed();
    const int port = (portIn > 0 && portIn <= 65535) ? portIn : 0;
    const QString security = normalisedSecurity(securityIn);
    accountChanging(m_s.username, host);
    m_s.host = host;
    m_s.port = port;
    m_s.security = security;
    save(QStringLiteral("host"), host);
    save(QStringLiteral("port"), port);
    save(QStringLiteral("security"), security);
    emit sendingSettingsChanged();
}

void ReportMailer::setUsername(const QString &usernameIn)
{
    ensureLoaded();
    const QString u = R::headerSafe(usernameIn).remove(QRegularExpression(QStringLiteral("\\s")));
    accountChanging(u, m_s.host);
    m_s.username = u;
    save(QStringLiteral("username"), u);
    emit sendingSettingsChanged();
}

void ReportMailer::setCopyToSelf(bool on)
{
    ensureLoaded();
    if (m_s.copyToSelf == on) return;
    m_s.copyToSelf = on;
    save(QStringLiteral("copyToSelf"), on);
    emit sendingSettingsChanged();
}

QVariantMap ReportMailer::setPassword(const QString &passwordIn)
{
    ensureLoaded();
    const QString account = R::mailAccount(m_s.username, m_s.host);
    QString pw = passwordIn;
    // Google shows an app password as four groups of four, Apple as four groups joined by dashes kept as
    // typed; spaces are never part of either. Elsewhere only the ends are trimmed, as a paste brings them.
    if (m_s.provider == QLatin1String("gmail") || m_s.provider == QLatin1String("icloud"))
        pw.remove(QRegularExpression(QStringLiteral("\\s")));
    else
        pw = pw.trimmed();

    if (pw.isEmpty()) {
        R::forgetMailPassword(account);
        m_s.hasPassword = false;
        emit sendingSettingsChanged();
        return { {QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()} };
    }
    if (account.isEmpty())
        return { {QStringLiteral("ok"), false},
                 {QStringLiteral("error"), tr("Fill in the user name and the mail server first.")} };
    QString err;
    if (!R::storeMailPassword(account, pw, &err)) {
        ppWarn() << qPrintable(QStringLiteral("[ReportMailer] password not saved: %1").arg(err));
        m_s.hasPassword = R::hasMailPassword(account);
        emit sendingSettingsChanged();
        return { {QStringLiteral("ok"), false}, {QStringLiteral("error"), err} };
    }
    ppInfo() << "[ReportMailer] password for sending saved"
             << (R::mailCredentialsInMemory() ? "(in memory: test/dry run)" : "(system store)");
    m_s.hasPassword = true;
    emit sendingSettingsChanged();
    return { {QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()} };
}

void ReportMailer::forgetSending()
{
    ensureLoaded();
    R::forgetMailPassword(R::mailAccount(m_s.username, m_s.host));
    {
        QSettings s = ppSettings();
        s.remove(QStringLiteral("ReportSending"));
    }
    m_s = Sending{};
    m_s.security = QStringLiteral("starttls");
    ppInfo() << "[ReportMailer] sending reports: set-up and password forgotten";
    emit sendingSettingsChanged();
}

// ── Sending reports: sending ────────────────────────────────────────────────────────────────────

QString ReportMailer::fromAddressFor(const QString &replyTo) const
{
    ensureLoaded();
    return m_s.username.contains(QLatin1Char('@')) ? m_s.username : replyTo.trimmed();
}

void ReportMailer::setSending(bool on)
{
    if (m_sending == on) return;
    m_sending = on;
    emit sendingChanged();
}

void ReportMailer::setProgress(double p)
{
    p = qBound(0.0, p, 1.0);
    if (qFuzzyCompare(p + 1.0, m_progress + 1.0)) return;
    m_progress = p;
    emit sendProgressChanged();
}

QVariantMap ReportMailer::send(const QString &sessionDir, const QStringList &to, const QString &replyToIn,
                               const QString &fromName, const QString &subject, const QString &body,
                               const QString &filePath)
{
    const auto fail = [this](const QString &why) {
        ppWarn() << qPrintable(QStringLiteral("[ReportMailer] not sent: %1").arg(why));
        setLastAction(QStringLiteral("send:failed"));
        return QVariantMap{ {QStringLiteral("ok"), false}, {QStringLiteral("error"), why} };
    };
    if (m_sending) return fail(tr("A report is already being sent. Wait for it to finish."));
    if (!sendConfigured()) return fail(tr("Sending isn't set up. Set it up in Settings → Studio."));

    QStringList addresses;
    for (const QString &a : to) {
        const QString t = a.trimmed();
        if (!t.isEmpty()) addresses << t;
    }
    if (addresses.isEmpty()) return fail(tr("There is no email address to send it to."));
    for (const QString &a : addresses)
        if (!R::isPlausibleAddress(a)) return fail(tr("%1 doesn't look like an email address.").arg(a));
    const QString replyTo = replyToIn.trimmed();
    if (!replyTo.isEmpty() && !R::isPlausibleAddress(replyTo))
        return fail(tr("The coach's email in Settings → Studio doesn't look like an email address."));
    const QString from = fromAddressFor(replyTo);
    if (!R::isPlausibleAddress(from))
        return fail(tr("There is no address to send from. Use your email address as the user name in "
                       "Settings → Studio."));

    const QFileInfo file(filePath);
    if (filePath.isEmpty() || !file.isFile())
        return fail(tr("The report's PDF is not there any more. Make the report again."));
    QFile f(file.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return fail(tr("The report's PDF could not be read."));

    R::MailMessage m;
    m.fromName = fromName;
    m.fromAddress = from;
    m.to = addresses;
    if (copyToSelf()) m.bcc = QStringList{ from };
    m.replyTo = replyTo;
    m.subject = subject;
    m.body = body;
    m.attachmentName = file.fileName();
    m.attachment = f.readAll();
    return startSend(Job::Report, QDir::cleanPath(sessionDir), m, file.absoluteFilePath());
}

QVariantMap ReportMailer::sendTest(const QString &fromName, const QString &fromAddress)
{
    const auto fail = [this](const QString &why) {
        ppWarn() << qPrintable(QStringLiteral("[ReportMailer] test not sent: %1").arg(why));
        setLastAction(QStringLiteral("test:failed"));
        return QVariantMap{ {QStringLiteral("ok"), false}, {QStringLiteral("error"), why} };
    };
    if (m_sending) return fail(tr("A report is already being sent. Wait for it to finish."));
    if (!sendConfigured()) return fail(tr("Sending isn't set up yet."));
    const QString from = fromAddressFor(fromAddress);
    if (!R::isPlausibleAddress(from))
        return fail(tr("There is no address to send the test to. Use your email address as the user name."));

    R::MailMessage m;
    m.fromName = fromName;
    m.fromAddress = from;
    m.to = QStringList{ from };
    m.subject = tr("PinPoint Studio test message");
    m.body = tr("This is a test message from PinPoint Studio.\n\nReports sent from this computer will come from "
                "%1.").arg(from);
    return startSend(Job::Test, QString(), m, QString());
}

QVariantMap ReportMailer::startSend(Job job, const QString &sessionDir, const R::MailMessage &m,
                                    const QString &attachmentPath)
{
    const QString kind = job == Job::Test ? QStringLiteral("test") : QStringLiteral("send");
    m_job = job;
    m_jobDir = sessionDir;
    m_jobBytes = m.attachment.size();

    if (dryRun()) {
        // What would have gone, without the body, and with no password read and no socket opened.
        const QByteArray mime = R::buildMime(m, QDateTime::currentDateTime(), R::makeMessageId(m.fromAddress));
        const QString via = QStringLiteral("%1:%2 (%3)").arg(host()).arg(port()).arg(security());
        ppInfo() << qPrintable(QStringLiteral("[ReportMailer] DRY RUN %1 to=%2 bcc=%3 from=%4 replyTo=%5 subject=\"%6\" "
                                              "attachment=%7 size=%8 encoded=%9")
                                   .arg(kind, m.to.join(QLatin1Char(',')), m.bcc.join(QLatin1Char(',')), m.fromAddress,
                                        m.replyTo, m.subject, attachmentPath.isEmpty() ? QStringLiteral("-") : attachmentPath,
                                        QString::number(m.attachment.size()), QString::number(mime.size()))
                               + QStringLiteral(" via=") + via);
        setSending(true);
        setProgress(0.0);
        QTimer::singleShot(0, this, [this]() {
            setProgress(1.0);
            endSend(true, QStringLiteral("dry-run"), QString());
        });
        return { {QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()} };
    }

    ensureLoaded();
    R::SmtpConfig cfg;
    cfg.host = m_s.host;
    cfg.port = quint16(m_s.port);
    cfg.security = securityOf(m_s.security);
    cfg.username = m_s.username;
    if (!cfg.username.isEmpty()) {
        cfg.password = R::readMailPassword(R::mailAccount(m_s.username, m_s.host));
        if (cfg.password.isEmpty()) {
            m_job = Job::None;
            m_s.hasPassword = false;
            emit sendingSettingsChanged();
            ppWarn() << "[ReportMailer] not sent: the saved password could not be read";
            setLastAction(kind + QStringLiteral(":failed"));
            return { {QStringLiteral("ok"), false},
                     {QStringLiteral("error"), tr("The password for sending isn't saved any more. Paste it again in "
                                                  "Settings → Studio.")} };
        }
    }
    if (!m_smtp) {
        m_smtp = new R::SmtpClient(this);
        connect(m_smtp, &R::SmtpClient::progress, this, [this](qint64 sent, qint64 total) {
            setProgress(total > 0 ? double(sent) / double(total) : 0.0);
        });
        connect(m_smtp, &R::SmtpClient::finished, this, &ReportMailer::onSmtpFinished);
    }
    ppInfo() << qPrintable(QStringLiteral("[ReportMailer] %1 via %2:%3 (%4), %5 recipient(s)%6")
                               .arg(kind, m_s.host).arg(m_s.port).arg(m_s.security)
                               .arg(m.to.size() + m.bcc.size())
                               .arg(attachmentPath.isEmpty() ? QString()
                                                             : QStringLiteral(", %1 (%2 bytes)")
                                                                   .arg(QFileInfo(attachmentPath).fileName())
                                                                   .arg(m.attachment.size())));
    setSending(true);
    setProgress(0.0);
    m_smtp->send(cfg, m);
    cfg.password.fill(QLatin1Char(' '));
    return { {QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()} };
}

void ReportMailer::onSmtpFinished(bool ok, R::SmtpError code, const QString &serverReply)
{
    if (m_job == Job::None) return;
    if (ok) {
        setProgress(1.0);
        endSend(true, QStringLiteral("smtp"), QString());
        return;
    }
    ensureLoaded();
    const qint64 size = m_jobBytes > 0 ? m_jobBytes : (m_smtp ? m_smtp->messageBytes() : 0);
    const QString words = R::sendErrorWords(code, m_s.provider, m_s.host, m_s.port, serverReply,
                                            m_smtp ? m_smtp->rejectedRecipient() : QString(), size);
    endSend(false, QStringLiteral("smtp"), words);
}

void ReportMailer::endSend(bool ok, const QString &method, const QString &error)
{
    const Job job = m_job;
    const QString dir = m_jobDir;
    m_job = Job::None;
    m_jobDir.clear();
    m_jobBytes = 0;
    setSending(false);
    if (!ok) setProgress(0.0);
    if (job == Job::Test) {
        setLastAction(QStringLiteral("test:") + (ok ? method : QStringLiteral("failed")));
        emit testFinished(ok, error, QDateTime::currentMSecsSinceEpoch());
    } else {
        setLastAction(QStringLiteral("send:") + (ok ? method : QStringLiteral("failed")));
        emit sendFinished(dir, ok, method, error);
    }
}
