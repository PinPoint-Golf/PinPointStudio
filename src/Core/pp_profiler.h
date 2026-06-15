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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ── Resource profiler ─────────────────────────────────────────────────────────
// A sibling of PpMessageLog: a thread-safe singleton that accumulates named
// timing / call / memory counters, surfaced in the resource monitor and dumped
// into PpMessageLog (and therefore the exported log) on a cadence.
//
// Two tiers, mirroring pp_debug.h's "always-on ppInfo vs dev-only ppDebug":
//
//   BASELINE  (PINPOINT_PROFILE_BASELINE, default ON — ships in release during
//             alpha/beta).  Always collects, ignores the runtime toggle.  Cheap:
//             a handful of atomic adds per call.  This is the "standard" data —
//             a small fixed set of structural scopes, memory categories, and the
//             process/thread gauge sampled by the controller.
//
//   DEEP      (PINPOINT_PROFILE, default OFF, AND a runtime toggle, default off).
//             Fine-grained scopes plus per-scope CPU time (an extra thread-clock
//             read per scope).  Flip on from the monitor while reproducing an
//             issue; off, the timer is never even constructed.
//
// Both compile-time flags are set in CMakeLists.txt.  When a tier's flag is 0 its
// macros expand to ((void)0): no binary footprint, no runtime cost.
//
// Hot-path discipline: every accumulate is a relaxed atomic on a record whose
// address is interned once per call site via a function-local static, so the
// steady-state cost is a pointer deref + 2–4 relaxed atomics with no locking and
// no hashing.  Nothing here takes a mutex on a producer path.  Snapshotting and
// first-touch interning are the only locked operations and happen off the hot
// path.  Scope time is monotonic WALL ns (what you wait for); the process gauge
// is the CPU-saturation signal.  [seam] If a single scope is hammered from many
// threads and the shared cache line contends, shard ScopeRecord per thread and
// merge in snapshot() — the public surface does not change.

#ifndef PINPOINT_PROFILE_BASELINE
#  define PINPOINT_PROFILE_BASELINE 1
#endif
#ifndef PINPOINT_PROFILE
#  define PINPOINT_PROFILE 0
#endif

