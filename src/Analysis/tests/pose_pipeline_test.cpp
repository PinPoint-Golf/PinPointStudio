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

// Standalone test for the plumbing behind pose_inference_performance_plan.md
// steps 1 and 2 (src/Analysis/pose_pipeline.h), with fakes in place of ViTPose:
//   - InstanceCache: one object per key, built once, a rejected build not cached,
//     and the one-user-at-a-time lease;
//   - runOrderedPipeline: N = 4 producers deliver the IDENTICAL sequence to N = 1
//     on a synthetic frame source whose "pose" is a deterministic function of the
//     frame index, with the producers' finishing order deliberately scrambled;
//     fetch() called strictly in job order and never concurrently (the frame
//     source's single-reader contract); the window never exceeds the depth;
//     exceptions from either side propagate with every thread joined.
//   - step 4 (pose_schedule.h): the face-on selectors pick exactly the frames the old inline
//     loops did; the zone schedule's edges, strides and fallbacks; the consumer bracket rule.

#include "../pose_pipeline.h"
#include "../pose_schedule.h"
#include "../../Core/pp_tuned_constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

namespace {

// ── fakes ───────────────────────────────────────────────────────────────────

struct FakeEstimator {
    std::string model;
    int         threads = 0;
    bool        ready   = true;
};

using Key = std::tuple<std::string, std::string, int>;   // PoseRunner's key shape

// A "frame" of the synthetic source: a few hundred bytes derived from the index,
// standing in for a Bayer payload. The source keeps ONE resident frame, like
// SwingPayloadSource — a fetch overwrites what the previous one returned.
struct FakeSource {
    std::vector<uint8_t> resident;
    std::atomic<int>     inFetch{0};
    std::atomic<int>     overlap{0};          // fetches that ran concurrently
    std::vector<size_t>  fetchOrder;          // written only under the fetch stage

    const std::vector<uint8_t> &payloadOf(size_t frame)
    {
        if (inFetch.fetch_add(1) != 0)
            overlap.fetch_add(1);
        resident.resize(256);
        uint32_t x = uint32_t(frame) * 2654435761u + 12345u;
        for (auto &b : resident) {
            x = x * 1664525u + 1013904223u;
            b = uint8_t(x >> 24);
        }
        fetchOrder.push_back(frame);
        inFetch.fetch_sub(1);
        return resident;
    }
};

// The "keypoints" of one frame: a deterministic float function of its bytes,
// heavy enough (and jittered in time) that four producers finish out of order.
struct FakePose {
    int64_t              t_us = 0;
    std::array<float, 8> kp{};
};

FakePose fakePrepare(size_t frame, const std::vector<uint8_t> &bytes)
{
    FakePose p;
    p.t_us = int64_t(frame) * 6452;   // ~155 fps
    for (size_t j = 0; j < p.kp.size(); ++j) {
        double acc = 0.0;
        for (size_t i = 0; i < bytes.size(); ++i)
            acc += std::sin(double(bytes[i]) * 0.01 * double(j + 1) + double(i));
        p.kp[j] = float(acc);
    }
    // Uneven work: frames ≡ 0 mod 3 are slow, so with 4 producers frame k+1
    // routinely finishes before frame k.
    if (frame % 3 == 0)
        std::this_thread::sleep_for(std::chrono::microseconds(800));
    return p;
}

struct RunResult {
    std::vector<FakePose> poses;
    std::vector<size_t>   consumeOrder;
    std::vector<size_t>   fetchOrder;
    int                   overlap   = 0;
    size_t                maxWindow = 0;
};

// The PoseRunner shape: fetch copies the resident payload out (N producers) or
// prepares in place (1 producer); process prepares the copy; consume collects.
RunResult runPool(const std::vector<size_t> &frames, int producers, size_t depth)
{
    FakeSource src;
    RunResult  r;
    std::atomic<size_t> fetched{0}, consumed{0}, maxWindow{0};
    const bool serial = producers <= 1;

    struct Fetched { bool prepared = false; FakePose pose; std::vector<uint8_t> raw; size_t frame = 0; };

    const std::function<Fetched(size_t)> fetch = [&](size_t k) {
        Fetched f;
        f.frame = frames[k];
        const std::vector<uint8_t> &payload = src.payloadOf(frames[k]);
        const size_t inWindow = fetched.fetch_add(1) + 1 - consumed.load();
        size_t prev = maxWindow.load();
        while (inWindow > prev && !maxWindow.compare_exchange_weak(prev, inWindow)) {}
        if (serial) {
            f.pose     = fakePrepare(f.frame, payload);
            f.prepared = true;
        } else {
            f.raw = payload;
        }
        return f;
    };
    const std::function<FakePose(size_t, Fetched &&)> process = [&](size_t, Fetched &&f) {
        return f.prepared ? f.pose : fakePrepare(f.frame, f.raw);
    };
    const std::function<void(size_t, FakePose &&)> consume = [&](size_t k, FakePose &&p) {
        r.consumeOrder.push_back(k);
        r.poses.push_back(std::move(p));
        consumed.fetch_add(1);
    };

    runOrderedPipeline<Fetched, FakePose>(frames.size(), producers, depth, fetch, process, consume);
    r.fetchOrder = src.fetchOrder;
    r.overlap    = src.overlap.load();
    r.maxWindow  = maxWindow.load();
    return r;
}

bool bitwiseEqual(const std::vector<FakePose> &a, const std::vector<FakePose> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].t_us != b[i].t_us)
            return false;
        if (std::memcmp(a[i].kp.data(), b[i].kp.data(), sizeof(float) * a[i].kp.size()) != 0)
            return false;
    }
    return true;
}

} // namespace

