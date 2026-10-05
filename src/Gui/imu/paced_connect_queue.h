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

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>

// The paced BLE connect queue (session_wizard_refactor_design.md §4.11, finding F10).
//
// Connecting several BLE peripherals back to back makes BlueZ (and, less dramatically,
// CoreBluetooth) trip over its own GATT state, so the app has always connected them one
// at a time with a gap between. That queue was written twice in QML — the start wizard's
// IMU step and PpImuPanel — each with its own Timer, index and "connecting" flag, and each
// living on a page that could be torn down mid-queue. ImuManager now owns the one queue;
// this class is its timing, factored out so the timing is testable without a device stack.
//
// Contract:
//   start(ids, gap)  the first id is offered to the callback IMMEDIATELY, each next one
//                    `gap` ms after the previous ACCEPTED one. A second start() while
//                    active REPLACES whatever was still pending.
//   the callback     returns true when it issued a connect, false to SKIP the id (not
//                    enumerated, already connected, …). ⚠ A skipped id costs no gap: the
//                    gap exists to space real connects, and waiting 2 s to do nothing
//                    would only make the last sensor late.
//   cancel()         drops everything pending. A connect already issued is not undone.
//   active           true while at least one id is still waiting for its turn — the same
//                    meaning the QML queues' `_connecting` had: it goes false the moment
//                    the LAST id is offered, not a gap later.
class PacedConnectQueue : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    using SelectFn = std::function<bool(const QString &deviceId)>;

    explicit PacedConnectQueue(SelectFn select, QObject *parent = nullptr);

    void start(const QStringList &deviceIds, int gapMs);
    void cancel();

    bool        active()  const { return m_active; }
    QStringList pending() const { return m_pending; }

signals:
    void activeChanged();

private:
    // Offers ids from the front of the queue until one is accepted (or the queue is
    // empty), then arms the gap timer if anything is left.
    void drain();
    void setActive(bool on);

    SelectFn    m_select;
    QStringList m_pending;
    QTimer      m_timer;
    bool        m_active = false;
};
