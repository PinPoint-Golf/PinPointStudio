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

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include "pp_profiler.h"   // brings PINPOINT_PROFILE_BASELINE / PINPOINT_PROFILE defaults

// QObject bridge between the resource profiler core (Profiler + osmetrics) and
// the monitor's PROFILER panel. Mirrors ResourceMonitorController's shape:
// snapshot → QVariantList/QVariantMap rows with pre-formatted "…Str" fields,
// polled by QML; no JavaScript formatting in the view.
//
// Sampler ownership (critical): this controller owns the SINGLE app-lifetime
// 1 s timer that is the only caller of osmetrics::sampleProcess()/sampleThreads()
// — those carry global delta-baseline state and must have exactly one owner at a
// fixed cadence. The timer caches the gauge + per-thread rows and drives the 60 s
// dumpToLog() cadence; it runs regardless of screen visibility (baseline data is
// "collected as standard"). refresh() (the screen's 500 ms timer, only while
// visible) rebuilds the scope/memory/stats tables from Profiler::snapshot() and
// reads the CACHED gauge — it must never re-sample.
class ProfilerController : public QObject
{
    Q_OBJECT

    // Compile-time availability — the QML panel hides itself when false (GA).
    Q_PROPERTY(bool available     READ available     CONSTANT)
    Q_PROPERTY(bool deepAvailable READ deepAvailable CONSTANT)
    Q_PROPERTY(bool deepEnabled   READ deepEnabled   WRITE setDeepEnabled NOTIFY deepChanged)

    // Process gauge + watermarks (cached from the 1 s sampler).
    Q_PROPERTY(double  cpuPercent        READ cpuPercent        NOTIFY gaugeChanged)
    Q_PROPERTY(double  peakCpuPercent    READ peakCpuPercent    NOTIFY gaugeChanged)
    Q_PROPERTY(quint64 rssBytes          READ rssBytes          NOTIFY gaugeChanged)
    Q_PROPERTY(quint64 peakRssBytes      READ peakRssBytes      NOTIFY gaugeChanged)
    Q_PROPERTY(QString cpuPercentStr     READ cpuPercentStr     NOTIFY gaugeChanged)
    Q_PROPERTY(QString peakCpuPercentStr READ peakCpuPercentStr NOTIFY gaugeChanged)
    Q_PROPERTY(QString rssBytesStr       READ rssBytesStr       NOTIFY gaugeChanged)
    Q_PROPERTY(QString peakRssBytesStr   READ peakRssBytesStr   NOTIFY gaugeChanged)
    Q_PROPERTY(QVariantList threads      READ threads           NOTIFY gaugeChanged)

    // GPU gauge (cached from the 1 s sampler). gpuAvailable == false means no GPU
    // memory source resolved on this host (e.g. CPU-only inference) — the panel
    // shows a muted "none" line. Device used/total are 0 when the backend cannot
    // report them (DXGI/Metal report process + total only).
    Q_PROPERTY(bool    gpuAvailable          READ gpuAvailable          NOTIFY gaugeChanged)
    Q_PROPERTY(bool    gpuUnified            READ gpuUnified            NOTIFY gaugeChanged)
    Q_PROPERTY(QString gpuBackend            READ gpuBackend            NOTIFY gaugeChanged)
    Q_PROPERTY(QString gpuDeviceName         READ gpuDeviceName         NOTIFY gaugeChanged)
    Q_PROPERTY(quint64 gpuProcessBytes       READ gpuProcessBytes       NOTIFY gaugeChanged)
    Q_PROPERTY(quint64 gpuPeakProcessBytes   READ gpuPeakProcessBytes   NOTIFY gaugeChanged)
    Q_PROPERTY(quint64 gpuDeviceUsedBytes    READ gpuDeviceUsedBytes    NOTIFY gaugeChanged)
    Q_PROPERTY(quint64 gpuDeviceTotalBytes   READ gpuDeviceTotalBytes   NOTIFY gaugeChanged)
    Q_PROPERTY(QString gpuProcessBytesStr    READ gpuProcessBytesStr    NOTIFY gaugeChanged)
    Q_PROPERTY(QString gpuPeakProcessBytesStr READ gpuPeakProcessBytesStr NOTIFY gaugeChanged)
    Q_PROPERTY(QString gpuDeviceUsedBytesStr READ gpuDeviceUsedBytesStr NOTIFY gaugeChanged)
    Q_PROPERTY(QString gpuDeviceTotalBytesStr READ gpuDeviceTotalBytesStr NOTIFY gaugeChanged)

    // Scope + memory tables (rebuilt by refresh()).
    Q_PROPERTY(QVariantList scopes       READ scopes            NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList memory       READ memory            NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList gpuMemory    READ gpuMemory         NOTIFY snapshotChanged)

    // STATS HISTORY — the dedicated PpStatsLog ring, filtered in C++.
    Q_PROPERTY(QVariantList statsHistory    READ statsHistory    NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList statsCategories READ statsCategories NOTIFY snapshotChanged)

