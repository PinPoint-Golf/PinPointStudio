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

#ifdef HAVE_OPENCV

// User-in-the-loop ball-detection calibration state machine
// (docs/design/ball_detection_calibration.md §5). One controller per live
// CameraInstance, created on demand by CameraManager::ballCalibrationFor()
// and parented to the instance. GUI-thread; the detector's capture stream
// arrives via queued signals, model fitting runs on QtConcurrent.
//
//   idle → promptEmpty ─confirm()→ captureEmpty → promptBall ─confirm()→
//   settle → captureBall → fit → validateRemove → validatePlace ─┐
//        ▲                                                        │ round
//        └── (validation rounds repeat; 2 consecutive clean) ─────┘
//   → done (profile persisted + applied)   |   failed (reason + retry)
//
// QML drives it with begin() / confirm() / cancel() / retry() /
// acceptMarginal() and binds phase / instruction / progress / robustness.

#include "ball_calibration_logic.h"
#include "ball_detector.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <vector>

class AppSettings;
class CameraInstance;

class BallCalibrationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString phase        READ phase        NOTIFY phaseChanged)
    Q_PROPERTY(QString instruction  READ instruction  NOTIFY phaseChanged)
    Q_PROPERTY(QString failReason   READ failReason   NOTIFY phaseChanged)
    Q_PROPERTY(bool    awaitingConfirm READ awaitingConfirm NOTIFY phaseChanged)
    // Seconds until the Continue prompt auto-presses (-1 = no countdown).
    // Every awaiting-confirm phase auto-advances after kConfirmCountdownSecs
    // so the user knows exactly how long they have to do the ask.
    Q_PROPERTY(int     confirmCountdown READ confirmCountdown NOTIFY countdownChanged)
    Q_PROPERTY(bool    busy         READ busy         NOTIFY phaseChanged)
    Q_PROPERTY(double  progress     READ progress     NOTIFY progressChanged)
    Q_PROPERTY(double  robustness   READ robustness   NOTIFY sessionChanged)
    Q_PROPERTY(double  margin       READ margin       NOTIFY sessionChanged)
    Q_PROPERTY(int     validationRound READ validationRound NOTIFY sessionChanged)
    Q_PROPERTY(bool    canAcceptMarginal READ canAcceptMarginal NOTIFY phaseChanged)

public:
    BallCalibrationController(CameraInstance *instance, const QString &cameraKey,
                              AppSettings *appSettings, QObject *parent = nullptr);

    QString phase()       const { return m_phase; }
    QString instruction() const;
    QString failReason()  const { return m_failReason; }
    bool    awaitingConfirm() const;
    int     confirmCountdown() const { return m_countdown; }
    bool    busy()        const;
    double  progress()    const { return m_progress; }
    double  robustness()  const { return m_session.robustness(); }
    double  margin()      const { return m_session.current.margin; }
    int     validationRound() const { return m_session.cleanRounds; }
    bool    canAcceptMarginal() const;

    Q_INVOKABLE void begin();
    Q_INVOKABLE void confirm();          // "area is clear" / "ball placed" buttons
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retry()  { begin(); }
    Q_INVOKABLE void acceptMarginal();   // persist despite margin < floor
    Q_INVOKABLE void clearSaved();       // drop live profile + persisted file

    // Saved-profile probe for UI status lines (margin / age / roi) WITHOUT a
    // connected camera. Empty map = no usable saved profile.
    Q_INVOKABLE QVariantMap savedProfileInfo() const;

    // Profile file path for a camera key (shared with CameraManager's
    // connect-time restore).
    static QString profilePathFor(const QString &cameraKey);

signals:
    void phaseChanged();
    void countdownChanged();
    void progressChanged();
    void sessionChanged();
    void completed();
    void cancelled();

private slots:
    void onCalibFrame(const cv::Mat &roiBgr, int have, int target);
    void onLiveDetection(const BallDetection &det);

private:
    void setPhase(const QString &phase, const QString &failReason = QString());
    void startCapture(int frames);
    void runFit();
    void onFitDone();
    void startValidateRemove();
    void startValidatePlace();
    void finishRound(bool removeClean, bool placeAcquired);
    void persistAndFinish();
    void pushCandidateProfile();
    void failNoFrames();

    QPointer<CameraInstance> m_instance;
    QString      m_cameraKey;
    AppSettings *m_appSettings = nullptr;

    QString m_phase = QStringLiteral("idle");
    QString m_failReason;
    double  m_progress = 0.0;

    // Capture state
    std::vector<cv::Mat> m_emptyFrames;   // 2N: first half fits bg, second half scores
    std::vector<cv::Mat> m_ballFrames;    // N
    bool    m_capturingBall = false;
    // Scene-stillness gate: capture frames are stored only after
    // kStillFramesNeeded consecutive frames show no change beyond the
    // scene's own noise (ballcal::frameStillness) — so capture can't start
    // while the user is still stepping out of view.
    int     m_captureTarget = 0;          // frames to store once the gate opens
    bool    m_gateOpen = false;
    int     m_stillRun = 0;
    cv::Mat m_prevCalibFrame;
    QTimer  m_stillnessTimeout;           // scene never settles → actionable failure
    QTimer  m_settleTimer;
    QTimer  m_frameTimeout;               // no frames arriving → actionable failure
    QTimer  m_holdTimer;                  // validate hold windows
    QTimer  m_acquireTimeout;             // validate place: max wait for the ball
    QTimer  m_countdownTimer;             // 1 Hz auto-confirm countdown
    QTimer  m_countdownDelay;             // read-the-prompt grace before ticking
    int     m_countdown = -1;

    // Fit results
    pinpoint::ballcal::BallCalProfile m_candidate;
    QFutureWatcher<pinpoint::ballcal::BallCalProfile> m_fitWatcher;

    // Validation state
    pinpoint::ballcal::CalibSession m_session;
    std::vector<double> m_roundEmpty, m_roundBall;
    bool   m_roundRemoveClean = false;
    bool   m_sawFalsePositive = false;
    double m_falsePosX = 0.0, m_falsePosY = 0.0;
    int    m_holdFound = 0, m_holdTotal = 0;

    bool m_priorBallEnabled = true;       // restored on cancel/finish
};

#endif // HAVE_OPENCV
