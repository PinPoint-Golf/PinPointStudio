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
#include "../../DomainModel/athlete.h"            // DiagnosticsDoors (appDiagnosticsDoors)
#include "../../Stores/firing_reader_store.h"     // the app's firing reader (appDiagnosticsDoors)

#include <QDir>
#include <algorithm>
#include <QMetaObject>
#include <utility>
#include <QPointer>
#include <QQmlEngine>

using namespace pinpoint::analysis;

// THE ADAPTER'S LINK TO ITS SESSION (refactor step 3.7-a; session_diagnostics_model.h, "The thin adapter"): it holds a
// use of the Session WHILE A SCREEN SHOWS IT (a view bound; refactor 3.7-e, CARRY 3.7-a and R2-m2: no sweep releases a
// session a screen shows, and none is held for good once no screen does), hears the owner, and takes each applied
// diagnosis into the adapter (mirror).
class SessionDiagnosticsOwnerLink final : public SessionDiagnosticsModel::AdapterLink
{
public:
    SessionDiagnosticsOwnerLink(SessionDiagnosticsModel *model, pinpoint::Session *session)
        : m_model(model), m_session(session), m_owner(session ? session->diagnostics() : nullptr),
          m_pipsOnly(model->m_adapterSurface == QLatin1String("cards"))
    {
        if (!m_owner) return;
        // ONLY WHILE A VIEW IS BOUND (review M2): with none, a new diagnosis is owed and taken in when one binds (wake).
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::diagnosisApplied, model, [this] {
            if (m_model->m_views > 0) mirror();
            else                      m_owed = true;
            updateBusy();   // `loading` ends with the first diagnosis, whether or not busy moves
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
        QObject::connect(m_owner, &pinpoint::SessionDiagnostics::clubBaselineChanged, model, busyLater);   // J-49
        // The owner heard these first (it connected at its attach, before this adapter was made): its ask is out.
        QObject::connect(m_session, &pinpoint::Session::shotAdded,    model, busyLater);
        QObject::connect(m_session, &pinpoint::Session::shotTrashed,  model, busyLater);
        QObject::connect(m_session, &pinpoint::Session::sessionEnded, model, busyLater);
    }
    ~SessionDiagnosticsOwnerLink() override
    {
        if (m_using && m_session) m_session->dropUse();   // null while the Session itself is going (the adapter is its child)
    }

    void viewed(bool on) override
    {
        if (on == m_using || !m_session) return;
        m_using = on;
        if (on) m_session->addUse();
        else    m_session->dropUse();
    }

    bool notRecordedHere() const override
    {
        return m_session && m_session->inProgress() && !m_session->runningHere();
    }

    void freezeBaselineIfNeeded() override
    {
        if (m_owner) m_owner->freezeClubBaselineIfNeeded();
    }

    void wake() override
    {
        // THE CARDS' PIP ORDER IS TAKEN AGAIN ON EVERY BIND (refactor 3.7-e; review R2-m1 of 3.7-c): a panel that closed on
        // a session with no new diagnosis left its card order behind, and the pips must keep it ("the pips do not move when
        // a panel opens or closes"). Cheap: the rows the pips read and one surfaceChanged.
        if (m_pipsOnly) {
            m_owed = false;
            mirrorPips();
            return;
        }
        if (std::exchange(m_owed, false)) mirror();
    }

    void updateBusy()
    {
        SessionDiagnosticsModel &m = *m_model;
        const bool b = m_owner && (!m_owner->idle() || m_owner->writePending());
        // Loading (3.7-c review B3): the owner has applied no diagnosis yet while its Session holds shots.
        const bool l = m_owner && !m_owner->diagnosis() && m_session && !m_session->shots().isEmpty();
        const bool w = m_owner && m_owner->clubBaselineWaiting();   // J-49: the club baseline not ready
        if (b == m.m_adapterBusy && l == m.m_adapterLoading && w == m.m_adapterBaselineWaiting) return;
        m.m_adapterBusy    = b;
        m.m_adapterLoading = l;
        m.m_adapterBaselineWaiting = w;
        emit m.busyChanged();
    }

    // THE OWNER'S APPLIED DIAGNOSIS, TAKEN IN (the class comment): its rows as the reduction read them, its ledgers, links,
    // stage, composed outcomes and typed results, and the golfer's word it read — then the adapter's view half only.
    void mirror() override
    {
        if (m_pipsOnly) {
            mirrorPips();
            return;
        }
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
        m.m_explainOwed = true;   // a new diagnosis: a finished session's footer is explained from it (5.4x)
        m.rebuild();
        if (arrived) emit m.shotIngested(newest, !m.m_quiet);
        QMetaObject::invokeMethod(m_model, [this] { updateBusy(); }, Qt::QueuedConnection);
    }

    // THE CAROUSEL CARDS' ADAPTER (surface "cards"; refactor 3.7-c, review B1): the pips with no panel up. Only what
    // pipsFor / firedCountFor / shotIdForSwingDir read — each shot's rows as the owner's reduction read them, the owner's
    // ledgers and the shots' folders — taken from the owner's applied diagnosis, and a pip order (below). No view half: no
    // graph, no explanation, no ranking, no surfaces. Then one surfaceChanged for the cards.
    void mirrorPips()
    {
        SessionDiagnosticsModel &m = *m_model;
        m_owed = false;
        pinpoint::PostShotTrace::Scope trace("pips");   // [PostShot]: the cards' share of a diagnosis
        const SessionDiagnosis *d = m_owner ? m_owner->diagnosis() : nullptr;
        const std::shared_ptr<const SessionReductionInput> in = m_owner ? m_owner->diagnosisInput() : nullptr;
        m.m_swingDirs.clear();
        if (d && in) {
            m.m_ownerInput = in;
            m.m_shots      = in->shots;
            m.m_ledgers    = d->ledgers;
            if (m_session)
                for (const ShotRecord &r : m.m_shots)
                    if (pinpoint::Shot *shot = m_session->shot(pinpoint::ShotRef(m_session->id(), r.shotId)))
                        m.m_swingDirs.insert(r.shotId, shot->folder());
            // THE PIP ORDER (review B1, "check the order"): the panel's own card order when a panel of this session has
            // worked one out (its stage panel's, else its cast's), so the pips do not move when a panel opens or closes;
            // before any has, the session's patterns first, in the ledger's order — what a panel's order leads with —
            // then (pipsFor) every other tracked condition in the ledger's order.
            QStringList order;
            if (m_session)
                for (const QString surface : { QStringLiteral("panel"), QStringLiteral("cast") })
                    for (SessionDiagnosticsModel *sib : m_session->findChildren<SessionDiagnosticsModel *>(Qt::FindDirectChildrenOnly))
                        if (order.isEmpty() && sib != m_model && sib->m_adapterSurface == surface) order = sib->m_displayOrder;
            if (order.isEmpty())
                for (const ConditionLedger &l : m.m_ledgers)
                    if (l.tier == Tier::Pattern) order << l.id;
            m.m_displayOrder = order;
        } else {
            m.m_ownerInput.reset();
            m.m_shots.clear();
            m.m_ledgers.clear();
        }
        pinpoint::PostShotTrace::Scope t("emit");   // the cards' pip rows (QML) re-read
        emit m.surfaceChanged();
    }

private:
    SessionDiagnosticsModel               *m_model = nullptr;
    QPointer<pinpoint::Session>            m_session;
    QPointer<pinpoint::SessionDiagnostics> m_owner;
    int  m_newest   = -1;      // the newest swing the last mirror showed
    bool m_mirrored = false;
    bool m_owed     = false;   // a diagnosis landed while no view was bound
    bool m_using    = false;   // a use of the Session is held (a view is bound: viewed)
    bool m_pipsOnly = false;   // the "cards" surface: mirrorPips, not the view half
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
    Q_UNUSED(folder);   // the folder the screen chose names the Session it passes; with none, the empty adapter
    const QString key = surface.isEmpty() ? QStringLiteral("panel") : surface;
    if (auto *s = qobject_cast<pinpoint::Session *>(session))
        if (SessionDiagnosticsModel *m = SessionDiagnosticsModel::adapterFor(s, key))
            return watched(m);
    SessionDiagnosticsModel *&empty = m_empty[key];
    if (!empty) {
        empty = new SessionDiagnosticsModel(static_cast<pinpoint::Session *>(nullptr), key);
        empty->setParent(this);
        QQmlEngine::setObjectOwnership(empty, QQmlEngine::CppOwnership);
    }
    return watched(empty);
}

// ── What a panel shows (refactor 3.7-c; the header) ───────────────────────────────────────────────────────────

SessionDiagnosticsModel *SessionDiagnosticsAdapters::watched(SessionDiagnosticsModel *m)
{
    if (!m) return m;
    for (const QPointer<SessionDiagnosticsModel> &p : std::as_const(m_handedOut))
        if (p == m) return m;
    m_handedOut.erase(std::remove_if(m_handedOut.begin(), m_handedOut.end(),
                                     [](const QPointer<SessionDiagnosticsModel> &p) { return p.isNull(); }),
                      m_handedOut.end());
    m_handedOut.append(m);
    if (m->adapterSurface() == QLatin1String("cards"))
        return m;   // the cards' own adapter is not a panel: nothing a binding on shownFor needs to hear
    connect(m, &SessionDiagnosticsModel::viewedChanged, this, &SessionDiagnosticsAdapters::tellShown);
    connect(m, &QObject::destroyed, this, [this] { tellShown(); });
    return m;
}

int SessionDiagnosticsAdapters::shownCount() const
{
    int n = 0;
    for (const QPointer<SessionDiagnosticsModel> &p : m_handedOut)
        if (p && p->views() > 0 && p->adapterSurface() != QLatin1String("cards")) ++n;
    return n;
}

SessionDiagnosticsModel *SessionDiagnosticsAdapters::shownFor(QObject *session, const QString &folder) const
{
    // A Session's: its stage panel's adapter first (the screen beside the carousel), else its wall cast's — both show the
    // Session's one owner. Found among the Session's children (adapterFor's home), so nothing is made.
    // Only the two panel surfaces count (review M1): the cards' own adapter ("cards") is not a panel.
    auto *s = qobject_cast<pinpoint::Session *>(session);
    if (s && s->diagnostics()) {
        SessionDiagnosticsModel *cast = nullptr;
        for (SessionDiagnosticsModel *m : s->findChildren<SessionDiagnosticsModel *>(Qt::FindDirectChildrenOnly)) {
            if (!m->isAdapter() || m->views() == 0) continue;
            if (m->adapterSurface() == QLatin1String("panel")) return m;
            if (m->adapterSurface() == QLatin1String("cast")) cast = m;
        }
        return cast;
    }
    Q_UNUSED(folder);
    return nullptr;   // no Session's owner: nothing judges the folder here, so no panel shows a model for it
}

bool SessionDiagnosticsAdapters::judgedHere(QObject *session, const QString &folder) const
{
    Q_UNUSED(folder);
    auto *s = qobject_cast<pinpoint::Session *>(session);
    return s && s->diagnostics();   // a Session's owner judges it; nothing else does (refactor 5.4b: J-a3 went)
}

// ── The app's doors (the header; refactor 3.7-a, here since 3.7-e) ────────────────────────────────────────────────────

pinpoint::DiagnosticsDoors pinpoint::appDiagnosticsDoors(WorkPool *pool, WorkReceiver *receiver, RuleData *ruleData,
                                                         const SessionRepository &sessions, RecordingRepository &recordings)
{
    DiagnosticsDoors d;
    d.pool         = pool;
    d.receiver     = receiver;
    d.ruleData     = ruleData;
    d.reader       = std::make_shared<SwingStoreFiringReader>(sessions, recordings);   // parses a miss, writes the sidecar
    d.persist      = true;
    d.writeSummary = true;   // the catch-up's cut-over (3.7-e): the owner is work_ons.json's one writer too
    d.attach       = &SessionDiagnostics::attach;
    return d;
}
