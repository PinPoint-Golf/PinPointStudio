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

// InferenceThread: ONE long-lived thread that runs the tasks handed to it, one at a time, in the
// order they arrived. The caller waits for its task's result; a task's exception is rethrown in
// the caller. Private to src/Pose: PoseRunner owns the one instance, and every cached ViTPose
// session is bound to it (PoseEstimatorViTPose::bindInferenceThread), so that thread is the only
// one that ever builds or runs those sessions.
//
// WHY A THREAD OF ITS OWN (step 3.7, build/refactor/step-3/3.7/NOTES-L.md; J-43, Mark, 21 Nov 2026;
// DEC-194, R-CON-5's exception): ONNX Runtime's CUDA provider does not reuse arena memory that Runs
// on an earlier thread freed. The cached session lives for the process, and the analysis DAG calls
// it from a new std::thread every shot (analysis_dag.h builds its pool per run), so its CUDA arena
// grew by 1 GiB about every 4 shots, filled the 16 GB card near shot 35 and pose then ran ~75 times
// slower. On the cabin PC's bench, the same calls from one persistent thread stayed flat (1452 MB
// over 40 shots) at the same pose time. This is the first instance of the long-lived-thread helper
// the platform refactor may generalise (docs/implementation/platform_refactor_assessment.md, its
// R-CON-5 proposal); it is deliberately not that helper.
//
// Re-entrancy: a task that calls submit() or call() on its own thread runs the new work directly,
// at once, so it can never wait on itself.
// Lifetime: stop() refuses new tasks, runs every task already queued, and joins. After it, submit()
// and call() throw std::runtime_error. The destructor stops. PoseRunner's instance is never
// destroyed; it is stopped at exit (PoseRunner::stopInferenceThread).
// No Qt, ORT or OpenCV here, so it is unit-tested with fakes (src/Pose/tests/inference_thread_test.cpp).

#include <cassert>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace pinpoint::pose {

class InferenceThread {
public:
    InferenceThread() : m_thread([this] { loop(); }) { m_id = m_thread.get_id(); }
    ~InferenceThread()
    {
        assert(!onThread() && "an InferenceThread is never destroyed by its own task");
        stop();
    }
    InferenceThread(const InferenceThread &) = delete;
    InferenceThread &operator=(const InferenceThread &) = delete;

    bool            onThread() const noexcept { return std::this_thread::get_id() == m_id; }
    std::thread::id id() const noexcept { return m_id; }

    // Queue f; its result (or exception) arrives through the future. On the thread itself, f runs
    // now and the future is ready on return. Throws std::runtime_error once stopped.
    template <class F>
    std::future<std::invoke_result_t<std::decay_t<F>>> submit(F &&f)
    {
        using R   = std::invoke_result_t<std::decay_t<F>>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> result = task->get_future();
        if (onThread()) {
            (*task)();
            return result;
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_stopping)
                throw std::runtime_error("pinpoint::pose::InferenceThread: stopped, task refused");
            m_queue.emplace_back([task] { (*task)(); });
        }
        m_cv.notify_one();
        return result;
    }

    // Run f on the thread and wait for it: its value, or its exception rethrown here. On the
    // thread itself, f runs directly.
    template <class F>
    std::invoke_result_t<std::decay_t<F>> call(F &&f)
    {
        if (onThread())
            return std::forward<F>(f)();
        return submit(std::forward<F>(f)).get();
    }

    // Refuse new tasks, finish the queued ones and join. Idempotent and safe from several threads.
    // Called by a task on the thread itself it can only refuse new work: the join is left to the
    // next stop() from another thread.
    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_stopping = true;
        }
        m_cv.notify_all();
        if (onThread())
            return;
        std::lock_guard<std::mutex> jl(m_joinMutex);
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    void loop()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this] { return m_stopping || !m_queue.empty(); });
                if (m_queue.empty())
                    return;   // stopping, and everything queued has run
                job = std::move(m_queue.front());
                m_queue.pop_front();
            }
            job();   // a packaged_task: an exception lands in the caller's future, never here
        }
    }

    std::mutex                        m_mutex;       // the queue and m_stopping
    std::condition_variable           m_cv;
    std::deque<std::function<void()>> m_queue;
    bool                              m_stopping = false;
    std::mutex                        m_joinMutex;   // one joiner at a time
    std::thread::id                   m_id;
    std::thread                       m_thread;      // LAST: it starts once every member above exists
};

} // namespace pinpoint::pose
