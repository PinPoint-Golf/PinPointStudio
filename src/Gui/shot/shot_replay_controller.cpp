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

#include "shot_replay_controller.h"

#include "app_settings.h"
#include "../../Core/pp_debug.h"
#include "../charts/charts_work.h"            // ChartsLane — the one worker idiom (R-CON-5)
#include "../../DomainModel/shot.h"           // the followed Shot: its events and its RecordingSource
#include "../../Buffer/swing_window.h"        // a LoadedRecording owns one (RecordingSource::read)

#include <algorithm>
#include <any>

namespace {
// What a read of the Shot hands back to the GUI thread: the analysis block and its read origin (LoadedRecording's).
struct AnalysisRead {
    pinpoint::ShotRef shot;
    bool              ok = false;
    QString           error;
    QJsonObject       analysis;
    qint64            originUs = 0;
};
} // namespace

ShotReplayController::ShotReplayController(AppSettings *appSettings, QObject *parent)
    : ShotReplayController(makeDiskReplaySource(), appSettings, parent)
{
}

ShotReplayController::ShotReplayController(std::unique_ptr<ReplaySource> source, AppSettings *appSettings,
                                           QObject *parent)
    : QObject(parent)
    , m_appSettings(appSettings)
    , m_source(std::move(source))
{
    // Forward the source's transport/clock signals as our own; the QML-facing
    // change signals (activeChanged) are owned by the controller and emitted on
    // start()/stop()/abort.
    connect(m_source.get(), &ReplaySource::positionChanged, this, &ShotReplayController::positionChanged);
    connect(m_source.get(), &ReplaySource::spanChanged,     this, &ShotReplayController::spanChanged);
    connect(m_source.get(), &ReplaySource::playingChanged,  this, &ShotReplayController::playingChanged);
    connect(m_source.get(), &ReplaySource::impactLoopPlayingChanged,
            this, &ShotReplayController::impactLoopPlayingChanged);
    connect(m_source.get(), &ReplaySource::speedChanged,    this, &ShotReplayController::speedChanged);
    connect(m_source.get(), &ReplaySource::failed,          this, &ShotReplayController::replayFailed);
    connect(m_source.get(), &ReplaySource::aborted,         this, &ShotReplayController::onAborted);
    // Queued: the source emits this from inside QMediaPlayer's own mediaStatusChanged
    // callback, and the host's reaction (auto-return to Capture) tears the source down
    // via stop()/unload(). Deferring out of the player's callback avoids re-entrant
    // teardown — same reasoning as DiskReplaySource's InvalidMedia path.
    connect(m_source.get(), &ReplaySource::playbackEnded,   this, &ShotReplayController::playbackEnded,
            Qt::QueuedConnection);
}

ShotReplayController::~ShotReplayController()
{
    follow(nullptr);   // disconnects; the lane goes with this, and a read in flight finds no owner (charts_work.h)
}

pinpoint::Shot *ShotReplayController::followedShot() const
{
    return m_shot.data();
}

bool ShotReplayController::waitForAnalysisRead(int msTimeout)
{
    return !m_lane || m_lane->waitForDone(msTimeout);
}

void ShotReplayController::follow(pinpoint::Shot *shot)
{
    if (shot == m_shot.data() && (shot != nullptr) == bool(m_onReanalysed))
        return;
    QObject::disconnect(m_onReanalysed);
    QObject::disconnect(m_onCaptureCompleted);
    m_onReanalysed = m_onCaptureCompleted = {};
    m_shot = shot;   // a read still in flight for a shot let go is dropped when it comes back (rereadAnalysis)
    if (!shot)
        return;
    // Direct, as every listener of a domain event (work_pool.h): heard in command order, on the GUI thread.
    m_onReanalysed       = connect(shot, &pinpoint::Shot::reanalysed, this,
                                   [this](const pinpoint::ShotRef &) { rereadAnalysis(); }, Qt::DirectConnection);   // EVT-02
    m_onCaptureCompleted = connect(shot, &pinpoint::Shot::captureCompleted, this,
                                   [this](const pinpoint::ShotRef &) { rereadAnalysis(); }, Qt::DirectConnection);   // EVT-22
}

// The Shot's Recording, read on the lane (a worker): only its analysis block and read origin come back, and are
// applied only if the replay is still on that shot. A newer ask supersedes an older one (latest wins).
void ShotReplayController::rereadAnalysis()
{
    pinpoint::Shot *shot = m_shot.data();
    if (!m_active || !shot)
        return;
    if (!m_lane)
        m_lane = std::make_unique<pinpoint::charts::ChartsLane>(this, pinpoint::WorkPriority::Normal);
    const pinpoint::RecordingSource source = shot->recordingSource();   // a value: the ShotRef and its reader
    m_lane->ask(
        [source] {
            AnalysisRead r;
            r.shot = source.shot();
            const auto loaded = source.read();
            r.ok       = loaded.ok;
            r.error    = loaded.error;
            r.analysis = loaded.analysisIn;
            r.originUs = loaded.analysisOriginUs;
            return std::any(std::move(r));
        },
        [this](std::any &&v) {
            const AnalysisRead r = std::any_cast<AnalysisRead>(std::move(v));
            if (!m_active || !m_shot || !(m_shot->ref() == r.shot))
                return;   // the replay moved on
            if (!r.ok) {
                ppWarn() << "[ShotReplay] the shot" << r.shot.toString() << "could not be read again:" << r.error;
                return;
            }
            if (m_source->applyAnalysis(r.analysis, r.originUs)) {
                refreshAnalysisDetail();
                emit analysisDetailChanged();
            }
        });
}

bool ShotReplayController::start(int shotId, const QString &swingDir, double speed, bool autoPlay)
{
    if (swingDir.isEmpty())
        return false;

    const bool trim = m_appSettings && m_appSettings->replayTrimToSwing();
    if (m_source->load(swingDir, std::clamp(speed, 0.1, 1.0), trim)) {
        // The source always begins playing; hold it on the first frame instead. The seek
        // after the pause is what puts that frame up on the paused players.
        if (!autoPlay && m_source->playing()) {
            m_source->togglePlay();
            m_source->seekToUs(m_source->positionUs());
        }
        m_shotId   = shotId;
        m_swingDir = swingDir;
        m_active   = true;
        follow(m_resolveShot ? m_resolveShot(swingDir) : nullptr);
        refreshAnalysisDetail();
        emitActive();   // covers streams / analysisDetail / swingDir / shotId
        return true;
    }

    // Load failed. A valid doc with no playable stream leaves the source empty —
    // clear our active state so no zombie stage lingers. A bad path/doc leaves the
    // previous replay's streams intact (streamCount() > 0) — keep it running.
    if (m_active && m_source->streamCount() == 0) {
        m_active   = false;
        m_shotId   = -1;
        m_swingDir.clear();
        follow(nullptr);
        refreshAnalysisDetail();
        emitActive();
    }
    return false;
}

void ShotReplayController::stop()
{
    if (!m_active)
        return;
    m_source->unload();
    m_active   = false;
    m_shotId   = -1;
    m_swingDir.clear();
    follow(nullptr);
    refreshAnalysisDetail();
    emitActive();
}

void ShotReplayController::onAborted()
{
    // The source tore itself down (e.g. invalid master media). Mirror it.
    if (!m_active)
        return;
    m_active   = false;
    m_shotId   = -1;
    m_swingDir.clear();
    follow(nullptr);
    refreshAnalysisDetail();
    emitActive();
}
