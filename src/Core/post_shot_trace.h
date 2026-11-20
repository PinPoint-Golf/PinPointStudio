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

// PostShotTrace — what the GUI thread spends after a live shot lands, step by step, as ONE log line per shot
// (refactor 3.7-S, the post-shot stall: build/refactor/step-3/3.7/NOTES-S.md). Platform ring: Qt only, no PinPoint
// type, header-only like atomic_write.h, so every ring may time a step without a new link dependency.
//
//   PostShotTrace::begin(shot, sink)   the shot's document has landed (ShotProcessor::onSwingDocWritten). Opens the
//                                      shot's window on THIS thread; a window still open is written first (marked
//                                      "window cut" if a step was still running)
//   PostShotTrace::Scope s("name");    a step: its wall time is recorded only while a window is open AND only on the
//                                      thread that opened it. Anywhere else (a worker, a tool, a test, Home) it costs
//                                      one comparison and records nothing. Scopes nest; the line shows the nesting
//   PostShotTrace::end()               write the line now (the window also closes itself `quietMs` after its last step)
//
// The line, through the sink (the app: ppInfo, the one log):
//
//   [PostShot] <shot> span 3949 ms, timed 3810 ms: evt01 1.2, notify 2312, addShot 2, shotProcessed 640 (lm.write 410
//     (lock 0.1, doc 405), refresh 180 (read 160, review 12, emit 8)), settle 90 (rebuild 30 (emit 12), persist 55 …
//
// `span` runs from begin() to the end of the last step; `timed` is the sum of the top-level steps; span − timed is
// what the GUI thread did in between that no scope names (other events, QML, the event loop itself). Values are ms,
// one decimal under 10. NO BEHAVIOUR CHANGE: a scope only reads a clock.
//
// A window holds at most kMaxSteps steps (the rest are counted, not kept), so a window left open by a burst of
// work cannot grow without bound. Per process: a DLL that includes this header has its own (inactive) copy.

#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
#include <utility>

namespace pinpoint {

class PostShotTrace
{
public:
    struct Step {
        const char *name = "";
        int         depth = 0;
        qint64      ns = 0;
    };
    struct Window {
        QString     shot;
        qint64      spanNs = 0;
        QList<Step> steps;
        int         dropped = 0;   // steps past kMaxSteps, not kept
        int         cut = 0;       // steps still running when the next begin() closed the window (3.7-I)
    };
    // `line` is the formatted line (no tag); `window` the same, structured (the bench reads it).
    using Sink = std::function<void(const QString &line, const Window &window)>;

    static constexpr int kMaxSteps = 256;

    static void begin(const QString &shot, Sink sink, int quietMs = 4000)
    {
        State &s = state();
        if (s.open.load(std::memory_order_relaxed)) {
            end();   // a window still open is written first …
            if (s.open.load(std::memory_order_relaxed) && s.owner.load(std::memory_order_relaxed) == QThread::currentThread())
                write(/*cutSteps=*/s.depth);   // … even with a step still running (a begin() inside a scope): marked cut
        }
        s.owner.store(QThread::currentThread(), std::memory_order_relaxed);
        s.open.store(true, std::memory_order_relaxed);
        s.quietMs = quietMs;
        s.sink    = std::move(sink);
        s.window  = Window{};
        s.window.shot = shot;
        s.depth   = 0;
        s.lastEndNs = 0;
        ++s.generation;
        s.clock.start();
        scheduleQuietEnd();
    }

    static void end()
    {
        State &s = state();
        if (!s.open.load(std::memory_order_relaxed) || s.owner.load(std::memory_order_relaxed) != QThread::currentThread())
            return;   // only the thread that opened the window closes it
        if (s.depth > 0) { scheduleQuietEnd(); return; }   // a step still running: written when it is done
        write(0);
    }

    // Any thread may ask (a worker's atomicWrite does): `open` and `owner` are atomics, and everything else is touched
    // only by the owner thread, so a worker reads two words and records nothing.
    static bool recording()
    {
        const State &s = state();
        return s.open.load(std::memory_order_relaxed) && s.owner.load(std::memory_order_relaxed) == QThread::currentThread();
    }

