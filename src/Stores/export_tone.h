/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE DISPLAY TONE CURVE ON WHAT A GOLFER TAKES AWAY (#183). The export zip (swing_zip_exporter.h) is the one
// thing that leaves the app with a shot's video in it. A raw-Bayer camera's stored MP4 is linear and plays dark
// in any other player, so the zip carries a curved COPY: each such clip decoded frame by frame, mapped through
// displayToneLut() (Video/display_tone.h, the same table the screen draws), and encoded again; and thumb.jpg
// likewise when it came from such a stream (ruled: the Product Owner, 26 Nov 2026, #183 batch 3 (4)). The stored
// files are only ever read here, never written.
//
// Not the analysis' path: the stored MP4 stays what the recording loader decodes. Nothing that feeds the
// analysis may include this file or display_tone.h (display_tone_test scans).

#include "video_encoder.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <string>

namespace pinpoint {

// Which of one swing's files the zip curves, from its document (the stream rule: displayToneFor on each video
// stream's source.pixelFormat).
struct ExportToneMembers {
    // Video file name → that stream's encoder settings for the curved copy: the playback rate it was recorded
    // at (playback.fps, 30 by default) and whether it is the impact camera's clip (setup.perspective 4).
    struct Clip {
        int  playbackFps = 30;
        bool impact      = false;
    };
    QHash<QString, Clip> clips;
    // thumb.jpg came from a raw-Bayer stream: the face-on stream the recorder writes it from, else the first
    // stream it recorded from the cameras (a stream with a source format: a phone clip never makes one). The
    // zip's thumb.jpg is then curved: ruled (the Product Owner, 26 Nov 2026, #183 batch 3 (4)).
    // ⚠ The recorder falls back to its other streams when the face-on one can't make a thumbnail
    // (swing_exporter.cpp); the document doesn't say which stream did, so on a rig that mixes a webcam with a
    // studio camera this guess can pick the wrong curve.
    bool thumbnail = false;
};
ExportToneMembers exportToneMembers(const QJsonObject &doc);

// The encoder settings of a curved copy (a second generation of a lossy clip). H.264 whatever the stored codec:
// a clip a golfer takes away must play in every player, and H.265 doesn't. A lower CRF than the default
// recording (23) keeps the second generation from adding visible loss; the impact clip keeps the recorder's own
// finer setting (shot_processor.cpp kImpactClipCrf).
inline constexpr const char *kExportToneCodec     = "h264";
inline constexpr int         kExportToneCrf       = 18;
inline constexpr int         kExportToneImpactCrf = 12;
inline constexpr const char *kExportTonePreset    = "fast";

struct RetoneResult {
    bool        ok     = false;
    long long   frames = 0;       // frames written: every frame the stored clip decodes to, in order
    std::string error;
};

// Decodes srcMp4 frame by frame, maps every channel of every frame through lut, and writes it to enc, opened
// here with cfg at the clip's own dimensions (cfg.width/height are overwritten). One frame in memory at a time.
// Fails when the clip can't be opened or decodes to nothing, or the encoder refuses a frame.
RetoneResult retoneClip(const std::string &srcMp4, IVideoEncoder &enc, VideoEncoderConfig cfg,
                        const std::array<std::uint8_t, 256> &lut);

// thumb.jpg through lut, as JPEG bytes (the quality the recorder writes it at). Empty when it can't be read.
QByteArray retoneJpeg(const QString &path, const std::array<std::uint8_t, 256> &lut);

} // namespace pinpoint
