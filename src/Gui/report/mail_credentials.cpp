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

#include "mail_credentials.h"

#include <QHash>

// The portable half: the in-memory store for tests and probes, and the dispatch to the platform's.

namespace pinpoint::report {

namespace {
QHash<QString, QString> &memoryStore()
{
    static QHash<QString, QString> s;
    return s;
}
} // namespace

bool mailCredentialsInMemory()
{
    return qEnvironmentVariable("PINPOINT_MAIL_CREDENTIALS") == QLatin1String("memory")
           || qEnvironmentVariableIntValue("PINPOINT_MAIL_DRYRUN") == 1;
}

QString mailAccount(const QString &username, const QString &host)
{
    const QString u = username.trimmed();
    const QString h = host.trimmed().toLower();
    if (u.isEmpty() || h.isEmpty()) return {};
    return u + QLatin1Char('@') + h;
}

bool storeMailPassword(const QString &account, const QString &password, QString *error)
{
    if (account.isEmpty()) {
        if (error) *error = QStringLiteral("There is no login to keep a password for.");
        return false;
    }
    if (password.isEmpty()) return forgetMailPassword(account);
    if (mailCredentialsInMemory()) {
        memoryStore().insert(account, password);
        return true;
    }
    return platformStoreMailPassword(account, password, error);
}

QString readMailPassword(const QString &account)
{
    if (account.isEmpty()) return {};
    if (mailCredentialsInMemory()) return memoryStore().value(account);
    return platformReadMailPassword(account);
}

bool hasMailPassword(const QString &account)
{
    if (account.isEmpty()) return false;
    if (mailCredentialsInMemory()) return memoryStore().contains(account);
    return platformHasMailPassword(account);
}

bool forgetMailPassword(const QString &account)
{
    if (account.isEmpty()) return true;
    if (mailCredentialsInMemory()) {
        memoryStore().remove(account);
        return true;
    }
    return platformForgetMailPassword(account);
}

} // namespace pinpoint::report
