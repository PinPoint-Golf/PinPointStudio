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

#include "pp_profiler.h"
#include "pp_os_metrics.h"
#include "pp_debug.h"
#include "PpStatsLog.h"

#include <QString>

#include <algorithm>
#include <cstdio>

namespace pinpoint::profiling {

std::atomic<bool> Profiler::s_deepEnabled{false};

Profiler &Profiler::instance()
{
    static Profiler s_instance;
    return s_instance;
}

ScopeRecord *Profiler::internScope(const char *name)
{
    const std::string key(name ? name : "");
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_scopeIndex.find(key); it != m_scopeIndex.end())
        return it->second;
    auto rec  = std::make_unique<ScopeRecord>();
    rec->name = name;                 // assumed static literal (macro call sites)
    ScopeRecord *raw = rec.get();
    m_scopes.push_back(std::move(rec));
    m_scopeIndex.emplace(key, raw);
    return raw;
}

ScopeRecord *Profiler::internScopeCopied(const std::string &name)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_scopeIndex.find(name); it != m_scopeIndex.end())
        return it->second;
    // Own the name — a deque never invalidates references to existing elements,
    // so owned.c_str() stays valid for the life of the process (unlike
    // internScope, whose char* is assumed to be a static literal).
    const std::string &owned = m_ownedNames.emplace_back(name);
    auto rec  = std::make_unique<ScopeRecord>();
    rec->name = owned.c_str();
    ScopeRecord *raw = rec.get();
    m_scopes.push_back(std::move(rec));
    m_scopeIndex.emplace(name, raw);
    return raw;
}

MemRecord *Profiler::internMem(const char *name)
{
    const std::string key(name ? name : "");
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_memIndex.find(key); it != m_memIndex.end())
        return it->second;
    auto rec  = std::make_unique<MemRecord>();
    rec->name = name;
    MemRecord *raw = rec.get();
    m_mem.push_back(std::move(rec));
    m_memIndex.emplace(key, raw);
    return raw;
}

void Profiler::setDeepEnabled(bool on)
{
    const bool was = s_deepEnabled.exchange(on, std::memory_order_relaxed);
    if (was != on)
        ppInfo() << "[Profiler] deep profiling" << (on ? "enabled" : "disabled");
}

Profiler::Snapshot Profiler::snapshot() const
{
    Snapshot snap;
    snap.deep_enabled = deepEnabled();

    std::lock_guard<std::mutex> lk(m_mutex);
    snap.scopes.reserve(m_scopes.size());
    for (const auto &r : m_scopes) {
        const uint64_t calls = r->calls.load(std::memory_order_relaxed);
        ScopeStat s;
        s.name          = r->name ? r->name : "";
        s.calls         = calls;
        s.wall_ns_total = r->wall_ns_total.load(std::memory_order_relaxed);
        const uint64_t mn = r->wall_ns_min.load(std::memory_order_relaxed);
        s.wall_ns_min   = (calls == 0 || mn == UINT64_MAX) ? 0 : mn;
        s.wall_ns_max   = r->wall_ns_max.load(std::memory_order_relaxed);
        s.cpu_ns_total  = r->cpu_ns_total.load(std::memory_order_relaxed);
        snap.scopes.push_back(std::move(s));
    }
    snap.memory.reserve(m_mem.size());
    for (const auto &r : m_mem) {
        MemStat m;
        m.name          = r->name ? r->name : "";
        m.current_bytes = r->current_bytes.load(std::memory_order_relaxed);
        m.peak_bytes    = r->peak_bytes.load(std::memory_order_relaxed);
        snap.memory.push_back(std::move(m));
    }
    return snap;
}

void Profiler::reset()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto &r : m_scopes) {
            r->calls.store(0, std::memory_order_relaxed);
            r->wall_ns_total.store(0, std::memory_order_relaxed);
            r->wall_ns_min.store(UINT64_MAX, std::memory_order_relaxed);
            r->wall_ns_max.store(0, std::memory_order_relaxed);
            r->cpu_ns_total.store(0, std::memory_order_relaxed);
        }
        for (const auto &r : m_mem) {
            // Leave current_bytes — it tracks live allocations.  Pull the peak
            // down to the current level so the watermark starts fresh.
            const int64_t cur = r->current_bytes.load(std::memory_order_relaxed);
            r->peak_bytes.store(cur, std::memory_order_relaxed);
        }
    }
    osmetrics::reset();
}

