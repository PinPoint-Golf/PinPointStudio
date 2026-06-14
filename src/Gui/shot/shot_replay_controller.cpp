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

#include <algorithm>

ShotReplayController::ShotReplayController(QObject *parent)
    : QObject(parent)
    , m_source(makeDiskReplaySource())
{
    // Forward the source's transport/clock signals as our own; the QML-facing
    // change signals (activeChanged) are owned by the controller and emitted on
    // start()/stop()/abort.
    connect(m_source.get(), &ReplaySource::positionChanged, this, &ShotReplayController::positionChanged);
    connect(m_source.get(), &ReplaySource::spanChanged,     this, &ShotReplayController::spanChanged);
    connect(m_source.get(), &ReplaySource::playingChanged,  this, &ShotReplayController::playingChanged);
    connect(m_source.get(), &ReplaySource::speedChanged,    this, &ShotReplayController::speedChanged);
    connect(m_source.get(), &ReplaySource::failed,          this, &ShotReplayController::replayFailed);
    connect(m_source.get(), &ReplaySource::aborted,         this, &ShotReplayController::onAborted);
}

ShotReplayController::~ShotReplayController() = default;

QVariantList ShotReplayController::streams() const
{
    QVariantList out;
    for (const ReplayStreamInfo &s : m_source->streams())
        out.append(QVariantMap{
            { QStringLiteral("index"),       s.index },
            { QStringLiteral("perspective"), s.perspective },
            { QStringLiteral("aspect"),      s.aspect },
            { QStringLiteral("hasAnalysis"), s.hasAnalysis } });
    return out;
}

bool ShotReplayController::start(int shotId, const QString &swingDir, double speed)
{
    if (swingDir.isEmpty())
        return false;

    if (m_source->load(swingDir, std::clamp(speed, 0.1, 1.0))) {
        m_shotId   = shotId;
        m_swingDir = swingDir;
        m_active   = true;
        emit activeChanged();   // covers streams / analysisDetail / swingDir / shotId
        return true;
    }

    // Load failed. A valid doc with no playable stream leaves the source empty —
    // clear our active state so no zombie stage lingers. A bad path/doc leaves the
    // previous replay's streams intact (streamCount() > 0) — keep it running.
    if (m_active && m_source->streamCount() == 0) {
        m_active   = false;
        m_shotId   = -1;
        m_swingDir.clear();
        emit activeChanged();
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
    emit activeChanged();
}

void ShotReplayController::onAborted()
{
    // The source tore itself down (e.g. invalid master media). Mirror it.
    if (!m_active)
        return;
    m_active   = false;
    m_shotId   = -1;
    m_swingDir.clear();
    emit activeChanged();
}
