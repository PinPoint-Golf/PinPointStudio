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

#include <QtGlobal>

// BusyActivity — keeps macOS App Nap off for as long as it lives.
//
// A Mac app whose windows are all out of sight is napped: its timers are coalesced and it runs at a lower
// priority. Work the user started and is waiting on (printing a report) holds one of these, which is
// NSProcessInfo's user-initiated activity; the system may still sleep the display. Elsewhere there is no
// App Nap and this is nothing.
class BusyActivity
{
public:
    explicit BusyActivity(const char *reason);
    ~BusyActivity();
    Q_DISABLE_COPY_MOVE(BusyActivity)

private:
    void *m_token = nullptr;      // the NSProcessInfo activity, retained (macOS only)
};

#if !defined(Q_OS_MACOS)
inline BusyActivity::BusyActivity(const char *) {}
inline BusyActivity::~BusyActivity() {}
#endif
