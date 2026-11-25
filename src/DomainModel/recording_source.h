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

// RecordingSource — how any thread reads one shot's Recording (refactor step 3.4a; build/refactor/step-3/BRIEF.md
// §2.2, §4.3). The Shot hands it out (Shot::recordingSource); the analysis services take it (refactor step 3.4b-ii:
// AnalysisService::load, SwingDiskLoader::load, reanalyzeShot, reanalyseAndPersist — 04 step 3 item 8, "they ask
// the Shot"), so a re-analysis on a worker reads the shot without the worker ever touching the Shot. In a header
// of its own so those services, which take only this value, name no Shot, Session or Athlete.
//
// Domain model ring (src/DomainModel): names the reading interface it wraps, never a store.

#include "recording_reader.h"   // RecordingReader, LoadedRecording, RecordingLoadOptions
#include "../Vocabulary/Identity/identity.h"

#include <QString>

#include <memory>

namespace pinpoint {

// HOW ANY THREAD READS ONE SHOT'S RECORDING (BRIEF §2.2, §4.3; 04 step 3 item 8: the analysis services and the
// firing job "ask the Shot"). An immutable value: the ShotRef and the reader the Shot was given. It ONLY WRAPS
// THE EXISTING READER (RecordingReader, keyed by ShotRef: DEC-160), which resolves the shot's folder only
// through SessionRepository::swingFolder, the one store call any thread may make (the v1.9 reviewer's
// condition). So a source whose session is no longer published (trashed, released, its library gone) reads
// NOTHING: ok = false, with the reader's reason. It adds no ShotRef→folder translation and no read path of
// its own. Made on the GUI thread by Shot::recordingSource(); read from any thread.
class RecordingSource
{
public:
    RecordingSource() = default;
    RecordingSource(const ShotRef &shot, std::shared_ptr<RecordingReader> reader)
        : m_shot(shot), m_reader(std::move(reader)) {}

    const ShotRef &shot() const { return m_shot; }
    // No shot, or no reader to read it with: read() answers ok = false.
    bool isNull() const { return m_shot.isNull() || !m_reader; }

    // The shot's Recording (a LoadedRecording) through the reader (RecordingReader::load). A TEMPLATE, ITS
    // RESULT TYPE DEPENDENT, ONLY SO ITS BODY IS COMPILED WHERE IT IS CALLED: a LoadedRecording owns a
    // SwingWindow (src/Buffer), which the caller — who destroys the result — must see whole anyway, while the
    // many targets that link the Session and its shots never call it and so need none of the Buffer. Options
    // defaults to RecordingLoadOptions.
    template <typename Options = RecordingLoadOptions>
    auto read(const Options &options = Options()) const
    {
        using Result = decltype(m_reader->load(m_shot, options));   // LoadedRecording
        if (isNull()) {
            Result none;
            none.error = m_shot.isNull() ? QStringLiteral("no shot was named")
                                         : QStringLiteral("the shot %1 has no reader").arg(m_shot.toString());
            return none;
        }
        return m_reader->load(m_shot, options);
    }

private:
    ShotRef                          m_shot;
    std::shared_ptr<RecordingReader> m_reader;
};

} // namespace pinpoint
