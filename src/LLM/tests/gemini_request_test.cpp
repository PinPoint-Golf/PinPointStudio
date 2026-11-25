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
// error text, which the engine writes to the app log. Builds the request only: no network
// manager, no network call, and the key is a made-up sentinel.

#include "GeminiLlmEngine.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <cstdio>

static int g_fail = 0;

static void check(const char *label, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_fail;
}

int main()
{
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

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
