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

// The analysis as a dependency graph (docs/design/analysis_dag_design.md §2, step B).
//
// The 39 stages ran strictly one after another (runStages): 18.6 s on the studio over
// the 21 corpus swings, of which the leaves — Impact, the seven body-metric stages,
// AddressMarks, Bindings — were never on anyone's path. Here the graph is DERIVED from
// each stage's StageDecl (analysis_stage.h) and the AUTHORED ORDER:
//
//   for every earlier stage i and later stage j, i → j when
//     i writes what j reads (RAW), i reads what j writes (WAR — a reader between a writer
//     and a later mutator keeps its authored place), both write it (WAW), i appends
//     detail.series and j reads or overwrites it, i overwrites it and j appends, either is
//     an undeclared barrier, or both are consecutive members of a serial group.
//   Every stage implicitly reads `halted`, so RequireProducts orders everything around it
//   exactly as the loop did (a halt skips everything after it, nothing before it).
//
// Because every edge points forward in the authored list, the sequential loop is one valid
// schedule of this graph; the executor only overlaps what the declarations allow. Three
// things are kept exactly as the loop had them:
//
//   * detail->series ORDER (the serialised output). A stage that only appends writes into
//     its own buffer (tl_seriesSink); the buffers are spliced in authored order as a cursor
//     walks the series-touching stages. A stage that reads or overwrites detail.series is
//     started only when the cursor stands on it — the series then holds exactly the prefix
//     the loop would have shown it, and nothing else touches the vector while it runs.
//   * ONE reader per camera. A stage that fetches a camera's frames (window.faceOn / .dtl /
//     .impact, mapped to the job's source ids) never runs beside another holding the same
//     source: the payload contract keeps the bytes valid only until the next fetch on that
//     source, and the MP4 reader re-decodes from frame 0 on a back-seek. SwingWindow also
//     serialises the fetch itself per source (swing_window.cpp).
//     With decode.frameStore a camera's frames are fetched ONCE, by its FrameDecode stage
//     (frame_store.h), into frames.<cam>; a consumer that reads only frames.<cam> holds no
//     camera, so two of them run at once. Any stage still declaring window.<cam> is given an
//     implicit read of frames.<cam> (below), so it never fetches beside the store's build.
//   * `ctx.seg` as a barrier: SegResolve → EventRefine → PositionsLadder → TimelineFusion is
//     a declared serial group (they all write seg, so the edges already say so; the group is
//     exported so the drawing can show it).
//
// Scheduling: a bounded pool of std::threads; among the ready stages the one with the
// longest chain still below it goes first (ties: authored order), so DtlShaft (the chain)
// takes the down-the-line camera before AddressMarks (a leaf). canRun is evaluated when a
// stage starts, on its own thread; a halt stops new starts and lets the running finish.
// Header-only (Qt Core + the standard library) so the unit test drives it with fake stages.

#include "analysis_stage.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace pinpoint::analysis {

// The serial groups: members run as one ordered unit, in this order, wherever a profile
// holds them (a member the profile lacks is simply absent from the chain).
inline const std::vector<std::vector<QString>> &stageSerialGroups()
{
    static const std::vector<std::vector<QString>> k = {
        { QStringLiteral("SegResolve"), QStringLiteral("EventRefine"),
          QStringLiteral("PositionsLadder"), QStringLiteral("TimelineFusion") } };
    return k;
}

// StageEdge, not DagEdge: src/Diagnostics/dag_layout.h already declares a
// pinpoint::analysis::DagEdge (the diagnostics graph's drawn edge, larger, with a QString).
// Two inline constructors of one mangled name is an ODR violation the linker resolves to
// whichever it meets first — in the app that was the diagnostics one, whose constructor
// wrote its QString past the end of this struct's `e` on the stack, zeroing the loop's
// references, and every in-app re-analysis of 7 Oct 2026 died at buildStageGraph with
// di == nullptr (swinglab never compiles dag_layout.h, so it never reproduced there).
struct StageEdge {
    int         from = -1, to = -1;
    QStringList kinds;     // raw | war | waw | append | halted | serial | barrier
    QStringList via;       // the resources that made it
    bool        reduced = false;   // implied by a longer path (transitive reduction)
};

