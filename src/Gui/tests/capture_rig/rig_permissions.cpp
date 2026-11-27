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

// THE CAPTURE TEST RIG ASKS macOS FOR NO PERMISSION. A camera instance asks for the camera's before it starts its
// back-end (CameraInstance::startRecording), and under the headless switch the app's own src/Gui/app/macos_permissions.mm
// refuses every request, which would leave the stand-in camera unstarted. The rig compiles the app's sources, so
// cmake/PinPointCaptureRig.cmake leaves that file out and this one defines the same three functions, from the same
// header, touching no permission store: the camera is granted, because the only camera the rig can list is the
// stand-in (no real one is enumerated under the headless switch); the microphone and speech, which the rig never
// uses, are refused. macOS only, as the file it replaces.

#include "macos_permissions.h"

void requestMicrophonePermission(std::function<void(bool granted)> callback) { callback(false); }

void requestSpeechRecognitionPermission(std::function<void(bool granted)> callback) { callback(false); }

void requestCameraPermission(std::function<void(bool granted)> callback) { callback(true); }
