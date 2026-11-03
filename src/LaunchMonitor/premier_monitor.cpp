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

#include "premier_monitor.h"

#include "pp_debug.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace pinpoint::lm {

ForesightPremierMonitor::ForesightPremierMonitor(QObject *parent) : LaunchMonitorBase(parent)
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &ForesightPremierMonitor::pollNow);
}

ForesightPremierMonitor::~ForesightPremierMonitor() = default;

QString ForesightPremierMonitor::defaultFolder()
{
    // Unity's persistentDataPath for Premier. LocalLow has no QStandardPaths location of its
    // own; it sits beside Local and Roaming under the profile.
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (home.isEmpty())
        return QString();
    return QDir(home).filePath(QStringLiteral("AppData/LocalLow/Foresight Sports/Foresight Premier"));
}

QString ForesightPremierMonitor::sourceDescription() const
{
    if (m_dir.isEmpty())
        return QString();
    if (!m_snapStamps.isEmpty())
        return m_snapStamps.constBegin().key();
    return QDir(m_dir).filePath(snapshotPattern());
}

void ForesightPremierMonitor::setSourcePath(const QString &path)
{
    if (m_dir == path)
        return;
    m_dir = path;
    // A new folder is a new baseline.
    resetBookkeeping();
    if (m_running)
        start();
}

