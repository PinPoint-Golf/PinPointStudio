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

#pragma once

// ChartsLane — THE CHARTS' ONE THREADING IDIOM (refactor 5.5a; R-CON-5; J-74 Q6): every Charts job — a presenter's
// compute, its stills, a session's series read — runs on THE APP'S ONE WorkPool (src/Core/work_pool.h), on a serial
// lane of its own (one lane per instance, keyed by a number from nextWorkNumber()), and its value comes back through
// THE ONE WorkReceiver on the GUI thread (postToOwner). No Charts code owns a thread pool any more: PresenterPool and
// the stills' QThreadPool::globalInstance() are gone (F-08, F-09).
//
// How a result finds its owner: the composition root's resolver hands every address whose owner is "charts:<n>" to
// ChartsLane::target(), which looks the lane up by its instance number in a GUI-thread registry. A lane whose owner is
// gone is not there: the result is dropped ("owner gone"), so DESTROYING AN OWNER NEVER WAITS FOR ITS JOB (#173): the
// lane is cancelled, the running job finishes into nothing. An ask that is not the lane's latest comes back
// "superseded" and is dropped too — so a coalescing owner never applies an older answer over a newer one.
//
// THE RULE FOR A JOB BODY (work_pool.h): values only — the inputs, the worker caches' shared_ptrs, a thread-safe reader.
// Never `this`. The apply function runs on the GUI thread, and only while the owner lives.
//
//     m_lane.ask([w, in] { return std::any(compute(*w, in)); },
//                [this](std::any &&v) { apply(std::any_cast<std::shared_ptr<Result>>(std::move(v))); });
//
// INSTALL. The app installs its pool and receiver (main.cpp) before any QML is loaded and uninstalls them after the
// engine has gone. A process that installs none (a unit test, a tool) gets a small pool and receiver of the charts' own,
// made on first use and owned by the QCoreApplication (destroyed with it, the pool first) — the same idiom, so the code
// path under test is the app's. With no QCoreApplication at all, a job runs inline.
//
// Unit-tested in src/Gui/tests/charts_work_test.cpp.

#include "../../Diagnostics/derived/series/session_series_types.h"
#include "../../Core/work_pool.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <any>
#include <functional>
#include <map>

namespace pinpoint::charts {

// SeriesSource — WHAT A PRESENTER READS: a session's snapshot of its shots' series, and when it changed (refactor 5.5a).
// The session's own series (SessionSeries, session_series.h: one per Session, its child) is the one the app hands out;
// this small base keeps the presenters and their tests off the Session's whole closure. A presenter's `model` is one of
// these, typed (F-12: no QObject* resolved by qobject_cast on every read).
class SeriesSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("a session's series belongs to its Session: ChartsSeries.of(app.session)")
    // The session the snapshot describes (cleaned, absolute); a reader's place is stamped with it (PpReaderPlace).
    Q_PROPERTY(QString sessionDir READ sessionDir NOTIFY sessionChanged)
    // A read is in flight; the previous snapshot stays published meanwhile.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int shotCount READ shotCount NOTIFY snapshotChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY snapshotChanged)
    Q_PROPERTY(int revision READ revision NOTIFY snapshotChanged)

public:
    using QObject::QObject;
    // Never null once read; an empty session is an empty snapshot.
    virtual std::shared_ptr<const SessionSnapshot> snapshot() const = 0;
    virtual QString sessionDir() const = 0;
    virtual bool    busy() const = 0;
    virtual int     skippedCount() const { return 0; }
    // Why a swing of this session is not in the snapshot ("" when it is, or when it is not one of the session's swings:
    // a trashed one). The ★ reference's words when its swing is left out (refactor 5.5c).
    virtual QString skipReasonOf(const QString &swingDir) const { Q_UNUSED(swingDir); return {}; }
    int shotCount() const { const auto s = snapshot(); return s ? s->count() : 0; }
    int revision() const { const auto s = snapshot(); return s ? int(s->revision & 0x7fffffff) : 0; }

signals:
    void sessionChanged();
    void busyChanged();
    // The snapshot was replaced: everything above that reads it is read again.
    void snapshotChanged();
};

// THE QML DOOR TO A SESSION'S OWN SERIES (refactor 5.5a; J-74 Q1): `ChartsSeries.of(app.session)`, or
// `ChartsSeries.of(app.sessionAt(folder))` for a Trust view whose swing is in another session. A singleton type, not a
// context global; it holds nothing. The series is the Session's child named "sessionSeries" (SessionSeries::attach,
// session_series.h), found by name so this module names no domain type.
class ChartsSeries : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    Q_INVOKABLE pinpoint::charts::SeriesSource *of(QObject *session) const
    {
        return session ? session->findChild<SeriesSource *>(QStringLiteral("sessionSeries"), Qt::FindDirectChildrenOnly) : nullptr;
    }
};

class ChartsLane
{
public:
    // `owner` lives on the GUI thread; the lane's results are applied there, only while it lives.
    explicit ChartsLane(QObject *owner, pinpoint::WorkPriority priority = pinpoint::WorkPriority::Low);
    ~ChartsLane();   // unregisters and cancels the lane's waiting jobs; never waits
    ChartsLane(const ChartsLane &)            = delete;
    ChartsLane &operator=(const ChartsLane &) = delete;

    // Run `work` on this lane; `apply` its value on the GUI thread. A job that throws applies nothing (logged).
    void ask(std::function<std::any()> work, std::function<void(std::any &&)> apply);

    // Asks not yet come back (kept, superseded or dropped).
    int  inFlight() const { return int(m_applies.size()); }
    bool idle() const { return m_applies.empty(); }
    // Spin the GUI thread's event loop until this lane is idle — for ever with -1. True when it is. Tests and the
    // synchronous switch only; the app never waits on a lane.
    bool waitForDone(int msTimeout = -1);
    // The live session's lane runs in front of a reviewed one's of the same priority (work_pool.h).
    void setForeground(bool on);

    quint64 instance() const { return m_instance; }

    // ── The composition root's half ───────────────────────────────────────────────────────────────
    static void install(pinpoint::WorkPool *pool, pinpoint::WorkReceiver *receiver);
    static void uninstall();
    // The owner text every Charts address carries: "charts:<instance>".
    static bool                 owns(const pinpoint::WorkAddress &a);
    static pinpoint::WorkTarget target(const pinpoint::WorkAddress &a);

private:
    void deliver(pinpoint::WorkVerdict verdict, pinpoint::WorkResult &&result);

    QPointer<QObject>       m_owner;
    pinpoint::WorkPriority  m_priority;
    quint64                 m_instance = 0;
    quint64                 m_latest   = 0;
    std::map<quint64, std::function<void(std::any &&)>> m_applies;   // by ask number, in order
};

} // namespace pinpoint::charts
