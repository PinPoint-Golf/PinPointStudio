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

#include "ppsw_qt.h"

#include <QJsonArray>
#include <cmath>

namespace pinpoint::ppswqt {

ppsw::Value fromQt(const QJsonValue &v)
{
    switch (v.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            return ppsw::Value();
        case QJsonValue::Bool:
            return ppsw::Value(v.toBool());
        case QJsonValue::Double: {
            // An integer stays an integer. Qt 6 itself holds every whole value that fits a qint64 as
            // an integer — an int, an integral JSON token, and a double such as 37.0 or -0.0 alike
            // (swing_store_test records it) — so "whole and in range" IS Qt's own int/double split.
            // Asked directly rather than through toVariant(): a QVariant per number was most of the
            // cost of saving a 650k-number pose track.
            const double d = v.toDouble();
            if (d >= -9223372036854775808.0 && d < 9223372036854775808.0 && d == std::trunc(d))
                return ppsw::Value(int64_t(d));
            return ppsw::Value(d);
        }
        case QJsonValue::String:
            return ppsw::Value(v.toString().toStdString());
        case QJsonValue::Array: {
            const QJsonArray a = v.toArray();
            ppsw::Array out;
            out.reserve(size_t(a.size()));
            for (const QJsonValue &e : a) out.push_back(fromQt(e));
            return ppsw::Value(std::move(out));
        }
        case QJsonValue::Object:
            return fromQt(v.toObject());
    }
    return ppsw::Value();
}

ppsw::Value fromQt(const QJsonObject &o)
{
    ppsw::Object out;
    out.members.reserve(size_t(o.size()));
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        out.add(it.key().toStdString(), fromQt(it.value()));
    return ppsw::Value(std::move(out));
}

namespace {

QJsonValue ndElement(const ppsw::NdArray &a, uint64_t i)
{
    if (a.isNull(i)) return QJsonValue(QJsonValue::Null);
    switch (a.dtype) {
        case ppsw::DType::Bool: return QJsonValue(a.asDouble(i) != 0.0);
        case ppsw::DType::F32:
        case ppsw::DType::F64:  return QJsonValue(a.asDouble(i));
        case ppsw::DType::U64: {
            const uint64_t u = a.asUInt64(i);
            if (u <= uint64_t(INT64_MAX)) return QJsonValue(qint64(u));
            return QJsonValue(double(u));
        }
        default:                return QJsonValue(qint64(a.asInt64(i)));
    }
}

// Row-major walk: dimension `dim` of the array, starting at element `cursor`.
QJsonArray ndToQt(const ppsw::NdArray &a, size_t dim, uint64_t &cursor)
{
    QJsonArray out;
    const uint64_t n = a.shape[dim];
    if (dim + 1 == a.shape.size()) {
        for (uint64_t i = 0; i < n; ++i) out.append(ndElement(a, cursor++));
    } else {
        for (uint64_t i = 0; i < n; ++i) out.append(ndToQt(a, dim + 1, cursor));
    }
    return out;
}

} // namespace

QJsonValue toQt(const ppsw::Value &v)
{
    using K = ppsw::Value::Kind;
    switch (v.kind()) {
        case K::Null:   return QJsonValue(QJsonValue::Null);
        case K::Bool:   return QJsonValue(v.asBool());
        case K::Int:    return QJsonValue(qint64(v.asInt()));
        case K::UInt:   return QJsonValue(double(v.asUInt()));
        case K::Double: return QJsonValue(v.asDouble());
        case K::String: return QJsonValue(QString::fromStdString(v.asString()));
        case K::Array: {
            QJsonArray out;
            for (const ppsw::Value &e : v.asArray()) out.append(toQt(e));
            return out;
        }
        case K::Object: {
            QJsonObject out;
            for (const auto &m : v.asObject().members)
                out.insert(QString::fromStdString(m.first), toQt(m.second));
            return out;
        }
        case K::NdArray: {
            uint64_t cursor = 0;
            return ndToQt(v.asNdArray(), 0, cursor);
        }
        case K::Table:
            // Column-wise with presence bitmaps — the library's own expansion is the one that is
            // tested against the spec, so reuse it rather than re-deriving the row layout here.
            return toQt(ppsw::toPlain(v));
        case K::Ref:
            break;   // unresolved: the caller should have loaded the whole tree
    }
    return QJsonValue(QJsonValue::Null);
}

QJsonObject toQtObject(const ppsw::Value &v)
{
    if (v.kind() != ppsw::Value::Kind::Object) return {};
    return toQt(v).toObject();
}

} // namespace pinpoint::ppswqt
