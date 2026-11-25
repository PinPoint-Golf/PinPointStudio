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

#include <QNetworkRequest>
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
// Secrets: SecretsManager key "geminiApiKey" / env GEMINI_API_KEY. The caller hands the
// key in; it is sent in the x-goog-api-key request header, never in the request's address,
// so it cannot reach the app log or a proxy's log (a failed reply's error text quotes the
// address). A request carrying the key follows a redirect only to the same site, so a
// redirect cannot hand the key to another host. buildRequest() is the one place a request
// is made, and gemini_request_test checks it.

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

    // The streaming request for one chat() call: the model in the path, the key in the
    // x-goog-api-key header, redirects followed only to the same site. No network access;
    // chat() posts what this returns.
    static QNetworkRequest buildRequest(const QString &model, const QString &apiKey);

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
