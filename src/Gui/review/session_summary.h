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

#include "../../LaunchMonitor/lm_device.h"
#include "../../DomainModel/session_roll_up.h"   // ShotSummaryInput, the one fold, the device order (5.7a-P1)

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <iterator>

// Pure session-metadata aggregation — no Qt-GUI / QML deps, so the unit test in
// src/Analysis/tests compiles it standalone. SessionReviewController feeds it
// per-shot inputs (live shots read from the in-memory model; past shots read
// from disk via SwingDocReader) and turns the result into a SessionListModel row.

namespace pinpoint {

// ShotSummaryInput and the fold (rollUpSwings) are the session index entry's (DomainModel/session_roll_up.h, refactor
// 5.7a-P1): this header turns a roll-up into the labels a row shows.

struct SessionSummary {
    int         shotCount   = 0;
    int         avgQuality  = 0;  // mean of shot scores, rounded; 0 when empty
    QString     lengthLabel;      // "42 min" / "1 h 12 m" / "" when span unknown
    QString     clubMix;          // distinct clubs, first-seen order, " · "-joined
    QString     dayLabel;         // "Today" / "Yesterday" / "Mon 8 Jun"
    QString     timeLabel;        // "hh:mm" of the earliest shot
    QStringList previewThumbs;    // up to maxThumbs absolute thumbnail paths
    QStringList devices;          // every device any shot used, canonical (lm_device.h), in sortDeviceTokens order
};

// The short name a device token is shown by — a tag on a session row and a filter chip.
inline QString deviceLabel(const QString &token)
{
    if (token == QLatin1String("faceOn"))     return QStringLiteral("Face-on");
    if (token == QLatin1String("dtl"))        return QStringLiteral("DTL");
    if (token == QLatin1String("impact"))     return QStringLiteral("Impact cam");
    if (token == QLatin1String("camera"))     return QStringLiteral("Camera");
    if (token == QLatin1String("hackmotion")) return QStringLiteral("HackMotion");
    if (token == QLatin1String("imu"))        return QStringLiteral("Motion sensor");
    // The launch monitor by its DEVICE, never its connector (lm_device.h).
    if (token.startsWith(QLatin1String("lm:"))) return lm::deviceName(token.mid(3));
    return token;
}

// "42 min" under an hour, "1 h 12 m" above; empty for a non-positive span.
inline QString formatSessionLength(qint64 spanMs)
{
    if (spanMs <= 0)
        return {};
    const qint64 mins = (spanMs + 30000) / 60000;          // round to nearest minute
    if (mins < 60)
        return QString::number(mins) + QStringLiteral(" min");
    return QString::number(mins / 60) + QStringLiteral(" h ")
         + QString::number(mins % 60) + QStringLiteral(" m");
}

// "Today" / "Yesterday" / "Mon 8 Jun" for an instant relative to now (both epoch ms).
inline QString relativeDayLabel(qint64 instantMs, qint64 nowMs)
{
    if (instantMs <= 0)
        return {};
    const QDate d   = QDateTime::fromMSecsSinceEpoch(instantMs).date();
    const QDate now = QDateTime::fromMSecsSinceEpoch(nowMs).date();
    const qint64 ago = d.daysTo(now);
    if (ago == 0) return QStringLiteral("Today");
    if (ago == 1) return QStringLiteral("Yesterday");
    return d.toString(QStringLiteral("ddd d MMM"));
}

// Sentence-case a canonical club id for DISPLAY only ("DRIVER" -> "Driver",
// "7 IRON" -> "7 iron", "GAP WEDGE" -> "Gap wedge"). Storage/keys keep the
// uppercase club_vocabulary.h id. Mirror of the ClubFormat QML singleton.
inline QString clubDisplayLabel(const QString &id)
{
    const QString t = id.trimmed();
    if (t.isEmpty())
        return {};
    return t.left(1).toUpper() + t.mid(1).toLower();
}

// A session's roll-up as a row shows it. `nowMs` anchors the relative day label. Pure and deterministic given its inputs.
inline SessionSummary summaryOf(const SessionSwingRollUp &r, qint64 nowMs)
{
    SessionSummary s;
    s.shotCount = r.summarised;
    if (r.summarised == 0)
        return s;
    s.avgQuality = r.meanScore.value_or(0);
    QStringList clubLabels;
    clubLabels.reserve(r.clubs.size());
    for (const QString &c : r.clubs)
        clubLabels.append(clubDisplayLabel(c));
    s.clubMix       = clubLabels.join(QStringLiteral(" · "));
    s.previewThumbs = r.thumbnails;
    s.devices       = r.devices;
    if (r.firstMs > 0) {
        s.lengthLabel = formatSessionLength(r.spanMs);
        s.dayLabel    = relativeDayLabel(r.firstMs, nowMs);
        s.timeLabel   = QDateTime::fromMSecsSinceEpoch(r.firstMs).toString(QStringLiteral("hh:mm"));
    }
    return s;
}

// Aggregate a session's shots: the one fold (rollUpSwings), then its labels. `maxThumbs` caps the film-strip preview.
inline SessionSummary summarizeSession(const QVector<ShotSummaryInput> &shots,
                                       qint64 nowMs, int maxThumbs = SessionSwingRollUp::kMaxThumbs)
{
    return summaryOf(rollUpSwings(shots, -1, maxThumbs), nowMs);
}

} // namespace pinpoint
