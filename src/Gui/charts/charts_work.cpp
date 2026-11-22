/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "charts_work.h"
#include "../../Core/pp_debug.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHash>
#include <QThread>

#include <memory>

namespace pinpoint::charts {

namespace {

const QString kOwnerPrefix = QStringLiteral("charts:");

// The installed pool and receiver (the app's), else the charts' own (a test's, a tool's), made on first use.
pinpoint::WorkPool     *g_pool     = nullptr;
pinpoint::WorkReceiver *g_receiver = nullptr;

// THE REGISTRY: live lanes by instance number. GUI thread only (the receiver resolves on its own thread, the GUI's).
QHash<quint64, ChartsLane *> &lanes()
{
    static QHash<quint64, ChartsLane *> h;
    return h;
}

// A process with no installed pool: one of the charts' own, owned by the application object, which destroys the pool
// before the receiver (the pool's running jobs may still post).
class OwnWork : public QObject
{
public:
    explicit OwnWork(QObject *parent)
        : QObject(parent),
          receiver(std::make_unique<pinpoint::WorkReceiver>([](const pinpoint::WorkAddress &a) { return ChartsLane::target(a); })),
          pool(std::make_unique<pinpoint::WorkPool>(std::max(2, QThread::idealThreadCount() / 2)))
    {
        setObjectName(QStringLiteral("ChartsOwnWork"));
    }
    ~OwnWork() override
    {
        pool.reset();
        receiver.reset();
    }
    std::unique_ptr<pinpoint::WorkReceiver> receiver;
    std::unique_ptr<pinpoint::WorkPool>     pool;
};

OwnWork *ownWork()
{
    static QPointer<OwnWork> own;
    if (!own && QCoreApplication::instance())
        own = new OwnWork(QCoreApplication::instance());
    return own.data();
}

} // namespace

void ChartsLane::install(pinpoint::WorkPool *pool, pinpoint::WorkReceiver *receiver)
{
    g_pool = pool;
    g_receiver = receiver;
}

void ChartsLane::uninstall()
{
    g_pool = nullptr;
    g_receiver = nullptr;
}

bool ChartsLane::owns(const pinpoint::WorkAddress &a) { return a.owner.startsWith(kOwnerPrefix); }

pinpoint::WorkTarget ChartsLane::target(const pinpoint::WorkAddress &a)
{
    pinpoint::WorkTarget t;
    ChartsLane *lane = lanes().value(a.instance, nullptr);
    if (!lane || !lane->m_owner) return t;   // OwnerGone: dropped, nobody told
    t.instance  = lane->m_instance;
    t.itemFound = true;
    t.latestAsk = lane->m_latest;
    t.deliver   = [lane](pinpoint::WorkVerdict v, pinpoint::WorkResult &&r) { lane->deliver(v, std::move(r)); };
    return t;
}

ChartsLane::ChartsLane(QObject *owner, pinpoint::WorkPriority priority)
    : m_owner(owner), m_priority(priority), m_instance(pinpoint::nextWorkNumber())
{
    lanes().insert(m_instance, this);
}

ChartsLane::~ChartsLane()
{
    lanes().remove(m_instance);
    pinpoint::WorkPool *pool = g_pool;
    if (!pool) if (OwnWork *o = ownWork()) pool = o->pool.get();
    if (pool) pool->cancelLane(m_instance);   // the running job finishes into nothing: its result finds no owner
}

void ChartsLane::setForeground(bool on)
{
    pinpoint::WorkPool *pool = g_pool;
    if (!pool) if (OwnWork *o = ownWork()) pool = o->pool.get();
    if (pool) pool->setForeground(m_instance, on);
}

void ChartsLane::ask(std::function<std::any()> work, std::function<void(std::any &&)> apply)
{
    if (!work) return;
    const quint64 ask = pinpoint::nextWorkNumber();
    m_latest = ask;
    pinpoint::WorkPool     *pool = g_pool;
    pinpoint::WorkReceiver *rx   = g_receiver;
    if (!pool || !rx) {
        if (OwnWork *o = ownWork()) { pool = o->pool.get(); rx = o->receiver.get(); }
    }
    if (!pool || !rx) {
        // No application object: inline, as a tool with no worker threads runs its jobs (R-HL-2).
        std::any v;
        try { v = work(); } catch (...) { ppWarn() << "[Charts] a job threw; nothing applied"; return; }
        if (apply) apply(std::move(v));
        return;
    }
    m_applies.emplace(ask, std::move(apply));
    pinpoint::WorkAddress address{ kOwnerPrefix + QString::number(m_instance), m_instance, QString(), ask };
    if (!pinpoint::postToOwner(*pool, *rx, m_instance, std::move(address), std::move(work), m_priority))
        m_applies.erase(ask);
}

void ChartsLane::deliver(pinpoint::WorkVerdict verdict, pinpoint::WorkResult &&result)
{
    const auto it = m_applies.find(result.address.ask);
    if (it == m_applies.end()) return;
    std::function<void(std::any &&)> apply = std::move(it->second);
    m_applies.erase(it);
    if (verdict != pinpoint::WorkVerdict::Kept) return;   // superseded: a newer ask's answer is on its way
    if (!result.error.isEmpty()) {
        ppWarn() << "[Charts] a job failed:" << result.error;
        return;
    }
    if (apply && m_owner) apply(std::move(result.value));
}

bool ChartsLane::waitForDone(int msTimeout)
{
    const QDeadlineTimer deadline = msTimeout < 0 ? QDeadlineTimer(QDeadlineTimer::Forever) : QDeadlineTimer(msTimeout);
    while (!m_applies.empty() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (!m_applies.empty()) QThread::msleep(1);
    }
    return m_applies.empty();
}

} // namespace pinpoint::charts
