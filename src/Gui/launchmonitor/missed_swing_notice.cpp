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

#include "missed_swing_notice.h"

#include "../app/notification_center.h"

#include <QCoreApplication>
#include <QDateTime>

using pinpoint::lm::LaunchMonitorReading;
using pinpoint::lm::ShotPairing;

MissedSwingNotice::MissedSwingNotice(std::function<qint64()> clockUtcMs)
    : m_clock(clockUtcMs ? std::move(clockUtcMs) : [] { return QDateTime::currentMSecsSinceEpoch(); })
{
}

QString MissedSwingNotice::title()
{
    return QCoreApplication::translate("MissedSwingNotice",
                                       "A swing was hit while the last one was being analysed, and it wasn't recorded");
}

QString MissedSwingNotice::detail()
{
    return QCoreApplication::translate("MissedSwingNotice",
                                       "Wait for “Analysing” to clear before the next swing.");
}

void MissedSwingNotice::setPipelineBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    const qint64 now = m_clock();
    if (busy) {
        m_spans.push_back({ now, -1 });
        while (int(m_spans.size()) > kSpans)
            m_spans.pop_front();
    } else if (!m_spans.empty()) {
        m_spans.back().toMs = now;
    }
}

QString MissedSwingNotice::keyOf(const LaunchMonitorReading &r) const
{
    // An empty id is no identity: two such readings are never the same shot.
    if (r.deviceShotId.isEmpty())
        return {};
    return r.deviceKind + QLatin1Char('/') + r.deviceShotId;
}

void MissedSwingNotice::remember(const QString &key)
{
    if (key.isEmpty() || m_seen.contains(key))
        return;
    m_seen.append(key);
    while (m_seen.size() > kIds)
        m_seen.removeFirst();
}

bool MissedSwingNotice::struckWhileBusy(const LaunchMonitorReading &r) const
{
    if (r.contactAtMs) {
        const qint64 t = *r.contactAtMs;
        for (const Span &s : m_spans)
            if (t >= s.fromMs && (s.toMs < 0 || t <= s.toMs))
                return true;
        return false;
    }
    return m_busy;   // no contact time: the moment it arrived is all there is
}

bool MissedSwingNotice::hear(const ShotPairing::Offer &offer, const LaunchMonitorReading &reading, bool becameShot)
{
    const QString key = keyOf(reading);
    if (offer.disposition != ShotPairing::Disposition::Discarded || becameShot) {
        remember(key);   // this shot has a swing; a restatement of it later is not a miss
        return false;
    }
    if (!key.isEmpty() && m_seen.contains(key))
        return false;
    if (!struckWhileBusy(reading))
        return false;
    if (m_quiet && m_quiet())
        return false;

    remember(key);
    if (!m_sink)
        return false;
    NotificationCenter::Notification n;
    n.id       = QString::fromLatin1(kId);
    n.severity = NotificationCenter::Warn;
    n.kind     = NotificationCenter::Event;
    n.title    = title();
    n.detail   = detail();
    n.glyph    = QStringLiteral("⊘");   // ⊘, as a shot this host refused
    m_sink->post(n);   // logs it too
    return true;
}
