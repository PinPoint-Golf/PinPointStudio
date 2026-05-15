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

#include <QObject>
#include <QVariantList>
#include <QList>

#include "device_enumerator.h"

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

public:
    explicit CameraManager(pinpoint::EventBuffer *buffer = nullptr,
                           QObject *parent = nullptr);
    ~CameraManager() override;

    QVariantList cameraList() const;
    QVariantList instances()  const;
    bool isRecording()        const;
    bool anySelected()        const;
    QString bufferState()     const;

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

private:
    struct CameraEntry {
        Device device;
        bool selected = false;
        VideoController *controller = nullptr;
    };

    QList<CameraEntry> m_cameras;
    bool               m_recording    = false;
    pinpoint::EventBuffer *m_eventBuffer = nullptr;

    VideoController *createController(const Device &device);
};
