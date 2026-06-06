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

#include <QMediaPlayer>
#include <QMessageLogContext>
#include <cstdarg>
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
// PinPoint Studio messages bypass this via PpLogStream. This handler suppresses
// high-volume Qt framework categories and routes Qt warnings/errors into
// PpMessageLog so they appear in the resource monitor's application log.

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

    // Capture warnings and above into the in-app log regardless of debug level.
    // Prefix the logging category when it's not the anonymous default.
    if (type >= QtWarningMsg) {
        QString logMsg = msg;
        if (ctx.category && QLatin1StringView(ctx.category) != QLatin1StringView("default"))
            logMsg = QStringLiteral("[") + QString::fromLatin1(ctx.category) + QStringLiteral("] ") + msg;
        PpMessageLog::instance()->append(type, logMsg);
    }

#if PINPOINT_DEBUG_LEVEL == 0
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

#ifdef PP_HAS_DLSYM
// ── FFmpeg av_log capture ─────────────────────────────────────────────────────
// Routes libavutil log output (e.g. the "Input #0, mov,mp4,..." stream dumps
// printed when Qt Multimedia opens a media file) into PpMessageLog instead of
// stderr.  Numeric constants mirror libavutil/log.h — no header dependency.

static constexpr int kAvLogError   = 16;
static constexpr int kAvLogWarning = 24;
static constexpr int kAvLogInfo    = 32;

static void ffmpegLogCallback(void *, int level, const char *fmt, va_list vl)
{
    // Custom callbacks receive every message regardless of av_log_set_level —
    // filter here.  Drop verbose/debug/trace.
    if (level > kAvLogInfo)
        return;

    char chunk[1024];
    vsnprintf(chunk, sizeof chunk, fmt, vl);

    // av_log emits partial lines (the stream dump is built from many calls) —
    // accumulate per thread and flush one PpMessageLog entry per full line.
    thread_local QString pending;
    pending += QString::fromUtf8(chunk);

    qsizetype nl;
    while ((nl = pending.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = pending.first(nl).trimmed();
        pending.remove(0, nl + 1);
        if (line.isEmpty())
            continue;

        QtMsgType type = QtInfoMsg;
        if      (level <= kAvLogError)   type = QtCriticalMsg;
        else if (level <= kAvLogWarning) type = QtWarningMsg;
        PpMessageLog::instance()->append(type, QStringLiteral("[FFmpeg] ") + line);
    }
}

// Install ffmpegLogCallback on every loaded libavutil instance.  TWO instances
// coexist: the app binary links the system copy (.so.60, via the swing-export
// encoder) which is loaded before main(), while Qt Multimedia's FFmpeg plugin
// links the Qt-bundled copy (.so.59).  The callback pointer lives in static
// storage inside each instance, so both must be set — do not stop at the
// first one found.
static void installAvLogCallbackOnAllInstances()
{
    using SetCb = void(*)(void(*)(void *, int, const char *, va_list));
    static const char * const kLibs[] = {
        "libavutil.so.59",      // Qt 6.7–6.11
        "libavutil.so.58",      // Qt 6.4–6.6
        "libavutil.so.60",      // system
        "libavutil.so",
        "libavutil.59.dylib",   // macOS Qt bundle
        "libavutil.58.dylib",
        "libavutil.dylib",
        nullptr
    };

    for (int i = 0; kLibs[i]; ++i) {
        // Only instances that are already loaded — RTLD_NOLOAD never loads.
        void *h = dlopen(kLibs[i], RTLD_LAZY | RTLD_NOLOAD);
        if (!h)
            continue;
        if (auto fn = reinterpret_cast<SetCb>(dlsym(h, "av_log_set_callback")))
            fn(&ffmpegLogCallback);
        dlclose(h);   // balance the RTLD_NOLOAD refcount; instance stays loaded
    }
}
#endif

void PinPointDebug::install()
{
    qInstallMessageHandler(ppMessageHandler);

    whisper_log_set(silentLogCallback, nullptr);
    ggml_log_set(silentLogCallback, nullptr);

    // Capture FFmpeg's av_log output into PpMessageLog.  This early pass only
    // reaches instances already loaded before main() (the system libavutil
    // linked by the swing-export encoder).  The Qt-bundled instance is handled
    // by installFfmpegLogCapture() once QGuiApplication exists.
#ifdef PP_HAS_DLSYM
    installAvLogCallbackOnAllInstances();
#endif
}

void PinPointDebug::installFfmpegLogCapture()
{
#ifdef PP_HAS_DLSYM
    // Force the Qt Multimedia FFmpeg plugin to load NOW.  Its integration
    // constructor (setupFFmpegLogger in qffmpegmediaintegration.cpp)
    // unconditionally calls av_log_set_callback — without QT_FFMPEG_DEBUG set,
    // its callback delegates to av_log_default_callback, i.e. raw stderr.
    // Constructing a throwaway QMediaPlayer creates the integration
    // deterministically; afterwards our capture callback wins the race.
    { QMediaPlayer probe; }

    installAvLogCallbackOnAllInstances();
#endif
}
