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

// TtsPhraseCache (media/tts_phrase_cache.h) — the bookkeeping behind TtsController's prepared phrases
// (Mark, 27 Oct 2026: "the phrases should be prepared in advance"). No audio stack: the payload is a
// plain QByteArray here, as it is PCM in the controller.
//
//   K1 the key is (backend, voice, text): another voice or backend is a miss
//   K2 beginPrepare is idempotent: a cached or pending phrase is not prepared twice
//   K3 invalidate (voice / backend changed) empties it, and a synthesis begun before it is refused
//   K4 capacity: the least recently used phrase goes; a hit refreshes it
//   K5 abandon clears a pending mark (a failed prepare can be asked for again) — but not a newer one

#include "media/tts_phrase_cache.h"

#include <QByteArray>
#include <QCoreApplication>

#include <cstdio>

static int g_fail = 0;
static int g_run  = 0;

static void check(const char *label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", label);
        ++g_fail;
    }
}

using Cache = TtsPhraseCache<QByteArray>;

static TtsPhraseKey key(const char *text, const char *voice = "af_sky", const char *backend = "local")
{
    return { QString::fromLatin1(backend), QString::fromLatin1(voice), QString::fromLatin1(text) };
}

static void testKey()
{
    Cache c;
    const auto k = key("Turn back to address.");
    check("K1 a fresh cache holds nothing", !c.contains(k) && c.find(k) == nullptr && c.size() == 0);
    check("K1 beginPrepare of a new phrase says go", c.beginPrepare(k));
    check("K1 stored", c.store(k, QByteArray("pcm-sky"), c.generation()));
    check("K1 found, its audio", c.find(k) && *c.find(k) == QByteArray("pcm-sky"));
    check("K1 another voice is a miss", !c.contains(key("Turn back to address.", "bf_emma")));
    check("K1 another backend is a miss", !c.contains(key("Turn back to address.", "af_sky", "cloud")));
    check("K1 another text is a miss", !c.contains(key("Turn back to address")));
    check("K1 the parts do not run together", key("c", "ab").id() != key("bc", "a").id());
}

static void testIdempotent()
{
    Cache c;
    const auto k = key("Turn back to address.");
    check("K2 first prepare goes", c.beginPrepare(k));
    check("K2 pending", c.isPending(k) && !c.contains(k));
    check("K2 a second prepare while pending does not", !c.beginPrepare(k));
    c.store(k, QByteArray("pcm"), c.generation());
    check("K2 stored → no longer pending", !c.isPending(k) && c.contains(k));
    check("K2 a prepare of a cached phrase does not", !c.beginPrepare(k));
}

static void testInvalidate()
{
    Cache c;
    const auto k = key("Turn back to address.");
    c.beginPrepare(k);
    c.store(k, QByteArray("pcm"), c.generation());
    const quint64 g0 = c.generation();
    c.beginPrepare(key("Other."));
    c.invalidate();
    check("K3 invalidate empties it", c.size() == 0 && !c.contains(k));
    check("K3 and clears pending", !c.isPending(key("Other.")));
    check("K3 the generation moves on", c.generation() == g0 + 1);
    check("K3 audio from a synthesis begun before is refused", !c.store(key("Other."), QByteArray("old"), g0));
    check("K3 nothing was stored", !c.contains(key("Other.")));
    check("K3 after it, a prepare goes again", c.beginPrepare(k));
    check("K3 and is stored under the new generation", c.store(k, QByteArray("new"), c.generation()) && *c.find(k) == QByteArray("new"));
}

static void testCapacity()
{
    Cache c(3);
    check("K4 capacity as given", c.capacity() == 3);
    check("K4 a capacity below one is one", Cache(0).capacity() == 1);
    check("K4 the default is a handful", Cache().capacity() == Cache::kDefaultCapacity && Cache::kDefaultCapacity >= 4);
    for (const char *t : { "a", "b", "c" })
        c.store(key(t), QByteArray(t), c.generation());
    (void)c.find(key("a"));                       // a is now the most recently used
    c.store(key("d"), QByteArray("d"), c.generation());
    check("K4 still three", c.size() == 3);
    check("K4 the least recently used went (b)", !c.contains(key("b")));
    check("K4 the refreshed one stayed (a)", c.contains(key("a")) && c.contains(key("c")) && c.contains(key("d")));
    c.store(key("c"), QByteArray("c2"), c.generation());   // re-store: replaced, refreshed, not duplicated
    c.store(key("e"), QByteArray("e"), c.generation());
    check("K4 a re-stored phrase is replaced and refreshed", c.size() == 3 && *c.find(key("c")) == QByteArray("c2")
                                                             && !c.contains(key("a")));
}

static void testAbandon()
{
    Cache c;
    const auto k = key("Turn back to address.");
    c.beginPrepare(k);
    c.abandon(k, c.generation());
    check("K5 abandoned → not pending, can be asked again", !c.isPending(k) && c.beginPrepare(k));
    const quint64 g0 = c.generation();
    c.invalidate();
    c.beginPrepare(k);
    c.abandon(k, g0);
    check("K5 an old prepare's abandon leaves the new pending mark", c.isPending(k));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("tts_phrase_cache_test — TtsController's prepared phrases (tts_phrase_cache.h)\n");
    testKey();
    testIdempotent();
    testInvalidate();
    testCapacity();
    testAbandon();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
