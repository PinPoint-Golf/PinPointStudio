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

// THE REPORT'S NUMBER FOR A SWING IN A CHARTS PAYLOAD (step 2.7b; audit F-07). The report numbers a swing by its
// swing number (ID-SWING-NUMBER, DEC-91): `swingNoOfDir` maps each swing the report reads (cleaned folder) to it.
// A swing the payload names that the report does not list (analysed, but with no ledger row) is numbered by the
// swing number the payload carries (`shotNo`, else `shotId`), else its folder's (SwingPaths) — never the
// presenter's `ordinal`, a rank, which after a gap can equal another swing's printed number. The ordinal only
// when none of those is there (a fixture with no folder).

#include "../../Stores/swing_paths.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QVariantMap>

namespace pinpoint::report {

inline int reportSwingNo(const QHash<QString, int> &swingNoOfDir, const QVariantMap &m)
{
    const QString dir = QDir::cleanPath(m.value(QStringLiteral("swingDir")).toString());
    if (const auto it = swingNoOfDir.constFind(dir); it != swingNoOfDir.constEnd()) return it.value();
    for (const char *k : { "shotNo", "shotId" })
        if (const int n = m.value(QLatin1String(k)).toInt(); n > 0) return n;
    if (const int n = pinpoint::SwingPaths::swingNumberOf(QFileInfo(dir).fileName()); n > 0) return n;
    return m.value(QStringLiteral("ordinal")).toInt();
}

} // namespace pinpoint::report
