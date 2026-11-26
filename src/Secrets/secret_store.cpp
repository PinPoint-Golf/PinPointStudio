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

#include "secret_store.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

// The portable half: the names on each platform, the in-memory store for tests and probes, the secrets
// read once and held, and the dispatch to the platform's store.

namespace pinpoint::secrets {

namespace {

QString keyOf(const Service &service, const QString &account)
{
    return service.id + QLatin1Char('\n') + account;
}

// Both maps under one lock: the platform's store is asked outside it.
QMutex &lock()
{
    static QMutex m;
    return m;
}

QHash<QString, QString> &memoryStore()
{
    static QHash<QString, QString> s;
    return s;
}

// The secrets read from, or written to, the platform's store in this run.
QHash<QString, QString> &held()
{
    static QHash<QString, QString> s;
    return s;
}

} // namespace

bool secretsInMemory()
{
    return qEnvironmentVariable("PINPOINT_SECRETS") == QLatin1String("memory")
           || qEnvironmentVariable("PINPOINT_MAIL_CREDENTIALS") == QLatin1String("memory")
           || qEnvironmentVariableIntValue("PINPOINT_MAIL_DRYRUN") == 1;
}

NativeName nativeName(const Service &service, const QString &account)
{
    NativeName n;
    n.account = account;
    n.macService = service.name;
    n.macAccount = account;
    n.windowsTarget = QStringLiteral("PinPointStudio/") + service.id + QLatin1Char('/') + account;
    n.windowsUser = account;
    n.settingsSecretKey = service.id + QStringLiteral("/password");
    n.settingsAccountKey = service.id + QStringLiteral("/passwordAccount");
    return n;
}

bool writeSecret(const Service &service, const QString &account, const QString &secret, QString *error)
{
    if (account.isEmpty()) {
        if (error) *error = QStringLiteral("There is no account to keep a secret for.");
        return false;
    }
    if (secret.isEmpty()) return removeSecret(service, account);
    const QString key = keyOf(service, account);
    if (secretsInMemory()) {
        QMutexLocker g(&lock());
        memoryStore().insert(key, secret);
        return true;
    }
    const bool ok = platformWriteSecret(nativeName(service, account), secret, error);
    QMutexLocker g(&lock());
    if (ok) held().insert(key, secret);
    else held().remove(key);   // what the store holds now is not known: ask it next time
    return ok;
}

QString readSecret(const Service &service, const QString &account)
{
    if (account.isEmpty()) return {};
    const QString key = keyOf(service, account);
    if (secretsInMemory()) {
        QMutexLocker g(&lock());
        return memoryStore().value(key);
    }
    {
        QMutexLocker g(&lock());
        const auto it = held().constFind(key);
        if (it != held().constEnd()) return it.value();
    }
    const QString secret = platformReadSecret(nativeName(service, account));
    if (!secret.isEmpty()) {
        QMutexLocker g(&lock());
        held().insert(key, secret);
    }
    return secret;
}

bool hasSecret(const Service &service, const QString &account)
{
    if (account.isEmpty()) return false;
    const QString key = keyOf(service, account);
    if (secretsInMemory()) {
        QMutexLocker g(&lock());
        return memoryStore().contains(key);
    }
    {
        QMutexLocker g(&lock());
        if (held().contains(key)) return true;
    }
    return platformHasSecret(nativeName(service, account));
}

bool removeSecret(const Service &service, const QString &account)
{
    if (account.isEmpty()) return true;
    const QString key = keyOf(service, account);
    if (secretsInMemory()) {
        QMutexLocker g(&lock());
        memoryStore().remove(key);
        return true;
    }
    {
        QMutexLocker g(&lock());
        held().remove(key);
    }
    return platformRemoveSecret(nativeName(service, account));
}

} // namespace pinpoint::secrets
