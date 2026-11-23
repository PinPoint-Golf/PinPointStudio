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

// SwingStoreSeriesReader — SeriesReader (src/DomainModel/series_reader.h) over the swing documents (refactor 5.5a).
// Adapters ring. The read the Charts panel's old loader made (SessionSeriesModel::ensureShot, deleted in 5.5a), moved:
// the document's stamp BEFORE the read (SwingStore::info), then SwingStore::loadAnalysis — the root chunk and
// /analysis/metrics, never a per-frame track (7–9 ms a swing, where the whole document costs 150–220 ms); document() is
// SwingStore::load, the whole document, for the Posture picture's poses only (refactor 5.5c). The folder is
// resolved ONLY through SessionRepository::swingFolder (any thread); "" reads nothing. Safe from any thread; holds a
// reference to the session repository, which outlives every job (the composition root's).
//
// Also here, the ★ reference swing's label (`seriesSwingLabelOf`): its swing number and its session's day, through the
// session store's one-argument identity (SessionDiagnosticsFiles::identityOf), for the Charts presenters' ★ label. Since
// 5.5c the ★ itself is read through its own session's series, never by folder.

#include "../DomainModel/series_reader.h"

#include <functional>

namespace pinpoint {

class SessionRepository;

class SwingStoreSeriesReader final : public SeriesReader
{
public:
    explicit SwingStoreSeriesReader(const SessionRepository &sessions) : m_sessions(&sessions) {}
    // A test's or a tool's: the folder of a shot from its own map (no repository).
    explicit SwingStoreSeriesReader(std::function<QString(const ShotRef &)> folderOf) : m_folderOf(std::move(folderOf)) {}

    DocumentStamp stamp(const ShotRef &shot) const override;
    SeriesRead    analysis(const ShotRef &shot) const override;
    SeriesRead    document(const ShotRef &shot) const override;

private:
    QString folderOf(const ShotRef &shot) const;

    const SessionRepository                  *m_sessions = nullptr;
    std::function<QString(const ShotRef &)>   m_folderOf;
};

// "shot 3, 18 Aug 2026": which swing a swing folder is, for a label (the Charts reference ★). Its swing number and its
// session's day (SessionIdentity's: the session file's start, else the day a legacy folder's name records). A part
// that is unknown is left out.
QString seriesSwingLabelOf(const QString &swingDir);

} // namespace pinpoint
