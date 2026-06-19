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

#include "reanalysis_controller.h"

#include "../Core/pp_debug.h"

ReanalysisController::ReanalysisController(QObject *parent)
    : QObject(parent)
{
}

void ReanalysisController::reanalyse(const QVariantList &ids)
{
    if (ids.isEmpty()) {
        ppInfo() << "[Reanalysis] reanalyse() called with no shots — ignored";
        return;
    }

    // Seam only: the real pipeline (reload swing.json + media, re-run
    // pose/biomech, re-score, write through) lands in a follow-up. Log the
    // request via PpMessageLog (ppInfo routes there) and let the host toast.
    ppInfo() << "[Reanalysis] queued" << ids.size() << "shot(s) — id(s):" << ids;
    emit reanalyseQueued(ids.size());
}

void ReanalysisController::setReanalysing(bool on)
{
    if (m_reanalysing == on)
        return;
    m_reanalysing = on;
    emit reanalysingChanged();
}
