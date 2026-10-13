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

// The two pieces of plumbing behind pose_inference_performance_plan.md steps 1
// and 2, kept free of ORT/OpenCV/Qt so they are unit-testable with fakes
// (src/Analysis/tests/pose_pipeline_test.cpp). PoseRunner instantiates both.
//
//   InstanceCache<Key, T>   — step 1: one loaded object per distinct key for the
//                             life of the process. PoseRunner keeps its ViTPose
//                             estimator here, so the ORT session (a 360 MB model
//                             load + EP init, rebuilt on EVERY PoseRunner::run()
//                             before — face-on and DTL, every shot) is built once.
//   runOrderedPipeline(...) — step 2: N producer threads doing the per-frame CPU
//                             work (demosaic, resize, normalise) feeding ONE
//                             consumer (inference) in exact job order through a
//                             bounded reorder window. One producer and depth 3 is
//                             the pre-step-2 pipeline exactly.

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

// ── Step 1: process-wide keyed instance cache ───────────────────────────────
//
// THE RULE: an entry is used by ONE thread at a time. acquire() returns a Lease
// that holds the entry's own mutex for as long as the lease lives, so a second
// PoseRunner::run() on the same model (another camera, another shot, another
// analysis worker) waits for the first to finish rather than sharing the ORT
// session and the estimator's per-run state (decode mode, whole-body flag, the
// last result, the DARK scratch). Inference is sequential today — face-on then
// DTL, one shot after another — so the wait costs nothing in practice.
//
// Creation runs under the cache-wide mutex: two first callers for the same key
// build ONE object, not two (a model load is ~1 s and 360 MB–1.2 GB). A factory
// result the `keep` predicate rejects (model missing, load failed) is handed to
// the caller for this run only and NOT cached, so the next run retries — the L
// model downloaded mid-session is picked up without a restart.
template <class Key, class T>
class InstanceCache {
    struct Entry {
        std::mutex         use;   // held by the Lease for the whole run
        std::unique_ptr<T> obj;
    };

public:
    class Lease {
    public:
        Lease() = default;
        Lease(Lease &&) noexcept = default;
        Lease &operator=(Lease &&) noexcept = default;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;

        T   *get() const { return m_obj; }
        T   &operator*() const { return *m_obj; }
        T   *operator->() const { return m_obj; }
        bool cached() const { return bool(m_entry); }   // false: a rejected one-off

    private:
        friend class InstanceCache;
        std::shared_ptr<Entry>       m_entry;   // keeps a cached entry alive
        std::unique_lock<std::mutex> m_lock;    // the one-user-at-a-time rule
        std::unique_ptr<T>           m_oneOff;  // a rejected (uncached) object
        T                           *m_obj = nullptr;
    };

    template <class Factory, class Keep>
    Lease acquire(const Key &key, Factory &&make, Keep &&keep)
    {
        std::shared_ptr<Entry> e;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_entries.find(key);
            if (it != m_entries.end()) {
                e = it->second;
            } else {
                std::unique_ptr<T> obj = make();
                Lease l;
                if (!obj || !keep(*obj)) {
                    l.m_oneOff = std::move(obj);
                    l.m_obj    = l.m_oneOff.get();
                    return l;
                }
                e = std::make_shared<Entry>();
                e->obj = std::move(obj);
                m_entries.emplace(key, e);
            }
        }
        Lease l;
        l.m_lock  = std::unique_lock<std::mutex>(e->use);   // outside m_mutex: waiting
        l.m_obj   = e->obj.get();                           // here must not block
        l.m_entry = std::move(e);                           // other keys' lookups
        return l;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_entries.size();
    }

private:
    mutable std::mutex                       m_mutex;
    std::map<Key, std::shared_ptr<Entry>>    m_entries;
};

