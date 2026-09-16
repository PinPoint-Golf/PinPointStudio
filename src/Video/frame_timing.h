/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#pragma once

#include <QMetaType>

// When a frame was EXPOSED, travelling with the frame itself.
//
// ⚠ IT TRAVELS WITH THE FRAME, and that is the point (2026-09-15). The exposure side channel
// (VideoInputBase::lastMeasuredExposureUs) is read after a QueuedConnection hop, so at 592 fps it can
// already belong to a later frame; a "last instant" getter has the same race, and a capture instant that
// belongs to the wrong frame is worse than none. Frames carry this instead: a field on RawVideoFrame, and
// an argument beside the QVideoFrame on videoFrameReady().
//
// `captureUs` is on the EventBuffer clock (steady_clock µs) and 0 means the backend has no instant of its
// own — a webcam whose driver tells us nothing — in which case CameraInstance stamps arrival, as it always
// did. `arrivalUs` is the host clock at the earliest point the backend had the frame in hand, BEFORE any
// copy or queue hop, so `arrivalUs − captureUs` is what the delivery path cost and a host stall is visible
// rather than silently folded into the timeline.
struct FrameTiming {
    qint64 captureUs = 0;     // exposure instant, host clock µs; 0 = this backend has none
    qint64 arrivalUs = 0;     // host clock µs when the backend first had the frame
    qint64 deviceNs  = -1;    // the camera's own timestamp, ns on its clock; -1 = none
    qint64 frameId   = -1;    // the camera's own frame counter; -1 = none
    // How captureUs was arrived at: the camera's clock mapped onto ours (Device), a platform instant that
    // is already on our clock (DevicePts, AVFoundation), or arrival time (HostArrival).
    enum class Source : quint8 { HostArrival = 0, Device = 1, DevicePts = 2 };
    Source source = Source::HostArrival;
};

inline const char *frameTimestampSourceName(FrameTiming::Source s)
{
    switch (s) {
    case FrameTiming::Source::Device:    return "device";
    case FrameTiming::Source::DevicePts: return "devicePts";
    case FrameTiming::Source::HostArrival: break;
    }
    return "hostArrival";
}

Q_DECLARE_METATYPE(FrameTiming)
