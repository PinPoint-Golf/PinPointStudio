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

// The values the Shot's and the Session's golfer commands take (refactor step 3.4b-i; build/refactor/step-3/
// BRIEF.md §4.3, §4.12; docs/developer/information_model.md CLS-SHOT, CLS-SESSION, EVT-06, EVT-08, EVT-10,
// OBJ-TUNING-LOG, OBJ-SESSION-INTENT). Plain values, in a header of their own so the interim door the adapters
// reach the commands through (src/App/shot_command_door.h) needs no QObject header.
//
// Domain model ring (src/DomainModel): Qt value types and the repositories' result type only.

#include "recording_repository.h"   // WriteResult
#include "session_input.h"          // DeclaredFocus, IntentRun, SessionInput

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <optional>

class QThreadPool;

namespace pinpoint {

// WHAT A LABEL CORRECTION TELLS THE TUNING LOG (OBJ-TUNING-LOG, FILE-ATH-04; DEC-43): the correction the golfer
// made and the shot's measures at that moment. The Shot keeps the last one it saved (lastLabelCorrection) and
// its Athlete, hearing EVT-06, appends it as one line. `sessionName` and `swingDir` are the line's provenance
// fields exactly as the caller files the shot today (the session folder's name; the swing folder the ledger
// row was filed under, which for a session recorded on another machine is that machine's path), so the line
// carries today's values.
struct LabelCorrectionNote {
    QString     family;      // the label family
    QString     detected;    // what was detected ("" = none)
    QString     corrected;   // what the golfer says it was ("" = none in that family)
    QJsonObject measures;    // the shot's measures behind the label (the diagnostics model's shotMeasures)
    QString     sessionName;
    QString     swingDir;
};

// A "FLAG FOR REVIEW" (the panel's "no reason found" button): the tuning log's other line kind (FILE-ATH-04), which
// the Athlete appends as the label correction's (refactor step 3.4b-i, review 1: one writer of the log). The shot's
// composed phrase and labels and its measures at that moment, with today's provenance fields. No event: the
// reference names none for it, and nothing derived reads the log.
struct TuningFlagNote {
    QString     phrase;      // the composed outcome phrase
    QJsonArray  labels;      // the composed label ids
    QJsonObject measures;
    QString     sessionName;
    QString     swingDir;
};

// WHERE A COMMAND'S SAVE IS MADE WHEN IT IS NOT MADE NOW (D25; DEC-183: each command's save stays on the thread
// its caller makes it on today). `pool` null: saved now, on this thread. Otherwise the swing file is rewritten on
// `pool` — the caller's serial worker (the diagnostics panel's review pool, which today keeps that 28–33 MB
// rewrite off the GUI thread) — and the Shot emits its event on its own thread once the save has landed, then
// calls `done` there. `done` is called exactly once, whether or not the Shot still exists by then.
struct SaveLater {
    QThreadPool                             *pool = nullptr;
    std::function<void(const WriteResult &)> done;
};

// ONE FIELD OF THE SESSION'S INTENT (OBJ-SESSION-INTENT; FILE-SESS-02), as one of Session::setSessionIntent's
// calls changes it: the declared focus, the declared miss, or one screen's answer. Unset fields are left as the
// session's file holds them.
struct SessionIntentEdit {
    std::optional<DeclaredFocus> focus;
    std::optional<QString>       miss;
    QString                      screenId;            // not empty: this screen's answer is `screenPresent`
    bool                         screenPresent = false;
};

} // namespace pinpoint
