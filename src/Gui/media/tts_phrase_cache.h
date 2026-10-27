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
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

// The spoken phrases TtsController has synthesised IN ADVANCE (prepare), so that speak() of one of
// them plays at once instead of waiting for the synthesis (Mark, 27 Oct 2026: "the phrases should be
// prepared in advance, ideally when the session setup is instantiated — makes no sense to do it at the
// last minute"). Pure bookkeeping, no audio: the payload is a template parameter, so the key, the
// eviction and the invalidation are unit-tested without the audio stack (tts_phrase_cache_test).
//
// KEY: (backend, voice, text). The same words in another voice, or from the cloud instead of the local
// model, are different audio.
//
// GENERATION: invalidate() (the voice or the backend changed) empties the cache and bumps the
// generation. A synthesis started before it carries the old generation, and store() refuses it — the
// audio it produced is in the old voice. Pending marks (a prepare dispatched, its audio not back yet)
// make prepare idempotent while it is in flight.
//
// CAPACITY: a handful of phrases (the routines' instructions). Beyond it the least recently used goes.

struct TtsPhraseKey
{
    QString backend;
    QString voice;
    QString text;

    QString id() const { return backend + QChar(0x1f) + voice + QChar(0x1f) + text; }
    bool operator==(const TtsPhraseKey &o) const
    {
        return backend == o.backend && voice == o.voice && text == o.text;
    }
    bool operator!=(const TtsPhraseKey &o) const { return !(*this == o); }
};

template <typename Audio>
class TtsPhraseCache
{
public:
    static constexpr int kDefaultCapacity = 8;

    explicit TtsPhraseCache(int capacity = kDefaultCapacity)
        : m_capacity(capacity < 1 ? 1 : capacity) {}

    int     capacity()   const { return m_capacity; }
    int     size()       const { return int(m_audio.size()); }
    quint64 generation() const { return m_generation; }

    bool contains(const TtsPhraseKey &k)  const { return m_audio.contains(k.id()); }
    bool isPending(const TtsPhraseKey &k) const { return m_pending.contains(k.id()); }

    // The audio for k, or nullptr; a hit makes k the most recently used.
    const Audio *find(const TtsPhraseKey &k)
    {
        const QString id = k.id();
        auto it = m_audio.constFind(id);
        if (it == m_audio.constEnd())
            return nullptr;
        m_order.removeOne(id);
        m_order.append(id);
        return &it.value();
    }

    // A prepare is about to be dispatched for k: false (nothing to do) when k is already cached or
    // already pending; otherwise k is marked pending and true is returned.
    bool beginPrepare(const TtsPhraseKey &k)
    {
        const QString id = k.id();
        if (m_audio.contains(id) || m_pending.contains(id))
            return false;
        m_pending.insert(id);
        return true;
    }

    // The prepare's audio is back. Stored (and k no longer pending) unless the cache was invalidated
    // since the prepare began (`generation` is what generation() was then): stale audio is refused.
    // The least recently used phrase is evicted beyond capacity. Returns whether it was stored.
    bool store(const TtsPhraseKey &k, const Audio &audio, quint64 generation)
    {
        if (generation != m_generation)
            return false;
        const QString id = k.id();
        m_pending.remove(id);
        m_audio.insert(id, audio);
        m_order.removeOne(id);
        m_order.append(id);
        while (m_order.size() > m_capacity)
            m_audio.remove(m_order.takeFirst());
        return true;
    }

    // The prepare failed or was dropped: k is no longer pending (only if nothing invalidated since).
    void abandon(const TtsPhraseKey &k, quint64 generation)
    {
        if (generation == m_generation)
            m_pending.remove(k.id());
    }

    // The voice or the backend changed: everything cached or pending is the old audio.
    void invalidate()
    {
        ++m_generation;
        m_audio.clear();
        m_order.clear();
        m_pending.clear();
    }

private:
    int                    m_capacity;
    quint64                m_generation = 0;
    QHash<QString, Audio>  m_audio;
    QStringList            m_order;     // least recently used first
    QSet<QString>          m_pending;
};
