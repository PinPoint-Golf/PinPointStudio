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
#include "STTBackend.h"
#include <QNetworkRequest>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// STT backend that transcribes audio via the Azure Cognitive Services Speech REST API.
// Used automatically when no GPU is available for local (whisper.cpp) inference and an
// Azure Speech key is configured (azureSttApiKey or azureTtsApiKey).
//
// Each transcribe() call POSTs a WAV chunk to Azure and emits transcriptionReady on
// success.  Silence gating is enabled so only speech-bearing chunks are sent.
//
// The key is sent in the Ocp-Apim-Subscription-Key header. A request carrying it follows a
// redirect only to the same site, so a redirect cannot hand the key to another host.
// buildRequest() is the one place a request is made, and azure_speech_request_test checks it.
class STTBackendAzure : public STTBackend {
    Q_OBJECT
public:
    explicit STTBackendAzure(const QString& apiKey, QObject* parent = nullptr);

    bool    loadModel(const QString& modelPath) override;
    void    transcribe(const std::vector<float>& pcmF32) override;
    void    stopStreaming() override;
    bool    isReady()              const override;
    bool    requiresModelFile()    const override { return false; }
    bool    requiresSilenceGating() const override { return true; }
    QString backendLabel()         const override { return QStringLiteral("Cloud"); }

    // The request for one transcribe() call: the key in the Ocp-Apim-Subscription-Key header,
    // redirects followed only to the same site. No network access; transcribe() posts it.
    static QNetworkRequest buildRequest(const QString& apiKey);

private:
    static QByteArray buildWav(const std::vector<float>& pcmF32);

    QString                m_apiKey;
    QNetworkAccessManager* m_nam   = nullptr;
    QNetworkReply*         m_reply = nullptr;
};
