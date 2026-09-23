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

// ppsw_qt.h — the Qt JSON model ↔ libppswing's document tree.
//
// The one subtlety is integers. Qt 6 keeps an integer JSON token as an integer internally
// (QJsonValue::toVariant() answers qlonglong), and a value built from an int stays one; a
// double stays a double even when it is integral. Both directions keep that distinction, so a
// document read from swing.ppsw holds the same QJsonValue types as one parsed from swing.json.
//
// Private to pinpoint_swingstore and its test: nothing else should need a ppsw type.

#include <ppswing/value.h>

#include <QJsonObject>
#include <QJsonValue>

namespace pinpoint::ppswqt {

ppsw::Value fromQt(const QJsonValue &v);
ppsw::Value fromQt(const QJsonObject &o);

// Any tree without Refs (resolve with Reader::loadAll first). NdArray and Table nodes expand to
// the plain arrays and objects a swing.json reader would have seen. A uint64 above INT64_MAX has
// no QJsonValue form and becomes a double — swing documents hold none.
QJsonValue  toQt(const ppsw::Value &v);
QJsonObject toQtObject(const ppsw::Value &v);   // empty unless v is an Object

} // namespace pinpoint::ppswqt
