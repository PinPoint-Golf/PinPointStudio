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

// HOW A SCREEN REACHES A SESSION'S THIN ADAPTER (refactor step 3.7-a; build/refactor/step-3/BRIEF.md §4.10; D1: DEC-164;
// D3: DEC-165). `diagnosticsAdapters` in QML (a context property main.cpp sets, as `app` is):
//
//   diagnosticsAdapters.of(app.sessionAt(folder), surface)
//
// answers the Session's thin adapter for that surface ("panel": the stage panel; "cast": the wall cast) — a
// SessionDiagnosticsModel MADE BY C++ (SessionDiagnosticsModel::adapterFor), the Session's child, showing the Session's
// one SessionDiagnostics (LIVE-2). QML never creates one (lint A2). With no Session — a folder no athlete of the library
// holds, or no folder yet — an EMPTY adapter for the surface, which judges nothing and shows the panel's cold state; the
// panel says why (its notice). (Until refactor 5.4b a folder in the current golfer's folder that the golfer's Athlete did
// not hold — a session whose file named another athlete id, J-a3 — kept the old engine's model: none is left on the
// library since migration M1, and the old engine is deleted.)

// WHAT A PANEL SHOWS, FOR A SCREEN THAT RIDES ON IT (refactor step 3.7-c: the carousel's per-shot pips):
//
//   diagnosticsAdapters.shownFor(app.sessionAt(folder), folder)
//
// answers the model a diagnostics panel is SHOWING for that folder (a view bound: addView) — the Session's stage-panel
// adapter, else its wall-cast adapter — or null. It never makes one, so the cards add no view work of their own: they read the pips of the view half a
// panel already runs, and with no panel up there is nothing to read. `shownCount` (the models with a view bound) and its
// `shownChanged` tell a binding when to ask again: a panel's first view bound or last dropped, a model gone. A panel rebuilt on a mode switch (its incoming view bound before its outgoing one goes)
// tells nothing. Only the "panel" and "cast" surfaces are answered (review M1), and only models handed out by of() are
// watched: of() is the one door a screen uses (adapterFor is its implementation).
//
// THE CARDS' OWN ADAPTER (surface "cards"; 3.7-c review B1: the pips show with no panel up, BRIEF.md §3.1): with no
// panel showing the session, the carousel reads diagnosticsAdapters.of(session, "cards") and binds a view on it. That
// adapter takes only what the pips read from each applied diagnosis (the rows, the ledgers, the folders: no view half).
// Its pip order is a panel's card order once a panel of the session has worked one out, else the session's patterns
// first, then the ledger's order. Never answered by shownFor.
//
// THE ADAPTER'S SESSION-FACING HALF is here too (session_diagnostics_adapter.cpp: SessionDiagnosticsOwnerLink), so the
// model's own file links nothing of the domain model: only the app and the tests that link the Session and its owner
// compile this pair. NOT a QML type (not in PP_QML_SOURCES), like diag_parallel_run: the QML suites need none of it.
//
// Adapters ring (src/Gui): reads the Session and its SessionDiagnostics; writes nothing of theirs but through their
// commands (the live freeze through Session::freezeClubBaseline).

#include "session_diagnostics_model.h"   // whole, for moc (of()'s type)

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace pinpoint {
class RecordingRepository;
class RuleData;
class SessionRepository;
class WorkPool;
class WorkReceiver;
struct DiagnosticsDoors;

// THE APP'S DIAGNOSTICS DOORS (refactor step 3.7-a; DEC-02, 04 step 3 item 4: always fire; here since 3.7-e, review M9):
// what main.cpp hands every Athlete, put together in one place so diag_stage_panel_test and post_shot_bench wire the owner
// exactly as the app does. The owner WRITES: diagnostics.json (`persist`), the shots' phase-grid and summary sidecars as
// the old engine's per-shot read did (the reader's default policy: parse a miss, write it), and — since the catch-up's
// cut-over (3.7-e) — the session summary, work_ons.json (`writeSummary`): every file of a session's diagnostics has this
// one writer. On `pool` with `receiver`, never synchronous. `attach` is SessionDiagnostics::attach (main.cpp wraps it to
// show each owner's failed writes). Every pointer and reference is borrowed and must outlive every Athlete given the doors.
DiagnosticsDoors appDiagnosticsDoors(WorkPool *pool, WorkReceiver *receiver, RuleData *ruleData,
                                     const SessionRepository &sessions, RecordingRepository &recordings);
} // namespace pinpoint

class SessionDiagnosticsAdapters : public QObject
{
    Q_OBJECT
    // How many models this door handed out have a view bound (a panel up on them). See "What a panel shows", above.
    Q_PROPERTY(int shownCount READ shownCount NOTIFY shownChanged)
public:
    explicit SessionDiagnosticsAdapters(QObject *parent = nullptr) : QObject(parent) {}
    // `session`: a pinpoint::Session (app.sessionAt's answer), or null; `folder`: the folder the screen chose. Never null.
    Q_INVOKABLE SessionDiagnosticsModel *of(QObject *session, const QString &surface, const QString &folder = QString());
    // The model a panel shows for `session` (app.sessionAt's answer) or, with none, for `folder`; null when no panel shows
    // one. Never makes a model (refactor 3.7-c; above).
    Q_INVOKABLE SessionDiagnosticsModel *shownFor(QObject *session, const QString &folder = QString()) const;
    // Whether anything judges this folder's swings here: a Session's owner. False for a folder no Session holds: the
    // carousel shows its warning where the pips would be (3.7-c review B1). `folder` is kept for the callers' shape.
    Q_INVOKABLE bool judgedHere(QObject *session, const QString &folder) const;
    int shownCount() const;

signals:
    void shownChanged();

private:
    // Every model this door hands out is watched for a view bound or dropped, a re-pointing and its end (shownChanged).
    SessionDiagnosticsModel *watched(SessionDiagnosticsModel *m);
    void tellShown() { emit shownChanged(); }

    QList<QPointer<SessionDiagnosticsModel>>  m_handedOut;   // the models of() answered, each once
    QHash<QString, SessionDiagnosticsModel *> m_empty;    // one empty adapter per surface, children of this
};