// ── Step 4: which frames get posed (pose_schedule.h) ─────────────────────────

// The face-on selection loops exactly as PoseRunner::run() carried them inline before step 4
// (pose_runner.cpp, two-pass pass 1 / pass 2 and the single pass) — copied, not re-derived, so
// a change to the shared selectors that moves one face-on frame fails here.
static std::vector<size_t> oldCoarse(size_t n, int coarseStride)
{
    const size_t coarse = static_cast<size_t>(std::max(1, coarseStride));
    std::vector<size_t> out;
    for (size_t i = 0; i < n; i += coarse) out.push_back(i);
    return out;
}
static std::vector<size_t> oldFill(const std::vector<int64_t> &ts, size_t pLo, size_t pHi, int coarseStride,
                                   int denseStride, int sparseStride, int64_t impactUs, int64_t denseLo,
                                   int64_t denseHi)
{
    const size_t coarse = static_cast<size_t>(std::max(1, coarseStride));
    const size_t denseStep  = static_cast<size_t>(std::max(1, denseStride));
    const size_t sparseStep = static_cast<size_t>(std::max(1, sparseStride));
    std::vector<size_t> out;
    for (size_t i = pLo; i < pHi; ++i) {
        if ((i % coarse) == 0)
            continue;
        const bool dense = impactUs >= 0 && ts[i] >= denseLo && ts[i] <= denseHi;
        if ((i % (dense ? denseStep : sparseStep)) != 0)
            continue;
        out.push_back(i);
    }
    return out;
}
static std::vector<size_t> oldSingle(const std::vector<int64_t> &ts, size_t iAddr0, size_t i0, size_t i1,
                                     int addressStride, int sparseStride, int64_t impactUs, int64_t denseLo,
                                     int64_t denseHi)
{
    const int stride = std::max(1, sparseStride);
    const int addrStride = std::max(1, addressStride);
    std::vector<size_t> out;
    for (size_t i = iAddr0; i < i1; ++i) {
        const bool inAddressZone = i < i0;
        const bool dense = !inAddressZone && impactUs >= 0 && ts[i] >= denseLo && ts[i] <= denseHi;
        if (inAddressZone) {
            if ((i % static_cast<size_t>(addrStride)) != 0)
                continue;
        } else if (!dense && (i % static_cast<size_t>(stride)) != 0) {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

// A 4 s ring at ~155 fps with a little jitter and two dropped frames, starting at 1 s.
static std::vector<int64_t> ringTimes()
{
    std::vector<int64_t> ts;
    int64_t t = 1000000;
    for (int i = 0; i < 620; ++i) {
        ts.push_back(t + ((i * 7919) % 5) * 37);
        t += (i == 300 || i == 451) ? 12903 : 6452;
    }
    return ts;
}

static void testFrameSchedules()
{
    std::printf("\nframe schedules (step 4)\n");
    const std::vector<int64_t> ts = ringTimes();

    // (a) Face-on: the shared selectors pick the identical frame set to the old inline loops,
    // over impact placements (in-window, early, late, none) and the strides the app uses.
    {
        bool same = true;
        int cases = 0;
        for (int64_t impact : { int64_t(3000000), int64_t(1200000), int64_t(4900000), int64_t(-1) })
            for (int dS : { 1, 2 })
                for (int sS : { 1, 4, 6 })
                    for (int coarse : { 1, 12 }) {
                        const int64_t lo = impact - 500000, hi = impact + 250000;
                        for (size_t pLo : { size_t(0), size_t(37) })
                            for (size_t pHi : { ts.size(), size_t(500) }) {
                                same = same && selectTwoPassFill(ts, pLo, pHi, coarse, dS, sS, impact, lo, hi)
                                                   == oldFill(ts, pLo, pHi, coarse, dS, sS, impact, lo, hi);
                                ++cases;
                            }
                        same = same && selectTwoPassCoarse(ts.size(), coarse) == oldCoarse(ts.size(), coarse);
                        for (size_t iAddr0 : { size_t(10), size_t(100) })
                            for (int aS : { 1, 15 }) {
                                same = same && selectSinglePass(ts, iAddr0, 100, 560, aS, sS, impact, lo, hi)
                                                   == oldSingle(ts, iAddr0, 100, 560, aS, sS, impact, lo, hi);
                                ++cases;
                            }
                    }
        char label[160];
        std::snprintf(label, sizeof label,
                      "face-on two-pass and single-pass selection identical to the pre-step-4 loops (%d cases)", cases);
        CHECK(label, same);
    }

    // (b) Zones: first zone decides, stride counted from the zone's first frame, both edges posed,
    // the rest at restStride.
    {
        // Frames 0..99 at exactly 10 ms from t = 0: frame i is at i × 10 ms.
        std::vector<int64_t> t10;
        for (int i = 0; i < 100; ++i) t10.push_back(int64_t(i) * 10000);
        const std::vector<PoseZone> zones = { { 405000, 605000, 1 },     // frames 41..60
                                              { 233000, 605000, 2 } };   // frames 24..40 (41.. taken)
        const std::vector<size_t> got = selectZoneSchedule(t10, 3, 97, zones, 4);
        std::vector<size_t> want;
        for (size_t i = 3; i <= 23; i += 4) want.push_back(i);           // rest run 3..23: 3,7,11,15,19,23
        for (size_t i = 24; i <= 40; i += 2) want.push_back(i);          // zone 2 run 24..40 (40 = edge)
        for (size_t i = 41; i <= 60; ++i) want.push_back(i);             // zone 1, every frame
        for (size_t i = 61; i < 97; i += 4) want.push_back(i);           // rest run 61..96: 61,65,…,93
        want.push_back(96);                                               // + the run's last frame
        CHECK("zones: first zone decides, stride from the zone's first frame, edges posed", got == want);

        // A zone whose stride does not divide it still poses its last frame; an off-grid index
        // phase changes nothing (the stride is relative, not i % stride).
        const std::vector<PoseZone> one = { { 500000, 570000, 3 } };      // frames 50..57
        const std::vector<size_t> g2 = selectZoneSchedule(t10, 50, 58, one, 4);
        CHECK("zones: stride 3 over 8 frames poses 50, 53, 56 and the edge 57",
              g2 == std::vector<size_t>({ 50, 53, 56, 57 }));
        CHECK("zones: empty range selects nothing", selectZoneSchedule(t10, 5, 5, zones, 4).empty());
    }

    // (c) Schedule B off the ladder, and a missing phase's fallback.
    {
        const DtlSchedule s = dtlZoneSchedule(1000000, 1300000, 2400000, 2200000);
        CHECK("schedule B: dense [P2 − 50, P8 + 150] at 1, back [P1 − 100, P2 − 50] at 2, rest 4",
              s.ok && s.zones.size() == 2 && s.zones[0].fromUs == 1250000 && s.zones[0].toUs == 2550000
                  && s.zones[0].stride == 1 && s.zones[1].fromUs == 900000 && s.zones[1].toUs == 1250000
                  && s.zones[1].stride == 2 && s.restStride == 4 && !s.p2Fallback && !s.p8Fallback);
        const DtlSchedule n2 = dtlZoneSchedule(1000000, -1, 2400000, 2200000);
        CHECK("no P2 ⇒ P1 + 250 ms (dense from 1.2 s)",
              n2.ok && n2.p2Fallback && n2.zones[0].fromUs == 1200000 && n2.zones[1].toUs == 1200000);
        const DtlSchedule n8 = dtlZoneSchedule(1000000, 1300000, -1, 2200000);
        CHECK("no P8 ⇒ impact + 150 ms (dense to 2.5 s)",
              n8.ok && n8.p8Fallback && n8.zones[0].toUs == 2500000);
        CHECK("no P1 ⇒ refused (every frame)", !dtlZoneSchedule(-1, 1300000, 2400000, 2200000).ok);
        CHECK("no P8 and no impact ⇒ refused", !dtlZoneSchedule(1000000, 1300000, -1, -1).ok);

        // On the ring: frame count and the dense zone fully posed.
        const DtlSchedule a = dtlZoneSchedule(2000000, 2300000, 3300000, 3150000);
        const std::vector<size_t> pick = selectZoneSchedule(ts, 0, ts.size(), a.zones, a.restStride);
        size_t denseIn = 0, densePicked = 0;
        for (size_t i = 0; i < ts.size(); ++i)
            if (ts[i] >= a.zones[0].fromUs && ts[i] <= a.zones[0].toUs) ++denseIn;
        for (size_t i : pick)
            if (ts[i] >= a.zones[0].fromUs && ts[i] <= a.zones[0].toUs) ++densePicked;
        std::printf("      ring: %zu of %zu frames posed under schedule B (%zu dense)\n", pick.size(), ts.size(),
                    densePicked);
        CHECK("schedule B on a 4 s ring: every dense frame posed, under 0.6 of the ring overall",
              denseIn > 0 && densePicked == denseIn && pick.size() * 5 < ts.size() * 3);
    }

    // (d) The consumer bracket (bracketAt): OFF is the old 12 / 6 ms rule; with localGap a thinned
    // track pairs at its own spacing and still refuses a dropout.
    {
        auto at = [](const std::vector<int64_t> &v) { return [&v](size_t i) { return v[i]; }; };
        const BracketRule off;
        BracketRule on;
        on.localGap = true;
        // stride 4 at 155 fps: 25.8 ms gaps; the face-on instant 13 ms from either neighbour.
        std::vector<int64_t> s4;
        for (int i = 0; i < 20; ++i) s4.push_back(int64_t(i) * 25806);
        const int64_t mid = s4[10] + 12903;
        CHECK("26 ms gaps, instant mid-gap: OFF finds nothing", !bracketAt(s4.size(), at(s4), mid, off).ok);
        const Bracket b4 = bracketAt(s4.size(), at(s4), mid, on);
        CHECK("26 ms gaps, instant mid-gap: localGap interpolates (w = 0.5)",
              b4.ok && b4.a == 10 && b4.b == 11 && std::fabs(b4.w - 0.5) < 1e-9);
        // a dropout in a stride-1 track: 6.45 ms frames, frames 10 and 11 missing (19 ms hole).
        std::vector<int64_t> s1;
        for (int i = 0; i < 30; ++i) if (i != 10 && i != 11) s1.push_back(int64_t(i) * 6452);
        const int64_t inHole = 10 * 6452 + 3226;
        CHECK("stride-1 dropout (19 ms hole): localGap still refuses it",
              !bracketAt(s1.size(), at(s1), inHole, on).ok && !bracketAt(s1.size(), at(s1), inHole, off).ok);
        // OFF is byte-for-byte the old rule over a sweep of instants on the ring.
        bool sameOff = true;
        for (int64_t t = ts.front() - 20000; t < ts.back() + 20000; t += 1733) {
            // the pre-step-4 lambda, verbatim in effect
            auto hi = std::lower_bound(ts.begin(), ts.end(), t);
            size_t a = 0, b = 0; double w = 0; bool ok = true;
            if (hi == ts.end()) a = b = ts.size() - 1;
            else if (hi == ts.begin()) a = b = 0;
            else {
                b = size_t(hi - ts.begin()); a = b - 1;
                const double span = double(ts[b] - ts[a]);
                if (span > 12000.0 || span <= 0) {
                    const size_t n = (t - ts[a]) <= (ts[b] - t) ? a : b;
                    if (std::llabs(ts[n] - t) > 6000) ok = false;
                    a = b = n;
                } else w = double(t - ts[a]) / span;
            }
            if (ok && a == b && std::llabs(ts[a] - t) > 6000) ok = false;
            const Bracket g = bracketAt(ts.size(), at(ts), t, off);
            sameOff = sameOff && g.ok == ok && (!ok || (g.a == a && g.b == b && g.w == w));
        }
        CHECK("bracket OFF == the pre-step-4 12 ms / 6 ms rule at every instant of the ring", sameOff);
    }
}

int main()
{
    // ── tuned defaults ──────────────────────────────────────────────────────
    std::printf("=== pose.* throughput defaults ===\n");
    CHECK("pose.sessionCache defaults ON", pinpoint::tuned::pose::kSessionCache);
    CHECK("pose.producerThreads defaults to auto (0)", pinpoint::tuned::pose::kProducerThreads == 0);
    CHECK("pose.queueDepth defaults to 8", pinpoint::tuned::pose::kQueueDepth == 8);

    // ── step 1: InstanceCache ───────────────────────────────────────────────
    std::printf("=== InstanceCache (session cache) ===\n");
    {
        InstanceCache<Key, FakeEstimator> cache;
        int builds = 0;
        auto make = [&](const std::string &m, int t, bool ready) {
            return [&builds, m, t, ready] {
                ++builds;
                auto e = std::make_unique<FakeEstimator>();
                e->model = m; e->threads = t; e->ready = ready;
                return e;
            };
        };
        auto keep = [](const FakeEstimator &e) { return e.ready; };

        const Key kB{ "vitpose-b.onnx", "coreml>cpu", 0 };
        const Key kL{ "vitpose-l.onnx", "coreml>cpu", 0 };
        const Key kB4{ "vitpose-b.onnx", "coreml>cpu", 4 };

        FakeEstimator *b1 = nullptr, *b2 = nullptr, *l1 = nullptr, *b4 = nullptr;
        { auto lease = cache.acquire(kB, make("b", 0, true), keep); b1 = lease.get();
          CHECK("first acquire is cached", lease.cached()); }
        { auto lease = cache.acquire(kB, make("b", 0, true), keep); b2 = lease.get(); }
        CHECK("same key → the SAME estimator", b1 && b1 == b2);
        CHECK("same key → built once", builds == 1);
        { auto lease = cache.acquire(kL, make("l", 0, true), keep); l1 = lease.get(); }
        { auto lease = cache.acquire(kB4, make("b", 4, true), keep); b4 = lease.get(); }
        CHECK("different model → a different estimator", l1 && l1 != b1);
        CHECK("different intra-op request → a different estimator", b4 && b4 != b1 && b4 != l1);
        CHECK("three keys → three entries, three builds", cache.size() == 3 && builds == 3);
        CHECK("the entry keeps what its factory built", b1->model == "b" && l1->model == "l" && b4->threads == 4);

        // A failed load is not cached: the next run retries (the L model may have
        // been downloaded meanwhile).
        const Key kBad{ "missing.onnx", "coreml>cpu", 0 };
        bool firstCached = true;
        { auto lease = cache.acquire(kBad, make("x", 0, false), keep);
          firstCached = lease.cached();
          CHECK("a rejected build is still handed to its caller", lease.get() && !lease->ready); }
        CHECK("a rejected build is not cached", !firstCached && cache.size() == 3);
        { auto lease = cache.acquire(kBad, make("x", 0, true), keep);
          CHECK("…and the next acquire builds again and caches", lease.cached() && lease->ready); }
        CHECK("rebuild counted", builds == 5 && cache.size() == 4);

        // One user at a time: a second acquire of a held entry waits for the lease.
        std::atomic<bool> secondGot{ false };
        std::thread other;
        {
            auto lease = cache.acquire(kB, make("b", 0, true), keep);
            other = std::thread([&] {
                auto l2 = cache.acquire(kB, make("b", 0, true), keep);
                secondGot = true;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CHECK("a held entry blocks a second user", !secondGot.load());
            // A different key is not blocked by it.
            auto lL = cache.acquire(kL, make("l", 0, true), keep);
            CHECK("another key's entry is not blocked", lL.get() == l1);
        }
        other.join();
        CHECK("the second user gets it once the lease ends", secondGot.load());
        CHECK("no rebuild for the waiters", builds == 5);
    }

    // ── step 2: runOrderedPipeline ──────────────────────────────────────────
    std::printf("=== runOrderedPipeline (producer pool) ===\n");
    {
        // A PoseRunner-like job list: sparse stride outside, dense inside.
        std::vector<size_t> frames;
        for (size_t i = 0; i < 600; ++i)
            if (i % 4 == 0 || (i >= 250 && i < 370))
                frames.push_back(i);

        const RunResult one  = runPool(frames, 1, 3);   // OFF: today's pipeline
        const RunResult four = runPool(frames, 4, 8);   // ON

        CHECK("N=1 poses every job", one.poses.size() == frames.size());
        CHECK("N=4 keypoint list is BITWISE identical to N=1", bitwiseEqual(one.poses, four.poses));
        bool inOrder = true;
        for (size_t k = 0; k < four.consumeOrder.size(); ++k)
            inOrder = inOrder && four.consumeOrder[k] == k;
        CHECK("N=4 consumes strictly in job order", inOrder && four.consumeOrder.size() == frames.size());
        CHECK("N=4 fetches strictly in frame order", four.fetchOrder == frames);
        CHECK("N=4 never fetches concurrently (single-reader source)", four.overlap == 0);
        // The measured window counts from fetch to the END of consume, so the
        // frame in inference (already popped, out of the pipeline's window) adds 1.
        CHECK("N=1 depth 3: ≤ 3 queued + 1 in inference", one.maxWindow <= 4);
        CHECK("N=4 depth 8: ≤ 8 queued + 1 in inference", four.maxWindow <= 9);

        // Depth smaller than the pool, and more producers than jobs.
        const RunResult tight = runPool(frames, 4, 2);
        CHECK("N=4 depth 2 still identical", bitwiseEqual(one.poses, tight.poses) && tight.maxWindow <= 3);
        const std::vector<size_t> few{ 3, 9, 27 };
        CHECK("more producers than jobs", bitwiseEqual(runPool(few, 1, 3).poses, runPool(few, 8, 8).poses));
        CHECK("zero jobs is a no-op", runPool({}, 4, 8).poses.empty());

        // Exceptions: a producer failure surfaces on the caller; a consumer
        // failure stops the pool; both return with every thread joined.
        bool threw = false;
        try {
            runOrderedPipeline<int, int>(
                100, 4, 8,
                [](size_t k) { return int(k); },
                [](size_t k, int &&v) { if (k == 37) throw std::runtime_error("decode"); return v; },
                [](size_t, int &&) {});
        } catch (const std::runtime_error &) { threw = true; }
        CHECK("a producer exception is rethrown on the consumer thread", threw);
        threw = false;
        size_t consumedBeforeThrow = 0;
        try {
            runOrderedPipeline<int, int>(
                100, 4, 8,
                [](size_t k) { return int(k); },
                [](size_t, int &&v) { return v; },
                [&](size_t k, int &&) { ++consumedBeforeThrow; if (k == 10) throw std::runtime_error("infer"); });
        } catch (const std::runtime_error &) { threw = true; }
        CHECK("a consumer exception propagates after the pool stops", threw && consumedBeforeThrow == 11);
    }

    // ── Step 3: BatchGatherer — the consumer's batches for pose.batchSize ─────
    // A 4-producer pipeline feeds a gatherer whose flush stands in for one Run()
    // over the batch: per item a deterministic "pose" of the frame index. The
    // concatenated results must be the one-frame sequence exactly (same order, same
    // values), every batch full but the last, the last the remainder.
    std::printf("\n-- BatchGatherer (step 3) --\n");
    {
        auto poseOf = [](size_t k) { return double(k) * 1.25 + std::sin(double(k)); };
        for (size_t nJobs : { size_t(0), size_t(1), size_t(7), size_t(64), size_t(209), size_t(441) }) {
            std::vector<double> single;
            for (size_t k = 0; k < nJobs; ++k)
                single.push_back(poseOf(k));
            for (size_t batch : { size_t(1), size_t(3), size_t(8), size_t(16) }) {
                std::vector<double>  got;
                std::vector<size_t>  sizes;
                BatchGatherer<size_t> g(batch, [&](std::vector<size_t> &&b) {
                    sizes.push_back(b.size());
                    for (size_t k : b)
                        got.push_back(poseOf(k));
                });
                runOrderedPipeline<size_t, size_t>(
                    nJobs, 4, std::max<size_t>(8, 2 * batch),
                    [](size_t k) { return k; },
                    [](size_t k, size_t &&v) {
                        // Scramble the producers' finishing order.
                        std::this_thread::sleep_for(std::chrono::microseconds((k * 37) % 50));
                        return v;
                    },
                    [&](size_t, size_t &&v) { g.push(std::move(v)); });
                g.finish();
                bool shapeOk = true;
                for (size_t i = 0; i + 1 < sizes.size(); ++i)
                    shapeOk = shapeOk && sizes[i] == batch;
                const size_t rem = nJobs % batch;
                if (!sizes.empty())
                    shapeOk = shapeOk && sizes.back() == (rem ? rem : batch);
                const size_t nBatches = (nJobs + batch - 1) / batch;
                char label[128];
                std::snprintf(label, sizeof label,
                              "%zu jobs, batch %zu: same sequence as one frame per Run()", nJobs, batch);
                CHECK(label, single.size() == got.size()
                             && (single.empty() || std::memcmp(single.data(), got.data(),
                                                               single.size() * sizeof(double)) == 0));
                std::snprintf(label, sizeof label,
                              "%zu jobs, batch %zu: %zu batches, full but the last", nJobs, batch, nBatches);
                CHECK(label, shapeOk && sizes.size() == nBatches && g.pending() == 0);
            }
        }
        // finish() on an empty gatherer flushes nothing; batch 0 behaves as 1.
        size_t flushes = 0;
        BatchGatherer<int> z(0, [&](std::vector<int> &&) { ++flushes; });
        z.finish();
        z.push(1);
        CHECK("empty finish() flushes nothing; batch 0 == batch 1", flushes == 1 && z.batch() == 1);
    }

    testFrameSchedules();

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
