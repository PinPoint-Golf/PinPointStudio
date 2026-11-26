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

// THE LINUX "STORE" IS THE SETTINGS FILE, AND THAT IS NOT A SECURE STORE. Linux is not a studio platform
// (the studios run macOS and Windows), and there is no secret store every desktop shares — libsecret needs
// a running keyring daemon, KWallet another — so rather than a dependency for a platform no coach sends
// from, the secret sits in ppSettings() under <service id>/password, readable by anyone who can read
// this user's home folder. <service id>/passwordAccount says which account it belongs to, so it is never
// offered to another server. One secret per service: writing another account's replaces it. Removing
// takes both, for that account only. The report-sending password's keys are ReportSending/password and
// ReportSending/passwordAccount, as they were before the store (nativeName(), secret_store.cpp).

#include "../Core/pp_settings.h"

namespace pinpoint::secrets {

bool platformWriteSecret(const NativeName &name, const QString &password, QString *error)
{
    QSettings s = ppSettings();
    s.setValue(name.settingsAccountKey, name.account);
    s.setValue(name.settingsSecretKey, password);
    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("The password could not be saved in the settings file.");
        return false;
    }
    return true;
}

QString platformReadSecret(const NativeName &name)
{
    const QSettings s = ppSettings();
    if (s.value(name.settingsAccountKey).toString() != name.account) return {};
    return s.value(name.settingsSecretKey).toString();
}

bool platformHasSecret(const NativeName &name)
{
    return !platformReadSecret(name).isEmpty();
}

bool platformRemoveSecret(const NativeName &name)
{
    QSettings s = ppSettings();
    if (s.value(name.settingsAccountKey).toString() == name.account) {
        s.remove(name.settingsSecretKey);
        s.remove(name.settingsAccountKey);
        s.sync();
    }
    return true;
}

} // namespace pinpoint::secrets
