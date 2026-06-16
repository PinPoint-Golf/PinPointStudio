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

#include <QObject>
#include <QString>

// Thin QML-facing wrapper over the static SecretsManager so the settings UI can
// read and write API keys (and react to their presence). SecretsManager itself
// is a non-QObject utility with env-var → QSettings precedence; this bridge adds
// change notification so toggles can gate on whether a provider key exists.
//
// Exposed to QML as the context property `secrets` (see main.cpp).
class SecretsBridge : public QObject
{
    Q_OBJECT
    // True when an Azure Speech key is configured (covers both STT and TTS —
    // STT falls back to the TTS key). hasGeminiKey covers the AI Coach (LLM).
    Q_PROPERTY(bool hasAzureKey  READ hasAzureKey  NOTIFY keysChanged)
    Q_PROPERTY(bool hasGeminiKey READ hasGeminiKey NOTIFY keysChanged)

public:
    explicit SecretsBridge(QObject *parent = nullptr) : QObject(parent) {}

    // Effective value of a secret (env var wins over QSettings — see SecretsManager).
    Q_INVOKABLE QString read(const QString &name) const;
    // Persist a secret to QSettings and notify listeners. Empty value clears it.
    Q_INVOKABLE void    write(const QString &name, const QString &value);

    bool hasAzureKey()  const;
    bool hasGeminiKey() const;

signals:
    // Emitted after any write(); drives hasAzureKey/hasGeminiKey re-evaluation and
    // lets controllers refresh cloud-fallback availability when a key is entered.
    void keysChanged();
};
