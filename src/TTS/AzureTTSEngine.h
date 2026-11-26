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
#include "TTSEngine.h"
#include <QNetworkRequest>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// TTSEngine backend that synthesises speech via Azure Cognitive Services TTS.
// Used automatically on CPU-only builds when an Azure TTS API key is available.
// Stores key under SecretsManager "azureTtsApiKey" / env AZURE_TTS_API_KEY.
// The key is sent in the Ocp-Apim-Subscription-Key header. A request carrying it follows a
// redirect only to the same site, so a redirect cannot hand the key to another host.
// buildRequest() is the one place a request is made, and azure_speech_request_test checks it.
//
// Connection lifecycle (one HTTP POST per synthesise() call):
//   - No persistent connection — each call opens, streams, and closes.
//   - stop() aborts any in-flight request; audioReady() will not fire for it.
class AzureTTSEngine : public TTSEngine {
    Q_OBJECT
public:
    explicit AzureTTSEngine(const QString &apiKey, QObject *parent = nullptr);

    bool    loadModel(const QString &, const QString &, const QString &) override;
    void    synthesise(const QString &text) override;
    void    stop() override;
    bool    isReady()     const override { return m_ready; }
    QString gpuBackend()  const override { return QStringLiteral("Cloud"); }

    // The request for one synthesise() call: the key in the Ocp-Apim-Subscription-Key header,
    // redirects followed only to the same site. No network access; synthesise() posts it.
    static QNetworkRequest buildRequest(const QString &apiKey);

private:
    QString buildSsml(const QString &text) const;

    QString                m_apiKey;
    QNetworkAccessManager *m_nam   = nullptr;
    QNetworkReply         *m_reply = nullptr;
    bool                   m_ready = false;
};
