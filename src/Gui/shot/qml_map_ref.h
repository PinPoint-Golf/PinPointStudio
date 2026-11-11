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

#include <QMetaType>
#include <QVariant>
#include <QVariantMap>

// ⚠ A DETAIL QML ONLY PASSES ON TO C++: QmlMapRef. QML hands a C++ map to C++ through a QML
// binding by converting it TWICE — the map into JavaScript when it is read, and the JavaScript
// back into a QVariantMap when it is written to a C++ QVariantMap property. For the ~650k-node
// swing detail the second half alone was 1–2 s on every shot change: the Charts and Trust panels
// wrote shotReplay.analysisDetail into six presenters' pickedDetail (#173 follow-up, 11 Nov 2026).
// QmlMapRef is the map wrapped in a type QML does not know, so QML carries it as an opaque value —
// it can be stored in a `var`, tested for truth and passed on, but not looked inside — and the C++
// property it is written to unwraps the same implicitly shared map: no copy either way.
//   C++ side: expose `QVariant xRef` (QmlPayload::ref()); take it with a QVariant property whose
//   setter reads qmlMapRefValue(v). QML side: `presenter.pickedDetailRef: shotReplay.analysisDetailRef`.
struct QmlMapRef
{
    QVariantMap map;
};
Q_DECLARE_METATYPE(QmlMapRef)

// The map a QmlMapRef carries. A plain map (a test's, or `({})` from QML) is taken as it is, and
// anything else is empty.
inline QVariantMap qmlMapRefValue(const QVariant &v)
{
    if (v.metaType() == QMetaType::fromType<QmlMapRef>()) return v.value<QmlMapRef>().map;
    if (v.metaType() == QMetaType::fromType<QVariantMap>()) return v.toMap();
    return {};
}
inline QVariant qmlMapRef(const QVariantMap &m) { return QVariant::fromValue(QmlMapRef{ m }); }
