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

// Shot — one shot, ITS OWN OBJECT, a child of its Session (refactor step 3.4a; build/refactor/step-3/BRIEF.md
// §4.3; docs/developer/information_model.md OBJ-SHOT, CLS-SHOT, LC-SHOT, CLS-SHOT-DIAG; P10, DEC-95,
// DEC-141, DEC-142).
//
//   ref()              its ShotRef (ID-SHOT): its session's id and its swing number. Never a folder
//   kind()             captured or device-only; never changes (CLS-SHOT). Given by Session::addShot; for a shot
//                      the session lists from disk, read ONCE from its document when first asked
//                      (RecordingRepository::isDeviceOnly — the one classification, R-REC-8)
//   summary()          the small summary a carousel row needs (ShotSummary below). THE SHAPE ONLY in step
//                      3.4a: its fields are filled by step 3.4b, with the Shot's commands for its own facts
//   diagnostics()      ShotDiagnostics: a read-only view of its findings, EMPTY until step 3.5 fires them
//                      ("-", never a zero: DEC-96)
//   recordingSource()  a RecordingSource: how any thread reads this shot's Recording (below)
//
// OBTAINABLE ONLY FROM ITS SESSION (P10): its constructor is private and Session is its one friend;
// Session::shot(ref) and Session::shots() hand out the instance the session holds. It is removed by
// Session::trashShot, which deletes it LATER (deleteLater), so a view still bound to it never touches freed
// memory.
//
// "HOLDS ITS RECORDING" (CLS-SHOT) READS AS "OWNS AND REACHES", NOT "KEEPS IN MEMORY" (BRIEF §4.3, J-4): the
// Shot never keeps the parsed swing document (28–33 MB); it reads through its repository when asked.
//
// NOT YET (by design of run 3.4a-i): the Shot's commands for its own facts (rate, note, correctClub,
// excludeReading, correctLabel, setShotIntent, setDeclaredClub, applyAnalysis, completeCapture, the markup
// truth) are step 3.4b's; it fires nothing until step 3.5 (fireFindings, its ask number).
//
// Domain model ring (src/DomainModel): names no store type.

#include "athlete.h"            // ShotDoors
#include "recording_reader.h"   // RecordingReader, LoadedRecording, RecordingLoadOptions (RecordingSource)
#include "../Identity/identity.h"
#include "../Recording/recording.h"   // recording::ShotKind

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace pinpoint {

class Session;

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

// The small summary a carousel row shows (BRIEF §4.3 "a small summary for the rows"; today's
// ShotListModel::Shot, shot_list_model.h). THE SHAPE ONLY in step 3.4a: every field is at its default until
// step 3.4b fills it from the Shot's own facts (its review and its analysis) — so a default here means
// "not filled yet", and nothing may read one as the shot's value before then.
struct ShotSummary {
    QString club;               // the shot's club, as the row names it ("" = none known yet)
    int     rating = 0;         // the golfer's stars, 0–5
    bool    hasVideo = false;
    bool    dataWarning = false;    // an integrity block warns
    bool    lmExcluded = false;     // the launch monitor board's exclusion
    QString lmClubOverride;         // the board's club correction ("" = none)
};

// ShotDiagnostics (OBJ-SHOT-DIAG, CLS-SHOT-DIAG): a read-only view of the shot's findings and derived data,
// reached through the Shot; owns and caches nothing. EMPTY IN STEP 3.4a: nothing fires until step 3.5, so it
// has no value yet, and a view shows "-" for it, never a zero (DEC-96).
class ShotDiagnostics
{
public:
    // Whether the shot's findings have fired. False until step 3.5.
    bool fired() const { return false; }
};

class Shot : public QObject
{
    Q_OBJECT
public:
    ShotRef               ref() const { return m_ref; }
    int                   swingNumber() const { return m_ref.swingNumber(); }
    recording::ShotKind   kind() const;
    const ShotSummary    &summary() const { return m_summary; }
    const ShotDiagnostics &diagnostics() const { return m_diagnostics; }
    RecordingSource       recordingSource() const { return RecordingSource(m_ref, m_doors.reader); }

private:
    friend class Session;   // P10: made, held and removed by its Session only
    // `kind` known (addShot) or not (a shot listed from disk: read from its document when first asked).
    Shot(const ShotRef &ref, std::optional<recording::ShotKind> kind, const ShotDoors &doors, Session *parent);

    ShotRef                                    m_ref;
    ShotDoors                                  m_doors;
    mutable std::optional<recording::ShotKind> m_kind;
    ShotSummary                                m_summary;
    ShotDiagnostics                            m_diagnostics;
};

} // namespace pinpoint
