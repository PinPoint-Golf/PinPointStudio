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

#pragma once

// THE SESSION INDEX ENTRY'S SUMMARY (refactor step 5.7a-P1; information_model.md OBJ-SESSION-INDEX, proposal P1, approved by
// the Solution Architect on 23 Nov 2026): what a list of sessions shows of a session beyond its identity and status, as
// SessionIndexEntry::summary holds it, filled through SessionRepository::deferredSummary:
//   - the swing roll-up: the swings' cheap summaries folded (count, first swing time, span, clubs, devices, mean score,
//     thumbnails, indexed);
//   - the size on disk: every file in the session's folder;
//   - the lead work-on: work-on 1 of the session's summary (FILE-SESS-05), under this build's rule only.
// Raw values only, no labels: "Today", "42 min" and a club's display name are a view's (Gui/review/session_summary.h).
// A fact not known is said so (a flag, an optional), never a 0.
//
// THE ONE FOLD (rollUpSwings) is the one every session list runs, the live row's included: a swing's cheap summary
// (ShotSummaryInput) in, the roll-up out. Pure and header-only (session_summary_test compiles it standalone).
//
// Domain model ring (src/DomainModel): vocabulary below it only (the launch monitor's device names).

#include "../LaunchMonitor/lm_device.h"   // canonicalDeviceToken (vocabulary)

#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <iterator>
#include <optional>

namespace pinpoint {

// One swing's contribution to its session's roll-up: its cheap summary (SwingDocReader::readSwingSummary), or a live shot's
// facts as the live model holds them.
struct ShotSummaryInput {
    int     score        = 0;     // 0–100 quality
    bool    hasVideo     = false;
    qint64  wallclockMs  = 0;     // absolute instant (epoch ms); 0 = unknown
    QString club;                 // equipment used, its canonical id ("7 IRON")
    QString thumbnailPath;        // absolute path; empty if none extracted
    QStringList devices;          // swingDeviceTokens() of the shot ("faceOn", "hackmotion", "lm:gcquad"), as stored
};

// The fixed order device tokens are shown in: cameras, then the wrist and body sensors, then the
// launch monitor(s) — so a row's tags and the filter chips line up from session to session.
inline int deviceTokenRank(const QString &token)
{
    static const char *const kOrder[] = { "faceOn", "dtl", "impact", "camera", "hackmotion", "imu" };
    for (int i = 0; i < int(std::size(kOrder)); ++i)
        if (token == QLatin1String(kOrder[i])) return i;
    return int(std::size(kOrder));                       // "lm:<kind>" and anything newer
}

inline void sortDeviceTokens(QStringList &tokens)
{
    std::stable_sort(tokens.begin(), tokens.end(), [](const QString &a, const QString &b) {
        const int ra = deviceTokenRank(a), rb = deviceTokenRank(b);
        return ra != rb ? ra < rb : a < b;
    });
}

// THE SWING ROLL-UP.
struct SessionSwingRollUp {
    static constexpr int kMaxThumbs = 4;   // the film strip's preview

    int         swings     = 0;   // the swing folders listed (the live row: its shots)
    int         summarised = 0;   // of them, the ones whose cheap summary read: the ones folded below
    qint64      firstMs    = 0;   // the earliest capture time of a summarised swing (epoch ms); 0: no swing says when
    qint64      spanMs     = 0;   // the latest capture time minus the earliest; 0 when firstMs is 0 (or one instant)
    QStringList clubs;            // distinct, in first-seen order, as recorded (trimmed ids, not display names)
    QStringList devices;          // every device any swing used, canonical (lm_device.h), in sortDeviceTokens order
    std::optional<int> meanScore; // the rounded mean of the summarised swings' scores (a failed swing scores 0 and
                                  // counts); none when no swing is summarised
    QStringList thumbnails;       // the first kMaxThumbs summarised swings' thumbnails that have one (absolute)

    // Every swing listed has its cheap summary: the folded facts are the whole session's.
    bool indexed() const { return summarised == swings; }
};

// THE LEAD WORK-ON: work-on 1 of the session's summary (work_ons.json, FILE-SESS-05).
struct SessionLeadWorkOn {
    bool    known = false;   // the summary is there and holds a record of this build's rule (an older rule's is about to
                             // be derived again, and its work-on 1 may not be the new rule's): false is NA, never "none"
    QString id;              // its rank-1 entry (else its first); empty with `known`: the session has no work-on
    QString name;
};

// SessionIndexEntry::summary.
struct SessionIndexSummary {
    bool               read  = false;   // false: not read yet (a bare enumerate): every field below is NA
    SessionSwingRollUp swings;
    qint64             bytes = 0;       // the size on disk: every file in the folder, an archived session's stubs
                                        // included (what the folder takes, not what it held)
    SessionLeadWorkOn  workOn;
};

// THE ONE FOLD. `swingFolders`: how many swings the session lists (-1: the shots given, as for a live row whose every shot
// is in hand).
inline SessionSwingRollUp rollUpSwings(const QVector<ShotSummaryInput> &shots, int swingFolders = -1,
                                       int maxThumbs = SessionSwingRollUp::kMaxThumbs)
{
    SessionSwingRollUp r;
    r.summarised = int(shots.size());
    r.swings     = swingFolders < 0 ? r.summarised : swingFolders;
    if (shots.isEmpty())
        return r;

    qint64 scoreSum = 0;
    qint64 minT = 0, maxT = 0;
    bool   haveT = false;
    for (const ShotSummaryInput &in : shots) {
        scoreSum += in.score;
        if (in.wallclockMs > 0) {
            if (!haveT) { minT = maxT = in.wallclockMs; haveT = true; }
            else        { minT = std::min(minT, in.wallclockMs);
                          maxT = std::max(maxT, in.wallclockMs); }
        }
        const QString c = in.club.trimmed();
        if (!c.isEmpty() && !r.clubs.contains(c))
            r.clubs.append(c);
        if (!in.thumbnailPath.isEmpty() && r.thumbnails.size() < maxThumbs)
            r.thumbnails.append(in.thumbnailPath);
        // Canonical here, where every session list gathers its shots: a summary written before
        // `launchMonitor.device` existed still says "lm:premier", and it is the same GC Quad.
        for (const QString &raw : in.devices)
            if (const QString d = lm::canonicalDeviceToken(raw); !r.devices.contains(d))
                r.devices.append(d);
    }
    sortDeviceTokens(r.devices);
    // Rounded mean over every summarised shot (failed/IMU-only shots score 0 and count).
    r.meanScore = int((scoreSum + shots.size() / 2) / shots.size());
    if (haveT) {
        r.firstMs = minT;
        r.spanMs  = maxT - minT;
    }
    return r;
}

} // namespace pinpoint
