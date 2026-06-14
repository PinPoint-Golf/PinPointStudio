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

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>

// Pure session-metadata aggregation — no Qt-GUI / QML deps, so the unit test in
// src/Analysis/tests compiles it standalone. SessionReviewController feeds it
// per-shot inputs (live shots read from the in-memory model; past shots read
// from disk via SwingDocReader) and turns the result into a SessionListModel row.

namespace pinpoint {

// One shot's contribution to its session's summary.
struct ShotSummaryInput {
    int     score        = 0;     // 0–100 quality
    bool    hasVideo     = false;
    qint64  wallclockMs  = 0;     // absolute instant (epoch ms); 0 = unknown
    QString club;                 // equipment used (stub "DRIVER" until detection lands)
    QString thumbnailPath;        // absolute path; empty if none extracted
};

struct SessionSummary {
    int         shotCount   = 0;
    int         avgQuality  = 0;  // mean of shot scores, rounded; 0 when empty
    QString     lengthLabel;      // "42 min" / "1 h 12 m" / "" when span unknown
    QString     clubMix;          // distinct clubs, first-seen order, " · "-joined
    QString     dayLabel;         // "Today" / "Yesterday" / "Mon 8 Jun"
    QString     timeLabel;        // "hh:mm" of the earliest shot
    QStringList previewThumbs;    // up to maxThumbs absolute thumbnail paths
};

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

// Aggregate a session's shots. `nowMs` anchors the relative day label; `maxThumbs`
// caps the film-strip preview. Pure and deterministic given its inputs.
inline SessionSummary summarizeSession(const QVector<ShotSummaryInput> &shots,
                                       qint64 nowMs, int maxThumbs = 4)
{
    SessionSummary s;
    s.shotCount = shots.size();
    if (shots.isEmpty())
        return s;

    qint64 scoreSum = 0;
    qint64 minT = 0, maxT = 0;
    bool   haveT = false;
    QStringList clubs;
    for (const ShotSummaryInput &in : shots) {
        scoreSum += in.score;
        if (in.wallclockMs > 0) {
            if (!haveT) { minT = maxT = in.wallclockMs; haveT = true; }
            else        { minT = std::min(minT, in.wallclockMs);
                          maxT = std::max(maxT, in.wallclockMs); }
        }
        const QString c = in.club.trimmed();
        if (!c.isEmpty() && !clubs.contains(c))
            clubs.append(c);
        if (!in.thumbnailPath.isEmpty() && s.previewThumbs.size() < maxThumbs)
            s.previewThumbs.append(in.thumbnailPath);
    }

    // Rounded mean over every shot (failed/IMU-only shots score 0 and count).
    s.avgQuality = int((scoreSum + shots.size() / 2) / shots.size());
    s.clubMix    = clubs.join(QStringLiteral(" · "));
    if (haveT) {
        s.lengthLabel = formatSessionLength(maxT - minT);
        s.dayLabel    = relativeDayLabel(minT, nowMs);
        s.timeLabel   = QDateTime::fromMSecsSinceEpoch(minT).toString(QStringLiteral("hh:mm"));
    }
    return s;
}

} // namespace pinpoint
