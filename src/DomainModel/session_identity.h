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

// SessionIdentity — what a session is called and where it sits among the athlete's sessions
// (OBJ-SESSION-IDENTITY, information_model.md §5.2/§5.3; refactor step 2.1, brief §4.2).
//
// Holds the session's id, its folder (absolute), its start time (UTC) and the local day that start
// falls on, its type (by name, DEC-88), and its place among the athlete's sessions of that day
// ("n-of-day"). Computes the label a list shows ("Swing · 14 Nov · 2nd").
//
// ⚠ THE DAY AND THE ORDER COME FROM THE START TIME, NOT FROM A FOLDER NAME (F-06). Under the
// `name-date-type` naming pattern no folder starts with its date, so every `left(10)` parse returned
// nothing and the session was "ordered nowhere". Here the local day is the start time's, the order
// within a day is by start time, and a folder name is read only for a session that has no session
// file yet (`fromLegacyFolder`, below) — the one place outside SwingPaths that reads one (P7, lint A3).
//
// ⚠ A SESSION OLDER THAN STEP 2 GETS A NAME-BASED ID (D3; DEC-150). `legacySessionId` is a v5 uuid of
// the athlete's id and the folder's name, so the dry run, a re-run, the library, its archive mirror
// and an unmigrated copy (scratch, corpus) all agree on it without anything being written. The recipe
// is fixed for ever — changing a byte of it re-keys every old session:
//
//     uuid5(kLegacySessionNamespace, "<athlete uuid, lower case, no braces>/<folder name, NFC>")
//
// (UTF-8, as Python's `uuid.uuid5(ns, name)` hashes it, so a tool can reproduce it.)
//
// Domain model ring. No file I/O: a caller passes what it read (the session file's facts, or a
// folder's path and the best start time it has).

#include "../Identity/identity.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>
#include <QUuid>

namespace pinpoint {

// What a session folder's name says, for a session without a session file. Reads the four SET-04
// patterns and the app's first one, "<date>_session-NN" (165f10f1, used 5–9 Jun 2026: untyped, the
// counter after "session-").
struct LegacyFolderName {
    QDate   day;          // the yyyy-MM-dd token, wherever the pattern put it; invalid when none
    QString typeToken;    // as written: "Swing", "Wrist", "GRF", "Coach", "session" (no type); "" when none
    int     counter = 0;  // the trailing _NN; 0 when the name has none
    // A name this app's patterns could have made: a date and a counter.
    bool isSessionName() const { return day.isValid() && counter > 0; }
};

class SessionIdentity
{
public:
    // The v5 namespace of D3's derivation. Never change it.
    static const QUuid kLegacySessionNamespace;

    SessionIdentity() = default;

    // A session with a session file: its id, its folder, its type by name ("swing", "wrist", "grf",
    // "coach"; "" when it has none) and its start time. The local day is the start time's in `zone`.
    static SessionIdentity fromSessionFile(const SessionId &id, const QString &folder, const QString &type,
                                           const QDateTime &startedAtUtc,
                                           const QTimeZone &zone = QTimeZone::systemTimeZone());

    // A session without a session file (every session before step 2's migration; a corpus or
    // scratch copy). The id is D3's, from `athlete` and the folder's name; the type is the name's
    // type token; `startedAtUtc` is the best start time the caller has (its first swing's capture
    // time) — when it is invalid, the day is the one in the folder's name. Null when the athlete id
    // is null or the path has no folder name.
    static SessionIdentity fromLegacyFolder(const QString &folder, const AthleteId &athlete,
                                            const QDateTime &startedAtUtc = {},
                                            const QTimeZone &zone = QTimeZone::systemTimeZone());

    // Where a session without a session file sits, when no athlete can be named for its D3 id (a folder
    // with no athlete.json and no swing that records its athlete): its day, type and counter as
    // fromLegacyFolder reads them, and NO id — isNull() is true, so nothing can hold or command it. For a
    // caller that needs only the day and the order (step 2.7a: the diagnostics' day order). Empty for a
    // path with no folder name.
    static SessionIdentity placeOfLegacyFolder(const QString &folder, const QDateTime &startedAtUtc = {},
                                               const QTimeZone &zone = QTimeZone::systemTimeZone());

    // D3: the id of a session older than step 2. Null when either part is.
    static SessionId legacySessionId(const AthleteId &athlete, const QString &folderName);

    // Reads a legacy folder name under every naming pattern (SET-04: date-name-type, date-type-name,
    // name-date-type, date-only; and the first pattern, <date>_session-NN). `athleteToken`, when given (the athlete folder's name), is removed
    // first, so an athlete whose name is also a type word is not read as the type.
    static LegacyFolderName parseLegacyFolderName(const QString &name, const QString &athleteToken = {});

    // The type by name (DEC-88) for a legacy type token: "Swing" → "swing", "Wrist" → "wrist",
    // "GRF" → "grf", "Coach" → "coach"; anything else (the launch monitor's "session", none) → "".
    static QString typeFromToken(const QString &token);

    // Sets n-of-day on every identity in `sessions` (one athlete's): its place, from 1, among the
    // sessions of its local day. By start time when every session of that day has one; otherwise by
    // folder name (the order the app has always given a day: step 2.7a — a `_NN` counter is per type,
    // so it cannot order two types' sessions). Ties by folder name. A session with no day keeps
    // n-of-day 0.
    static void assignDayOrder(QList<SessionIdentity> &sessions);

    bool      isNull() const { return m_id.isNull(); }
    SessionId id() const { return m_id; }
    QString   folder() const { return m_folder; }
    QString   folderName() const;
    QDateTime startedAtUtc() const { return m_startedAtUtc; }
    QDate     localDay() const { return m_localDay; }
    QString   type() const { return m_type; }
    int       nOfDay() const { return m_nOfDay; }
    // True when this identity was read from its folder's name (no session file).
    bool      isLegacy() const { return m_legacy; }

    // "Swing · 14 Nov · 2nd": the type's word ("Session" when none), the local day, the place in it.
    // A part that is unknown is left out ("Swing · 14 Nov" before assignDayOrder).
    QString label() const;
    // "1st", "2nd", "3rd", "4th", "11th", "21st", …
    static QString ordinal(int n);

private:
    SessionId m_id;
    QString   m_folder;
    QDateTime m_startedAtUtc;
    QDate     m_localDay;
    QString   m_type;
    int       m_nOfDay = 0;
    bool      m_legacy = false;
};

} // namespace pinpoint
