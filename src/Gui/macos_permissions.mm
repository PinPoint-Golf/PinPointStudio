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

#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
#include "macos_permissions.h"

void requestMicrophonePermission(std::function<void(bool granted)> callback)
{
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];

    if (status == AVAuthorizationStatusAuthorized) {
        callback(true);
    } else if (status == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                               completionHandler:^(BOOL granted) {
            callback(granted);
        }];
    } else {
        // AVAuthorizationStatusDenied or AVAuthorizationStatusRestricted
        callback(false);
    }
}

void requestCameraPermission(std::function<void(bool granted)> callback)
{
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    if (status == AVAuthorizationStatusAuthorized) {
        callback(true);
    } else if (status == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                               completionHandler:^(BOOL granted) {
            callback(granted);
        }];
    } else {
        callback(false);
    }
}

void requestSpeechRecognitionPermission(std::function<void(bool granted)> callback)
{
    SFSpeechRecognizerAuthorizationStatus status = [SFSpeechRecognizer authorizationStatus];

    if (status == SFSpeechRecognizerAuthorizationStatusAuthorized) {
        callback(true);
    } else if (status == SFSpeechRecognizerAuthorizationStatusNotDetermined) {
        // The completion block always fires on the main thread.
        [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus s) {
            callback(s == SFSpeechRecognizerAuthorizationStatusAuthorized);
        }];
    } else {
        // Denied or restricted — callback immediately so callers can act.
        callback(false);
    }
}
