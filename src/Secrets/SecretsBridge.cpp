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

#include "SecretsBridge.h"
#include "SecretsManager.h"

QString SecretsBridge::read(const QString &name) const
{
    return SecretsManager::read(name);
}

void SecretsBridge::write(const QString &name, const QString &value)
{
    // Trim stray whitespace/newlines that commonly ride along on a pasted key and
    // would otherwise corrupt the auth header (Azure/Gemini reject it as a 401).
    SecretsManager::write(name, value.trimmed());
    emit keysChanged();
}

bool SecretsBridge::hasAzureKey() const
{
    // The single Azure field writes azureTtsApiKey; an azureSttApiKey (e.g. via
    // env var) is also honoured by the STT pipeline, so either counts.
    return !SecretsManager::read(QStringLiteral("azureTtsApiKey")).isEmpty()
        || !SecretsManager::read(QStringLiteral("azureSttApiKey")).isEmpty();
}

bool SecretsBridge::hasGeminiKey() const
{
    return !SecretsManager::read(QStringLiteral("geminiApiKey")).isEmpty();
}
