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

#pragma once

// SwingStoreFiringReader — FiringReader (src/DomainModel/firing_reader.h) over the swing documents (refactor step
// 3.5-i). Adapters ring.
//
// THE READ IS THE OLD ENGINE'S PER-SHOT READ, MOVED, NOT REWRITTEN (that engine was deleted in refactor 5.4b; refactor 3.3 left it
// there): the document's stamp taken BEFORE the read; no document → not read ("no document yet"); one lazy parse
// shared by the phase grid (readLivePhaseGrid, its sidecar written unless the policy says not) and the summary
// (readSwingSummary, likewise); the capture instant from clock.wallclock, else the file's mtime; the document released straight after;
// the hand and the two integrity facts. Plus the review's shot intent and label corrections, through the swing door
// (RecordingRepository::readReview, safe from any thread: the document's root alone, never its tracks), so a re-fire
// costs one read of the shot.
//
// The folder is resolved ONLY through SessionRepository::swingFolder (any thread: a mutex-guarded copy of what the
// identity map publishes). "" — the session not published (trashed, released, its library gone) — reads nothing.
// Safe from any thread; holds references to the two repositories, which outlive every job (the composition root's).

#include "../DomainModel/firing_reader.h"
#include "../Core/sidecar_policy.h"

namespace pinpoint {

class RecordingRepository;
class SessionRepository;

class SwingStoreFiringReader final : public FiringReader
{
public:
    // `writeSidecars`: ONLY whether the read may write the phase-grid and summary sidecars it rebuilds (the firing read's
    // behaviour, a known interim against R-CON-3: CARRY C5). The read ALWAYS parses the document when no current
    // sidecar serves it — a firing job runs on a worker, never a GUI thread — so true is SidecarPolicy::ParseAndWrite
    // and false is ParseNoWrite (src/Core/sidecar_policy.h). FALSE for the parallel run (refactor 3.5-ii; §4.9 "in
    // memory only"; pinpoint::parallel::doors, used by the tools and tests since 3.10 deleted the app's switch), so the
    // old model, where one runs beside, stays the only writer of every file of the session. Until 3.7-0 false also
    // meant "never parse" (the readers' old one bool), so the parallel run fired every live shot on an empty grid
    // before the old model's End sweep wrote the sidecars (D22).
    SwingStoreFiringReader(const SessionRepository &sessions, RecordingRepository &recordings, bool writeSidecars = true)
        : m_sessions(sessions), m_recordings(recordings),
          m_policy(writeSidecars ? SidecarPolicy::ParseAndWrite : SidecarPolicy::ParseNoWrite) {}

    SidecarPolicy policy() const { return m_policy; }

    FiringRead    read(const ShotRef &shot, const analysis::CharacteristicPack &pack) const override;
    DocumentStamp stamp(const ShotRef &shot) const override;
    ShotReview    review(const ShotRef &shot) const override;
    // The summary's day readings (refactor 5.5d; DEC-74): the grid exactly as read() reads it (its sidecar written, as
    // read()'s is, only under this reader's policy, the lock and outside the archive window), and the first stream
    // block's cameras (SwingStore::loadAnalysis: the root chunk, never the tracks).
    std::optional<analysis::SwingPhaseGrid> phaseGrid(const ShotRef &shot, const analysis::CharacteristicPack &pack) const override;
    analysis::SessionCameras                cameras(const ShotRef &shot) const override;

private:
    const SessionRepository &m_sessions;
    RecordingRepository     &m_recordings;
    SidecarPolicy            m_policy = SidecarPolicy::ParseAndWrite;   // never CachedOnly
};

} // namespace pinpoint
