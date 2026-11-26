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

// The Azure speech requests, text-to-speech (AzureTTSEngine) and speech-to-text (STTBackendAzure),
// carry the Azure key in the Ocp-Apim-Subscription-Key header. A redirect re-sends the headers, so
// each request follows one only to the same site: checked on the request, then end to end against
// two local servers on this machine, where the first redirects to the second. No call leaves the
// machine, and the key is a made-up sentinel.

#include "AzureTTSEngine.h"
#include "STTBackendAzure.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <string>

static int g_fail = 0;

static void check(const std::string &label, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++g_fail;
}

// A local HTTP server that answers each request, once its headers are in, with `response`, and
// keeps every byte it was sent.
struct LocalSite
{
    QTcpServer server;
    QByteArray received;
    bool       contacted = false;

    explicit LocalSite(const QByteArray &response)
    {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this, response]() {
            QTcpSocket *sock = server.nextPendingConnection();
            contacted = true;
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock, response]() {
                const bool hadHeaders = received.contains("\r\n\r\n");
                received += sock->readAll();
                if (!hadHeaders && received.contains("\r\n\r\n")) {
                    sock->write(response);
                    sock->disconnectFromHost();
                }
            });
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        });
    }
    quint16 port() const { return server.serverPort(); }
};

// One request kind: the policy on the request, then the request sent to a local site that
// redirects it (307 keeps the POST and its headers) to another site, a different port on this
// machine and so another origin. The manager is left at Qt's default policy, which follows that
// redirect, so only the request's own policy can stop it.
static void checkRedirect(const char *kind, const QNetworkRequest &req, const QString &key)
{
    const std::string k = std::string(kind) + ": ";

    const QVariant policy = req.attribute(QNetworkRequest::RedirectPolicyAttribute);
    check(k + "redirect policy set on the request",   policy.isValid());
    check(k + "redirect policy is same-origin only",
          policy.isValid() && policy.toInt() == QNetworkRequest::SameOriginRedirectPolicy);

    LocalSite other(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    LocalSite site(QByteArrayLiteral("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:")
                   + QByteArray::number(other.port())
                   + QByteArrayLiteral("/elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    check(k + "two local servers listening", site.server.isListening() && other.server.isListening());

    // The real request, with only its scheme, host and port pointed at the first local site.
    QNetworkRequest local = req;
    QUrl siteUrl = req.url();
    siteUrl.setScheme(QStringLiteral("http"));
    siteUrl.setHost(QStringLiteral("127.0.0.1"));
    siteUrl.setPort(site.port());
    local.setUrl(siteUrl);

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkReply *reply = nam.post(local, QByteArrayLiteral("body"));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(10000);
    if (!reply->isFinished())
        loop.exec();

    check(k + "the request finished within 10 s",       reply->isFinished());
    check(k + "the site itself received the key",       site.received.contains(key.toUtf8()));
    check(k + "the other site was never contacted",     !other.contacted);
    check(k + "the other site never received the key",  !other.received.contains(key.toUtf8()));
    check(k + "the refused redirect is an error",       reply->error() != QNetworkReply::NoError);
    check(k + "the refused redirect is not a cancel",   reply->error() != QNetworkReply::OperationCanceledError);
    check(k + "the error text does not carry the key",  !reply->errorString().contains(key));
    std::printf("  (%s: the reply's error: %d, \"%s\")\n", kind, int(reply->error()),
                qPrintable(reply->errorString()));
    reply->abort();
    delete reply;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString key = QStringLiteral("SENTINEL-not-a-real-key_0123456789");

    // Text to speech: the request synthesise() posts.
    const QNetworkRequest tts = AzureTTSEngine::buildRequest(key);
    check("TTS: key in the Ocp-Apim-Subscription-Key header",
          tts.rawHeader("Ocp-Apim-Subscription-Key") == key.toUtf8());
    check("TTS: key absent from the address",       !tts.url().toString().contains(key));
    check("TTS: https, the UK West speech host",
          tts.url().scheme() == QStringLiteral("https")
          && tts.url().host() == QStringLiteral("ukwest.tts.speech.microsoft.com")
          && tts.url().path() == QStringLiteral("/cognitiveservices/v1"));
    check("TTS: SSML content type, 24 kHz PCM out",
          tts.rawHeader("Content-Type") == "application/ssml+xml"
          && tts.rawHeader("X-Microsoft-OutputFormat") == "raw-24khz-16bit-mono-pcm");
    checkRedirect("TTS", tts, key);

    // Speech to text: the request transcribe() posts.
    const QNetworkRequest stt = STTBackendAzure::buildRequest(key);
    check("STT: key in the Ocp-Apim-Subscription-Key header",
          stt.rawHeader("Ocp-Apim-Subscription-Key") == key.toUtf8());
    check("STT: key absent from the address",       !stt.url().toString().contains(key));
    check("STT: https, the UK West speech host",
          stt.url().scheme() == QStringLiteral("https")
          && stt.url().host() == QStringLiteral("ukwest.stt.speech.microsoft.com")
          && stt.url().query() == QStringLiteral("language=en-GB&format=simple"));
    check("STT: WAV in, JSON back",
          stt.rawHeader("Content-Type") == "audio/wav; codec=audio/pcm; samplerate=16000"
          && stt.rawHeader("Accept") == "application/json");
    checkRedirect("STT", stt, key);

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