struct StageGraph {
    std::vector<QString>          names;
    std::vector<StageDecl>        decls;
    std::vector<std::vector<int>> preds, succs;       // every ordering constraint
    std::vector<StageEdge>          edges;              // the same, with reasons
    std::vector<int>              seriesOrder;        // stages touching detail.series, authored order
    std::vector<char>             seriesGated;        // reads/overwrites detail.series
    std::vector<char>             seriesBuffered;     // appends only
    std::vector<int>              height;             // longest node chain to a sink (priority)
    std::vector<int>              rank;               // longest node chain from a root (drawing)
    std::vector<std::vector<int>> groups;             // serial groups present (stage indices)
};

namespace dagdetail {
inline bool has(const std::vector<QString> &v, const QString &r)
{
    return std::find(v.begin(), v.end(), r) != v.end();
}
} // namespace dagdetail

inline StageGraph buildStageGraph(const SessionProfile &profile, const ShotAnalysisJob *job = nullptr)
{
    using dagdetail::has;
    StageGraph g;
    const int n = int(profile.stages.size());
    g.names.resize(size_t(n));
    g.decls.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        g.names[size_t(i)] = profile.stages[size_t(i)]->name();
        StageDecl d = job ? profile.stages[size_t(i)]->declFor(*job) : profile.stages[size_t(i)]->decl();
        // Every stage reads `halted` (the loop checks it before each stage).
        if (!d.haltSafe && !has(d.writes, QString::fromLatin1(res::kHalted))
            && !has(d.reads, QString::fromLatin1(res::kHalted)))
            d.reads.push_back(QString::fromLatin1(res::kHalted));
        // A stage that fetches a camera's window also reads that camera's frame store
        // (decode.frameStore): it may take its frames from it, and if it does not it must still
        // not interleave its fetches with the store's build. So it is ordered after the store.
        for (size_t k = 0, nr = d.reads.size(); k < nr; ++k) {
            const QString f = res::framesFor(d.reads[k]);
            if (!f.isEmpty() && !has(d.reads, f) && !has(d.writes, f)) d.reads.push_back(f);
        }
        g.decls[size_t(i)] = std::move(d);
    }
    // Serial groups → index chains.
    std::vector<std::pair<int, int>> serialPairs;
    for (const std::vector<QString> &grp : stageSerialGroups()) {
        std::vector<int> idx;
        for (const QString &nm : grp)
            for (int i = 0; i < n; ++i)
                if (g.names[size_t(i)] == nm) { idx.push_back(i); break; }
        std::sort(idx.begin(), idx.end());
        if (idx.size() >= 2) {
            for (size_t k = 1; k < idx.size(); ++k) serialPairs.push_back({ idx[k - 1], idx[k] });
            g.groups.push_back(idx);
        }
    }

    g.preds.assign(size_t(n), {});
    g.succs.assign(size_t(n), {});
    const QString series = QString::fromLatin1(res::kSeries);
    const QString halted = QString::fromLatin1(res::kHalted);
    for (int j = 0; j < n; ++j) {
        const StageDecl &dj = g.decls[size_t(j)];
        for (int i = 0; i < j; ++i) {
            const StageDecl &di = g.decls[size_t(i)];
            StageEdge e;
            e.from = i; e.to = j;
            const auto add = [&e](const QString &kind, const QString &via) {
                if (!e.kinds.contains(kind)) e.kinds << kind;
                if (!via.isEmpty() && !e.via.contains(via)) e.via << via;
            };
            if (di.barrier || dj.barrier) add(QStringLiteral("barrier"), QString());
            for (const QString &r : di.writes) {
                const QString kind = r == halted ? QStringLiteral("halted") : QStringLiteral("raw");
                if (has(dj.reads, r))   add(kind, r);
                if (has(dj.writes, r))  add(QStringLiteral("waw"), r);
                if (has(dj.appends, r)) add(QStringLiteral("append"), r);
            }
            for (const QString &r : di.reads)
                if (has(dj.writes, r)) add(r == halted ? QStringLiteral("halted") : QStringLiteral("war"), r);
            for (const QString &r : di.appends)
                if (has(dj.reads, r) || has(dj.writes, r)) add(QStringLiteral("append"), r);
            for (const auto &p : serialPairs)
                if (p.first == i && p.second == j) add(QStringLiteral("serial"), QString());
            if (!e.kinds.isEmpty()) {
                g.preds[size_t(j)].push_back(i);
                g.succs[size_t(i)].push_back(j);
                g.edges.push_back(std::move(e));
            }
        }
    }

    // detail.series roles.
    g.seriesGated.assign(size_t(n), 0);
    g.seriesBuffered.assign(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        const StageDecl &d = g.decls[size_t(i)];
        const bool reads = has(d.reads, series) || has(d.writes, series) || d.barrier;
        const bool appends = has(d.appends, series);
        if (reads) g.seriesGated[size_t(i)] = 1;
        else if (appends) g.seriesBuffered[size_t(i)] = 1;
        if (reads || appends) g.seriesOrder.push_back(i);
    }

    // Transitive reduction (for the drawing only): reach[i][k] over the forward edges.
    std::vector<std::vector<char>> reach(static_cast<size_t>(n), std::vector<char>(size_t(n), 0));
    for (int i = n - 1; i >= 0; --i)
        for (int s : g.succs[size_t(i)]) {
            reach[size_t(i)][size_t(s)] = 1;
            for (int k = 0; k < n; ++k)
                if (reach[size_t(s)][size_t(k)]) reach[size_t(i)][size_t(k)] = 1;
        }
    for (StageEdge &e : g.edges)
        for (int s : g.succs[size_t(e.from)])
            if (s != e.to && s < e.to && reach[size_t(s)][size_t(e.to)]) { e.reduced = true; break; }

    g.height.assign(size_t(n), 1);
    for (int i = n - 1; i >= 0; --i)
        for (int s : g.succs[size_t(i)])
            g.height[size_t(i)] = std::max(g.height[size_t(i)], 1 + g.height[size_t(s)]);
    g.rank.assign(size_t(n), 0);
    for (int j = 0; j < n; ++j)
        for (int p : g.preds[size_t(j)])
            g.rank[size_t(j)] = std::max(g.rank[size_t(j)], g.rank[size_t(p)] + 1);
    return g;
}

