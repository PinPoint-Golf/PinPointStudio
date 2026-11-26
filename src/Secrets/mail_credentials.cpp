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
#include "secret_store.h"

// The report-sending password through the credential store. The service's id and name are the ones the
// mail password's own platform files used before the store existed (macOS: service "PinPoint Studio report
// sending"; Windows: target "PinPointStudio/ReportSending/<account>"; Linux: ReportSending/password and
// ReportSending/passwordAccount). Changing either loses every password a coach has saved.

namespace pinpoint::report {

namespace S = pinpoint::secrets;

const S::Service &mailPasswordService()
{
    static const S::Service s{ QStringLiteral("ReportSending"), QStringLiteral("PinPoint Studio report sending") };
    return s;
}

bool mailCredentialsInMemory()
{
    return S::secretsInMemory();
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
    return S::writeSecret(mailPasswordService(), account, password, error);
}

QString readMailPassword(const QString &account)
{
    return S::readSecret(mailPasswordService(), account);
}

bool hasMailPassword(const QString &account)
{
    return S::hasSecret(mailPasswordService(), account);
}

bool forgetMailPassword(const QString &account)
{
    return S::removeSecret(mailPasswordService(), account);
}

} // namespace pinpoint::report