// ── Step 2: ordered producer pool ───────────────────────────────────────────
//
// Jobs 0..nJobs-1. Each producer, in a loop:
//   fetch(k)              — SERIALISED and in job order (k = 0, 1, 2, …): the
//                           frame source is single-reader by contract (one
//                           resident frame per source; the MP4 reader decodes
//                           forward and rewinds to frame 0 on any back-seek), so
//                           the fetch must copy out what it needs before the
//                           next fetch;
//   process(k, fetched)   — CONCURRENT across producers: the demosaic + resize +
//                           normalise that dominate the CPU side (~5–8 ms a
//                           688×1024 BayerRG8 frame against a few ms of Run()).
// consume(k, item) runs on the CALLING thread strictly in k order.
//
// Bounded: job k is fetched only while k < (jobs handed to consume) + depth, so at
// most `depth` items exist between fetch and consume — memory is depth × one
// 590 KB input tensor. With one producer that is exactly the old FIFO's
// "push while queue.size() < 3" (an item leaves the window when the consumer
// pops it, before it runs inference), so producers = 1, depth = 3 reproduces the
// pre-step-2 schedule as well as its output.
//
// An exception from fetch/process is caught on the producer, stops the pool and
// is rethrown on the calling thread; one from consume stops and joins the pool
// before it propagates. No thread outlives the call.
template <class Fetched, class Item>
void runOrderedPipeline(size_t nJobs, int producers, size_t depth,
                        const std::function<Fetched(size_t)>          &fetch,
                        const std::function<Item(size_t, Fetched &&)> &process,
                        const std::function<void(size_t, Item &&)>    &consume)
{
    if (nJobs == 0)
        return;
    depth = std::max<size_t>(1, depth);
    const size_t nThreads = std::min<size_t>(std::max(1, producers), nJobs);

    std::mutex              mtx;           // window + ready map + flags
    std::mutex              fetchMtx;      // serialises fetch() in k order
    std::condition_variable cvSpace, cvReady;
    std::map<size_t, Item>  ready;         // processed, awaiting their turn
    size_t                  nextFetch   = 0;
    size_t                  nextConsume = 0;   // jobs popped by the consumer
    bool                    stop        = false;
    std::exception_ptr      error;

    auto worker = [&] {
        try {
            for (;;) {
                // Holding fetchMtx across the window wait AND the fetch is what
                // makes fetch order == k order: whoever takes k fetches k before
                // anyone can take k + 1.
                std::unique_lock<std::mutex> fl(fetchMtx);
                size_t k;
                {
                    std::unique_lock<std::mutex> lk(mtx);
                    cvSpace.wait(lk, [&] {
                        return stop || nextFetch >= nJobs || nextFetch < nextConsume + depth;
                    });
                    if (stop || nextFetch >= nJobs)
                        return;
                    k = nextFetch++;
                }
                Fetched f = fetch(k);
                fl.unlock();
                Item item = process(k, std::move(f));
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    ready.emplace(k, std::move(item));
                }
                cvReady.notify_one();
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (!error)
                    error = std::current_exception();
                stop = true;
            }
            cvSpace.notify_all();
            cvReady.notify_all();
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(nThreads);

    // Stop and join on EVERY exit (normal drain, a consumer exception, a
    // producer exception rethrown below) — never a detached thread, never a
    // producer parked on a full window.
    struct Joiner {
        std::vector<std::thread> &pool;
        std::mutex               &m;
        std::condition_variable  &space;
        bool                     &stop;
        ~Joiner()
        {
            {
                std::lock_guard<std::mutex> lk(m);
                stop = true;
            }
            space.notify_all();
            for (std::thread &t : pool)
                if (t.joinable())
                    t.join();
        }
    } joiner{pool, mtx, cvSpace, stop};

    for (size_t i = 0; i < nThreads; ++i)
        pool.emplace_back(worker);

    for (size_t k = 0; k < nJobs; ++k) {
        Item item;
        {
            std::unique_lock<std::mutex> lk(mtx);
            cvReady.wait(lk, [&] { return error || ready.count(k) != 0; });
            if (error)
                std::rethrow_exception(error);
            auto it = ready.find(k);
            item = std::move(it->second);
            ready.erase(it);
            nextConsume = k + 1;
        }
        cvSpace.notify_all();
        consume(k, std::move(item));
    }
}

// ── Step 3: batch gathering on the consumer ────────────────────────────────
//
// The consumer of runOrderedPipeline receives frames one at a time in job order;
// batched inference (pose.batchSize) wants B of them per Run(). push() appends in
// arrival order and hands the full batch to `flush` the moment it holds B; finish()
// hands over the partial remainder (a final batch of 1..B−1). Every item reaches
// flush exactly once, batches in push order and items within a batch in push
// order — the concatenation of the batches IS the pushed sequence. batch ≤ 1 makes
// every push its own batch. Single-threaded: the consumer thread owns it.
template <class Item>
class BatchGatherer {
public:
    using Flush = std::function<void(std::vector<Item> &&)>;

    BatchGatherer(size_t batch, Flush flush)
        : m_batch(std::max<size_t>(1, batch)), m_flush(std::move(flush))
    {
        m_pending.reserve(m_batch);
    }

    void push(Item &&item)
    {
        m_pending.push_back(std::move(item));
        if (m_pending.size() >= m_batch)
            flushPending();
    }
    void finish()
    {
        if (!m_pending.empty())
            flushPending();
    }
    size_t pending() const { return m_pending.size(); }
    size_t batch() const { return m_batch; }

private:
    void flushPending()
    {
        std::vector<Item> b;
        b.swap(m_pending);
        m_pending.reserve(m_batch);
        m_flush(std::move(b));
    }

    size_t            m_batch;
    Flush             m_flush;
    std::vector<Item> m_pending;
};

} // namespace pinpoint::analysis