// The job's source id behind a camera resource; kInvalidSourceId when the job has none.
inline pinpoint::SourceId cameraSourceFor(const QString &r, const ShotAnalysisJob &job)
{
    if (r == QLatin1String(res::kWindowFaceOn))
        return job.cameraSources.empty() ? pinpoint::kInvalidSourceId : job.cameraSources.front();
    if (r == QLatin1String(res::kWindowDtl))    return job.dtlSource;
    if (r == QLatin1String(res::kWindowImpact)) return job.impactSource;
    return pinpoint::kInvalidSourceId;
}

// Run `profile` as its graph on `nThreads` pool threads. Same trace shape as runStages
// (one entry per stage, authored order) plus thread + offsets. A stage that throws stops
// new starts; the running ones finish; the first exception is rethrown here.
inline void runStagesParallel(const SessionProfile &profile, AnalysisContext &ctx, int nThreads)
{
    const StageGraph g = buildStageGraph(profile, &ctx.job);
    const int n = int(profile.stages.size());
    QElapsedTimer own;
    own.start();
    const QElapsedTimer &clock = ctx.wall.isValid() ? ctx.wall : own;

    std::vector<std::vector<pinpoint::SourceId>> cams(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        for (const QString &r : g.decls[size_t(i)].reads)
            if (res::isCameraResource(r)) {
                const pinpoint::SourceId s = cameraSourceFor(r, ctx.job);
                if (s != pinpoint::kInvalidSourceId
                    && std::find(cams[size_t(i)].begin(), cams[size_t(i)].end(), s) == cams[size_t(i)].end())
                    cams[size_t(i)].push_back(s);
            }

    enum class St { Waiting, Running, Done };
    std::vector<St> st(static_cast<size_t>(n), St::Waiting);
    std::vector<int> pending(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) pending[size_t(i)] = int(g.preds[size_t(i)].size());
    std::vector<std::vector<MetricSeries>> buf(static_cast<size_t>(n));
    std::vector<StageTraceEntry> trace(static_cast<size_t>(n));
    std::set<pinpoint::SourceId> busy;
    size_t cursor = 0;
    int done = 0, running = 0;
    std::exception_ptr err;
    std::mutex mu;
    std::condition_variable cv;

    const auto advanceCursor = [&]() {
        while (cursor < g.seriesOrder.size()) {
            const int s = g.seriesOrder[cursor];
            if (st[size_t(s)] != St::Done) break;
            if (g.seriesBuffered[size_t(s)] && !buf[size_t(s)].empty()) {
                std::vector<MetricSeries> &dst = ctx.detail->series;
                dst.insert(dst.end(), std::make_move_iterator(buf[size_t(s)].begin()),
                           std::make_move_iterator(buf[size_t(s)].end()));
                buf[size_t(s)].clear();
            }
            ++cursor;
        }
    };
    const auto markDone = [&](int i) {
        st[size_t(i)] = St::Done;
        ++done;
        for (pinpoint::SourceId c : cams[size_t(i)]) busy.erase(c);
        for (int s : g.succs[size_t(i)]) --pending[size_t(s)];
    };
    const auto pick = [&]() -> int {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (st[size_t(i)] != St::Waiting || pending[size_t(i)] != 0) continue;
            if (g.seriesGated[size_t(i)]
                && (cursor >= g.seriesOrder.size() || g.seriesOrder[cursor] != i)) continue;
            bool camFree = true;
            for (pinpoint::SourceId c : cams[size_t(i)]) if (busy.count(c)) { camFree = false; break; }
            if (!camFree) continue;
            if (best < 0 || g.height[size_t(i)] > g.height[size_t(best)]) best = i;
        }
        return best;
    };

    const auto worker = [&](int tid) {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            if (done == n || (err && running == 0)) { cv.notify_all(); return; }
            if (err) { cv.wait(lk); continue; }
            const int i = pick();
            if (i < 0) {
                if (running == 0) {   // cannot happen with forward edges; never hang on it
                    err = std::make_exception_ptr(std::logic_error("analysis DAG: no runnable stage"));
                    cv.notify_all();
                    continue;
                }
                cv.wait(lk);
                continue;
            }
            st[size_t(i)] = St::Running;
            ++running;
            for (pinpoint::SourceId c : cams[size_t(i)]) busy.insert(c);
            lk.unlock();

            AnalysisStage &stage = *profile.stages[size_t(i)];
            StageTraceEntry e;
            e.name    = stage.name();
            e.thread  = tid;
            e.startNs = clock.nsecsElapsed();
            std::exception_ptr thrown;
            try {
                if (!stage.canRun(ctx)) {
                    e.skipReason = stage.skipReason(ctx);
                } else {
                    tl_seriesSink = g.seriesBuffered[size_t(i)] ? &buf[size_t(i)] : nullptr;
                    QElapsedTimer t;
                    t.start();
                    stage.run(ctx);
                    e.elapsedNs = t.nsecsElapsed();
                    e.ran = true;
                }
            } catch (...) {
                thrown = std::current_exception();
            }
            tl_seriesSink = nullptr;
            e.endNs = clock.nsecsElapsed();

            lk.lock();
            --running;
            trace[size_t(i)] = std::move(e);
            markDone(i);
            if (thrown && !err) err = thrown;
            advanceCursor();
            // A halt stops new starts. Every stage after the halting one depends on it
            // (implicit `halted` read), so none of them is running: skip them all. (A
            // haltSafe stage may be running or done; it cannot have run in a context that
            // halts — StageDecl::haltSafe.)
            if (ctx.halted) {
                const qint64 now = clock.nsecsElapsed();
                for (int k = 0; k < n; ++k)
                    if (st[size_t(k)] == St::Waiting) {
                        StageTraceEntry h;
                        h.name = profile.stages[size_t(k)]->name();
                        h.skipReason = QStringLiteral("halted");
                        h.startNs = h.endNs = now;
                        h.thread = tid;
                        trace[size_t(k)] = std::move(h);
                        markDone(k);
                    }
                advanceCursor();
            }
            cv.notify_all();
        }
    };

    const int threads = std::max(1, std::min(nThreads, std::max(1, n)));
    std::vector<std::thread> pool;
    pool.reserve(size_t(threads));
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker, t);
    for (std::thread &t : pool) t.join();

    ctx.trace.insert(ctx.trace.end(), std::make_move_iterator(trace.begin()),
                     std::make_move_iterator(trace.end()));
    if (err) std::rethrow_exception(err);
}

