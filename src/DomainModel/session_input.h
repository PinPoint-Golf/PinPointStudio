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

// SessionInput — the golfer's SESSION-LEVEL input, as the session keeps it (refactor step 2.5; brief
// §4.8; docs/developer/information_model.md FILE-SESS-02, OBJ-SESSION-INTENT, R-INPUT-2, R-INPUT-3;
// keys DEC-160: IM-S2-3). Source facts, owned by the session (until step 3 written by today's callers,
// the diagnostics model's commands, through the session's files: IM-S2-4).
//
//   declaredFocus   the declared work-on (DEC-138): which condition, from which shot (a row index: the
//                   shots before it are the baseline)
//   declaredMiss    the miss the golfer says they are fighting ("" = none)
//   screenAnswers   a screen's answer per condition id (true = present)
//   intentRuns      "from here on" runs of shot intent (R-INPUT-2). Today the app keeps ONE run (the
//                   latest replaces it), so the list holds at most one entry
//
// The shot-level half — a shot's intent and its label corrections — is the shot's own, on its `review`
// (FILE-SHOT-02; DEC-13), read and written through RecordingRepository (ReviewEdit, ShotReview).
//
// EACH FIELD IS OPTIONAL, and absent means "this file does not hold it": a session no golfer input was
// written for since step 2.5, or a file written by a build that did not know the key. Until migration M3
// (step 2.8) moves the older copy, such a field is read from diagnostics.json's older keys instead (the
// lead's call J-33; SessionDiagnosticsFiles::golferInput). The writer always writes all four, so once a
// session's input has been written here, this file is the whole of it.
//
// Domain model ring: Qt value types only.

#include <QMap>
#include <QString>
#include <QVector>

#include <optional>

namespace pinpoint {

struct DeclaredFocus {
    QString conditionId;     // "" = none declared
    int     fromShot = -1;   // the first focus shot's row index; -1 = none
    friend bool operator==(const DeclaredFocus &a, const DeclaredFocus &b)
    {
        return a.conditionId == b.conditionId && a.fromShot == b.fromShot;
    }
};

struct IntentRun {
    int     fromShot = -1;   // the first shot (swing number) the run covers
    QString shape;           // "fade", "draw", … ("" = no run)
    friend bool operator==(const IntentRun &a, const IntentRun &b)
    {
        return a.fromShot == b.fromShot && a.shape == b.shape;
    }
};

struct SessionInput {
    std::optional<DeclaredFocus>      declaredFocus;
    std::optional<QString>            declaredMiss;
    std::optional<QMap<QString, bool>> screenAnswers;
    std::optional<QVector<IntentRun>> intentRuns;

    bool isEmpty() const { return !declaredFocus && !declaredMiss && !screenAnswers && !intentRuns; }
    friend bool operator==(const SessionInput &a, const SessionInput &b)
    {
        return a.declaredFocus == b.declaredFocus && a.declaredMiss == b.declaredMiss
            && a.screenAnswers == b.screenAnswers && a.intentRuns == b.intentRuns;
    }
};

} // namespace pinpoint
