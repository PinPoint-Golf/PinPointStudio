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

// ⚠ THE CAPTURE TEST RIG MUST NOT BE ABLE TO MAKE A SOUND. Every camera instance holds a TingPlayer, which plays when
// a ball is seen to arrive. The rig compiles the app's sources, so cmake/PinPointCaptureRig.cmake leaves the real
// src/Audio/ting_player.cpp out and this file defines the same class, from the same header, with no audio device:
// play() only counts (src/Gui/tests/setup/silent_ting_player.h is the QML suite's equivalent).

#include "ting_player.h"

#include <QBuffer>
#include <algorithm>
#include <atomic>

namespace capture_rig { std::atomic_int g_tingPlays{0}; }

TingPlayer::TingPlayer(QObject *parent) : QObject(parent) {}

TingPlayer::~TingPlayer() = default;

void TingPlayer::play() { capture_rig::g_tingPlays.fetch_add(1); }

void TingPlayer::setVolume(double v) { m_volume = std::clamp(v, 0.0, 1.0); }

void TingPlayer::setFrequency(double hz) { m_frequency = hz; }

QByteArray TingPlayer::synthesize(double) { return {}; }
