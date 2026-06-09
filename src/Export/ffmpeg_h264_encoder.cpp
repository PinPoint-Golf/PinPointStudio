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

#include "ffmpeg_h264_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "../Core/pp_debug.h"

namespace pinpoint {

namespace {

// av_err2str() is a C compound-literal macro and does not compile as C++.
QString avErrorString(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // namespace

FfmpegH264Encoder::~FfmpegH264Encoder()
{
    cleanup();
}

bool FfmpegH264Encoder::open(const VideoEncoderConfig& cfg)
{
    if (m_fmt) {
        ppError() << "[SwingExport] encoder already open";
        return false;
    }
    if (cfg.width <= 0 || cfg.height <= 0 || (cfg.width % 2) || (cfg.height % 2)) {
        ppError() << "[SwingExport] invalid/odd dimensions" << cfg.width << "x" << cfg.height;
        return false;
    }

    // Our linked libav instance is distinct from Qt Multimedia's bundled copy
    // (which pp_debug.cpp silences via dlsym) — set its log level here.
    av_log_set_level(AV_LOG_WARNING);

    m_width  = cfg.width;
    m_height = cfg.height;

    int rc = avformat_alloc_output_context2(&m_fmt, nullptr, nullptr, cfg.path.c_str());
    if (rc < 0 || !m_fmt) {
        ppError() << "[SwingExport] avformat_alloc_output_context2:" << avErrorString(rc);
        cleanup();
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        ppError() << "[SwingExport] libx264 encoder not available in this FFmpeg build";
        cleanup();
        return false;
    }

    m_stream = avformat_new_stream(m_fmt, nullptr);
    if (!m_stream) {
        ppError() << "[SwingExport] avformat_new_stream failed";
        cleanup();
        return false;
    }

    m_enc = avcodec_alloc_context3(codec);
    if (!m_enc) {
        ppError() << "[SwingExport] avcodec_alloc_context3 failed";
        cleanup();
        return false;
    }

    m_enc->width     = cfg.width;
    m_enc->height    = cfg.height;
    m_enc->pix_fmt   = AV_PIX_FMT_YUV420P;
    m_enc->time_base = AVRational{1, cfg.out_fps};

    // Frequent keyframes + no B-frames → frame-accurate seeks decode only a few
    // frames, so replay scrubbing stays responsive (the x264 default keyint of
    // ~250 makes a short swing one GOP, forcing a decode from frame 0 per seek).
    m_enc->gop_size     = cfg.gop > 0 ? cfg.gop : 10;
    m_enc->max_b_frames = 0;

    // Tag colour as BT.709, limited range (the sws conversion below matches).
    m_enc->color_primaries = AVCOL_PRI_BT709;
    m_enc->color_trc       = AVCOL_TRC_BT709;
    m_enc->colorspace      = AVCOL_SPC_BT709;
    m_enc->color_range     = AVCOL_RANGE_MPEG;

    av_opt_set(m_enc->priv_data, "preset",  cfg.preset.c_str(), 0);
    av_opt_set(m_enc->priv_data, "crf",     std::to_string(cfg.crf).c_str(), 0);
    av_opt_set(m_enc->priv_data, "profile", "high", 0);

    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        m_enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    rc = avcodec_open2(m_enc, codec, nullptr);
    if (rc < 0) {
        ppError() << "[SwingExport] avcodec_open2:" << avErrorString(rc);
        cleanup();
        return false;
    }

    rc = avcodec_parameters_from_context(m_stream->codecpar, m_enc);
    if (rc < 0) {
        ppError() << "[SwingExport] avcodec_parameters_from_context:" << avErrorString(rc);
        cleanup();
        return false;
    }
    m_stream->time_base = m_enc->time_base;

    rc = avio_open(&m_fmt->pb, cfg.path.c_str(), AVIO_FLAG_WRITE);
    if (rc < 0) {
        ppError() << "[SwingExport] avio_open" << QString::fromStdString(cfg.path)
                  << ":" << avErrorString(rc);
        cleanup();
        return false;
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    rc = avformat_write_header(m_fmt, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        ppError() << "[SwingExport] avformat_write_header:" << avErrorString(rc);
        cleanup();
        return false;
    }
    m_headerWritten = true;

    // BGR24 -> YUV420P with ITU-709 coefficients (full-range RGB in,
    // limited-range YUV out, matching the stream tags above).
    m_sws = sws_getContext(cfg.width, cfg.height, AV_PIX_FMT_BGR24,
                           cfg.width, cfg.height, AV_PIX_FMT_YUV420P,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        ppError() << "[SwingExport] sws_getContext failed";
        cleanup();
        return false;
    }
    const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(m_sws, coeffs, 1 /* src full (RGB) */,
                             coeffs, 0 /* dst limited */,
                             0, 1 << 16, 1 << 16);

    m_frame = av_frame_alloc();
    m_pkt   = av_packet_alloc();
    if (!m_frame || !m_pkt) {
        ppError() << "[SwingExport] av_frame_alloc/av_packet_alloc failed";
        cleanup();
        return false;
    }
    m_frame->format = AV_PIX_FMT_YUV420P;
    m_frame->width  = cfg.width;
    m_frame->height = cfg.height;
    m_frame->color_primaries = AVCOL_PRI_BT709;
    m_frame->color_trc       = AVCOL_TRC_BT709;
    m_frame->colorspace      = AVCOL_SPC_BT709;
    m_frame->color_range     = AVCOL_RANGE_MPEG;
    rc = av_frame_get_buffer(m_frame, 0);
    if (rc < 0) {
        ppError() << "[SwingExport] av_frame_get_buffer:" << avErrorString(rc);
        cleanup();
        return false;
    }

    m_pts      = 0;
    m_finished = false;
    return true;
}

bool FfmpegH264Encoder::writeBgr(const cv::Mat& bgr)
{
    if (!m_fmt || !m_enc || m_finished) {
        ppError() << "[SwingExport] writeBgr on closed encoder";
        return false;
    }
    if (bgr.type() != CV_8UC3 || bgr.cols != m_width || bgr.rows != m_height) {
        ppError() << "[SwingExport] writeBgr: frame mismatch, got"
                  << bgr.cols << "x" << bgr.rows << "type" << bgr.type()
                  << "expected" << m_width << "x" << m_height << "CV_8UC3";
        return false;
    }

    int rc = av_frame_make_writable(m_frame);
    if (rc < 0) {
        ppError() << "[SwingExport] av_frame_make_writable:" << avErrorString(rc);
        return false;
    }

    const uint8_t* srcData[1]   = {bgr.data};
    const int      srcStride[1] = {static_cast<int>(bgr.step)};
    sws_scale(m_sws, srcData, srcStride, 0, m_height,
              m_frame->data, m_frame->linesize);

    m_frame->pts = m_pts++;

    rc = avcodec_send_frame(m_enc, m_frame);
    if (rc < 0) {
        ppError() << "[SwingExport] avcodec_send_frame:" << avErrorString(rc);
        return false;
    }
    return drainPackets(false);
}

bool FfmpegH264Encoder::finish()
{
    if (!m_fmt || !m_enc || m_finished)
        return false;

    int rc = avcodec_send_frame(m_enc, nullptr);   // enter flush mode
    if (rc < 0 && rc != AVERROR_EOF) {
        ppError() << "[SwingExport] avcodec_send_frame(flush):" << avErrorString(rc);
        cleanup();
        return false;
    }
    if (!drainPackets(true)) {
        cleanup();
        return false;
    }

    rc = av_write_trailer(m_fmt);
    if (rc < 0) {
        ppError() << "[SwingExport] av_write_trailer:" << avErrorString(rc);
        cleanup();
        return false;
    }

    m_finished = true;
    cleanup();
    return true;
}

bool FfmpegH264Encoder::drainPackets(bool flushing)
{
    for (;;) {
        int rc = avcodec_receive_packet(m_enc, m_pkt);
        if (rc == AVERROR(EAGAIN))
            return !flushing;   // EAGAIN while flushing is a logic error
        if (rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            ppError() << "[SwingExport] avcodec_receive_packet:" << avErrorString(rc);
            return false;
        }

        // The libx264 wrapper emits packets with duration 0. Every frame is
        // exactly one time_base tick here, so stamp it explicitly — otherwise
        // the mov muxer computes a short edit list (ending at the last DTS
        // rather than last pts+duration) and the final reordered frame lands
        // outside it, flagged "discard" and silently dropped by decoders.
        m_pkt->duration = 1;
        av_packet_rescale_ts(m_pkt, m_enc->time_base, m_stream->time_base);
        m_pkt->stream_index = m_stream->index;

        rc = av_interleaved_write_frame(m_fmt, m_pkt);
        if (rc < 0) {
            ppError() << "[SwingExport] av_interleaved_write_frame:" << avErrorString(rc);
            return false;
        }
    }
}

void FfmpegH264Encoder::cleanup()
{
    // Idempotent: callable from any failure point and from the destructor.
    // If the header was written but the trailer wasn't (mid-encode failure),
    // the file is left truncated — caller treats the export as failed.
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_pkt)
        av_packet_free(&m_pkt);
    if (m_enc)
        avcodec_free_context(&m_enc);
    if (m_fmt) {
        if (m_fmt->pb)
            avio_closep(&m_fmt->pb);
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
    }
    m_stream        = nullptr;   // owned by m_fmt
    m_headerWritten = false;
}

} // namespace pinpoint
