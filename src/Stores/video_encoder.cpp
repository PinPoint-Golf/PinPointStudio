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

#include "video_encoder.h"

#ifdef HAVE_FFMPEG
#include "ffmpeg_video_encoder.h"
#endif

namespace pinpoint {

std::unique_ptr<IVideoEncoder> makeVideoEncoder(const std::string& codec)
{
#ifdef HAVE_FFMPEG
    // AppSettings storage/videoCodec key -> libavcodec encoder name. H.264 and
    // H.265 are the supported cross-platform set; anything else (incl. a stale
    // "prores"/"raw" setting from an older build) falls back to H.264 so a swing
    // is never lost to an unsupported codec selection.
    if (codec == "h265")
        return std::make_unique<FfmpegVideoEncoder>("libx265");
    return std::make_unique<FfmpegVideoEncoder>("libx264");
#else
    (void)codec;
    return nullptr; // built without FFmpeg
#endif
}

} // namespace pinpoint
