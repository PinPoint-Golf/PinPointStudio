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
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

#include "trunk_ceremony_file.h"

class AthleteController;
class ImuManager;
namespace pinpoint { struct ImuSampleTap; }

// The trunk ceremony recorder (trunk_imu_sequence_grf_design.md §6.7, §16.2 W2.1c), and the C++ seam
// the trunk routine (W2.2, TrunkRoutine.qml) calls for the turn steps. QML context property
// `trunkCeremony` (main.cpp), like imuManager and sessionController.
//
// WHAT IT RECORDS. From begin() to end(), every sample of every sensor unit in the ceremony, through
// each device's tap (imu_sample_tap.h) — the host stamp and the ImuSample the EventBuffer would store:
//   * the trunk Witmotions: the holders of "pelvis" and "thorax" (ImuManager::deviceForRole);
//   * the wG3 when one holds "leadForearm": two lanes, lower arm (leadForearm) and palm (leadHand),
//     tapped in HmSessionWorker::writeSample, only samples with a mapped host time (as its ring).
// A sensor not live when begin() runs is not recorded (its step results say it sent no data).
// Bounded: kMaxSamplesPerLane per lane (10 min at 200 Hz); beyond that the tap drops and counts, and
// one ppWarn says so. A ceremony is one to two minutes.
//
// THE STEPS. stepStart(name) / stepEnd(name) bracket each step; a step repeated after a failure is a
// new entry. Names: "standTall", "turnAway", "turnThrough", and "armRoutine" for the wG3 routine's
// span (trunk + HackMotion: begin() at the arm's Calibrate step, so the trunk sensors record through
// it). mark(name) records an instant (start = end). attachResult(name, map) puts a record on the last
// step of that name: a map carrying "role" (calibrateTrunk's) goes under result[role], so one
// standTall step holds { pelvis: …, thorax: … }; any other map replaces the result.
//
// PERSISTENCE. Setup runs before the session, so end() holds the ceremony in memory; when the next
// session starts (SessionController::sessionStarted → onSessionStarted), it is written into the
// session folder as trunk_ceremony.json (format: trunk_ceremony_file.h), off the GUI thread, with one
// ppInfo line naming the path and the counts. A ceremony ended while a session is running is written
// at once. An earlier trunk_ceremony.json in the folder (an extended session) is kept, renamed
// trunk_ceremony_<n>.json. Dropped instead: on discard(); on a new begin(); and at session start when
// its solves are no longer the ones in force (every trunk sensor it solved now carries another
// calibration — the setup was abandoned and redone without it).
//
// CALL ORDER (W2.2):
//   begin()                                      at the first calibrate step (the arm's, with a wG3)
//   [stepStart("armRoutine") … stepEnd("armRoutine")]   trunk + wG3 only
//   stepStart("standTall")  … hold bar fills … pelvis.calibrateTrunk("pelvis"), thorax.calibrateTrunk("thorax")
//       attachResult("standTall", pelvisRecord); attachResult("standTall", thoraxRecord); stepEnd("standTall")
//   stepStart("turnAway")   … the golfer turns twice … var r = checkTurn("away", leftHanded); stepEnd("turnAway")
//   stepStart("turnThrough") … checkTurn("through", leftHanded); stepEnd("turnThrough")
//   end()                                        the trunk routine finished (pass or not)
//   discard()                                    the golfer left setup without starting the session
// A failed step is repeated by stepStart(sameName) again. checkTurn() reads the open step of its name
// (else the last one) from its start to now, so call it before that step's stepEnd.
class TrunkCeremonyRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    recording   READ recording   NOTIFY stateChanged)   // between begin() and end()
    Q_PROPERTY(bool    pendingWrite READ pendingWrite NOTIFY stateChanged) // ended, waiting for a session
    Q_PROPERTY(int     laneCount   READ laneCount   NOTIFY stateChanged)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY stateChanged)   // the open step, "" when none

