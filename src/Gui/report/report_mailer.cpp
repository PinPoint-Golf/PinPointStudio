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

#include "../../Core/pp_debug.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

ReportMailer::ReportMailer(QObject *parent) : QObject(parent) {}

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
