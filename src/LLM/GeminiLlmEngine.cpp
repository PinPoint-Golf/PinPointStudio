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

#include "GeminiLlmEngine.h"
#include "pp_debug.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

static constexpr char kBaseUrl[] =
    "https://generativelanguage.googleapis.com/v1beta/models/";

GeminiLlmEngine::GeminiLlmEngine(const QString &apiKey,
                                  const QString &model,
                                  QObject       *parent)
    : LlmEngine(parent)
    , m_apiKey(apiKey)
    , m_model(model)
{
}

bool GeminiLlmEngine::loadModel(const QString &)
{
    // No local files; create the network manager here so thread affinity is right.
    m_nam = new QNetworkAccessManager(this);
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_ready = true;
    emit modelLoaded();
    return true;
}

void GeminiLlmEngine::chat(const QVariantList &history, const QString &systemPrompt)
{
    if (!m_ready) {
        emit errorOccurred(QStringLiteral("Engine not ready"));
        return;
    }
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }

    m_accumulated.clear();
    m_lineBuf.clear();

    const QUrl url(QString::fromLatin1(kBaseUrl)
                   + m_model
                   + QStringLiteral(":streamGenerateContent?key=")
                   + m_apiKey
                   + QStringLiteral("&alt=sse"));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QByteArray body = buildRequestBody(history, systemPrompt);

    m_reply = m_nam->post(req, body);
    connect(m_reply, &QNetworkReply::readyRead, this, &GeminiLlmEngine::onReadyRead);
    connect(m_reply, &QNetworkReply::finished,  this, &GeminiLlmEngine::onFinished);
}

void GeminiLlmEngine::stop()
{
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
}

void GeminiLlmEngine::onReadyRead()
{
    if (!m_reply)
        return;

    m_lineBuf += m_reply->readAll();

    // Process complete lines delimited by \n.
    int pos = 0;
    while (true) {
        const int nl = m_lineBuf.indexOf('\n', pos);
        if (nl < 0)
            break;
        processLine(m_lineBuf.mid(pos, nl - pos).trimmed());
        pos = nl + 1;
    }
    m_lineBuf = m_lineBuf.mid(pos);
}

void GeminiLlmEngine::onFinished()
{
    if (!m_reply)
        return;

    if (m_reply->error() != QNetworkReply::NoError
            && m_reply->error() != QNetworkReply::OperationCanceledError) {
        const QString err = m_reply->errorString();
        ppWarn() << "[GeminiLLM] request failed:" << err;
        emit errorOccurred(err);
    } else if (!m_accumulated.isEmpty()) {
        emit responseReady(m_accumulated);
    }

    m_reply->deleteLater();
    m_reply = nullptr;
}

void GeminiLlmEngine::processLine(const QByteArray &line)
{
    // SSE format: "data: {json}"  or empty lines (keep-alive / separators).
    if (!line.startsWith("data:"))
        return;

    const QByteArray json = line.mid(5).trimmed();
    if (json.isEmpty() || json == "[DONE]")
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isNull())
        return;

    const QJsonArray candidates = doc.object()
        .value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty())
        return;

    const QJsonArray parts = candidates.first().toObject()
        .value(QStringLiteral("content")).toObject()
        .value(QStringLiteral("parts")).toArray();
    if (parts.isEmpty())
        return;

    const QString text = parts.first().toObject()
        .value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        return;

    m_accumulated += text;
    emit tokenReady(text);
}

QByteArray GeminiLlmEngine::buildRequestBody(const QVariantList &history,
                                              const QString      &systemPrompt)
{
    QJsonObject root;

    if (!systemPrompt.isEmpty()) {
        QJsonObject sysInstr;
        QJsonArray  sysParts;
        QJsonObject sysPart;
        sysPart[QStringLiteral("text")] = systemPrompt;
        sysParts.append(sysPart);
        sysInstr[QStringLiteral("parts")] = sysParts;
        root[QStringLiteral("system_instruction")] = sysInstr;
    }

    QJsonArray contents;
    for (const QVariant &v : history) {
        const QVariantMap msg  = v.toMap();
        const QString role = msg.value(QStringLiteral("role")).toString();
        const QString text = msg.value(QStringLiteral("text")).toString();

        QJsonObject part;
        part[QStringLiteral("text")] = text;

        QJsonObject content;
        // Gemini uses "user" and "model" roles (same convention we use internally).
        content[QStringLiteral("role")]  = role;
        content[QStringLiteral("parts")] = QJsonArray{ part };
        contents.append(content);
    }
    root[QStringLiteral("contents")] = contents;

    QJsonObject genCfg;
    genCfg[QStringLiteral("maxOutputTokens")] = 1024;
    genCfg[QStringLiteral("temperature")]     = 0.7;
    root[QStringLiteral("generationConfig")]  = genCfg;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
