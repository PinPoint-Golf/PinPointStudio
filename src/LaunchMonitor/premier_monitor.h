/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "launch_monitor_base.h"
#include "premier_session_parser.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>

#include <optional>

namespace pinpoint::lm {

// Reads a GC Quad through Foresight Premier, from the files Premier leaves in its per-user data
// folder (%USERPROFILE%\AppData\LocalLow\Foresight Sports\Foresight Premier). The design and
// every measurement behind it: docs/design/foresight_premier_connector.md §2.6 and §4.
//
// THE SOURCE IS SessionLiveSnapshot.<userId>.json. Premier rewrites it whole once per shot,
// ~0.6 s after contact, for as long as a RANGE session is open, and deletes it when the session
// ends. Training drills never write it, so in a drill this connector sees nothing.
//
// CLUB DATA LANDS ONE SHOT LATE. A shot is written with ClubData null; its club data only
// reaches disk with the next shot's write. Holding a shot until then would hand its reading to
// whichever swing is armed by then, so instead:
//   1. a new shot is emitted at once (readingAvailable), ball data only, with NO club drops —
//      its club data is pending, not missing;
//   2. once the shot is no longer the newest in the snapshot it is FINAL, and is emitted again
//      whole (readingUpdated, same deviceShotId, first readAtMs kept): with its club data, or
//      with every club field a `noData` drop when none came (a club with no stickers);
//   3. the newest shot of a session is finalised from SessionUploadQueue.<userId>.json, where
//      Premier flushes its club data on a normal exit to the menu. If it reaches the queue
//      still without club data (Premier was quit mid-session, or the club had no stickers —
//      the file cannot say which), it is logged and left ball-only rather than mislabelled.
// A shot whose ClubData is already present on first sight is final at once.
//
// THE QUEUE IS NEVER A SOURCE OF NEW SHOTS. It holds ended sessions (one of them, on the
// capture PC, rejected by the server and stuck there for good); a shot first seen there was
// never live here. It is read only to finalise a pending shot.
//
// STAT FIRST, OPEN ONLY ON CHANGE. Premier opens the upload queue EXCLUSIVELY, and a reader
// that opened it on every tick made Premier's upload fail with a sharing violation (§2.6.1).
// Each file is stat'ed every tick (size + mtime) and opened only when one of them moved — and
// the queue only while a shot is waiting on it. A failed open or a torn read is "try next tick".
//
// Polling and not QFileSystemWatcher, for the reasons gcquad_monitor.h gives.
class ForesightPremierMonitor : public LaunchMonitorBase
{
    Q_OBJECT

public:
    explicit ForesightPremierMonitor(QObject *parent = nullptr);
    ~ForesightPremierMonitor() override;

    Kind    kind() const override { return Kind::Premier; }
    QString sourceDescription() const override;

    void start() override;
    void stop() override;
    void setSourcePath(const QString &path) override;
    void setPollIntervalMs(int ms) override;

    // One poll, run directly. The timer calls this; tests call it themselves.
    void pollNow();

    // True while Premier has a range session open (a live snapshot exists).
    bool rangeSessionOpen() const { return !m_snapStamps.isEmpty(); }

    // Premier's data folder for the account PinPoint runs under. Reading another account's
    // profile is not a supported case (§4.1).
    static QString defaultFolder();

    static QString snapshotPattern() { return QStringLiteral("SessionLiveSnapshot.*.json"); }
    static QString queuePattern()    { return QStringLiteral("SessionUploadQueue.*.json"); }

    // Below this a "shot" is a ball knocked while setting up: Premier recorded a 2.1 mph,
    // 0° launch, −30° azimuth event as a real shot (§2.6.3). Never emitted.
    static constexpr double kGhostBallSpeedMph = 10.0;

signals:
    // A shot already emitted by readingAvailable, now final: the same deviceShotId with its
    // club data (or its club fields marked not measured). Replace, do not merge.
    void readingUpdated(const pinpoint::lm::LaunchMonitorReading &reading);

private:
    struct Stamp {
        qint64    size = -1;
        QDateTime mtime;
        bool operator==(const Stamp &o) const { return size == o.size && mtime == o.mtime; }
        bool operator!=(const Stamp &o) const { return !(*this == o); }
    };
    // An emitted shot that is not final yet.
    struct Pending {
        QString sessionId;
        qint64  firstReadAtMs = 0;
        QString sourcePath;
    };

    void prime();
    void resetBookkeeping();
    // Reads `path` if its stamp moved since `stamps` last saw it. Returns the parse when the
    // file was read and parsed; sets *formatChanged when the file parsed as JSON but no longer
    // has Premier's shape.
    std::optional<PremierFile> readIfChanged(const QString &path, QHash<QString, Stamp> &stamps,
                                             bool *formatChanged, QString *formatError);
    void processSnapshot(const PremierFile &file, const QString &path);
    void processQueue(const PremierFile &file);
    void emitFinal(LaunchMonitorReading reading, Pending p);
    void stamp(const QString &path, QHash<QString, Stamp> &stamps) const;
    void forgetVanished(QHash<QString, Stamp> &stamps, const QStringList &present) const;
    QStringList glob(const QString &pattern) const;

    QTimer  m_timer;
    QString m_dir;
    int     m_intervalMs = 250;
    bool    m_running    = false;
    bool    m_primed     = false;

    QHash<QString, Stamp> m_snapStamps;     // live snapshots present, by path
    QHash<QString, Stamp> m_queueStamps;    // upload queues present, by path
    QSet<QString>         m_seen;           // every LocalShotId observed: primed, gated, emitted
    QHash<QString, Pending> m_pending;      // emitted, not final yet
    QString               m_formatError;    // last schema failure, "" when the files read fine
};

} // namespace pinpoint::lm
