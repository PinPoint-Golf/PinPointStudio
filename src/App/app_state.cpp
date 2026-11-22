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

#include "app_state.h"
#include "../DomainModel/shot.h"   // R-APP-7: the landed shot's folder

#include <QDir>

namespace pinpoint {

namespace {
QString folderKey(const QString &dir) { return dir.isEmpty() ? QString() : QDir::cleanPath(dir); }
template <typename F, typename... A> void call(const F &f, A &&...a) { if (f) f(std::forward<A>(a)...); }
} // namespace

void ShotSelection::set(const ShotRef &shot, const QString &swingDir)
{
    if (shot == m_shot && swingDir == m_swingDir)
        return;
    m_shot     = shot;
    m_swingDir = swingDir;
    emit changed();
}

AppState::AppState(QObject *parent) : QObject(parent)
{
    // The shot summary is a view of the outcome's shot, and names it unless it is the selection: either moving, or the
    // session on screen, moves it (refactor 4-b).
    connect(&m_outcome, &ShotSelection::changed, this, &AppState::shotOutcomeChanged);
    connect(&m_selection, &ShotSelection::changed, this, &AppState::shotOutcomeChanged);
    connect(this, &AppState::sessionOnScreenChanged, this, &AppState::shotOutcomeChanged);
}

// THE CAROUSEL'S SHOT SUMMARY (refactor 4-b): worked out now, from the owners, by the composition root's door.
ShotOutcomeSummary AppState::shotOutcome() const
{
    if (!m_doors.shotOutcome || !m_onScreen || !m_outcome.valid()) return {};
    const bool isSelection = m_selection.valid() && m_selection.shot() == m_outcome.shot();
    return m_doors.shotOutcome(m_onScreen.data(), m_outcome.shot(), isSelection);
}

// The session on screen, heard through a context object of its own, so the last session's connections go with it:
// its EVT-01 moves the outcome (R-APP-7), and what its owners say (a result kept or unread, a shot gone) refreshes the
// summary in place.
void AppState::watchOutcome()
{
    delete m_outcomeWatch.data();
    if (!m_onScreen) return;
    m_outcomeWatch = new QObject(this);
    connect(m_onScreen.data(), &Session::shotAdded, m_outcomeWatch.data(), [this](const ShotRef &shot) { shotLanded(shot); });
    if (m_doors.watchOutcomeSummary)
        m_doors.watchOutcomeSummary(m_onScreen.data(), m_outcomeWatch.data(), [this] { emit shotOutcomeChanged(); });
}

// R-APP-7: a shot the session on screen adds (its document written, EVT-01) is the outcome now, over an older selection
// (Mark, 22 Nov 2026). The folder is the Shot's own (a property: the replay and the folder readers), as a pick's is.
void AppState::shotLanded(const ShotRef &shot)
{
    Shot *s = m_onScreen ? m_onScreen->shot(shot) : nullptr;
    if (s) m_outcome.set(shot, s->folder());
}

void AppState::setMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    emit modeChanged();
}

void AppState::setAutoReturn(bool on)
{
    if (on == m_autoReturn) return;
    m_autoReturn = on;
    emit autoReturnChanged();
}

// The cast follows the selection (J-a1: Q-07 is Mark's), so both are written here and nowhere else.
void AppState::select(const QString &shotRef, const QString &swingDir)
{
    const ShotRef ref = ShotRef::fromString(shotRef);
    m_selection.set(ref, swingDir);
    m_castSelection.set(ref, swingDir);
    m_outcome.set(ref, swingDir);   // R-APP-7: every pick, move or clear
}

// Load the selected shot onto the stage unless it is already the loaded one (so Replay↔Analyse, or a re-click,
// never restarts it). The post-shot replay always plays; a pick plays only with "Auto-replay after select" on.
void AppState::loadSelected(bool fromCapture)
{
    if (!m_selection.valid()) return;
    const QString loaded = m_doors.replayFolder ? m_doors.replayFolder() : QString();
    if (!loaded.isEmpty() && folderKey(loaded) == folderKey(m_selection.swingDir())) return;
    const bool autoPlay = fromCapture || (m_doors.autoReplayAfterSelect && m_doors.autoReplayAfterSelect());
    call(m_doors.replayStart, m_selection.swingNumber(), m_selection.swingDir(), autoPlay);
}

// A shot picked on a figure goes the carousel's own click path; Analyse keeps Analyse.
void AppState::pickShot(const QString &shotRef, const QString &swingDir)
{
    if (m_mode == Analyse) {
        select(shotRef, swingDir);
        enterAnalyse();
    } else {
        enterReplay(shotRef, swingDir);
    }
}

// Capture is the live session: leaving a review drops its selection; within the live session it is kept.
void AppState::enterCapture()
{
    setAutoReturn(false);
    setMode(Capture);
    call(m_doors.replayStop);
    if (m_doors.reviewFolder && !m_doors.reviewFolder().isEmpty()) {
        select({}, {});
        call(m_doors.resumeLive);
    }
}

void AppState::enterAnalyse()
{
    setAutoReturn(false);
    setMode(Analyse);
    loadSelected(false);
}

void AppState::showReplay()
{
    setAutoReturn(false);
    setMode(Replay);
    loadSelected(false);
}

