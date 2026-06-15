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

#include "profiler_controller.h"

#include "pp_os_metrics.h"
#include "pp_debug.h"
#include "PpStatsLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

// The four coarse stats categories, in display order — must match the tags
// dumpToLog() writes (GAUGE / THREAD / SCOPE / MEM).
const QStringList kCategoryNames = {
    QStringLiteral("GAUGE"), QStringLiteral("THREAD"),
    QStringLiteral("SCOPE"), QStringLiteral("MEM")
};

QString fmtCount(quint64 n)
{
    if (n >= 1000000) return QString::number(n / 1000000.0, 'f', 2) + QStringLiteral(" M");
    if (n >= 1000)    return QString::number(n / 1000.0,    'f', 1) + QStringLiteral(" K");
    return QString::number(n);
}

QString fmtBytes(qint64 n)
{
    const double v = double(n);
    if (n >= 1073741824LL || n <= -1073741824LL) return QString::number(v / 1073741824.0, 'f', 2) + QStringLiteral(" GB");
    if (n >= 1048576LL    || n <= -1048576LL)    return QString::number(v / 1048576.0,    'f', 1) + QStringLiteral(" MB");
    if (n >= 1024LL       || n <= -1024LL)       return QString::number(v / 1024.0,       'f', 0) + QStringLiteral(" KB");
    return QString::number(n) + QStringLiteral(" B");
}

QString fmtMs(quint64 ns)
{
    if (ns >= 1000000000ULL) return QString::number(ns / 1e9, 'f', 2) + QStringLiteral(" s");
    if (ns >= 1000000ULL)    return QString::number(ns / 1e6, 'f', 1) + QStringLiteral(" ms");
    if (ns >= 1000ULL)       return QString::number(ns / 1e3, 'f', 1) + QStringLiteral(" µs");
    return QString::number(ns) + QStringLiteral(" ns");
}

QString fmtPct(double p) { return QString::number(p, 'f', 1) + QStringLiteral("%"); }

} // namespace

ProfilerController::ProfilerController(QObject *parent)
    : QObject(parent)
{
    m_activeCategories = QSet<QString>(kCategoryNames.begin(), kCategoryNames.end());

    // The single gauge owner: a fixed-cadence app-lifetime timer that runs
    // regardless of screen visibility (baseline = collected as standard).
    m_sampler.setInterval(kSamplerIntervalMs);
    connect(&m_sampler, &QTimer::timeout, this, &ProfilerController::onSamplerTick);
    if (available()) {
        onSamplerTick();    // seed the OS baseline now (reports 0%); real % from next tick
        m_sampler.start();
    }
}

bool ProfilerController::deepEnabled() const
{
    return pinpoint::profiling::Profiler::deepEnabled();
}

void ProfilerController::setDeepEnabled(bool on)
{
    if (on == pinpoint::profiling::Profiler::deepEnabled())
        return;
    pinpoint::profiling::Profiler::setDeepEnabled(on);
    emit deepChanged();
}

QString ProfilerController::cpuPercentStr()     const { return fmtPct(m_cpuPct); }
QString ProfilerController::peakCpuPercentStr() const { return fmtPct(m_peakCpuPct); }
QString ProfilerController::rssBytesStr()       const { return fmtBytes(qint64(m_rss)); }
QString ProfilerController::peakRssBytesStr()   const { return fmtBytes(qint64(m_peakRss)); }

QVariantList ProfilerController::statsCategories() const
{
    QVariantList out;
    for (const QString &cat : kCategoryNames) {
        QVariantMap m;
        m[QStringLiteral("name")]   = cat;
        m[QStringLiteral("active")] = m_activeCategories.contains(cat);
        out.append(m);
    }
    return out;
}

// ── The single gauge owner ────────────────────────────────────────────────────
void ProfilerController::onSamplerTick()
{
    const pinpoint::osmetrics::ProcessSample proc = pinpoint::osmetrics::sampleProcess();
    const auto threads = pinpoint::osmetrics::sampleThreads();

    m_cpuPct     = proc.cpu_percent;
    m_peakCpuPct = proc.peak_cpu_percent;
    m_rss        = proc.rss_bytes;
    m_peakRss    = proc.peak_rss_bytes;

    m_threads.clear();
    for (const auto &t : threads) {
        QVariantMap m;
        m[QStringLiteral("name")]          = QString::fromStdString(t.name);
        m[QStringLiteral("cpuPercent")]    = t.cpu_percent;
        m[QStringLiteral("cpuPercentStr")] = fmtPct(t.cpu_percent);
        m_threads.append(m);
    }
    emit gaugeChanged();

    // 60 s baseline dump into the stats ring. dumpToLog() re-samples the gauge
    // (a sub-ms same-thread artifact perturbing the next delta) — the cached
    // gauge above already holds the clean 1 s value shown in the UI.
    if ((++m_tick % kDumpEveryTicks) == 0)
        pinpoint::profiling::Profiler::instance().dumpToLog();
}

