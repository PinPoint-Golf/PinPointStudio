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

#include <QRect>
#include <QRectF>
#include <QVideoFrame>
#include "raw_video_frame.h"

// Software crop helpers for the per-camera crop setting.
//
// The crop is stored normalized (0..1, relative to the full sensor) in
// AppSettings ("camera/roi"). Backends with hardware ROI support (Aravis,
// Spinnaker) apply it at start(); these helpers implement the software
// fallback for backends without hardware crop (Qt Multimedia, AVFoundation)
// and for the rare case where the hardware ROI set fails at runtime.
//
// All functions are free functions safe to call from the capture thread.

namespace pp_crop {

// True when the rect selects a real sub-region of the frame. Both an empty
// rect and the unit rect {0,0,1,1} ("Reset to full frame" persists the unit
// rect) mean "no crop".
bool cropIsActive(const QRectF &norm);

// Convert a normalized crop rect to a pixel rect within a w x h frame.
// x/y/width/height are snapped DOWN to the given alignments (2 for
// chroma-subsampled YUV and Bayer data so the UV pairing / CFA phase is
// preserved) and clamped so the rect never exceeds the frame bounds.
QRect snapCropRect(const QRectF &norm, int w, int h, int xAlign = 2, int yAlign = 2);

// Return a new CPU-backed frame containing the cropped region. On an
// unsupported pixel format or map failure the input frame is returned
// unchanged (logged once per session).
QVideoFrame   cropVideoFrame(const QVideoFrame &src, const QRectF &norm);
RawVideoFrame cropRawFrame(const RawVideoFrame &src, const QRectF &norm);

} // namespace pp_crop