public:
    // ~10 min at 200 Hz; 48 B a sample in RAM, so ≤ 5.8 MB a lane.
    static constexpr size_t kMaxSamplesPerLane = 120'000;

    TrunkCeremonyRecorder(ImuManager *imuManager, AthleteController *athlete, QObject *parent = nullptr);
    ~TrunkCeremonyRecorder() override;

    // The running session's folder, or "" when no session is running. Set by main.cpp
    // (SessionController::running + ShotProcessor::activeSessionDir) so this class needs neither.
    void setSessionDirResolver(std::function<QString()> fn) { m_sessionDir = std::move(fn); }

    bool    recording()    const { return m_recording; }
    bool    pendingWrite() const { return m_pending; }
    int     laneCount()    const { return int(m_taps.size()); }
    QString currentStep()  const;

    // Starts a new ceremony (dropping any held one) on the trunk sensors and the wG3 present now.
    // Returns false, and records nothing, when neither trunk role has a live sensor.
    Q_INVOKABLE bool begin();
    Q_INVOKABLE void stepStart(const QString &name);
    Q_INVOKABLE void stepEnd(const QString &name);
    Q_INVOKABLE void mark(const QString &name);
    Q_INVOKABLE void attachResult(const QString &stepName, const QVariantMap &result);
    // Stops recording and holds the ceremony for the session (or writes it now — see above).
    Q_INVOKABLE void end();
    // Drops the ceremony, recording or held. Nothing is written.
    Q_INVOKABLE void discard();

    // ── The turn check (design §6.3 steps 2 and 3; W2.1b checkTrunkTurn) ──────────────────────
    // direction: "away" | "through". Reads the open "turnAway"/"turnThrough" step (else the last
    // one) from its start to now on the pelvis and thorax lanes, each through its sensor's CURRENT
    // A/M (the stand-tall solve's, once calibrateTrunk applied one), runs checkTrunkTurn, attaches
    // the record to the step and returns it. Logs one ppInfo line.
    //
    // Returned keys (stable — W2.2 reads them):
    //   valid (bool)              every gate on both sensors and the order
    //   failure (string)          THE golfer-facing message for the step; "" when valid
    //   direction ("away"|"through"), leftHanded (bool), expectedSign (int, the sign about +Z asked)
    //   chestOverHips (double), orderOk (bool, not swapped), wrongWay (bool, both turned the other way)
    //   cause (string)            which rule gave `failure`: "" | "noData" | "tooLittle" | "wrongWay" |
    //                             "backwards" | "swapped" | "range" | "tilt" (TrunkTurnRecord::cause);
    //                             "backwards"/"swapped" send the routine back to stand tall
    //   pelvis, thorax (maps):    samples (int), rangeDeg, turnDeg, siTurnDeg, axisFromVerticalDeg
    //                             (double), rangeOk, senseOk, axisOk (bool), failure (string, this
    //                             sensor's own message), serial, deviceId (string), trunkCalibrated
    //                             (bool: A/M came from the stand-tall solve), windowStartUs,
    //                             windowEndUs (int64), alignA, mountM ([w,x,y,z])
    // When it cannot run — not recording, no such step, a bad direction — it returns
    // { valid: false, failure: "…" } and attaches nothing.
    Q_INVOKABLE QVariantMap checkTurn(const QString &direction, bool leftHanded);

public slots:
    // SessionController::sessionStarted — writes a held ceremony into the session folder.
    void onSessionStarted();

signals:
    void stateChanged();

private:
    struct TapRef {
        std::shared_ptr<pinpoint::ImuSampleTap> tap;
        QPointer<QObject> device;     // ImuInstance or HmInstance; may go away mid-ceremony
        size_t lane = 0;              // index into m_ceremony.lanes
        bool   warnedOverCap = false;
    };

    void drain();                     // every tap's pending samples into the lanes
    void stopTaps();
    trunk_ceremony_file::Step *lastStepNamed(const QString &name);
    bool solvesStillInForce() const;
    void write(const QString &sessionDir);
    void clear();

    ImuManager        *m_imu     = nullptr;
    AthleteController *m_athlete = nullptr;
    std::function<QString()> m_sessionDir;

    trunk_ceremony_file::Ceremony m_ceremony;
    std::vector<TapRef> m_taps;
    bool m_recording = false;
    bool m_pending   = false;         // ended and held for the session
};
