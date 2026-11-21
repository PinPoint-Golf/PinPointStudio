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

#include "session_diagnostics_adapter.h"
#include "session_diagnostics_model.h"
#include "../../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S): the adapter's share of a settle
#include "../../Diagnostics/engine/session_reduction.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"

#include <QDir>
#include <QMetaObject>
#include <utility>
#include <QPointer>
#include <QQmlEngine>

using namespace pinpoint::analysis;

// THE ADAPTER'S LINK TO ITS SESSION (refactor step 3.7-a; session_diagnostics_model.h, "The thin adapter"): it holds a
// use of the Session for the adapter's life (Q-18: no sweep releases a session a screen shows), hears the owner, and
// takes each applied diagnosis into the adapter (mirror).
class SessionDiagnosticsOwnerLink final : public SessionDiagnosticsModel::AdapterLink
{
public:
    SessionDiagnosticsOwnerLink(SessionDiagnosticsModel *model, pinpoint::Session *session)
        : m_model(model), m_session(session), m_owner(session ? session->diagnostics() : nullptr)
    {
        if (m_session) m_session->addUse();
        if (!m_owner) return;
        // ONLY WHILE A VIEW IS BOUND (review M2): with none, a new diagnosis is owed and taken in when one binds (wake).
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::diagnosisApplied, model, [this] {
            if (m_model->m_views > 0) mirror();
            else                      m_owed = true;
        });
        const auto busyLater = [this] {
            // A turn later: the owner starts its write's debounce after it announces, so `busy` falls once the written
            // file is there (Charts and Consistency re-read it then).
            QMetaObject::invokeMethod(m_model, [this] { updateBusy(); }, Qt::QueuedConnection);
        };
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::resultKept,   model, busyLater);
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::shotUnread,   model, busyLater);
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::batchSettled, model, busyLater);
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::written,      model, busyLater);
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::writeFailed,  model, busyLater);
        // The owner heard these first (it connected at its attach, before this adapter was made): its ask is out.
        QObject::connect(m_session, &pinpoint::Session::shotAdded,    model, busyLater);
        QObject::connect(m_session, &pinpoint::Session::shotTrashed,  model, busyLater);
        QObject::connect(m_session, &pinpoint::Session::sessionEnded, model, busyLater);
    }
    ~SessionDiagnosticsOwnerLink() override
    {
        if (m_session) m_session->dropUse();   // null while the Session itself is going (the adapter is its child)
    }

    bool notRecordedHere() const override
    {
        return m_session && m_session->inProgress() && !m_session->runningHere();
    }

    pinpoint::WriteResult freezeBaseline(const QJsonObject &root) override
    {
        if (!m_session)
            return { pinpoint::WriteResult::Status::Failed, QStringLiteral("the session is gone") };
        return m_session->freezeClubBaseline(root, /*rewrite=*/false);
    }

    void reloadBaseline(bool keepWhenAbsent) override
    {
        if (m_owner) m_owner->reloadClubBaseline(keepWhenAbsent);
    }

    void wake() override
    {
        if (std::exchange(m_owed, false)) mirror();
    }

    void updateBusy()
    {
        SessionDiagnosticsModel &m = *m_model;
        const bool b = m_owner && (!m_owner->idle() || m_owner->writePending());
        if (b == m.m_adapterBusy) return;
        m.m_adapterBusy = b;
        emit m.busyChanged();
    }

    // THE OWNER'S APPLIED DIAGNOSIS, TAKEN IN (the class comment): its rows as the reduction read them, its ledgers, links,
    // stage, composed outcomes and typed results, and the golfer's word it read — then the adapter's view half only.
    void mirror() override
    {
        SessionDiagnosticsModel &m = *m_model;
        m_owed = false;
        const SessionDiagnosis *d = m_owner ? m_owner->diagnosis() : nullptr;
        const std::shared_ptr<const SessionReductionInput> in = m_owner ? m_owner->diagnosisInput() : nullptr;
        if (!d || !in) {
            m.rebuild();
            return;
        }
        // ⏱ [PostShot] (refactor 3.7-S): the adapter's share of a shot's settle — the view half only (no reduction, no
        // write: the old model's `settle` with its rebuild and persist is gone from the screen thread).
        pinpoint::PostShotTrace::Scope trace("adapter");

        m.m_ownerInput         = in;
        m.m_shots              = in->shots;
        m.m_opt                = in->options;
        m.m_withSigma          = in->withSigma;
        m.m_ledgers            = d->ledgers;
        m.m_links              = d->links;
        m.m_stage              = d->stage;
        m.m_reachedEstablished = d->reachedEstablished;
        m.m_closed             = in->closed;
        m.m_ownerComposed      = d->composed;
        m.m_ownerResults       = d->results;
        m.m_baselineFrozen     = in->baseline.has_value();
        if (in->baseline) {
            m.adoptClubBaseline(*in->baseline);
        } else {
            m.m_baselineClubs.clear();
            m.m_clubBaseline.reset();
        }

        // The golfer's word, as the owner read it from its homes (the Session's input; each shot's review).
        const bool wordMoved = m.m_focusConditionId != in->focusConditionId || m.m_focusFromShot != in->focusFromShot
                            || m.m_declaredMiss != in->declaredMiss || m.m_screens != in->screens;
        m.m_focusConditionId = in->focusConditionId;
        m.m_focusFromShot    = in->focusFromShot;
        m.m_declaredMiss     = in->declaredMiss;
        m.m_screens          = in->screens;
        m.m_corrections      = in->corrections;
        // Each shot's intent as an override of the run, the run from the owner's snapshot (intentFor() joins them again).
        if (const auto snap = m_owner->snapshot()) {
            m.m_intentFromShot  = snap->intentRun.fromShot;
            m.m_intentFromShape = snap->intentRun.shape;
        }
        m.m_shotIntent.clear();
        for (auto it = in->intents.constBegin(); it != in->intents.constEnd(); ++it) {
            const QString run = (m.m_intentFromShot >= 0 && it.key() >= m.m_intentFromShot) ? m.m_intentFromShape : QString();
            if (it.value() != run) m.m_shotIntent.insert(it.key(), it.value());
        }

        // Which shots carried a reading, and their folders (the Session's own Shots).
        m.m_lmShots.clear();
        m.m_ingested.clear();
        m.m_swingDirs.clear();
        for (const ShotRecord &r : m.m_shots) {
            m.m_ingested.insert(r.shotId);
            if (const pinpoint::KeptShot *k = m_owner->kept(r.shotId); k && k->hasLaunchMonitor) m.m_lmShots.insert(r.shotId);
            if (m_session)
                if (pinpoint::Shot *shot = m_session->shot(pinpoint::ShotRef(m_session->id(), r.shotId)))
                    m.m_swingDirs.insert(r.shotId, shot->folder());
        }
        m.resolveSelectedSwingDir();

        // The after-shot moment: a new newest swing of a session being recorded here (the old model's shotIngested).
        const int newest = m.m_shots.empty() ? -1 : m.m_shots.back().shotId;
        const bool live = m_session && m_session->runningHere() && m_session->inProgress();
        const bool arrived = m_mirrored && live && newest > m_newest;
        m_newest   = newest;
        m_mirrored = true;

        if (wordMoved) emit m.intentChanged();
        m.rebuild();
        if (arrived) emit m.shotIngested(newest, !m.m_quiet);
        QMetaObject::invokeMethod(m_model, [this] { updateBusy(); }, Qt::QueuedConnection);
    }

