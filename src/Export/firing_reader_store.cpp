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

#include "session_files.h"
#include "swing_doc.h"
#include "swing_store.h"
#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"
#include "../DomainModel/recording_repository.h"
#include "../DomainModel/session_repository.h"
#include "../Diagnostics/measures/live_measure_source.h"

#include <QFileInfo>
#include <QJsonDocument>

namespace pinpoint {

namespace {

// How long a firing read waits for the session's lock before it leaves a sidecar unwritten (a best-effort cache: the
// next read that misses writes it). Short: a firing job never holds up capture, a review edit or the archive.
constexpr int kSidecarLockWaitMs = 250;

// THE SIDECARS A FIRING READ REBUILT, WRITTEN ONLY UNDER THE SESSION'S LOCK AND ONLY OUTSIDE THE ARCHIVE WINDOW (refactor
// 3.8-ii; REVIEW-e R1-m6, J-58). The read itself never writes (ParseNoWrite): a job already on a worker when an archive
// starts can parse a swing the archive is about to thin, or a stub it has just thinned. The archiver writes its marker
// (archived.json) under this same lock BEFORE it thins the first swing, and re-checks it under the lock before each swing
// (session_archiver.cpp), so a write that finds no marker under the lock lands in a swing not yet thinned, and the
// thinning then removes it with the other caches; with the marker there, nothing is written. Each sidecar keeps its old
// guard: the document must still be the one parsed (LazyDocument::stillCurrent).
void writeSidecarsUnderLock(const QString &swingDir, const analysis::SwingPhaseGrid *grid, const SwingSummary *summary,
                            const SwingStore::LazyDocument &parsed)
{
    const QString sessionDir = QFileInfo(swingDir).absolutePath();
    FolderLock lock(sessionDir);
    if (lock.lockOrJoin(kSidecarLockWaitMs) != FolderLock::Result::Acquired)
        return;   // another writer holds the session: no index this time
    if (QFileInfo::exists(SessionFiles::archiveMarker(sessionDir)))
        return;   // the session is being, or has been, archived: the library keeps no cache of it
    if (!parsed.stillCurrent())
        return;   // rewritten since the parse: never index old content under the new document's guard
    if (grid)
        (void)atomicWrite(analysis::phaseGridPath(swingDir),
                          QJsonDocument(analysis::savePhaseGrid(*grid, parsed.parsedInfo().size, parsed.parsedInfo().mtimeMs))
                              .toJson(QJsonDocument::Compact),
                          AtomicWriteOptions::intoExistingFolder());
    if (summary && summary->ok)
        (void)SwingDocReader::writeSummarySidecar(*summary);
}

} // namespace

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
    // Always parsed on a miss or a stale sidecar (3.7-0, the D22 fix), and NEVER written by the read: ParseAndWrite's
    // sidecars are written below, under the session's lock and outside the archive window (R1-m6).
    out.readings.grid = analysis::readLivePhaseGrid(swingDir, pack, SidecarPolicy::ParseNoWrite, analysis::PhaseGridConfig{}, &parsed);
    const bool gridParsed = parsed.loaded();   // the grid came from the document, not from a current sidecar
    // The capture instant the document records, not the file's mtime (detectShot's ⚠); the mtime as the fallback.
    const SwingSummary summary = SwingDocReader::readSwingSummary(swingDir, SidecarPolicy::ParseNoWrite, &parsed);
    out.timestampMs = summary.wallclockMs > 0 ? summary.wallclockMs : SwingStore::info(swingDir).mtimeMs;
    out.captureMs   = summary.wallclockMs > 0 ? summary.wallclockMs : 0;   // the cohort's date: never the mtime
    // The shot's wrist readings for its checkpoint profile (refactor 3.8-ii; DEC-123): only when the grid names a wrist DOF.
    out.readings.wrist = analysis::readWristInput(swingDir, out.readings.grid, &parsed);
    if (mayWriteSidecar(m_policy) && parsed.loaded())
        writeSidecarsUnderLock(swingDir, gridParsed ? &out.readings.grid : nullptr, &summary, parsed);
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
