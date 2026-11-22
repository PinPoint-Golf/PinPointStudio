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

#include "reanalysis_controller.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>

#include "../Analysis/reanalyse_and_persist.h"   // the one write-back
#include "../../App/shot_command_door.h"           // the Shot's read and write-back (refactor step 3.4b-ii)
#include "../Core/pp_debug.h"
#include "../Core/pp_os_metrics.h"

ReanalysisController::ReanalysisController(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcherBase::finished,
            this, &ReanalysisController::onWorkerFinished);
}

void ReanalysisController::reanalyse(const QVariantList &swingDirs)
{
    if (swingDirs.isEmpty()) {
        ppInfo() << "[Reanalysis] reanalyse() called with no shots — ignored";
        return;
    }

    // The caller passed already-resolved on-disk swing dirs (analysis-only shots
    // with no folder are dropped by swingDirsForIds). De-dupe against what's
    // already queued / in flight.
    QStringList dirs;
    for (const QVariant &v : swingDirs) {
        const QString d = v.toString();
        if (!d.isEmpty() && d != m_current && !m_queue.contains(d) && !dirs.contains(d))
            dirs << d;
    }
    if (dirs.isEmpty()) {
        ppInfo() << "[Reanalysis] nothing to re-analyse (no on-disk swings)";
        return;
    }

    m_queue += dirs;
    ppInfo() << "[Reanalysis] queued" << dirs.size() << "swing(s);" << m_queue.size() << "pending";
    emit reanalyseQueued(dirs.size());

    if (!m_reanalysing) {
        m_succeeded = 0;
        m_failed    = 0;
        m_lastError.clear();
        startNext();
    }
}

pinpoint::analysis::AnalysisWrite ReanalysisController::writeBackOf(const QString &dir, const pinpoint::ShotRef &shot) const
{
    // The Shot's own write-back (Shot::applyAnalysis, EVT-02), reached through `app` — the Session that holds it, the
    // golfer's whoever is current now (J-67) or the Unassigned owner's (§5.11). The door is asked again at the write, on
    // this thread: a shot trashed or a session let go mid-batch is not reached, and nothing is written (step 5.1: no
    // write of the controller's own; N1 is gone).
    pinpoint::ShotCommandDoor *door = m_commands;
    return [door, dir, shot](const pinpoint::AnalysisOutcome &outcome, const pinpoint::PersistOptions &options) {
        if (door) {
            const pinpoint::ForwardResult f =
                door->applyAnalysis(pinpoint::sessionFolderOf(dir), shot, outcome, options);
            if (f.reached)
                return f.result;
        }
        return pinpoint::WriteResult{ pinpoint::WriteResult::Status::Failed,
                                      QStringLiteral("no session holds the shot now (trashed, or its session let go)") };
    };
}

bool ReanalysisController::refreshIfDeviceOnly(const QString &dir)
{
    if (!m_commands)
        return false;
    // The edge (DEC-90): the queued folder to its ShotRef, on this (the owner) thread, through the Session that holds it.
    const pinpoint::ShotRef shot = m_commands->shotOfFolder(dir);
    // LAUNCH MONITOR ONLY is the DOCUMENT's classification (RecordingRepository::isDeviceOnly: the recorded kind,
    // else the content rule, R-REC-8), as before — not the Shot's kind: a camera shot whose export failed and whose
    // folder the launch monitor then filled (the rescue) is device-only by its document while its Shot was allocated
    // as captured (53a3bb0a's case; review 1, CN-1). Its rows are then derived again, through its Shot if held.
    const std::optional<bool> deviceOnly = m_commands->isDeviceOnly(pinpoint::sessionFolderOf(dir), shot);
    if (!deviceOnly || !*deviceOnly)
        return false;   // a camera shot, or one no Session holds (the worker's turn then says so)
    const pinpoint::analysis::PersistedReanalysis p =
        pinpoint::analysis::persistReanalysis(writeBackOf(dir, shot), nullptr, persistOptions());
    if (p.ok()) {
        ++m_succeeded;
        emit reanalysed(dir);
        ppInfo() << "[Reanalysis] launch monitor only:" << dir
                 << (p.write.changed() ? "— its readings' rows rebuilt" : "— its readings' rows were current");
    } else {
        ++m_failed;
        m_lastError = QStringLiteral("Couldn't update this shot's launch monitor readings: %1").arg(p.write.error);
        ppError() << "[Reanalysis] launch monitor refresh failed for" << dir << ":" << p.write.error;
    }
    return true;
}