private:
    SessionDiagnosticsModel               *m_model = nullptr;
    QPointer<pinpoint::Session>            m_session;
    QPointer<pinpoint::SessionDiagnostics> m_owner;
    int  m_newest   = -1;      // the newest swing the last mirror showed
    bool m_mirrored = false;
    bool m_owed     = false;   // a diagnosis landed while no view was bound
};

// ── The adapter (session_diagnostics_model.h) ─────────────────────────────────────────────────────────────────

SessionDiagnosticsModel::SessionDiagnosticsModel(pinpoint::Session *session, const QString &surface)
    : SessionDiagnosticsModel(static_cast<QObject *>(session))
{
    m_adapter        = true;
    m_adapterSurface = surface;
    setObjectName(QStringLiteral("sessionDiagnosticsAdapter:") + surface);
    if (!session)
        return;   // the empty adapter: no Session (no golfer, N1; no folder yet), so nothing to show and nothing judged
    if (const auto e = session->entry()) {
        m_sessionDir      = e->folder;
        m_sessionIdentity = e->identity;   // the identity map's (no second read)
    }
    auto *link = new SessionDiagnosticsOwnerLink(this, session);
    m_link.reset(link);
    emit sessionChanged();
    link->mirror();   // what the owner holds now, once: the panel binding next reads it
    link->updateBusy();
}

SessionDiagnosticsModel *SessionDiagnosticsModel::adapterFor(pinpoint::Session *session, const QString &surface)
{
    if (!session || !session->diagnostics())
        return nullptr;
    const QString key = surface.isEmpty() ? QStringLiteral("panel") : surface;
    for (SessionDiagnosticsModel *m : session->findChildren<SessionDiagnosticsModel *>(Qt::FindDirectChildrenOnly))
        if (m->m_adapter && m->m_adapterSurface == key)
            return m;
    auto *m = new SessionDiagnosticsModel(session, key);
    QQmlEngine::setObjectOwnership(m, QQmlEngine::CppOwnership);   // the Session's child: QML never collects it
    return m;
}

// ── How a screen reaches it ───────────────────────────────────────────────────────────────────────────────────

SessionDiagnosticsModel *SessionDiagnosticsAdapters::of(QObject *session, const QString &surface, const QString &folder)
{
    const QString key = surface.isEmpty() ? QStringLiteral("panel") : surface;
    if (auto *s = qobject_cast<pinpoint::Session *>(session))
        if (SessionDiagnosticsModel *m = SessionDiagnosticsModel::adapterFor(s, key))
            return m;
    // A folder in the current golfer's folder that no Session of theirs holds (another athlete id in its file): today's
    // path, an engine-mode model made here (the header; NOTES-a.md J-a3).
    const QString mine = m_athleteFolder ? m_athleteFolder() : QString();
    if (!folder.isEmpty() && !mine.isEmpty()
        && QDir::cleanPath(folder + QStringLiteral("/..")) == QDir::cleanPath(mine)) {
        SessionDiagnosticsModel *&unheld = m_unheld[key];
        if (!unheld) {
            unheld = new SessionDiagnosticsModel(this);
            unheld->setObjectName(QStringLiteral("sessionDiagnosticsUnheld:") + key);
            QQmlEngine::setObjectOwnership(unheld, QQmlEngine::CppOwnership);
        }
        return unheld;
    }
    SessionDiagnosticsModel *&empty = m_empty[key];
    if (!empty) {
        empty = new SessionDiagnosticsModel(static_cast<pinpoint::Session *>(nullptr), key);
        empty->setParent(this);
        QQmlEngine::setObjectOwnership(empty, QQmlEngine::CppOwnership);
    }
    return empty;
}
