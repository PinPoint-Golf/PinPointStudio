/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "firing_reader_store.h"

#include "swing_doc.h"
#include "swing_store.h"
#include "../DomainModel/recording_repository.h"
#include "../DomainModel/session_repository.h"
#include "../Diagnostics/measures/live_measure_source.h"

namespace pinpoint {

DocumentStamp SwingStoreFiringReader::stamp(const ShotRef &shot) const
{
    const QString swingDir = m_sessions.swingFolder(shot);
    if (swingDir.isEmpty())
        return {};
    const SwingStore::DocInfo doc = SwingStore::info(swingDir);
    return { doc.exists(), doc.size, doc.mtimeMs };
}

ShotReview SwingStoreFiringReader::review(const ShotRef &shot) const
{
    return m_recordings.readReview(shot);   // safe from any thread (golferInput reads reviews on a pool)
}

FiringRead SwingStoreFiringReader::read(const ShotRef &shot, const analysis::CharacteristicPack &pack) const
{
    FiringRead out;
    const QString swingDir = m_sessions.swingFolder(shot);
    if (swingDir.isEmpty()) {
        out.error = QStringLiteral("the session of %1 is not open").arg(shot.toString());
        return out;
    }
    // STAMPED BEFORE THE READ (detectShot's order): a re-analysis that rewrites the document after this point leaves a
    // stamp that no longer matches, so the next load asks the shot again.
    const SwingStore::DocInfo doc = SwingStore::info(swingDir);
    out.stamp = { doc.exists(), doc.size, doc.mtimeMs };
    // NO DOCUMENT YET, NO READING: a swing folder is made when the swing is allocated, seconds before its document.
    if (!doc.exists()) {
        out.error = QStringLiteral("no swing document yet for %1").arg(shot.toString());
        return out;
    }

    // ONE PARSE FOR BOTH READERS (issue #142), released straight after them (detectShot's comment holds).
    SwingStore::LazyDocument parsed(swingDir);
    out.readings.grid = analysis::readLivePhaseGrid(swingDir, pack, /*writeSidecar=*/true, analysis::PhaseGridConfig{},
                                                    &parsed);
    // The capture instant the document records, not the file's mtime (detectShot's ⚠); the mtime as the fallback.
    const SwingSummary summary = SwingDocReader::readSwingSummary(swingDir, /*writeSidecar=*/true, &parsed);
    out.timestampMs = summary.wallclockMs > 0 ? summary.wallclockMs : SwingStore::info(swingDir).mtimeMs;
    out.captureMs   = summary.wallclockMs > 0 ? summary.wallclockMs : 0;   // the cohort's date: never the mtime
    parsed.release();

    out.readings.handedness    = summary.handedness;
    out.readings.dataWarning   = summary.dataWarning;
    out.readings.captureBroken = summary.dataWarningDetail.value(QStringLiteral("capture")).toBool();
    out.readings.imuBroken     = summary.dataWarningDetail.value(QStringLiteral("imu")).toBool();

    // The golfer's word on the shot (its review, FILE-SHOT-02): the document's root alone, through the swing door.
    out.review = m_recordings.readReview(shot);
    out.ok = true;
    return out;
}

} // namespace pinpoint
