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

// The pack and corridor editor's typed QML API, read the way QML reads it (refactor step 3.9-ii).
//
// ModelBrowser hands QML Q_GADGET values (model_browser.h). QML reads them BY PROPERTY NAME, through
// each gadget's meta-object. These helpers do the same: asMap() turns a gadget into a map of its
// declared properties (recursively, a QList of a gadget into a list of maps), so a test written
// against the field names checks exactly the names QML binds to — a field a gadget does not declare
// reads as an absent key, as it would read `undefined` in QML.
//
// One difference from a map, and the tests that care say so: a gadget has no absent field. A field
// the old map left out is here its default ("" / false / 0 / an empty list), so a test of ABSENCE
// (`contains(key)`) is a test of the default instead. A QVariant field (a cell's or a field's
// `value`) is the exception: invalid where the map had no key, as before.
//
// The other direction: rowQuery(), graphOptions() and plotOptions() turn the option maps the tests
// were written with into the typed arguments, key by key — what QML does when it passes a plain
// object to a QML_STRUCTURED_VALUE parameter.

#include "model_browser.h"   // src/Gui/diagnosticmodel, on the tests' include path

#include <QMetaProperty>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>

namespace model_browser_test {

// A value as QML sees it: a gadget becomes the map of its properties, a list of gadgets (or of
// numbers) a list, anything else itself.
inline QVariant asVariant(const QVariant &v)
{
    const QMetaType t = v.metaType();
    if (!v.isValid()) return v;
    if (t.flags() & QMetaType::IsGadget) {
        const QMetaObject *mo = t.metaObject();
        QVariantMap        m;
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty p = mo->property(i);
            m.insert(QString::fromLatin1(p.name()), asVariant(p.readOnGadget(v.constData())));
        }
        return m;
    }
    switch (t.id()) {
    case QMetaType::QString: case QMetaType::QStringList: case QMetaType::QByteArray:
    case QMetaType::QVariantList: case QMetaType::QVariantMap:
        return v;
    default:
        break;
    }
    // A QList of a gadget, or of numbers: Qt converts any registered sequential container to a list.
    if (v.canConvert<QVariantList>()) {
        QVariantList l;
        for (const QVariant &e : v.toList()) l.append(asVariant(e));
        return l;
    }
    return v;
}

template <typename T> QVariantMap asMap(const T &gadget)
{
    return asVariant(QVariant::fromValue(gadget)).toMap();
}

template <typename T> QVariantList asMaps(const QList<T> &list)
{
    QVariantList out;
    for (const T &g : list) out.append(asMap(g));
    return out;
}

// A row, with the two C++-only members rows() sorts and searches on (not on the QML boundary, so not
// properties): the tests that check the sort keys read them here.
inline QVariantList asMaps(const QList<ModelRow> &list)
{
    QVariantList out;
    for (const ModelRow &r : list) {
        QVariantMap m = asMap(r);
        m.insert(QStringLiteral("sortKeys"), r.sortKeys);
        m.insert(QStringLiteral("searchText"), r.searchText);
        out.append(m);
    }
    return out;
}

// The query a filters map names: { search, sort, descending, facets: { key: [values…] }, ids }.
inline ModelRowQuery rowQuery(const QVariantMap &filters)
{
    ModelRowQuery q;
    q.search     = filters.value(QStringLiteral("search")).toString();
    q.sort       = filters.value(QStringLiteral("sort")).toString();
    q.descending = filters.value(QStringLiteral("descending")).toBool();
    q.ids        = filters.value(QStringLiteral("ids")).toStringList();
    const QVariantMap facets = filters.value(QStringLiteral("facets")).toMap();
    for (auto it = facets.constBegin(); it != facets.constEnd(); ++it) {
        ModelFacetPick p;
        p.key    = it.key();
        p.values = it.value().toStringList();
        q.facets.append(p);
    }
    return q;
}

inline ModelGraphOptions graphOptions(const QVariantMap &o)
{
    ModelGraphOptions g;
    const auto num = [&o](const char *k, double &into) {
        const QVariant v = o.value(QString::fromLatin1(k));
        if (v.isValid()) into = v.toDouble();
    };
    const auto whole = [&o](const char *k, int &into) {
        const QVariant v = o.value(QString::fromLatin1(k));
        if (v.isValid()) into = v.toInt();
    };
    const auto flag = [&o](const char *k, bool &into) {
        const QVariant v = o.value(QString::fromLatin1(k));
        if (v.isValid()) into = v.toBool();
    };
    num("nodeH", g.nodeH); num("gapX", g.gapX); num("gapY", g.gapY); num("rowH", g.rowH);
    num("captionH", g.captionH); num("rowCharW", g.rowCharW); num("rowMaxW", g.rowMaxW);
    num("padX", g.padX); num("charW", g.charW); num("minW", g.minW); num("maxW", g.maxW);
    whole("depth", g.depth); whole("maxPerRank", g.maxPerRank); whole("maxPerExpand", g.maxPerExpand);
    g.expanded = o.value(QStringLiteral("expanded")).toStringList();
    flag("includeMeasures", g.includeMeasures); flag("includeReferences", g.includeReferences);
    flag("includeScreened", g.includeScreened); flag("hideWeak", g.hideWeak);
    flag("hideProposed", g.hideProposed);
    return g;
}

inline ModelCorridorPlotOptions plotOptions(const QVariantMap &o)
{
    ModelCorridorPlotOptions p;
    const auto num = [&o](const char *k, double &into) {
        const QVariant v = o.value(QString::fromLatin1(k));
        if (v.isValid()) into = v.toDouble();
    };
    const auto whole = [&o](const char *k, int &into) {
        const QVariant v = o.value(QString::fromLatin1(k));
        if (v.isValid()) into = v.toInt();
    };
    num("width", p.width); num("height", p.height); num("windowMin", p.windowMin);
    num("windowMax", p.windowMax); whole("curveSteps", p.curveSteps);
    whole("maxSamplePoints", p.maxSamplePoints); num("previewMu", p.previewMu);
    num("previewIdealLo", p.previewIdealLo); num("previewIdealHi", p.previewIdealHi);
    return p;
}

} // namespace model_browser_test
