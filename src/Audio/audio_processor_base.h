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
#include <QAudioFormat>

// Abstract base for classes that consume a stream of raw PCM audio frames.
//
// Subclasses override processAudio() and are connected to an AudioInputBase
// via AudioInputBase::connectProcessor(), or manually via:
//   connect(input, &AudioInputBase::audioDataReady,
//           processor, &AudioProcessorBase::processAudio);
//
// processAudio() is called with whatever buffer size the platform delivers;
// subclasses must not assume a fixed chunk size.

class AudioProcessorBase : public QObject
{
    Q_OBJECT

public:
    explicit AudioProcessorBase(QObject *parent = nullptr);
    ~AudioProcessorBase() override = default;

public slots:
    virtual void processAudio(const QByteArray &data, const QAudioFormat &format) = 0;
};
