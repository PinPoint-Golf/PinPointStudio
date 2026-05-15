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

#include <QByteArray>
#include <QObject>

class QAudioSink;
class QBuffer;

// Plays a short synthesised ting tone on the default audio output.
// The tone is pre-rendered at construction time; play() is safe to call
// from the main thread at any time.  If already playing, the call is ignored.
class TingPlayer : public QObject
{
    Q_OBJECT

public:
    explicit TingPlayer(QObject *parent = nullptr);
    ~TingPlayer() override;

    void play();

private:
    static QByteArray synthesize();

    const QByteArray m_pcm;
    QAudioSink      *m_sink = nullptr;
    QBuffer         *m_buf  = nullptr;
};