namespace pinpoint::profiling {

// One interned timing/call counter.  Non-movable (holds atomics); the registry
// owns it via unique_ptr so its address is stable for the life of the process —
// callers cache a raw ScopeRecord* and must rely on that stability.
struct ScopeRecord {
    const char *name = nullptr;            // points at a static string literal
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> wall_ns_total{0};
    std::atomic<uint64_t> wall_ns_min{UINT64_MAX};
    std::atomic<uint64_t> wall_ns_max{0};
    std::atomic<uint64_t> cpu_ns_total{0}; // deep-tier only; stays 0 in baseline
};

// One interned memory category (current + peak bytes).  current_bytes is signed
// so paired ADD/SUB can briefly cross zero under races without underflowing.
struct MemRecord {
    const char *name = nullptr;
    std::atomic<int64_t> current_bytes{0};
    std::atomic<int64_t> peak_bytes{0};
};

inline void atomicMaxU(std::atomic<uint64_t> &a, uint64_t v)
{
    uint64_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

inline void atomicMinU(std::atomic<uint64_t> &a, uint64_t v)
{
    uint64_t cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

inline void memAdd(MemRecord *r, int64_t delta)
{
    const int64_t now = r->current_bytes.fetch_add(delta, std::memory_order_relaxed) + delta;
    if (delta > 0) {
        int64_t cur = r->peak_bytes.load(std::memory_order_relaxed);
        while (now > cur && !r->peak_bytes.compare_exchange_weak(cur, now, std::memory_order_relaxed)) {}
    }
}

// RAII scope timer.  Always records wall time; when deep, also attributes the
// calling thread's CPU time over the scope.  Defined in the .cpp so the header
// stays free of the OS-metrics include.
class ScopeTimer {
public:
    ScopeTimer(ScopeRecord *rec, bool deep);
    ~ScopeTimer();
    ScopeTimer(const ScopeTimer &) = delete;
    ScopeTimer &operator=(const ScopeTimer &) = delete;

private:
    ScopeRecord                          *m_rec;
    bool                                  m_deep;
    std::chrono::steady_clock::time_point m_wall0;
    uint64_t                              m_cpu0Ns = 0;
};

// RAII memory reservation: add `bytes` on entry, release on exit.  For buffers
// whose lifetime matches a scope (frame pools, encode staging, ORT arenas).
class MemScope {
public:
    MemScope(MemRecord *rec, int64_t bytes) : m_rec(rec), m_bytes(bytes) { memAdd(m_rec, m_bytes); }
    ~MemScope() { memAdd(m_rec, -m_bytes); }
    MemScope(const MemScope &) = delete;
    MemScope &operator=(const MemScope &) = delete;

private:
    MemRecord *m_rec;
    int64_t    m_bytes;
};

class Profiler {
public:
    static Profiler &instance();

    // Intern a name to a stable record.  Thread-safe; meant to be called once
    // per call site (cached behind a function-local static by the macros).
    ScopeRecord *internScope(const char *name);
    MemRecord   *internMem(const char *name);

    // Deep-tier runtime gate (the UI toggle).  Default off.
    static bool deepEnabled() noexcept { return s_deepEnabled.load(std::memory_order_relaxed); }
    static void setDeepEnabled(bool on);   // logs the transition via ppInfo

    struct ScopeStat {
        std::string name;
        uint64_t    calls;
        uint64_t    wall_ns_total;
        uint64_t    wall_ns_min;   // 0 when calls == 0
        uint64_t    wall_ns_max;
        uint64_t    cpu_ns_total;
    };
    struct MemStat {
        std::string name;
        int64_t     current_bytes;
        int64_t     peak_bytes;
    };
    struct Snapshot {
        std::vector<ScopeStat> scopes;   // registration order
        std::vector<MemStat>   memory;   // registration order
        bool                   deep_enabled;
    };

    // Copy out the current counters (relaxed loads — a monitor tolerates a tick
    // of staleness).  Locks only against concurrent interning, never the adds.
    Snapshot snapshot() const;

    // Zero the cumulative scope counters for a fresh measurement window; memory
    // current is left intact (it reflects live allocations) and only its peak is
    // pulled down to current.  Also resets the OS gauge peaks/baselines.  Call at
    // session start so the standard profile is per-session.
    void reset();

    // Emit a compact baseline summary through ppInfo so it lands in PpMessageLog
    // and the exported log file.  Driven every 60 s and at session end.  Scopes
    // are capped to the top N by total wall time to keep the log readable.
    void dumpToLog() const;

private:
    Profiler() = default;

    mutable std::mutex                              m_mutex;
    std::vector<std::unique_ptr<ScopeRecord>>       m_scopes;
    std::vector<std::unique_ptr<MemRecord>>         m_mem;
    std::unordered_map<std::string, ScopeRecord *>  m_scopeIndex;
    std::unordered_map<std::string, MemRecord *>    m_memIndex;

    static std::atomic<bool> s_deepEnabled;
};

} // namespace pinpoint::profiling

// ── Macro plumbing ────────────────────────────────────────────────────────────

#define PP_PROF_CONCAT_(a, b) a##b
#define PP_PROF_CONCAT(a, b)  PP_PROF_CONCAT_(a, b)
#define PP_PROF_UNIQ(base)    PP_PROF_CONCAT(base, __LINE__)

// ── Baseline tier (always-on when compiled) ───────────────────────────────────
#if PINPOINT_PROFILE_BASELINE

// Time the enclosing scope; on exit add 1 call + wall ns to the named record.
#  define PP_PROFILE_SCOPE(name)                                                  \
    static ::pinpoint::profiling::ScopeRecord *PP_PROF_UNIQ(pp_scope_rec_) =      \
        ::pinpoint::profiling::Profiler::instance().internScope(name);            \
    ::pinpoint::profiling::ScopeTimer PP_PROF_UNIQ(pp_scope_timer_)(              \
        PP_PROF_UNIQ(pp_scope_rec_), false)

// Bump a call counter only (events you don't want to time).
#  define PP_PROFILE_COUNT(name)                                                  \
    do {                                                                          \
        static ::pinpoint::profiling::ScopeRecord *pp_c_rec_ =                    \
            ::pinpoint::profiling::Profiler::instance().internScope(name);        \
        pp_c_rec_->calls.fetch_add(1, std::memory_order_relaxed);                \
    } while (0)

// Attribute a current-bytes delta to a memory category.
#  define PP_PROFILE_MEM_ADD(cat, bytes)                                          \
    do {                                                                          \
        static ::pinpoint::profiling::MemRecord *pp_m_rec_ =                      \
            ::pinpoint::profiling::Profiler::instance().internMem(cat);           \
        ::pinpoint::profiling::memAdd(pp_m_rec_, (int64_t)(bytes));               \
    } while (0)
#  define PP_PROFILE_MEM_SUB(cat, bytes)                                          \
    do {                                                                          \
        static ::pinpoint::profiling::MemRecord *pp_m_rec_ =                      \
            ::pinpoint::profiling::Profiler::instance().internMem(cat);           \
        ::pinpoint::profiling::memAdd(pp_m_rec_, -(int64_t)(bytes));              \
    } while (0)

// Scoped reservation: add on entry, release on exit.
#  define PP_PROFILE_MEM_SCOPE(cat, bytes)                                        \
    static ::pinpoint::profiling::MemRecord *PP_PROF_UNIQ(pp_ms_rec_) =           \
        ::pinpoint::profiling::Profiler::instance().internMem(cat);               \
    ::pinpoint::profiling::MemScope PP_PROF_UNIQ(pp_ms_)(                         \
        PP_PROF_UNIQ(pp_ms_rec_), (int64_t)(bytes))

#else
#  define PP_PROFILE_SCOPE(name)         ((void)0)
#  define PP_PROFILE_COUNT(name)         ((void)0)
#  define PP_PROFILE_MEM_ADD(cat, bytes) ((void)0)
#  define PP_PROFILE_MEM_SUB(cat, bytes) ((void)0)
#  define PP_PROFILE_MEM_SCOPE(cat, bytes) ((void)0)
#endif // PINPOINT_PROFILE_BASELINE

// ── Deep tier (compiled in only with PINPOINT_PROFILE, gated by the toggle) ────
#if PINPOINT_PROFILE

// Like PP_PROFILE_SCOPE but also attributes per-scope CPU time; the timer is
// constructed only while the runtime toggle is on, so disabled cost is one
// relaxed atomic load.  The name is still interned (so it lists, idle, in the UI
// when disabled).
#  define PP_PROFILE_SCOPE_DEEP(name)                                             \
    static ::pinpoint::profiling::ScopeRecord *PP_PROF_UNIQ(pp_dscope_rec_) =     \
        ::pinpoint::profiling::Profiler::instance().internScope(name);            \
    ::std::optional<::pinpoint::profiling::ScopeTimer> PP_PROF_UNIQ(pp_dscope_t_); \
    if (::pinpoint::profiling::Profiler::deepEnabled())                           \
        PP_PROF_UNIQ(pp_dscope_t_).emplace(PP_PROF_UNIQ(pp_dscope_rec_), true)

#  define PP_PROFILE_COUNT_DEEP(name)                                             \
    do {                                                                          \
        if (::pinpoint::profiling::Profiler::deepEnabled()) {                     \
            static ::pinpoint::profiling::ScopeRecord *pp_dc_rec_ =               \
                ::pinpoint::profiling::Profiler::instance().internScope(name);    \
            pp_dc_rec_->calls.fetch_add(1, std::memory_order_relaxed);           \
        }                                                                         \
    } while (0)

#else
#  define PP_PROFILE_SCOPE_DEEP(name) ((void)0)
#  define PP_PROFILE_COUNT_DEEP(name) ((void)0)
#endif // PINPOINT_PROFILE
