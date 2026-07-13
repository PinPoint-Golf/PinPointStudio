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
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <thread>

class SessionController;

// Hardware probe behind the "Motion capture quality" setting (General panel).
// Modelled on CudaRuntimeController: a single QObject, platform/EP code guarded
// inline, constructed as a stack object in main.cpp and exposed to QML via
// setContextProperty("motionCaptureProbe"). This is a probe, not a device
// integration, so it does NOT use the abstract-base + factory pattern.
//
// Two jobs:
//   1. Suitability (cheap, synchronous, on refresh()): does this machine have an
//      accelerated pose backend with enough graphics memory to run the High tier?
//      Drives highTierSupported / highTierBlockReason so the panel can grey the
//      High chip and fall back a persisted "High" to "Medium".
//   2. Timing (heavy, async, at most once per launch): micro-benchmark the two
//      *existing* pose models (MoveNet = Low, ViTPose-B = Medium) by reusing the
//      estimators' own rolling poseStatsUpdated() measurement, and derive a
//      whole-seconds-per-swing estimate per tier. High is projected from Medium
//      (ViTPose++-L is not integrated yet — see kHighToMediumComputeFactor) and
//      flagged provisional.
//
// Copy stays user-facing everywhere it surfaces: no "ViTPose"/"MoveNet"/"VRAM"/
// "execution provider" leaks into a Q_PROPERTY string.
class MotionCaptureProbe : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    probing                 READ probing                 NOTIFY stateChanged) // benchmark in flight
    Q_PROPERTY(bool    ready                   READ ready                   NOTIFY stateChanged) // completed ≥ once this launch
    Q_PROPERTY(QString backend                 READ backend                 NOTIFY stateChanged) // "" / CoreML / CUDA / DirectML
    Q_PROPERTY(QString deviceName              READ deviceName              NOTIFY stateChanged) // for display, best-effort
    Q_PROPERTY(bool    highTierSupported       READ highTierSupported       NOTIFY stateChanged)
    Q_PROPERTY(QString highTierBlockReason     READ highTierBlockReason     NOTIFY stateChanged) // "" when supported
    // Measured/estimated whole seconds per swing, keyed "Low"/"Medium"/"High";
    // value -1 = unknown (not yet measured / deferred).
    Q_PROPERTY(QVariantMap secondsPerSwing     READ secondsPerSwing         NOTIFY stateChanged)
    Q_PROPERTY(bool    highEstimateProvisional READ highEstimateProvisional NOTIFY stateChanged)

public:
    explicit MotionCaptureProbe(SessionController *session = nullptr, QObject *parent = nullptr);
    ~MotionCaptureProbe() override;

    bool        probing()                 const { return m_probing; }
    bool        ready()                   const { return m_ready; }
    QString     backend()                 const { return m_backend; }
    QString     deviceName()              const { return m_deviceName; }
    bool        highTierSupported()       const { return m_highTierSupported; }
    QString     highTierBlockReason()     const { return m_highTierBlockReason; }
    QVariantMap secondsPerSwing()         const { return m_secondsPerSwing; }
    bool        highEstimateProvisional() const { return m_highEstimateProvisional; }

    // Idempotent; kicks the timing benchmark if not already ready/probing. The
    // cheap suitability re-runs on every call (so a GPU added since launch is
    // reflected). Called from QML on panel appear (like cudaRuntime.refresh()).
    Q_INVOKABLE void refresh();

public slots:
    // Feed a live analysis pass's rolling pose stats so a REAL measurement of the
    // Medium (ViTPose) model supersedes the micro-benchmark. avgMs is per-frame
    // inference time. The analysis estimator can connect its poseStatsUpdated()
    // here; the micro-benchmark is only the fallback when no live value exists.
    void reportLivePoseStats(double avgMs, double fps);

signals:
    void stateChanged();

private:
    void runSuitabilityProbe();       // cheap; GUI thread
    void kickTimingBenchmark();       // async; spawns m_worker
    void runBenchmark(double &lowMs, double &mediumMs);  // heavy; worker thread
    void recomputeSecondsPerSwing();  // folds bench + live; GUI thread

    SessionController *m_session = nullptr;

    bool        m_probing = false;
    bool        m_ready   = false;
    QString     m_backend;
    QString     m_deviceName;
    bool        m_highTierSupported = false;
    QString     m_highTierBlockReason;
    QVariantMap m_secondsPerSwing;
    bool        m_highEstimateProvisional = false;

    // Raw per-frame inference times (ms; -1 = unknown). Kept so recompute can fold
    // a live Medium measurement in without re-benchmarking.
    double m_benchLowMs    = -1.0;   // MoveNet (Low)  micro-benchmark
    double m_benchMediumMs = -1.0;   // ViTPose (Medium) micro-benchmark
    double m_liveMediumMs  = -1.0;   // ViTPose (Medium) from a live analysis pass (preferred)

    std::atomic<bool> m_cancel{false};  // set in dtor to break the benchmark loop
    std::thread       m_worker;         // joined in dtor; started at most once
};
