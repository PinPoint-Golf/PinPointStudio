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

#include "../../Vocabulary/LaunchMonitor/launch_monitor_reading.h"
#include "../../LaunchMonitor/inferred/shot_pairing.h"

#include <QString>
#include <QStringList>

#include <deque>
#include <functional>

class NotificationCenter;

// Tells the golfer about a swing the app could not record because it was still analysing the last one
// (#281, the stopgap until overlapping capture, Stage D).
//
// While a shot is in the pipeline the shot detector is disarmed and the capture buffer is paused, so a ball hit
// then is never detected and not one frame of it is kept. The launch monitor still measures it, and its reading
// reaches the pairing with no shot waiting: "discarded". This class turns that one case into a notice, and leaves
// the reading discarded: no shot is invented.
//
// A discarded reading is a MISSED SWING only when its ball was struck while the pipeline was busy:
//   - with the device's contact time (Foresight Premier): that time falls inside a busy span. A reading that lands
//     just after the pipeline went idle is still told; a ball struck after the pipeline went idle (visit 1's swing 3,
//     which was then detected and recorded) is not;
//   - without one (FSX2020, GSPro): the pipeline is busy when the reading arrives.
// Never while a session is being reviewed (`quietWhen`), never for a reading the pairing took
// (claimed or parked) or that became a shot of its own, and once per device shot.
//
// Not a QObject and no I/O: LaunchMonitorController feeds it every offer, ShotProcessor's busy state reaches it
// through the controller, and it posts through the one NotificationCenter.
class MissedSwingNotice
{
public:
    // The notification's stable id (notification_center.h): two misses in one dwell show "(×2)".
    static constexpr const char *kId = "lm.swing.missed";
    // How many busy spans, and how many device shot ids, are remembered. A reading arrives within seconds.
    static constexpr int kSpans = 4;
    static constexpr int kIds   = 32;

    explicit MissedSwingNotice(std::function<qint64()> clockUtcMs = {});

    // Where the notice goes; null = nowhere (the controller without a composition root, as in a test).
    void setSink(NotificationCenter *sink) { m_sink = sink; }
    // True while no notice may be shown: a session is being reviewed (capture is off there).
    void setQuietWhen(std::function<bool()> quiet) { m_quiet = std::move(quiet); }

    // ShotProcessor::busy(), as LaunchMonitorController::setPipelineBusy hears it.
    void setPipelineBusy(bool busy);

    // Every reading the pairing was offered, with what became of it. `becameShot`: a discarded reading was saved
    // as a shot of its own (standalone shots). Returns true when the golfer was told.
    bool hear(const pinpoint::lm::ShotPairing::Offer &offer, const pinpoint::lm::LaunchMonitorReading &reading,
              bool becameShot = false);

    // The words, for the test and for the log.
    static QString title();
    static QString detail();

private:
    struct Span { qint64 fromMs = 0; qint64 toMs = -1; };   // toMs < 0: still busy
    bool    struckWhileBusy(const pinpoint::lm::LaunchMonitorReading &r) const;
    QString keyOf(const pinpoint::lm::LaunchMonitorReading &r) const;
    void    remember(const QString &key);

    std::function<qint64()> m_clock;
    std::function<bool()>   m_quiet;
    NotificationCenter     *m_sink = nullptr;
    bool                    m_busy = false;
    std::deque<Span>        m_spans;   // newest last
    QStringList             m_seen;    // device shots already paired or told, newest last
};
