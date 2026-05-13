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
#include <functional>

// Checks and, if needed, requests microphone access via AVFoundation.
// The callback is invoked with true if access is granted, false if denied.
// It may be called from any thread — dispatch to the main thread if needed.
void requestMicrophonePermission(std::function<void(bool granted)> callback);

// Checks and, if needed, requests speech recognition access via the Speech framework.
// The callback is invoked on the main thread with true if authorized, false otherwise.
// Call this before starting the STT pipeline so loadModel() finds a determined status.
void requestSpeechRecognitionPermission(std::function<void(bool granted)> callback);

// Checks and, if needed, requests camera access via AVFoundation.
// The callback is invoked with true if access is granted, false if denied.
// It may be called from any thread — dispatch to the main thread if needed.
void requestCameraPermission(std::function<void(bool granted)> callback);
