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

#include <QJSEngine>
#include <QJSValue>
#include <QJsonObject>
#include <QVariantMap>

// ⚠ READ THIS BEFORE PUBLISHING A BIG QVariantMap TO QML. It is the end-of-shot freeze.
//
// Qt hands a QVariantMap / QVariantList property to QML BY VALUE: every read of the property
// walks the whole tree and allocates a fresh JavaScript object for every node. There is no
// sharing and no caching — `a && a.series ? a.series : []` is THREE complete copies, and every
// binding in every live tile is another. The analysis detail of one swing is ~650k nodes (the
// 240 Hz synth pose tier alone is 380k; ~275k without it), it had ~18 readers across the two session screens the
// StackLayout keeps alive, and the whole cascade runs synchronously inside the notify emit, on
// the GUI thread, with the video stopped.
//
// Measured 16 Sept 2026 against the real 28 MB swing.json (bench: QQmlEngine + one
// Q_PROPERTY(QVariantMap), five reads per emit):
//     Mac, release Qt frameworks .................. 20 ms per copy
//     Studio PC, Release configuration ............ 19 ms per copy
//     Studio PC, Debug configuration (Qt6Qmld.dll)  554 ms per copy → one emit 0.7–1.5 s
// A Debug configuration on Windows links Qt's OWN debug DLLs, so the QML engine itself runs
// unoptimised; that is why the studio froze for 7.8 s per shot while the Mac never showed it.
//
// The rule this type enforces: convert ONCE per value, hand every reader the SAME JavaScript
// object. A Q_PROPERTY of type QJSValue is passed through to QML by reference (no copy), so
// the property costs one conversion when the value changes and nothing per read after that.
// The C++ side keeps reading the map. The one thing to remember on the QML side: the object
// is shared between readers — treat it as read-only (nothing in the codebase mutates it; a
// binding that needs a modified series builds its own array, as `_clubFan` does).
//
// Use it for anything QML-facing whose size grows with the swing: pose tiers, club/ball tracks,
// metric series. Small maps (a dozen scalar facts) can stay QVariantMap; the cost is per node.
class QmlPayload
{
public:
    QmlPayload() = default;
    explicit QmlPayload(QVariantMap map) : m_map(std::move(map)) {}

    // Replaces the value and drops the cached JavaScript object; the next QML read converts.
    void set(QVariantMap map)
    {
        m_map = std::move(map);
        m_js  = QJSValue();
    }
    QmlPayload &operator=(QVariantMap map) { set(std::move(map)); return *this; }

    // C++ readers: the map, as before. Implicitly shared, so copying it out is free.
    const QVariantMap &map() const { return m_map; }
    bool isEmpty() const { return m_map.isEmpty(); }

    // QML readers: the JavaScript object, built on first read and reused until set() is called.
    // The engine is the one main.cpp registers with setEngine() — the app's single QML engine.
    // (qjsEngine(owner) was tried first and is NOT reliable for a context-property object: its
    // JS wrapper is a weak reference the garbage collector drops between reads, after which the
    // lookup answers null and every read converts again — the bug this type exists to prevent.)
    // With no engine registered (unit tests, tools) the value is undefined, never a copy.
    //
    // ⚠ THE CONVERSION GOES THROUGH QJsonObject ON PURPOSE. engine->toScriptValue(QVariantMap)
    // looks right and is wrong: it yields a QJSValue that still carries the QVariant underneath,
    // and QML materialises a fresh object tree from it on EVERY property read — measured 42 ms
    // per emit on the Mac with four readers, identity (`src.detail === _d`) false. A QJsonObject
    // converts into plain JavaScript objects once; after that a read is a pointer, an emit whose
    // value did not change costs 0 ms, and every reader sees the same object. The price is JSON's
    // value model: integers become doubles (µs timestamps fit; 2^53) and anything that is not a
    // number, string, bool, list or map becomes null — these payloads are built from exactly
    // those types (toAnalysisDetail, SwingDocReader), so nothing is lost.
    // An empty map converts to `{}` — truthy, exactly as the QVariantMap property used to read,
    // so the `d && d.series` guards in QML keep their meaning.
    QJSValue js() const
    {
        if (!m_js.isUndefined())
            return m_js;
        if (!s_engine)
            return QJSValue();
        m_js = s_engine->toScriptValue(QJsonObject::fromVariantMap(m_map));
        return m_js;
    }

    // The QML engine every payload converts into. main.cpp calls this once, right after the
    // engine is constructed and before any QML is loaded. A payload converted under one engine
    // must never be read by another; this app has exactly one.
    static void setEngine(QJSEngine *engine) { s_engine = engine; }

private:
    static inline QJSEngine *s_engine = nullptr;
    QVariantMap      m_map;
    mutable QJSValue m_js;
};