    // ANALYSIS — per-stage aggregate (Analysis.Stage.* scopes, filtered out of the
    // generic scope table) and the per-run drill-down (the AnalysisProfileLog ring).
    Q_PROPERTY(QVariantList analysisStages READ analysisStages NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList analysisRuns   READ analysisRuns   NOTIFY snapshotChanged)

public:
    explicit ProfilerController(QObject *parent = nullptr);

    bool available() const
    {
        return PINPOINT_PROFILE_BASELINE || PINPOINT_PROFILE;
    }
    bool deepAvailable() const { return PINPOINT_PROFILE; }
    bool deepEnabled()   const;
    void setDeepEnabled(bool on);

    double  cpuPercent()        const { return m_cpuPct; }
    double  peakCpuPercent()    const { return m_peakCpuPct; }
    quint64 rssBytes()          const { return m_rss; }
    quint64 peakRssBytes()      const { return m_peakRss; }
    QString cpuPercentStr()     const;
    QString peakCpuPercentStr() const;
    QString rssBytesStr()       const;
    QString peakRssBytesStr()   const;
    QVariantList threads()      const { return m_threads; }

    bool    gpuAvailable()        const { return m_gpuAvailable; }
    bool    gpuUnified()          const { return m_gpuUnified; }
    QString gpuBackend()          const { return m_gpuBackend; }
    QString gpuDeviceName()       const { return m_gpuDeviceName; }
    quint64 gpuProcessBytes()     const { return m_gpuProc; }
    quint64 gpuPeakProcessBytes() const { return m_gpuPeakProc; }
    quint64 gpuDeviceUsedBytes()  const { return m_gpuDevUsed; }
    quint64 gpuDeviceTotalBytes() const { return m_gpuDevTotal; }
    QString gpuProcessBytesStr()     const;
    QString gpuPeakProcessBytesStr() const;
    QString gpuDeviceUsedBytesStr()  const;
    QString gpuDeviceTotalBytesStr() const;

    QVariantList scopes()       const { return m_scopes; }
    QVariantList memory()       const { return m_memory; }
    QVariantList gpuMemory()    const { return m_gpuMemory; }

    QVariantList statsHistory()    const { return m_statsFiltered; }
    QVariantList statsCategories() const;

    QVariantList analysisStages() const { return m_analysisStages; }
    QVariantList analysisRuns()   const { return m_analysisRuns; }

    // Screen-driven (500 ms while visible): rebuilds scope/memory/stats tables.
    Q_INVOKABLE void refresh();
    // Start a fresh measurement window (counters + OS-gauge peaks/baselines).
    Q_INVOKABLE void reset();
    // Emit a baseline summary into the stats ring now.
    Q_INVOKABLE void dumpToLog();
    // STATS HISTORY filter + maintenance (all resolved C++-side, no JS).
    Q_INVOKABLE void toggleStatsCategory(const QString &category);
    Q_INVOKABLE void setStatsTextFilter(const QString &text);
    Q_INVOKABLE void clearStats();
    Q_INVOKABLE QString exportStats();
    // ANALYSIS RUNS drill-down maintenance (mirrors the stats Clear/Export).
    Q_INVOKABLE void clearAnalysisRuns();
    Q_INVOKABLE QString exportAnalysisRuns();

signals:
    void gaugeChanged();
    void snapshotChanged();
    void deepChanged();

private:
    void onSamplerTick();      // the single gauge owner
    void pullStats();          // fetch new PpStatsLog entries into m_statsAll
    void rebuildStatsFiltered();
    void pullAnalysisRuns();   // fetch new AnalysisProfileLog runs into m_analysisRuns

    QTimer m_sampler;
    int    m_tick = 0;

    // Cached gauge (written only by onSamplerTick).
    double  m_cpuPct     = 0.0;
    double  m_peakCpuPct = 0.0;
    quint64 m_rss        = 0;
    quint64 m_peakRss    = 0;
    QVariantList m_threads;

    // Cached GPU gauge (written only by onSamplerTick).
    bool    m_gpuAvailable = false;
    bool    m_gpuUnified   = false;
    QString m_gpuBackend;
    QString m_gpuDeviceName;
    quint64 m_gpuProc      = 0;
    quint64 m_gpuPeakProc  = 0;
    quint64 m_gpuDevUsed   = 0;
    quint64 m_gpuDevTotal  = 0;

    QVariantList m_scopes;
    QVariantList m_memory;
    QVariantList m_gpuMemory;

    QVariantList  m_statsAll;       // every retained entry, newest-first
    QVariantList  m_statsFiltered;  // m_statsAll after category + text filter
    int           m_statsSeq = -1;  // last PpStatsLog seq fetched
    QSet<QString> m_activeCategories;
    QString       m_statsTextFilter;

    QVariantList  m_analysisStages;  // Analysis.Stage.* aggregate rows (refresh)
    QVariantList  m_analysisRuns;    // per-run drill-down, newest-first
    int           m_analysisSeq = -1;// last AnalysisProfileLog seq fetched

    static constexpr int kSamplerIntervalMs = 1000;
    static constexpr int kDumpEveryTicks    = 60;   // 60 s at 1 s cadence
    static constexpr int kMaxStatsEntries   = 1000;
};
