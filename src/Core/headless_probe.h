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

// PINPOINT_HEADLESS_PROBE=1 — THE HEADLESS START FOR PROBES (refactor 4-c; Mark, 22 Nov 2026: "it will be useful for
// the future"). Every agent probe of the app uses it.
//
// WHY. A plain launch — offscreen included — starts the device side at once: the IMU manager enumerates the Bluetooth
// adapters and scans, the transcription controller asks for Speech Recognition and the Microphone, the TTS controller
// opens the speakers and fetches its voice, the camera manager lists the cameras, the launch monitor opens its
// connector, the phone link listens and advertises, and the updater may check. On macOS the first three raise
// permission prompts on whoever's screen the session is on. A probe needs none of it.
//
// WHAT IT DOES, with the variable set to 1: each of those parts is not started, and says so once in the app log
// (`[HeadlessProbe] not started: <part>`, the test's evidence: headless_probe_test). The controllers QML binds to are
// still made, inert — no adapter, no thread, no device, no connector — so every screen loads and reads empty device
// lists. Below them, the calls that would reach the operating system's device layer (the macOS permission requests,
// the Bluetooth adapter list and scan) refuse under the switch and log `[HeadlessProbe] REFUSED <call>`: a backstop, so
// a path the switch missed raises no prompt either, and the test fails on that line.
//
// Only the exact value "1" turns it on; unset, or any other value ("01", "0x1", "true" included), it changes nothing. A
// golfer never sets it. Read from the environment on every call: the parts ask once, at their start.

#include <QByteArray>
#include <QtGlobal>

namespace pinpoint {

inline bool headlessProbe()
{
    return qgetenv("PINPOINT_HEADLESS_PROBE") == QByteArrayLiteral("1");
}

} // namespace pinpoint
