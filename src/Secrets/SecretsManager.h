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
#include <QString>

// Reads/writes application secrets from (priority order):
//   1. Environment variable: UPPER_SNAKE_CASE form of the key name
//      e.g. key "assemblyaiApiKey" → env var "ASSEMBLYAI_API_KEY"
//   2. QSettings under "secrets/<key>"
//      stored in the platform-native config location (no extra dependencies):
//        Linux  : ~/.config/<OrgName>/<AppName>.conf
//        macOS  : ~/Library/Preferences/<bundle-id>.plist
//        Windows: HKCU\Software\<OrgName>\<AppName>
//
// Call initializeDefaults() once at startup to seed QSettings from any
// compile-time defaults (set via -DASSEMBLYAI_API_KEY=<value> at cmake time).
class SecretsManager {
public:
    static QString read(const QString& key);
    static void    write(const QString& key, const QString& value);
    static void    initializeDefaults();

private:
    static QString toEnvVarName(const QString& key);
};
