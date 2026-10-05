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

#include <QDebug>
#include <QElapsedTimer>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// ⚠ THE TEST BINARY MUST NOT BE ABLE TO MAKE A SOUND. The calibration flow plays a ting on every
// completed calibration, and this suite completes dozens of them for real: on 5 Oct 2026 that
// rang out of the Mac's speakers for as long as the tests ran. Turning the volume down from the
// harness was not enough, because it only reaches the players that exist when it looks.
//
// So the real src/Audio/ting_player.* is filtered OUT of this target's module sources
// (src/Gui/CMakeLists.txt) and this stand-in takes the QML name. It has the same properties and
// the same play(), opens no audio device, and counts the calls so a test can still assert that
// the flow asked for its ting exactly once.
class TingPlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double frequency MEMBER m_frequency)
    Q_PROPERTY(double volume MEMBER m_volume)
    Q_PROPERTY(int playCount READ playCount NOTIFY playCountChanged)

public:
    explicit TingPlayer(QObject *parent = nullptr) : QObject(parent) {}

    int playCount() const { return m_playCount; }

    Q_INVOKABLE void play()
    {
        ++m_playCount;
        // One line per ting asked for, with the process-wide count and time. A flow that flips
        // "done" off and on would ring repeatedly in the app; this is how a run shows it.
        static QElapsedTimer clock;
        static int total = 0;
        if (!clock.isValid()) clock.start();
        qInfo().noquote() << QStringLiteral("TING #%1 at %2 ms (this player: %3, %4 Hz)")
                                 .arg(++total).arg(clock.elapsed()).arg(m_playCount).arg(m_frequency);
        emit playCountChanged();
    }

signals:
    void playCountChanged();

private:
    double m_frequency = 1046.5;
    double m_volume    = 1.0;
    int    m_playCount = 0;
};
