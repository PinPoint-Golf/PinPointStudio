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

// The Gemini engine's request carries the API key in the x-goog-api-key header and never in
// its address, where an inspecting proxy could record it and where Qt quotes it in a failed reply's
// error text, which the engine writes to the app log. A redirect re-sends the headers, so the
// request follows one only to the same site: checked on the request, then end to end against two
// local servers on this machine, where the first redirects to the second. No call leaves the
// machine, and the key is a made-up sentinel.

#include "GeminiLlmEngine.h"

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
#include <QUrlQuery>
#include <cstdio>

static int g_fail = 0;

static void check(const char *label, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString key   = QStringLiteral("SENTINEL-not-a-real-key_0123456789");
    const QString model = QStringLiteral("gemma-4-26b-a4b-it");

    const QNetworkRequest req = GeminiLlmEngine::buildRequest(model, key);
    const QUrl url = req.url();

    // The key appears nowhere in the address, in any of its spellings.
    const QString encoded = QString::fromLatin1(url.toEncoded());
    check("key absent from the encoded address",  !encoded.contains(key));
    check("key absent from the decoded address",
          !url.toString(QUrl::PrettyDecoded).contains(key)
          && !url.query(QUrl::FullyDecoded).contains(key));
    check("key absent from the percent-encoded key",
          !encoded.contains(QString::fromLatin1(QUrl::toPercentEncoding(key))));
    check("no key= item in the query",            !QUrlQuery(url).hasQueryItem(QStringLiteral("key")));

    // The key goes in the header Google's API reads.
    check("x-goog-api-key header present",        req.hasRawHeader("x-goog-api-key"));
    check("x-goog-api-key header is the key",     req.rawHeader("x-goog-api-key") == key.toUtf8());

    // The rest of the request is what chat() sent before the key moved.
    check("https, the Gemini API host",
          url.scheme() == QStringLiteral("https")
          && url.host() == QStringLiteral("generativelanguage.googleapis.com"));
    check("the model's streaming path",
          url.path() == QStringLiteral("/v1beta/models/") + model
                         + QStringLiteral(":streamGenerateContent"));
    check("alt=sse is the whole query",           url.query() == QStringLiteral("alt=sse"));
    check("JSON content type",
          req.header(QNetworkRequest::ContentTypeHeader).toString()
              == QStringLiteral("application/json"));

    // A redirect is followed only to the same scheme, host and port.
    const QVariant policy = req.attribute(QNetworkRequest::RedirectPolicyAttribute);
    check("redirect policy set on the request",   policy.isValid());
    check("redirect policy is same-origin only",
          policy.isValid() && policy.toInt() == QNetworkRequest::SameOriginRedirectPolicy);

    // End to end: the site redirects (307 keeps the POST and its headers) to another site, a
    // different port on this machine and so another origin. The manager is left at Qt's default
    // policy, which follows that redirect, so only the request's own policy can stop it.
    LocalSite other(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    LocalSite site(QByteArrayLiteral("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:")
                   + QByteArray::number(other.port())
                   + QByteArrayLiteral("/elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    check("two local servers listening",          site.server.isListening() && other.server.isListening());

    QNetworkRequest local = req;
    QUrl siteUrl = url;
    siteUrl.setScheme(QStringLiteral("http"));
    siteUrl.setHost(QStringLiteral("127.0.0.1"));
    siteUrl.setPort(site.port());
    local.setUrl(siteUrl);

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkReply *reply = nam.post(local, QByteArrayLiteral("{}"));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(10000);
    if (!reply->isFinished())
        loop.exec();

    check("the request finished within 10 s",     reply->isFinished());
    check("the site itself received the key",     site.received.contains(key.toUtf8()));
    check("the other site was never contacted",   !other.contacted);
    check("the other site never received the key", !other.received.contains(key.toUtf8()));
    check("the refused redirect is an error",     reply->error() != QNetworkReply::NoError);
    check("the error text does not carry the key", !reply->errorString().contains(key));
    std::printf("  (the reply's error: %d, \"%s\")\n", int(reply->error()),
                qPrintable(reply->errorString()));
    reply->abort();
    delete reply;

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
