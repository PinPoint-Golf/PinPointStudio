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
// THE READ IS detectShot's, MOVED, NOT REWRITTEN (session_diagnostics_model.cpp, detectShot; refactor 3.3 left it
// there): the document's stamp taken BEFORE the read; no document → not read ("no document yet"); one lazy parse
// shared by the phase grid (readLivePhaseGrid, its sidecar written) and the summary (readSwingSummary, its sidecar
// written); the capture instant from clock.wallclock, else the file's mtime; the document released straight after;
// the hand and the two integrity facts. Plus the review's shot intent and label corrections, through the swing door
// (RecordingRepository::readReview, safe from any thread: the document's root alone, never its tracks), so a re-fire
// costs one read of the shot.
//
// The folder is resolved ONLY through SessionRepository::swingFolder (any thread: a mutex-guarded copy of what the
// identity map publishes). "" — the session not published (trashed, released, its library gone) — reads nothing.
// Safe from any thread; holds references to the two repositories, which outlive every job (the composition root's).

#include "../DomainModel/firing_reader.h"

namespace pinpoint {

class RecordingRepository;
class SessionRepository;

class SwingStoreFiringReader final : public FiringReader
{
public:
    SwingStoreFiringReader(const SessionRepository &sessions, RecordingRepository &recordings)
        : m_sessions(sessions), m_recordings(recordings) {}

    FiringRead    read(const ShotRef &shot, const analysis::CharacteristicPack &pack) const override;
    DocumentStamp stamp(const ShotRef &shot) const override;
    ShotReview    review(const ShotRef &shot) const override;

private:
    const SessionRepository &m_sessions;
    RecordingRepository     &m_recordings;
};

} // namespace pinpoint
