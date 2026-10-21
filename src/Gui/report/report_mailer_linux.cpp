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

// THE LINUX HAND-OFF: xdg-email, which opens the desktop's default mail client with the message filled in
// (Thunderbird and Evolution take --attach). Started detached: the client outlives the call, and the coach
// sends from there. Reply-to has no xdg-email option; the coach's own account is the sender.

#include <QProcess>
#include <QStandardPaths>

namespace pinpoint::report {

QString platformCompose(const QStringList &to, const QString &replyTo, const QString &subject,
                        const QString &body, const QString &filePath)
{
    Q_UNUSED(replyTo);
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("xdg-email"));
    if (exe.isEmpty())
        return QStringLiteral("No mail program can be opened from here (xdg-email is not installed). Use Copy "
                              "address and Show folder to attach the report by hand.");
    QStringList args{ QStringLiteral("--utf8"), QStringLiteral("--subject"), subject, QStringLiteral("--body"), body,
                      QStringLiteral("--attach"), filePath };
    args << to;
    if (!QProcess::startDetached(exe, args))
        return QStringLiteral("The mail program could not be started. Use Copy address and Show folder to "
                              "attach the report by hand.");
    return {};
}

} // namespace pinpoint::report
