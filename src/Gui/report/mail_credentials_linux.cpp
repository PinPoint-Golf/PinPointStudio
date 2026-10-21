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

// THE LINUX "STORE" IS THE SETTINGS FILE, AND THAT IS NOT A SECURE STORE. Linux is not a studio platform
// (the studios run macOS and Windows), and there is no secret store every desktop shares — libsecret needs
// a running keyring daemon, KWallet another — so rather than a dependency for a platform no coach sends
// from, the password sits in ppSettings() under ReportSending/password, readable by anyone who can read
// this user's home folder. ReportSending/passwordAccount says which login it belongs to, so it is never
// offered to another server. forgetSending() removes both.

#include "../../Core/pp_settings.h"

namespace pinpoint::report {

namespace {
const QString kPassword = QStringLiteral("ReportSending/password");
const QString kAccount  = QStringLiteral("ReportSending/passwordAccount");
} // namespace

bool platformStoreMailPassword(const QString &account, const QString &password, QString *error)
{
    QSettings s = ppSettings();
    s.setValue(kAccount, account);
    s.setValue(kPassword, password);
    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("The password could not be saved in the settings file.");
        return false;
    }
    return true;
}

QString platformReadMailPassword(const QString &account)
{
    const QSettings s = ppSettings();
    if (s.value(kAccount).toString() != account) return {};
    return s.value(kPassword).toString();
}

bool platformHasMailPassword(const QString &account)
{
    return !platformReadMailPassword(account).isEmpty();
}

bool platformForgetMailPassword(const QString &account)
{
    QSettings s = ppSettings();
    if (s.value(kAccount).toString() == account) {
        s.remove(kPassword);
        s.remove(kAccount);
        s.sync();
    }
    return true;
}

} // namespace pinpoint::report
