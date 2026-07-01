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

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace pinpoint::analysis { struct SwingAnalysis; }

namespace pinpoint {

// The single, unified per-shot document. Raw capture manifest and derived analysis
// live in ONE swing.json — no separate analysis.json. Written once, on the GUI thread,
// at the analyzer∥exporter join (ShotProcessor::maybeJoin), so the two concurrent
// workers never write the same file.
class SwingDocWriter {
public:
    // Compose `rawManifest` (the exporter's returned pinpoint.swing tree) with the
    // analyzer's `analysis` (inline, additive "analysis" object) and write atomically
    // to <swingDir>/swing.json. Bumps schema to pinpoint.swing/2. analysis==nullptr →
    // raw only (no "analysis"). Returns false (and sets *error) on write failure.
    static bool writeSwingJson(const QString &swingDir, const QJsonObject &rawManifest,
                               const analysis::SwingAnalysis *analysis,
                               QString *error = nullptr);

    // Write-through of the user's review (rating 0–5, free-text note, club) into
    // an existing <swingDir>/swing.json: reads the doc, replaces the additive
    // "review" block, atomic rewrite (QSaveFile). Called from the shot model's
    // setRating/setNote/setClub so edits survive a restart. Returns false (and
    // sets *error) if the doc can't be read or rewritten — a shot whose swing.json
    // was never written (e.g. export failed) simply fails here harmlessly.
    static bool updateReview(const QString &swingDir, int rating, const QString &note,
                             const QString &club, QString *error = nullptr);
};

// A reloaded shot — everything ShotListModel::addPersistedShot needs to rebuild a
// carousel row from a swing.json on disk. The flat metrics + analysisDetail are
// reconstructed into the same shapes ShotProcessor produces live.
struct PersistedShot {
    bool        ok = false;
    QString     swingDir;
    int         ordinal = 0;
    QString     timestampLabel;     // hh:mm:ss from clock.wallclock
    qint64      wallclockMs = 0;    // absolute instant from clock.wallclock (epoch ms; 0 = unknown)
    QString     club;
    bool        hasVideo = false;
    QString     thumbnailPath;      // absolute, empty if none
    int         score = 0;
    int         rating = 0;         // 0–5 user stars (from the "review" block)
    QString     note;               // free-text user note (from the "review" block)
    QVariantMap metrics;            // key -> { label, value } at Impact
    QVariantMap analysisDetail;     // { tier, overall, series, phases } for the graph
    bool        dataWarning = false;// IMU re-fusion parity failed (imuIntegrity block) → not re-analysable
};

// Reads the single unified swing.json back into reloadable shots.
class SwingDocReader {
public:
    static PersistedShot readSwingJson(const QString &swingDir);
    // swing_*/ directories under sessionDir, ascending (swing_0001 .. swing_NNNN).
    static QStringList findSwingDirs(const QString &sessionDir);
    // Most recent session dir for an athlete, by directory modification time
    // (folder names embed the naming pattern, so a name sort isn't reliable);
    // empty if the library/athlete has none.
    static QString latestSessionDir(const QString &libraryRoot, const QString &athleteName);
    // All session dirs for an athlete, most-recently-modified first (same recency
    // basis as latestSessionDir). Empty list if the library/athlete has none.
    static QStringList sessionDirs(const QString &libraryRoot, const QString &athleteName);
};

} // namespace pinpoint