// The declared graph as JSON (pinpoint.analysisDag/1) — nodes with their declarations,
// rank and series role, every edge with why it exists and whether it is implied by a
// longer path, the serial groups, the series cursor order and which stages hold which
// camera. `timeline`, when given, is the run's analysis.timings.stages (authored order).
inline QJsonObject stageGraphToJson(const QString &profileName, const StageGraph &g,
                                    const std::vector<StageTiming> *timeline = nullptr)
{
    const auto arr = [](const std::vector<QString> &v) {
        QJsonArray a;
        for (const QString &s : v) a.append(s);
        return a;
    };
    QJsonArray nodes;
    for (size_t i = 0; i < g.names.size(); ++i) {
        const StageDecl &d = g.decls[i];
        QJsonArray cams;
        for (const QString &r : d.reads) if (res::isCameraResource(r)) cams.append(r);
        nodes.append(QJsonObject{
            { QStringLiteral("id"),      int(i) },
            { QStringLiteral("name"),    g.names[i] },
            { QStringLiteral("reads"),   arr(d.reads) },
            { QStringLiteral("writes"),  arr(d.writes) },
            { QStringLiteral("appends"), arr(d.appends) },
            { QStringLiteral("barrier"), d.barrier },
            { QStringLiteral("cameras"), cams },
            { QStringLiteral("series"),  g.seriesGated[i] ? QStringLiteral("gated")
                                         : g.seriesBuffered[i] ? QStringLiteral("buffered")
                                                                : QStringLiteral("none") },
            { QStringLiteral("rank"),    g.rank[i] },
            { QStringLiteral("height"),  g.height[i] } });
    }
    QJsonArray edges;
    for (const StageEdge &e : g.edges)
        edges.append(QJsonObject{
            { QStringLiteral("from"),    e.from },
            { QStringLiteral("to"),      e.to },
            { QStringLiteral("kinds"),   QJsonArray::fromStringList(e.kinds) },
            { QStringLiteral("via"),     QJsonArray::fromStringList(e.via) },
            { QStringLiteral("reduced"), e.reduced } });
    QJsonArray groups;
    for (const std::vector<int> &grp : g.groups) {
        QJsonArray a;
        for (int i : grp) a.append(g.names[size_t(i)]);
        groups.append(a);
    }
    QJsonArray seriesOrder;
    for (int i : g.seriesOrder) seriesOrder.append(g.names[size_t(i)]);
    QJsonObject o{
        { QStringLiteral("schema"),       QStringLiteral("pinpoint.analysisDag/1") },
        { QStringLiteral("profile"),      profileName },
        { QStringLiteral("resources"),    arr(res::all()) },
        { QStringLiteral("nodes"),        nodes },
        { QStringLiteral("edges"),        edges },
        { QStringLiteral("serialGroups"), groups },
        { QStringLiteral("seriesOrder"),  seriesOrder } };
    if (timeline) {
        QJsonArray tl;
        for (const StageTiming &e : *timeline) {
            QJsonObject so{
                { QStringLiteral("name"),    e.name },
                { QStringLiteral("ran"),     e.ran },
                { QStringLiteral("ms"),      e.ms },
                { QStringLiteral("startMs"), e.startMs },
                { QStringLiteral("endMs"),   e.endMs },
                { QStringLiteral("thread"),  e.thread } };
            if (!e.skipReason.isEmpty()) so.insert(QStringLiteral("skipReason"), e.skipReason);
            tl.append(so);
        }
        o.insert(QStringLiteral("timeline"), tl);
    }
    return o;
}

} // namespace pinpoint::analysis