// The selection is set BEFORE the mode: a panel rebuilt on the mode switch must find the pick already there.
void AppState::enterReplay(const QString &shotRef, const QString &swingDir, bool fromCapture)
{
    select(shotRef, swingDir);
    setAutoReturn(false);
    setMode(Replay);
    loadSelected(fromCapture);
    setAutoReturn(fromCapture);
}

void AppState::clearSelection()
{
    setAutoReturn(false);
    select({}, {});
    call(m_doors.replayStop);
}

// The selected shot is being trashed: hand the stage to its neighbour without changing the view (#56).
void AppState::moveSelection(const QString &shotRef, const QString &swingDir)
{
    setAutoReturn(false);
    select(shotRef, swingDir);
    if (m_mode != Capture) loadSelected(false);
}

void AppState::enterLoadedSession()
{
    setAutoReturn(false);
    select({}, {});
    setMode(Replay);
    call(m_doors.replayStop);
}

void AppState::replayEnded()
{
    if (m_autoReturn && m_mode == Replay)
        enterCapture();
}

void AppState::startSession(bool extend)
{
    // THE DECLARED FOCUS (refactor 4-d; FOCUS-TRACE.md L1, L2): Extend keeps what the session saved, so a pick is
    // dropped before it; a new session takes the pick through handOverFocus — on the capture target's change, which the
    // open emits, and again here — whether or not a diagnostics panel is up.
    if (extend)
        call(m_doors.dropPendingFocus);
    call(m_doors.openCaptureSession, extend);
    handOverFocus();
    call(m_doors.showLiveShots, extend ? captureTargetFolder() : QString());
    call(m_doors.startClock);
    call(m_doors.startCameras);
}

// THE ONE HAND-OVER (refactor 4-d; J-64): the Session owns its declared focus (OBJ-SESSION-INTENT), so the pick waits
// only until a session is open to take it, and is dropped only once one did (review B1: a Start on an unusable library
// opens none; its first swing's session, or a D7 restart's late start, takes it then). End drops what is left
// (SessionController::endSession).
void AppState::handOverFocus()
{
    if (!m_doors.pendingFocus || !m_doors.declareStartFocus || captureTargetFolder().isEmpty())
        return;
    const QString focus = m_doors.pendingFocus();
    if (!focus.isEmpty() && m_doors.declareStartFocus(focus))
        call(m_doors.dropPendingFocus);
}

void AppState::endSession(bool reloadCarousel)
{
    call(m_doors.stopCameras);
    call(m_doors.stopClock);
    call(m_doors.closeCaptureSession);
    if (reloadCarousel)
        call(m_doors.showLiveShots, m_doors.todaysFolder ? m_doors.todaysFolder() : QString());
}

// R-APP-3, today's chain (E-08) in one place: the reviewed session, else the capture target, else the live
// carousel's. The selection is scoped to it: a different session (by id; by folder when none can be named)
// clears it — an extend, or an End that leaves the same session on screen, does not (E-05).
void AppState::refreshSessionOnScreen()
{
    QString folder = m_doors.reviewFolder ? m_doors.reviewFolder() : QString();
    if (folder.isEmpty()) folder = captureTargetFolder();
    if (folder.isEmpty() && m_doors.liveFolder) folder = m_doors.liveFolder();
    Session *session = (!folder.isEmpty() && m_doors.sessionAt) ? m_doors.sessionAt(folder) : nullptr;
    const SessionId id = session ? session->id() : SessionId();

    // The selection is scoped to the last session that was on screen (its scope), compared by SessionId. Nothing on
    // screen — a carousel passing through empty, an End before the carousel shows today's — is not another session:
    // the selection waits for the next one (review B3).
    bool sameSession = true;
    if (!folder.isEmpty()) {
        sameSession = !id.isNull() ? id == m_scopeId
                                   : (m_scopeId.isNull() && folderKey(folder) == folderKey(m_scopeFolder));
        m_scopeId     = id;
        m_scopeFolder = folder;
    }
    const bool changed = folder != m_onScreenFolder || session != m_onScreen.data();
    m_onScreenFolder = folder;
    if (session != m_onScreen.data()) {
        // A Session let go of (released, or its athlete gone) leaves app.session null: say so.
        disconnect(m_onScreenGone);
        if (session)
            m_onScreenGone = connect(session, &QObject::destroyed, this, &AppState::sessionOnScreenChanged);
    }
    const bool sessionMoved = session != m_onScreen.data();
    m_onScreen       = session;
    if (sessionMoved) watchOutcome();
    if (!sameSession) {
        dropSelection();
        m_outcome.clear();   // R-APP-7: scoped to the session on screen, cleared with it
    }
    if (changed) emit sessionOnScreenChanged();
}

// An athlete switch: whatever was selected was the last athlete's (E-05), even if the new one has no session on screen.
void AppState::refreshAthlete()
{
    emit currentAthleteChanged();
    dropSelection();
    m_outcome.clear();
    m_scopeId     = {};
    m_scopeFolder.clear();
    refreshSessionOnScreen();
}

// The selection cleared because its session went, and the replay of it with it.
void AppState::dropSelection()
{
    if (!m_selection.valid()) return;
    const QString loaded = m_doors.replayFolder ? m_doors.replayFolder() : QString();
    if (!loaded.isEmpty() && folderKey(loaded) == folderKey(m_selection.swingDir()))
        call(m_doors.replayStop);
    select({}, {});
}

} // namespace pinpoint
