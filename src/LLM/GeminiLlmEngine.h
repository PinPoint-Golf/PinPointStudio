/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once
#include "LlmEngine.h"

#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

// LLM engine that streams responses from the Google Gemini API.
// Used automatically when LocalLlmEngine detects CPU-only execution.
//
// Connection lifecycle:
//   - loadModel() is a no-op (no local file); emits modelLoaded() immediately.
//   - Each chat() call opens one HTTP POST to streamGenerateContent and streams
//     SSE events until the response is complete or stop() aborts the request.
//   - QNetworkAccessManager is created in loadModel() (on the worker thread)
//     so its thread affinity is correct.
//
// Secrets: SecretsManager key "geminiApiKey" / env GEMINI_API_KEY.

class GeminiLlmEngine : public LlmEngine
{
    Q_OBJECT
public:
    explicit GeminiLlmEngine(const QString &apiKey,
                             const QString &model = QStringLiteral("gemma-4-26b-a4b-it"),
                             QObject       *parent = nullptr);

    bool    loadModel(const QString &) override;
    void    chat(const QVariantList &history, const QString &systemPrompt) override;
    void    stop() override;
    bool    isReady()    const override { return m_ready; }
    QString gpuBackend() const override { return QStringLiteral("Cloud"); }

private slots:
    void onReadyRead();
    void onFinished();

private:
    static QByteArray buildRequestBody(const QVariantList &history,
                                       const QString      &systemPrompt);
    void processLine(const QByteArray &line);

    QString                m_apiKey;
    QString                m_model;
    QNetworkAccessManager *m_nam   = nullptr;
    QNetworkReply         *m_reply = nullptr;
    bool                   m_ready = false;
    QString                m_accumulated;
    QByteArray             m_lineBuf;  // incomplete SSE line buffer
};