void ForesightPremierMonitor::setPollIntervalMs(int ms)
{
    m_intervalMs = qBound(50, ms, 10'000);
    if (m_timer.isActive())
        m_timer.start(m_intervalMs);
}

void ForesightPremierMonitor::resetBookkeeping()
{
    m_primed = false;
    m_snapStamps.clear();
    m_queueStamps.clear();
    m_seen.clear();
    m_pending.clear();
    m_formatError.clear();
    m_historyStamp = Stamp{};
    m_history.clear();
    m_historyError.clear();
    m_carryWaits.clear();
    m_carryById.clear();
}

void ForesightPremierMonitor::start()
{
    m_running = true;

    if (m_dir.isEmpty()) {
        m_timer.stop();
        setState(State::Disabled);
        return;
    }
    if (!QDir(m_dir).exists()) {
        m_timer.stop();
        setState(State::Error, tr("Folder not found: %1").arg(m_dir));
        return;
    }

    if (!m_primed)
        prime();
    m_timer.start(m_intervalMs);
}

void ForesightPremierMonitor::stop()
{
    m_running = false;
    m_timer.stop();
    setState(State::Disabled);
}

QStringList ForesightPremierMonitor::glob(const QString &pattern) const
{
    QStringList out;
    const QDir dir(m_dir);
    const QStringList names = dir.entryList({ pattern }, QDir::Files);
    for (const QString &n : names)
        out << dir.filePath(n);
    return out;
}

void ForesightPremierMonitor::stamp(const QString &path, QHash<QString, Stamp> &stamps) const
{
    const QFileInfo fi(path);
    stamps.insert(path, Stamp{ fi.size(), fi.lastModified() });
}

void ForesightPremierMonitor::forgetVanished(QHash<QString, Stamp> &stamps,
                                             const QStringList &present) const
{
    for (auto it = stamps.begin(); it != stamps.end();) {
        if (!present.contains(it.key()))
            it = stamps.erase(it);
        else
            ++it;
    }
}

std::optional<PremierFile> ForesightPremierMonitor::readIfChanged(const QString &path,
                                                                  QHash<QString, Stamp> &stamps,
                                                                  bool *formatChanged,
                                                                  QString *formatError)
{
    *formatChanged = false;
    const QFileInfo fi(path);
    const Stamp now{ fi.size(), fi.lastModified() };
    const auto it = stamps.constFind(path);
    if (it != stamps.constEnd() && *it == now)
        return std::nullopt;                    // unchanged: do not open it

    QByteArray bytes;
    {
        QFile f(path);
        // Premier holds its own handle only while it writes; a failed open is that moment.
        if (!f.open(QIODevice::ReadOnly))
            return std::nullopt;                // stamp untouched: try again next tick
        bytes = f.readAll();
    }                                           // closed here, before any parsing

    QString err;
    bool changed = false;
    auto parsed = parsePremierSessionFile(bytes, &err, &changed);
    if (!parsed) {
        if (changed) {
            // A whole, valid file in a shape we do not know. Re-reading it every tick will not
            // change that, so take the stamp and wait for Premier to write again.
            stamps.insert(path, now);
            *formatChanged = true;
            *formatError   = err;
        }
        // Otherwise a torn read: stamp untouched, so the next tick reads it again.
        return std::nullopt;
    }
    stamps.insert(path, now);
    return parsed;
}

void ForesightPremierMonitor::prime()
{
    // The base-class rule: nothing on disk at start is ours. Every shot already in a live
    // snapshot is marked seen without being emitted. The queues are only stat'ed — they are
    // never a source of new shots, and opening one costs Premier more than it costs us.
    const QStringList snaps = glob(snapshotPattern());
    for (const QString &p : snaps) {
        bool changed = false;
        QString err;
        if (const auto f = readIfChanged(p, m_snapStamps, &changed, &err)) {
            for (const PremierShot &s : f->shots)
                m_seen.insert(s.localShotId);
        } else if (changed) {
            m_formatError = err;
        }
    }
    for (const QString &p : glob(queuePattern()))
        stamp(p, m_queueStamps);

    m_primed = true;
    if (!m_formatError.isEmpty())
        setState(State::Error, tr("Foresight Premier's file format has changed: %1").arg(m_formatError));
    else
        setState(m_snapStamps.isEmpty() ? State::Waiting : State::Ready, QString());
    ppInfo() << "LaunchMonitor: Premier primed in" << m_dir << "—" << snaps.size()
             << "live snapshot(s)," << m_seen.size() << "shot(s) already on disk, not claimed";
}

void ForesightPremierMonitor::pollNow()
{
    if (!m_running || m_dir.isEmpty())
        return;
    if (!QDir(m_dir).exists()) {
        setState(State::Error, tr("Folder not found: %1").arg(m_dir));
        return;
    }
    if (!m_primed) {
        prime();
        return;
    }

    bool sawFormatChange = false;
    QString formatError;
    bool readAny = false;

    // ── The live snapshot ────────────────────────────────────────────────────
    const QStringList snaps = glob(snapshotPattern());
    for (const QString &p : snaps) {
        bool changed = false;
        QString err;
        if (const auto f = readIfChanged(p, m_snapStamps, &changed, &err)) {
            readAny = true;
            processSnapshot(*f, p);
        } else if (changed) {
            sawFormatChange = true;
            formatError = err;
        }
    }
    const bool hadSession = !m_snapStamps.isEmpty();
    forgetVanished(m_snapStamps, snaps);
    if (hadSession && m_snapStamps.isEmpty())
        ppInfo() << "LaunchMonitor: Premier's range session ended (live snapshot gone);"
                 << m_pending.size() << "shot(s) waiting on the upload queue for club data";

    // ── The upload queue, only while a shot waits on it ──────────────────────
    const QStringList queues = glob(queuePattern());
    if (!m_pending.isEmpty()) {
        for (const QString &p : queues) {
            bool changed = false;
            QString err;
            if (const auto f = readIfChanged(p, m_queueStamps, &changed, &err)) {
                readAny = true;
                processQueue(*f);
            } else if (changed) {
                sawFormatChange = true;
                formatError = err;
            }
        }
    } else {
        // Keep the stamps current without opening anything, so a later wait starts from the
        // queue as it is now rather than re-reading every rewrite it went through meanwhile.
        for (const QString &p : queues)
            stamp(p, m_queueStamps);
    }
    forgetVanished(m_queueStamps, queues);

    // ── history.json, only while a carry is owed ─────────────────────────────
    serviceCarryWaits();

    if (sawFormatChange) {
        m_formatError = formatError;
        setState(State::Error, tr("Foresight Premier's file format has changed: %1").arg(formatError));
        return;
    }
    if (readAny)
        m_formatError.clear();
    if (m_formatError.isEmpty())
        setState(m_snapStamps.isEmpty() ? State::Waiting : State::Ready, QString());
}

void ForesightPremierMonitor::processSnapshot(const PremierFile &file, const QString &path)
{
    // The newest shot of each session is the one whose club data has not landed yet.
    QHash<QString, qint64> newest;
    for (const PremierShot &s : file.shots)
        if (s.createdUtcMs > newest.value(s.sessionId, 0))
            newest.insert(s.sessionId, s.createdUtcMs);

    QSet<QString> present, sessions;
    for (const PremierShot &s : file.shots) {
        present.insert(s.localShotId);
        sessions.insert(s.sessionId);
        const bool isNewest = s.createdUtcMs >= newest.value(s.sessionId, 0);

        if (s.hidden) {
            // Never seen true (deletion removes the shot, §2.6.4); honoured if a build uses it.
            m_seen.insert(s.localShotId);
            m_pending.remove(s.localShotId);
            continue;
        }

        if (!m_seen.contains(s.localShotId)) {
            m_seen.insert(s.localShotId);
            const double mph = s.reading.ballSpeed.value_or(0.0);
            if (mph < kGhostBallSpeedMph) {
                ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId << "ignored — ball speed"
                         << mph << "mph is a knock, not a shot";
                continue;
            }

            LaunchMonitorReading out = s.reading;
            out.deviceKind = kindKey(kind());
            out.sourcePath = path;
            out.readAtMs   = QDateTime::currentMSecsSinceEpoch();

            if (s.hasClubData) {
                ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId << "read with its club data";
                attachCarryOrWait(out);
                emit readingAvailable(out);
                continue;
            }
            if (!isNewest) {
                // Two shots landed in one write we saw: the older one is already final.
                addClubNotMeasuredDrops(out);
                ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId
                         << "read after its club data was due — club not measured";
                attachCarryOrWait(out);
                emit readingAvailable(out);
                continue;
            }
            ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId
                     << "read, ball only — its club data comes with the next shot";
            m_pending.insert(s.localShotId, Pending{ s.sessionId, out.readAtMs, path, out });
            emit readingAvailable(out);
            continue;
        }

        const auto it = m_pending.constFind(s.localShotId);
        if (it == m_pending.constEnd())
            continue;                       // primed, gated or already final
        if (s.hasClubData) {
            emitFinal(s.reading, *it);
        } else if (!isNewest) {
            LaunchMonitorReading r = s.reading;
            addClubNotMeasuredDrops(r);
            ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId
                     << "has no club data a shot later — club not measured (no stickers?)";
            emitFinal(r, *it);
        }
    }

    // A pending shot gone from its own session's snapshot was deleted in Premier (a session
    // that has ended is no longer in the file at all, so its shots wait on the queue instead). We never
    // retract a reading already written; it simply stays ball-only.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (sessions.contains(it->sessionId) && !present.contains(it.key())) {
            ppInfo() << "LaunchMonitor: Premier shot" << it.key()
                     << "was deleted in Premier after it was read; its reading stays as written";
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

void ForesightPremierMonitor::processQueue(const PremierFile &file)
{
    for (const PremierShot &s : file.shots) {
        // Never a source of new shots: mark seen so a later snapshot cannot resurrect one.
        if (!m_seen.contains(s.localShotId)) {
            m_seen.insert(s.localShotId);
            continue;
        }
        const auto it = m_pending.constFind(s.localShotId);
        if (it == m_pending.constEnd())
            continue;
        if (s.hasClubData) {
            ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId
                     << "got its club data from the session-end flush";
            emitFinal(s.reading, *it);
        } else {
            // Either Premier was quit mid-session (the quit flush drops the last shot's club
            // data, §2.6.1) or the club had no stickers. The file cannot say which, so neither
            // is claimed: the reading stays ball-only, as first written.
            ppInfo() << "LaunchMonitor: Premier shot" << s.localShotId
                     << "reached the upload queue without club data (Premier quit mid-session, or"
                     << "no stickers); it stays ball-only";
            // Final now, ball-only. Its carry is still owed: sent alone as an update when found.
            LaunchMonitorReading r = it->reading;
            m_pending.remove(s.localShotId);
            if (attachCarryOrWait(r))
                emit readingUpdated(r);
        }
    }
}

void ForesightPremierMonitor::emitFinal(LaunchMonitorReading reading, Pending p)
{
    reading.deviceKind = kindKey(kind());
    reading.sourcePath = p.sourcePath;
    reading.readAtMs   = p.firstReadAtMs;     // when we first read the shot, not the update
    const QString id = reading.deviceShotId;
    m_pending.remove(id);
    attachCarryOrWait(reading);
    emit readingUpdated(reading);
}

// ── Carry, from ShotReplay/history.json (issue #148) ──────────────────────────

bool ForesightPremierMonitor::refreshHistory()
{
    const QString path = QDir(m_dir).filePath(historyPath());
    const QFileInfo fi(path);
    if (!fi.exists()) {
        m_history.clear();
        m_historyStamp = Stamp{};
        return false;
    }
    const Stamp now{ fi.size(), fi.lastModified() };
    if (now == m_historyStamp)
        return false;

    QByteArray bytes;
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;                       // Premier mid-write: next tick
        bytes = f.readAll();
    }
    QString err;
    bool changed = false;
    auto parsed = parsePremierHistoryFile(bytes, &err, &changed);
    if (!parsed) {
        if (changed) {
            // Carry is extra, never a reason to stop reading shots: log it, take the stamp.
            m_historyStamp = now;
            m_history.clear();
            if (err != m_historyError)
                ppWarn() << "LaunchMonitor: Premier's history.json has changed format, so no carry:" << err;
            m_historyError = err;
        }
        return false;                           // torn: stamp untouched, read again
    }
    m_historyStamp = now;
    m_history = std::move(*parsed);
    m_historyError.clear();
    return true;
}

std::optional<double> ForesightPremierMonitor::lookUpCarry(const LaunchMonitorReading &r)
{
    if (const auto it = m_carryById.constFind(r.deviceShotId); it != m_carryById.constEnd())
        return *it;
    if (!r.contactAtMs || !r.ballSpeed)
        return std::nullopt;
    return premierCarryFor(m_history, *r.contactAtMs + kPremierCreatedAfterContactMs, *r.ballSpeed);
}

bool ForesightPremierMonitor::attachCarryOrWait(LaunchMonitorReading &reading)
{
    if (reading.carryDistance)
        return true;
    if (!m_carryById.contains(reading.deviceShotId))
        refreshHistory();
    if (const auto c = lookUpCarry(reading)) {
        reading.carryDistance = *c;
        m_carryById.remove(reading.deviceShotId);
        m_carryWaits.remove(reading.deviceShotId);
        return true;
    }
    m_carryWaits.insert(reading.deviceShotId, CarryWait{ reading, m_clock() + kCarryRetryMs, false });
    return false;
}

void ForesightPremierMonitor::serviceCarryWaits()
{
    if (m_carryWaits.isEmpty())
        return;
    const bool fresh = refreshHistory();
    const qint64 now = m_clock();
    QList<LaunchMonitorReading> found;
    for (auto it = m_carryWaits.begin(); it != m_carryWaits.end();) {
        const bool due = !it->retried && now >= it->retryAtMs;
        if (!fresh && !due) {
            ++it;
            continue;
        }
        if (const auto c = lookUpCarry(it->reading)) {
            LaunchMonitorReading r = it->reading;
            r.carryDistance = *c;
            found.append(r);
            it = m_carryWaits.erase(it);
            continue;
        }
        if (due) {
            it->retried = true;
            ppInfo() << "LaunchMonitor: Premier shot" << it.key()
                     << "has no carry in history.json a second after it went final;"
                     << "left for the end-of-session sweep";
        }
        ++it;
    }
    for (const LaunchMonitorReading &r : found) {
        ppInfo() << "LaunchMonitor: Premier shot" << r.deviceShotId << "carry" << *r.carryDistance
                 << "yd from history.json";
        emit readingUpdated(r);
    }
}

void ForesightPremierMonitor::sweepCarry()
{
    if (m_dir.isEmpty() || (m_carryWaits.isEmpty() && m_pending.isEmpty()))
        return;
    refreshHistory();

    int filled = 0, missing = 0;
    QList<LaunchMonitorReading> out;

    // Final readings still owed their carry.
    for (auto it = m_carryWaits.cbegin(); it != m_carryWaits.cend(); ++it) {
        if (const auto c = lookUpCarry(it->reading)) {
            LaunchMonitorReading r = it->reading;
            r.carryDistance = *c;
            out.append(r);
            ++filled;
        } else {
            ppInfo() << "LaunchMonitor: Premier shot" << it.key()
                     << "has no record in history.json at session end; it stays without carry";
            ++missing;
        }
    }
    m_carryWaits.clear();

    // Shots still waiting for their club data: the carry goes out now with the ball data, and
    // is remembered so the final reading carries it too.
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->reading.carryDistance)
            continue;
        if (const auto c = lookUpCarry(it->reading)) {
            it->reading.carryDistance = *c;
            m_carryById.insert(it.key(), *c);
            out.append(it->reading);
            ++filled;
        } else {
            ppInfo() << "LaunchMonitor: Premier shot" << it.key()
                     << "(waiting for its club data) has no record in history.json at session end";
            ++missing;
        }
    }

    ppInfo() << "LaunchMonitor: Premier end-of-session carry sweep:" << filled << "filled,"
             << missing << "without a record in history.json";
    for (const LaunchMonitorReading &r : out)
        emit readingUpdated(r);
}

} // namespace pinpoint::lm
