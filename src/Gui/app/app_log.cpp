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

#include "app_log.h"

#include "pp_debug.h"

namespace {

// One string, streamed as const char*: QDebug quotes a QString operand, and a log line reading
// "[Setup]" "page → Sensors" would be noise in every line the flow writes.
QString line(const QString &tag, const QString &text)
{
    return QLatin1Char('[') + tag + QLatin1String("] ") + text;
}

} // namespace

void AppLog::info(const QString &tag, const QString &text)
{
    ppInfo() << qUtf8Printable(line(tag, text));
}

void AppLog::warn(const QString &tag, const QString &text)
{
    ppWarn() << qUtf8Printable(line(tag, text));
}
