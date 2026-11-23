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

// A TYPED RESULT AS A MAP, OFF THE QML BOUNDARY (refactor 5.5b; F-12, P9). The typed presenters hand QML Q_GADGET values
// (lint A6); two readers still want the old map shape and are not on the boundary: the report's payloads (until 5.6
// types the report) and the presenter tests' assertions. This is the ONE conversion, by reflection, so no presenter
// writes a second, hand-kept copy of its shape: a gadget becomes a QVariantMap of its properties (by name), a list of
// gadgets a QVariantList of those maps, recursively; any other value is kept as it is (a QVariant property holding
// null stays null).
//
//   QVariantMap m = chartsGadgetMap(presenter.thisShot());

#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QSequentialIterable>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace pinpoint::charts {

inline QVariant chartsGadgetVariant(const QVariant &v);

inline QVariantMap chartsGadgetMapOf(const QMetaObject &mo, const void *gadget)
{
    QVariantMap out;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
        const QMetaProperty p = mo.property(i);
        out.insert(QString::fromLatin1(p.name()), chartsGadgetVariant(p.readOnGadget(gadget)));
    }
    return out;
}

inline QVariant chartsGadgetVariant(const QVariant &v)
{
    const QMetaType t = v.metaType();
    if (!t.isValid()) return v;
    if (t.flags() & QMetaType::IsGadget) {
        if (const QMetaObject *mo = t.metaObject()) return chartsGadgetMapOf(*mo, v.constData());
        return v;
    }
    if (t.id() == QMetaType::QVariantMap) {
        QVariantMap m = v.toMap();
        for (auto it = m.begin(); it != m.end(); ++it) it.value() = chartsGadgetVariant(it.value());
        return m;
    }
    if (t.id() != QMetaType::QString && t.id() != QMetaType::QByteArray && t.id() != QMetaType::QStringList
        && v.canConvert<QSequentialIterable>()) {
        QVariantList out;
        const QSequentialIterable it = v.value<QSequentialIterable>();
        for (const QVariant &x : it) out.append(chartsGadgetVariant(x));
        return out;
    }
    return v;
}

template <class Gadget>
QVariantMap chartsGadgetMap(const Gadget &g)
{
    return chartsGadgetMapOf(Gadget::staticMetaObject, &g);
}

} // namespace pinpoint::charts
