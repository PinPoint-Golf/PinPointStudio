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

#include <QString>
#include <QList>
#include <QObject>
#include "../Video/video_input_factory.h"

enum class DeviceType {
    VideoInput,
    AudioInput,
    AudioOutput
};

struct Device {
    DeviceType type;
    VideoInputFactory::Backend backend; // For non-video, we can use a generic "System" or "Qt" backend
    QString id;
    QString description;
};

class DeviceEnumerator : public QObject
{
    Q_OBJECT

public:
    static DeviceEnumerator* instance();

    void enumerate();
    QList<Device> devices() const;

    void registerDevice(DeviceType type, VideoInputFactory::Backend backend, const QString &id, const QString &description);

private:
    explicit DeviceEnumerator(QObject *parent = nullptr);
    
    QList<Device> m_devices;
    bool m_videoEnumerated = false;
    bool m_audioInputEnumerated = false;
    bool m_audioOutputEnumerated = false;
};
