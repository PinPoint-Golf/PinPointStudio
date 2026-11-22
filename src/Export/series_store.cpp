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

#include "series_store.h"
#include "session_diagnostics_files.h"   // identityOf: the swing's session's day
#include "swing_paths.h"
#include "swing_store.h"
#include "../DomainModel/session_repository.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace pinpoint {

QString SwingStoreSeriesReader::folderOf(const ShotRef &shot) const
{
    if (m_sessions) return m_sessions->swingFolder(shot);
    return m_folderOf ? m_folderOf(shot) : QString();
}

DocumentStamp SwingStoreSeriesReader::stamp(const ShotRef &shot) const
{
    const QString swingDir = folderOf(shot);
    if (swingDir.isEmpty()) return {};
    const SwingStore::DocInfo doc = SwingStore::info(swingDir);
    return { doc.exists(), doc.size, doc.mtimeMs };
}

SeriesRead SwingStoreSeriesReader::analysis(const ShotRef &shot) const
{
    SeriesRead out;
    const QString swingDir = folderOf(shot);
    if (swingDir.isEmpty()) {
        out.error = QStringLiteral("the session of %1 is not open").arg(shot.toString());
        return out;
    }
    const SwingStore::DocInfo doc = SwingStore::info(swingDir);   // stamped BEFORE the read
    out.stamp = { doc.exists(), doc.size, doc.mtimeMs };
    if (!doc.exists()) {
        out.error = QStringLiteral("no swing document");
        return out;
    }
    QString err;
    out.root = SwingStore::loadAnalysis(swingDir, &err);
    if (out.root.isEmpty()) out.error = err.isEmpty() ? QStringLiteral("the swing document could not be read") : err;
    return out;
}

QString seriesSwingLabelOf(const QString &swingDir)
{
    const int n = swingDir.isEmpty() ? -1 : SwingPaths::swingNumberOf(QFileInfo(QDir::cleanPath(swingDir)).fileName());
    const QDate d = swingDir.isEmpty() ? QDate()
                  : SessionDiagnosticsFiles::identityOf(QFileInfo(QDir::cleanPath(swingDir)).absolutePath()).localDay();
    QStringList parts;
    if (n > 0) parts << QStringLiteral("shot %1").arg(n);
    if (d.isValid()) parts << QLocale(QLocale::English).toString(d, QStringLiteral("d MMM yyyy"));
    return parts.join(QStringLiteral(", "));
}

} // namespace pinpoint
