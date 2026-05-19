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

#include "pp_debug.h"
#include "PpMessageLog.h"

#include <whisper.h>
#include <ggml.h>

#ifdef PP_HAS_DLSYM
#include <dlfcn.h>
#endif

#include <QMessageLogContext>
#include <cstdio>

// ── PpLogStream ───────────────────────────────────────────────────────────────

PpLogStream::PpLogStream(QtMsgType t)
    : m_type(t), m_dbg(std::in_place, &m_buf)
{}

PpLogStream::~PpLogStream()
{
    m_dbg.reset();  // destructs QDebug, flushing its internal state to m_buf

    if (m_type == QtDebugMsg) {
        fprintf(stderr, "%s\n", m_buf.trimmed().toLocal8Bit().constData());
    } else {
        PpMessageLog::instance()->append(m_type, m_buf.trimmed());
    }

    if (m_type == QtFatalMsg) ::abort();
}

// ── Qt message handler ────────────────────────────────────────────────────────
// Pinpoint messages now bypass this via PpLogStream. This handler exists only
// to suppress high-volume Qt framework categories from reaching stderr.

static void ppMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (ctx.category) {
        const QLatin1StringView cat(ctx.category);

        if (cat.startsWith(QLatin1StringView("qt.multimedia.ffmpeg"))
         || cat.startsWith(QLatin1StringView("qt.multimedia.playbackengine"))
         || cat.startsWith(QLatin1StringView("qt.core.qfuture"))
         || cat.startsWith(QLatin1StringView("qt.bluetooth")))
            return;

#if PINPOINT_DEBUG_LEVEL < 3
        if (type < QtWarningMsg && cat.startsWith(QLatin1StringView("qt.")))
            return;
#endif
    }

#if PINPOINT_DEBUG_LEVEL == 0
    Q_UNUSED(type); Q_UNUSED(msg);
    if (type == QtFatalMsg) ::abort();
    return;
#else
    const QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:     fprintf(stderr, "%s\n",           localMsg.constData()); break;
    case QtWarningMsg:  fprintf(stderr, "WARNING: %s\n",  localMsg.constData()); break;
    case QtCriticalMsg: fprintf(stderr, "CRITICAL: %s\n", localMsg.constData()); break;
    case QtFatalMsg:    fprintf(stderr, "FATAL: %s\n",    localMsg.constData()); ::abort();
    }
#endif
}

// ── Dependency log suppressors ────────────────────────────────────────────────

static void silentLogCallback(ggml_log_level, const char *, void *) {}

void PinPointDebug::install()
{
    qInstallMessageHandler(ppMessageHandler);

    whisper_log_set(silentLogCallback, nullptr);
    ggml_log_set(silentLogCallback, nullptr);

    // Suppress FFmpeg's av_log output (Qt Multimedia's bundled FFmpeg prints
    // "Input #0, mov,mp4,..." at AV_LOG_INFO when opening media files).
    //
    // Qt bundles its own libavutil (e.g. .so.59) — separate instance from the
    // system package (.so.60), so dlsym(RTLD_DEFAULT,...) hits the wrong one.
    // Strategy: explicitly dlopen the versioned SONAME.  The binary RUNPATH
    // includes Qt's lib dir, so the correct file is found.  The dynamic
    // linker's load cache means the FFmpeg media plugin (loaded lazily later)
    // reuses this same instance and inherits the log level we set here.
    // AV_LOG_WARNING = 24  (numeric constant — no header dependency needed).
#if defined(PP_HAS_DLSYM) && PINPOINT_DEBUG_LEVEL < 3
    {
        using Fn = void(*)(int);
        static const char * const kLibs[] = {
            "libavutil.so.59",   // Qt 6.7–6.11
            "libavutil.so.58",   // Qt 6.4–6.6
            "libavutil.so.60",   // system (fallback)
            "libavutil.so",
            nullptr
        };

        bool done = false;

        for (int i = 0; !done && kLibs[i]; ++i) {
            if (void *h = dlopen(kLibs[i], RTLD_LAZY | RTLD_NOLOAD)) {
                if (auto fn = reinterpret_cast<Fn>(dlsym(h, "av_log_set_level")))
                    fn(24);
                dlclose(h);
                done = true;
            }
        }

        if (!done) {
            for (int i = 0; !done && kLibs[i]; ++i) {
                if (void *h = dlopen(kLibs[i], RTLD_LAZY)) {
                    if (auto fn = reinterpret_cast<Fn>(dlsym(h, "av_log_set_level")))
                        fn(24);
                    done = true;
                }
            }
        }
    }
#endif
}