// ── Compact formatters for the log dump (mirror the resource monitor style) ───
namespace {

std::string fmtBytes(int64_t n)
{
    char buf[48];
    const double v = double(n);
    if (n >= 1073741824LL || n <= -1073741824LL) snprintf(buf, sizeof buf, "%.2f GB", v / 1073741824.0);
    else if (n >= 1048576LL || n <= -1048576LL)  snprintf(buf, sizeof buf, "%.1f MB", v / 1048576.0);
    else if (n >= 1024LL    || n <= -1024LL)      snprintf(buf, sizeof buf, "%.0f KB", v / 1024.0);
    else                                          snprintf(buf, sizeof buf, "%lld B", (long long)n);
    return buf;
}

std::string fmtMs(uint64_t ns)
{
    char buf[48];
    if (ns >= 1000000000ull) snprintf(buf, sizeof buf, "%.2f s",  ns / 1e9);
    else if (ns >= 1000000ull) snprintf(buf, sizeof buf, "%.1f ms", ns / 1e6);
    else if (ns >= 1000ull)    snprintf(buf, sizeof buf, "%.1f us", ns / 1e3);
    else                       snprintf(buf, sizeof buf, "%llu ns", (unsigned long long)ns);
    return buf;
}

std::string fmtPct(double p)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f%%", p);
    return buf;
}

} // namespace

void Profiler::dumpToLog() const
{
    // Routes into the dedicated PpStatsLog ring — NOT the main PpMessageLog — so
    // the periodic/session-end summary never pollutes the application log or its
    // Export.  The profiler controller surfaces this ring in the monitor's STATS
    // HISTORY section (own filter + export).  Each line carries a coarse category
    // so the UI can filter without parsing text.
    const Snapshot snap = snapshot();
    const osmetrics::ProcessSample proc = osmetrics::sampleProcess();

    PpStatsLog *log = PpStatsLog::instance();

    log->append(QStringLiteral("GAUGE"),
        QStringLiteral("RSS %1 (peak %2)  CPU %3 (peak %4)")
            .arg(QString::fromStdString(fmtBytes(int64_t(proc.rss_bytes))),
                 QString::fromStdString(fmtBytes(int64_t(proc.peak_rss_bytes))),
                 QString::fromStdString(fmtPct(proc.cpu_percent)),
                 QString::fromStdString(fmtPct(proc.peak_cpu_percent))));

    const auto threads = osmetrics::sampleThreads();
    for (const auto &t : threads) {
        log->append(QStringLiteral("THREAD"),
            QStringLiteral("%1  %2")
                .arg(QString::fromStdString(t.name),
                     QString::fromStdString(fmtPct(t.cpu_percent))));
    }

    // Top scopes by total wall time.
    std::vector<const ScopeStat *> top;
    top.reserve(snap.scopes.size());
    for (const auto &s : snap.scopes)
        if (s.calls > 0) top.push_back(&s);
    std::sort(top.begin(), top.end(), [](const ScopeStat *a, const ScopeStat *b) {
        return a->wall_ns_total > b->wall_ns_total;
    });
    constexpr size_t kMaxScopeLines = 20;
    const size_t shown = std::min(top.size(), kMaxScopeLines);
    for (size_t i = 0; i < shown; ++i) {
        const ScopeStat *s = top[i];
        const uint64_t avg = s->calls ? s->wall_ns_total / s->calls : 0;
        log->append(QStringLiteral("SCOPE"),
            QStringLiteral("%1  n=%2 total=%3 avg=%4 max=%5")
                .arg(QString::fromStdString(s->name))
                .arg(qulonglong(s->calls))
                .arg(QString::fromStdString(fmtMs(s->wall_ns_total)),
                     QString::fromStdString(fmtMs(avg)),
                     QString::fromStdString(fmtMs(s->wall_ns_max))));
    }

    for (const auto &m : snap.memory) {
        if (m.current_bytes == 0 && m.peak_bytes == 0)
            continue;
        log->append(QStringLiteral("MEM"),
            QStringLiteral("%1  cur=%2 peak=%3")
                .arg(QString::fromStdString(m.name),
                     QString::fromStdString(fmtBytes(m.current_bytes)),
                     QString::fromStdString(fmtBytes(m.peak_bytes))));
    }
}

// ── ScopeTimer (defined here so the header avoids the OS-metrics include) ──────

ScopeTimer::ScopeTimer(ScopeRecord *rec, bool deep)
    : m_rec(rec)
    , m_deep(deep)
    , m_wall0(std::chrono::steady_clock::now())
{
    if (m_deep)
        m_cpu0Ns = osmetrics::threadCpuNowNs();
}

ScopeTimer::~ScopeTimer()
{
    const uint64_t ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - m_wall0).count());

    m_rec->calls.fetch_add(1, std::memory_order_relaxed);
    m_rec->wall_ns_total.fetch_add(ns, std::memory_order_relaxed);
    atomicMaxU(m_rec->wall_ns_max, ns);
    atomicMinU(m_rec->wall_ns_min, ns);

    if (m_deep) {
        const uint64_t cpuNow = osmetrics::threadCpuNowNs();
        if (cpuNow >= m_cpu0Ns)
            m_rec->cpu_ns_total.fetch_add(cpuNow - m_cpu0Ns, std::memory_order_relaxed);
    }
}

} // namespace pinpoint::profiling