void ReanalysisController::startNext()
{
    // Launch-monitor-only swings first, in queue order: no pose, no worker, a few KB each.
    while (!m_queue.isEmpty() && !m_liveBusy && refreshIfDeviceOnly(m_queue.first()))
        m_queue.removeFirst();

    if (m_queue.isEmpty()) {
        m_current.clear();
        m_startDeferred = false;
        setReanalysing(false);
        ppInfo() << "[Reanalysis] batch done —" << m_succeeded << "ok," << m_failed << "failed";
        emit reanalyseFinished(m_succeeded, m_failed, m_lastError);
        return;
    }

    // Hold the batch BETWEEN swings while a live shot is processing — never start a
    // second ViTPose alongside the live one. setLiveBusy(false) resumes it.
    if (m_liveBusy) {
        m_startDeferred = true;
        setReanalysing(true);   // batch is still in progress, just paused
        ppInfo() << "[Reanalysis] deferring next swing — live shot processing";
        return;
    }

    setReanalysing(true);
    m_current = m_queue.takeFirst();
    const QString dir = m_current;
    // One swing at a time: the analyzer's ViTPose pass runs at physical-core thread
    // count and streams a window from disk — sequential keeps memory/CPU bounded.
    // fullWindow stays OFF: it existed to compensate for the grip-speed bs0 lag
    // (span started mid-takeaway, so bounded re-analysis missed real swing) —
    // onset v2 fixed that at source, so re-analysis follows the same span-bounded
    // two-pass path as a live shot. ReanalyzeOptions::fullWindow remains as an
    // escape hatch for SwingLab/debugging only.
    // The edge (DEC-90): translated here, on the owner thread; the worker reads by the ShotRef, whose
    // folder the session repository resolves from any thread.
    m_currentShot = m_commands ? m_commands->shotOfFolder(dir) : pinpoint::ShotRef();
    // HOW THE WORKER READS IT (04 step 3 item 8, "they ask the Shot"): the Shot's RecordingSource, through the Session
    // that holds it. A shot no Session holds is not read (step 5.1: no reader of the controller's own; N1 is gone).
    const std::optional<pinpoint::RecordingSource> held =
        m_commands ? m_commands->recordingSourceOf(pinpoint::sessionFolderOf(dir), m_currentShot) : std::nullopt;
    const pinpoint::RecordingSource source = held ? *held : pinpoint::RecordingSource(m_currentShot, {});
    const bool notHeld = !held;
    m_watcher.setFuture(QtConcurrent::run([source, notHeld] {
        pinpoint::osmetrics::ThreadScope _tscope("Analysis.Worker");
        if (notHeld) {
            pinpoint::analysis::ReanalyzeResult none;
            none.error = QStringLiteral("This shot couldn't be re-analysed: no golfer's session holds it.");
            return none;
        }
        return pinpoint::analysis::reanalyzeShot(source, pinpoint::analysis::ReanalyzeOptions{});
    }));
}

void ReanalysisController::onWorkerFinished()
{
    const pinpoint::analysis::ReanalyzeResult r = m_watcher.result();
    const QString dir = m_current;
    const pinpoint::ShotRef shot = m_currentShot;
    m_current.clear();
    m_currentShot = {};

    if (!(r.ok && r.analysis.detail)) {
        ++m_failed;
        m_lastError = r.error.isEmpty()
                          ? QStringLiteral("This shot couldn't be re-analysed.")
                          : r.error;
        ppWarn() << "[Reanalysis] failed for" << dir << ":" << m_lastError;
        startNext();
        return;
    }
    // The one write-back, here on the GUI thread as always: the repository reads the document
    // fresh (preserving capture / streams / review), sets both integrity verdicts this pass reached
    // (a verdict it could not reach is withdrawn — carrying the capture-time block forward is what
    // made a false ⚠ survive every re-analysis) and replaces "analysis", honouring Save pose
    // keypoints. An empty read (the folder was trashed mid-batch, a transient failure) writes
    // nothing: writing then would replace the whole document with only {schema, analysis}.
    const pinpoint::analysis::PersistedReanalysis p =
        pinpoint::analysis::persistReanalysis(writeBackOf(dir, shot), &r, persistOptions());
    if (p.ok()) {
        ++m_succeeded;
        emit reanalysed(dir);   // host refreshes the row in the active model
        ppInfo() << "[Reanalysis] re-analysed" << dir << "— score" << p.score;
    } else if (p.write.status == pinpoint::WriteResult::Status::NoDocument) {
        ++m_failed;
        m_lastError = QStringLiteral("This shot's document could not be read, so it was left untouched.");
        ppWarn() << "[Reanalysis] write-back skipped —" << dir
                 << "document missing/unreadable (not overwriting)";
    } else {
        ++m_failed;
        m_lastError = QStringLiteral("Couldn't save the re-analysis: %1").arg(p.write.error);
        ppError() << "[Reanalysis] write-back failed for" << dir << ":" << p.write.error;
    }

    startNext();
}

void ReanalysisController::setLiveBusy(bool busy)
{
    if (m_liveBusy == busy)
        return;
    m_liveBusy = busy;
    // Live shot finished and a start was held off → resume the batch now.
    if (!busy && m_startDeferred && m_current.isEmpty()) {
        m_startDeferred = false;
        startNext();
    }
}

void ReanalysisController::setReanalysing(bool on)
{
    if (m_reanalysing == on)
        return;
    m_reanalysing = on;
    emit reanalysingChanged();
}