    class Scope
    {
    public:
        explicit Scope(const char *name)
        {
            if (!recording()) return;
            State &s = state();
            m_generation = s.generation;
            if (s.window.steps.size() >= kMaxSteps) { ++s.window.dropped; return; }
            m_index = int(s.window.steps.size());
            s.window.steps.push_back(Step{ name, s.depth, 0 });
            ++s.depth;
            m_t0 = s.clock.nsecsElapsed();
        }
        ~Scope()
        {
            if (m_index < 0) return;
            State &s = state();
            if (!recording() || s.generation != m_generation) return;
            const qint64 now = s.clock.nsecsElapsed();
            s.window.steps[m_index].ns = now - m_t0;
            --s.depth;
            s.lastEndNs = now;
            if (s.depth == 0) scheduleQuietEnd();
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        int     m_index = -1;
        quint64 m_generation = 0;
        qint64  m_t0 = 0;
    };

    static QString format(const Window &w)
    {
        const auto ms = [](qint64 ns) {
            const double v = ns / 1.0e6;
            return v < 10.0 ? QString::number(v, 'f', 1) : QString::number(qint64(v + 0.5));
        };
        qint64 timed = 0;
        for (const Step &st : w.steps)
            if (st.depth == 0) timed += st.ns;
        QString out = QStringLiteral("%1 span %2 ms, timed %3 ms:").arg(w.shot, ms(w.spanNs), ms(timed));
        int depth = 0;
        bool first = true;
        for (const Step &st : w.steps) {
            while (depth > st.depth) { out += QLatin1Char(')'); --depth; }
            if (st.depth > depth) { out += QStringLiteral(" ("); depth = st.depth; }
            else                  out += first ? QStringLiteral(" ") : QStringLiteral(", ");
            out += QLatin1String(st.name) + QLatin1Char(' ') + ms(st.ns);
            first = false;
        }
        while (depth-- > 0) out += QLatin1Char(')');
        if (w.dropped > 0) out += QStringLiteral(" (+%1 steps not kept)").arg(w.dropped);
        if (w.cut > 0) out += QStringLiteral(" (window cut: %1 steps still running)").arg(w.cut);
        return out;
    }

private:
    // Close the window and hand its line to the sink. `cutSteps` > 0: closed by the next begin() while steps were
    // still running; they show 0 and the line says so, so a line is never dropped without a word (REVIEW-I F5).
    static void write(int cutSteps)
    {
        State &s = state();
        s.open.store(false, std::memory_order_relaxed);
        ++s.generation;   // a still-running Scope of this window records nothing when it ends
        s.window.spanNs = s.lastEndNs;
        s.window.cut = cutSteps;
        Sink sink = std::exchange(s.sink, Sink{});
        if (sink) sink(format(s.window), s.window);
    }

    struct State {
        std::atomic<bool>      open { false };
        std::atomic<QThread *> owner { nullptr };
        int           quietMs = 4000;
        int           depth = 0;
        quint64       generation = 0;
        qint64        lastEndNs = 0;
        QElapsedTimer clock;
        Window        window;
        Sink          sink;
    };
    static State &state()
    {
        static State s;
        return s;
    }
    // The window closes itself once the GUI thread has been quiet in it for quietMs: the last shot of a session has
    // no next shot to close it. A single-shot timer per top-level step; a stale one (an older generation) does nothing.
    static void scheduleQuietEnd()
    {
        State &s = state();
        if (!s.open.load(std::memory_order_relaxed) || s.depth > 0) return;
        const quint64 g = s.generation;
        const qint64 at = s.lastEndNs;
        QTimer::singleShot(s.quietMs, [g, at] {
            State &t = state();
            if (t.open.load(std::memory_order_relaxed) && t.generation == g && t.lastEndNs == at) end();
        });
    }
};

} // namespace pinpoint