// ── Screen-driven table refresh (500 ms while visible) ────────────────────────
void ProfilerController::refresh()
{
    const auto snap = pinpoint::profiling::Profiler::instance().snapshot();

    m_scopes.clear();
    for (const auto &s : snap.scopes) {
        const quint64 avg = s.calls ? s.wall_ns_total / s.calls : 0;
        const bool    hasCpu = s.cpu_ns_total > 0;
        QVariantMap m;
        m[QStringLiteral("name")]     = QString::fromStdString(s.name);
        m[QStringLiteral("calls")]    = quint64(s.calls);
        m[QStringLiteral("callsStr")] = fmtCount(s.calls);
        m[QStringLiteral("totalStr")] = fmtMs(s.wall_ns_total);
        m[QStringLiteral("avgStr")]   = fmtMs(avg);
        m[QStringLiteral("minStr")]   = fmtMs(s.wall_ns_min);
        m[QStringLiteral("maxStr")]   = fmtMs(s.wall_ns_max);
        m[QStringLiteral("cpuStr")]   = hasCpu ? fmtMs(s.cpu_ns_total) : QString();
        m[QStringLiteral("deep")]     = hasCpu;
        m_scopes.append(m);
    }

    m_memory.clear();
    for (const auto &mem : snap.memory) {
        QVariantMap m;
        m[QStringLiteral("name")]         = QString::fromStdString(mem.name);
        m[QStringLiteral("currentBytes")] = qint64(mem.current_bytes);
        m[QStringLiteral("peakBytes")]    = qint64(mem.peak_bytes);
        m[QStringLiteral("currentStr")]   = fmtBytes(mem.current_bytes);
        m[QStringLiteral("peakStr")]      = fmtBytes(mem.peak_bytes);
        m_memory.append(m);
    }

    pullStats();
    rebuildStatsFiltered();
    emit snapshotChanged();
}

void ProfilerController::reset()
{
    pinpoint::profiling::Profiler::instance().reset();
    // The OS gauge baselines/peaks were just unseeded; clear the cached peaks so
    // the UI does not show a stale watermark for up to one sampler interval.
    m_cpuPct     = 0.0;
    m_peakCpuPct = 0.0;
    m_peakRss    = 0;
    emit gaugeChanged();
    refresh();   // tables now reflect zeroed counters
}

void ProfilerController::dumpToLog()
{
    pinpoint::profiling::Profiler::instance().dumpToLog();
    pullStats();
    rebuildStatsFiltered();
    emit snapshotChanged();
}

void ProfilerController::toggleStatsCategory(const QString &category)
{
    if (m_activeCategories.contains(category))
        m_activeCategories.remove(category);
    else
        m_activeCategories.insert(category);
    rebuildStatsFiltered();
    emit snapshotChanged();
}

void ProfilerController::setStatsTextFilter(const QString &text)
{
    if (m_statsTextFilter == text)
        return;
    m_statsTextFilter = text;
    rebuildStatsFiltered();
    emit snapshotChanged();
}

void ProfilerController::clearStats()
{
    // Mirror the message-log Clear: drop the local display copy and advance the
    // read cursor; the global PpStatsLog ring is left intact.
    m_statsAll.clear();
    m_statsFiltered.clear();
    m_statsSeq = PpStatsLog::instance()->currentSeq();
    emit snapshotChanged();
}

QString ProfilerController::exportStats()
{
    pullStats();   // make sure the file is current even if the screen was closed

    const QDateTime now = QDateTime::currentDateTime();
    const QString   ts  = now.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString   path =
        QDir(QDir::homePath()).filePath(QStringLiteral("PinPointStudio_stats_%1.txt").arg(ts));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ppError() << "[Profiler] Cannot open stats export file for writing:" << path;
        return QString();
    }

    QTextStream out(&file);
    out << "PinPointStudio resource-profiler stats — exported "
        << now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "\n\n";

    // m_statsAll is newest-first; write oldest-first.
    for (auto it = m_statsAll.crbegin(); it != m_statsAll.crend(); ++it) {
        const QVariantMap row = it->toMap();
        out << row.value(QStringLiteral("timestamp")).toString() << "  "
            << row.value(QStringLiteral("category")).toString().leftJustified(7) << "  "
            << row.value(QStringLiteral("message")).toString() << "\n";
    }

    return path;
}

// ── Stats ring plumbing ───────────────────────────────────────────────────────
void ProfilerController::pullStats()
{
    const auto newEntries = PpStatsLog::instance()->fetchSince(m_statsSeq);
    for (const auto &e : newEntries) {       // oldest-first → prepend for newest-first
        QVariantMap row;
        row[QStringLiteral("timestamp")] = e.timestamp;
        row[QStringLiteral("category")]  = e.category;
        row[QStringLiteral("message")]   = e.message;
        m_statsAll.prepend(row);
    }
    while (m_statsAll.size() > kMaxStatsEntries)
        m_statsAll.removeLast();
}

void ProfilerController::rebuildStatsFiltered()
{
    const QString needle = m_statsTextFilter.toLower();
    m_statsFiltered.clear();
    for (const QVariant &v : std::as_const(m_statsAll)) {
        const QVariantMap row = v.toMap();
        const QString cat = row.value(QStringLiteral("category")).toString();
        if (!m_activeCategories.contains(cat))
            continue;
        if (!needle.isEmpty()
            && !row.value(QStringLiteral("message")).toString().toLower().contains(needle)
            && !cat.toLower().contains(needle))
            continue;
        m_statsFiltered.append(row);
    }
}
