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
#include <memory>

class VideoInputBase;

class VideoInputFactory : public QObject
{
    Q_OBJECT

public:
    enum class Backend {
        Auto,
        QtMultimedia,
        AppleAVFoundation,
        Aravis,
        Spinnaker
    };
    Q_ENUM(Backend)

    // Creates the most appropriate backend for the current platform and hardware.
    // If backend is Auto, it will:
    // 1. Try Aravis if HAVE_ARAVIS is defined and a camera is found.
    // 2. Try AppleAVFoundation on macOS.
    // 3. Fall back to QtMultimedia.
    static VideoInputBase* create(Backend backend = Backend::Auto, QObject *parent = nullptr);

    // Returns the backend type of the given input.
    static Backend backendType(VideoInputBase *input);

    // Returns a list of available backends on this system.
    static QList<Backend> availableBackends();

    // Discovers all cameras across all backends and registers them with
    // DeviceEnumerator. Safe to call multiple times (duplicates are suppressed).
    static void enumerateDevices();
};
