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

#include "SecretsManager.h"
#include "pp_settings.h"
#include <QProcessEnvironment>

// Converts camelCase key to UPPER_SNAKE_CASE for env var lookup.
// "assemblyaiApiKey" → "ASSEMBLYAI_API_KEY"
QString SecretsManager::toEnvVarName(const QString& key)
{
    QString result;
    result.reserve(key.size() + 4);
    for (QChar c : key) {
        if (c.isUpper() && !result.isEmpty())
            result += QLatin1Char('_');
        result += c.toUpper();
    }
    return result;
}

QString SecretsManager::read(const QString& key)
{
    const QString envVal =
        QProcessEnvironment::systemEnvironment().value(toEnvVarName(key));
    if (!envVal.isEmpty())
        return envVal;

    return ppSettings().value(QStringLiteral("secrets/") + key).toString();
}

void SecretsManager::write(const QString& key, const QString& value)
{
    ppSettings().setValue(QStringLiteral("secrets/") + key, value);
}

// Persists keys into QSettings on first run so they survive without the
// original source on subsequent launches.
//
// Two seeding routes:
//   - Compile-time: -DASSEMBLYAI_API_KEY=<val> → ASSEMBLYAI_API_KEY_DEFAULT define
//   - Runtime env var: set AZURE_TTS_API_KEY (or any key in kRuntimeEnvKeys)
//     before the first launch; value is saved to QSettings and the env var is
//     not required again after that.
void SecretsManager::initializeDefaults()
{
    QSettings settings = ppSettings();

#ifdef ASSEMBLYAI_API_KEY_DEFAULT
    {
        const QString k = QStringLiteral("secrets/assemblyaiApiKey");
        if (settings.value(k).toString().isEmpty())
            settings.setValue(k, QStringLiteral(ASSEMBLYAI_API_KEY_DEFAULT));
    }
#endif

    // Keys whose values come from runtime env vars rather than compile-time defines.
    static const char * const kRuntimeEnvKeys[] = { "azureTtsApiKey", "azureSttApiKey" };
    const auto env = QProcessEnvironment::systemEnvironment();
    for (const char *key : kRuntimeEnvKeys) {
        const QString qkey       = QString::fromLatin1(key);
        const QString settingKey = QStringLiteral("secrets/") + qkey;
        if (settings.value(settingKey).toString().isEmpty()) {
            const QString val = env.value(toEnvVarName(qkey));
            if (!val.isEmpty())
                settings.setValue(settingKey, val);
        }
    }
}
