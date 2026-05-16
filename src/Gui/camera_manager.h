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

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <optional>
#include <vector>

#include "device_enumerator.h"
#include "swing_window.h"

namespace pinpoint { class EventBuffer; }
class VideoController;

class CameraManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList cameraList  READ cameraList  NOTIFY cameraListChanged)
    Q_PROPERTY(QVariantList instances   READ instances   NOTIFY instancesChanged)
    Q_PROPERTY(bool isRecording         READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool anySelected         READ anySelected NOTIFY instancesChanged)
    Q_PROPERTY(QString bufferState      READ bufferState NOTIFY bufferStateChanged)
    Q_PROPERTY(bool isReplaying         READ isReplaying NOTIFY isReplayingChanged)

public:
    explicit CameraManager(pinpoint::EventBuffer *buffer = nullptr,
                           QObject *parent = nullptr);
    ~CameraManager() override;

    QVariantList cameraList() const;
    QVariantList instances()  const;
    bool isRecording()        const;
    bool anySelected()        const;
    QString bufferState()     const;
    bool isReplaying()        const;

    Q_INVOKABLE void setSelected(int index, bool selected);
    Q_INVOKABLE void startAll();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void pauseBuffer();
    Q_INVOKABLE void resumeBuffer();

    // Sets the perspective on one camera and clears it from any other camera
    // that currently has the same non-zero perspective value.
    Q_INVOKABLE void setPerspective(QObject *controller, int perspective);

signals:
    void cameraListChanged();
    void instancesChanged();
    void isRecordingChanged();
    void bufferStateChanged();
    void isReplayingChanged();

private:
    struct CameraEntry {
        Device device;
        bool selected = false;
        VideoController *controller = nullptr;
    };

    struct ReplayTrack {
        VideoController                   *ctrl      = nullptr;
        pinpoint::SourceId                 sourceId  = pinpoint::kInvalidSourceId;
        std::vector<pinpoint::IndexEntry>  entries;
        size_t                             idx       = 0;
    };

    QList<CameraEntry>                   m_cameras;
    bool                                 m_recording        = false;
    int                                  m_ballPresentCount = 0;
    pinpoint::EventBuffer               *m_eventBuffer      = nullptr;
    bool                                 m_replaying        = false;
    std::optional<pinpoint::SwingWindow> m_swingWindow;
    int64_t                              m_replayWindowStartUs = 0;
    int64_t                              m_replayWindowEndUs   = 0;
    QElapsedTimer                        m_replayElapsed;
    QTimer                              *m_replayTimer      = nullptr;
    std::vector<ReplayTrack>             m_replayTracks;

    VideoController *createController(const Device &device);
    void startReplay();
    void stopReplay(bool autoResume = true);

private slots:
    void onCameraBallPresenceChanged(bool present);
    void onReplayTick();
};
