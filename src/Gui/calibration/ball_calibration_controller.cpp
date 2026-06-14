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

#ifdef HAVE_OPENCV

#include "ball_calibration_controller.h"
#include "app_settings.h"
#include "ball_calibration_store.h"
#include "camera_instance.h"
#include "pp_debug.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>

using namespace pinpoint::ballcal;

namespace {
constexpr int kEmptyFrames      = 30;     // first 15 fit the bg, second 15 score
constexpr int kBallFrames       = 15;
constexpr int kSettleMs         = 1200;   // hand-leaving-frame delay after "ball placed"
constexpr int kStillFramesNeeded   = 4;      // consecutive still frames to open the gate
constexpr int kStillnessTimeoutMs  = 20000;  // scene never settles → actionable failure
constexpr int kFrameTimeoutMs   = 5000;   // no frames at all → actionable failure
constexpr int kRemoveHoldMs     = 3000;   // sustained not-found while removed
constexpr int kPlaceHoldMs      = 1500;   // sustained found after placement
constexpr int kAcquireTimeoutMs = 30000;  // generous: the user is walking
constexpr int kConfirmCountdownSecs = 5;  // auto-press Continue after this
constexpr int kCountdownDelayMs     = 2000; // read-the-prompt grace before ticking
}

BallCalibrationController::BallCalibrationController(CameraInstance *instance,
                                                     const QString &cameraKey,
                                                     AppSettings *appSettings,
                                                     QObject *parent)
    : QObject(parent)
    , m_instance(instance)
    , m_cameraKey(cameraKey)
    , m_appSettings(appSettings)
{
    qRegisterMetaType<cv::Mat>();

    m_settleTimer.setSingleShot(true);
    m_frameTimeout.setSingleShot(true);
    m_holdTimer.setSingleShot(true);
    m_acquireTimeout.setSingleShot(true);
    m_stillnessTimeout.setSingleShot(true);

    // Scene-stillness gate never opened: frames are flowing (else the frame
    // timeout fires first) but something kept moving through the hitting area.
    connect(&m_stillnessTimeout, &QTimer::timeout, this, [this]() {
        const bool capturing = m_phase == QLatin1String("captureEmpty")
                            || m_phase == QLatin1String("captureBall");
        if (!capturing || m_gateOpen)
            return;
        if (BallDetector *det = m_instance ? m_instance->ballDetector() : nullptr)
            QMetaObject::invokeMethod(det, [det]() { det->cancelCalibCapture(); },
                                      Qt::QueuedConnection);
        m_frameTimeout.stop();
        setPhase(QStringLiteral("failed"),
                 tr("The hitting area never settled — something kept moving "
                    "through the camera's view (that includes you). Step "
                    "fully out of view and try again."));
    });

    // 1 Hz auto-confirm countdown for every awaiting-confirm prompt: the user
    // sees exactly how long they have to do the ask before Continue presses
    // itself (clicking earlier is always allowed). Ticking starts after a
    // read-the-prompt grace period.
    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this]() {
        if (m_countdown > 0) {
            --m_countdown;
            emit countdownChanged();
        }
        if (m_countdown == 0)
            confirm();          // stops the timer via setPhase
    });
    m_countdownDelay.setSingleShot(true);
    m_countdownDelay.setInterval(kCountdownDelayMs);
    connect(&m_countdownDelay, &QTimer::timeout, this, [this]() {
        if (awaitingConfirm())
            m_countdownTimer.start();
    });

    connect(&m_settleTimer, &QTimer::timeout, this, [this]() {
        if (m_phase == QLatin1String("settle")) {
            m_capturingBall = true;
            setPhase(QStringLiteral("captureBall"));
            startCapture(kBallFrames);
        }
    });
    connect(&m_frameTimeout, &QTimer::timeout, this, &BallCalibrationController::failNoFrames);
    connect(&m_fitWatcher, &QFutureWatcher<BallCalProfile>::finished,
            this, &BallCalibrationController::onFitDone);

    connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
        if (m_phase == QLatin1String("validateRemove")) {
            // Hold survived without a false positive → ball-removed half passed.
            m_roundRemoveClean = !m_sawFalsePositive;
            startValidatePlace();
        } else if (m_phase == QLatin1String("validatePlace")) {
            // Require the found-stream to have been solid through the hold.
            const bool acquired = m_holdTotal > 0
                                  && m_holdFound >= (m_holdTotal * 7) / 10;
            finishRound(m_roundRemoveClean, acquired);
        }
    });
    connect(&m_acquireTimeout, &QTimer::timeout, this, [this]() {
        if (m_phase == QLatin1String("validatePlace"))
            finishRound(m_roundRemoveClean, false);
    });

    if (m_instance && m_instance->ballDetector()) {
        BallDetector *det = m_instance->ballDetector();
        connect(det, &BallDetector::calibFrame,
                this, &BallCalibrationController::onCalibFrame, Qt::QueuedConnection);
        connect(det, &BallDetector::ballDetected,
                this, &BallCalibrationController::onLiveDetection, Qt::QueuedConnection);
    }
}

// ── QML-visible state ───────────────────────────────────────────────────────

QString BallCalibrationController::instruction() const
{
    if (m_phase == QLatin1String("promptEmpty"))
        return tr("Make sure the hitting area is completely clear, then tap Continue.");
    if (m_phase == QLatin1String("captureEmpty"))
        return m_gateOpen
            ? tr("Learning the empty hitting area — keep it clear…")
            : tr("Waiting for the hitting area to settle — keep it clear…");
    if (m_phase == QLatin1String("promptBall"))
        return tr("Place a ball anywhere in the hitting area, then tap Continue.");
    if (m_phase == QLatin1String("settle"))
        return tr("Hold on…");
    if (m_phase == QLatin1String("captureBall"))
        return m_gateOpen
            ? tr("Learning the ball — don't touch it…")
            : tr("Waiting for the hitting area to settle — stay out of view…");
    if (m_phase == QLatin1String("fit"))
        return tr("Tuning the detector…");
    if (m_phase == QLatin1String("promptRemove"))
        return tr("Remove the ball from the hitting area.");
    if (m_phase == QLatin1String("validateRemove"))
        return tr("Checking the empty hitting area…");
    if (m_phase == QLatin1String("promptPlace"))
        return tr("Place the ball again — try a slightly different spot.");
    if (m_phase == QLatin1String("validatePlace"))
        return tr("Checking ball detection…");
    if (m_phase == QLatin1String("done"))
        return tr("Ball detection calibrated.");
    if (m_phase == QLatin1String("failed"))
        return m_failReason;
    return QString();
}

bool BallCalibrationController::awaitingConfirm() const
{
    return m_phase == QLatin1String("promptEmpty")
        || m_phase == QLatin1String("promptBall")
        || m_phase == QLatin1String("promptRemove")
        || m_phase == QLatin1String("promptPlace");
}

bool BallCalibrationController::busy() const
{
    return m_phase != QLatin1String("idle")
        && m_phase != QLatin1String("done")
        && m_phase != QLatin1String("failed");
}

bool BallCalibrationController::canAcceptMarginal() const
{
    return m_phase == QLatin1String("failed")
        && m_candidate.valid
        && m_session.current.margin > 0.0;
}

// ── Protocol ────────────────────────────────────────────────────────────────

void BallCalibrationController::begin()
{
    if (!m_instance || !m_instance->ballDetector()) {
        setPhase(QStringLiteral("failed"),
                 tr("Connect the camera before calibrating."));
        return;
    }
    if (m_instance->roi().isEmpty()) {
        setPhase(QStringLiteral("failed"),
                 tr("Draw a hitting area on the camera view first."));
        return;
    }

    // Ball inference must run for frames to reach the detector; remember the
    // per-screen gate so cancel/finish can restore it.
    m_priorBallEnabled = m_instance->ballEnabled();
    m_instance->setBallEnabled(true);

    // The capture/fit must observe the RAW scene, not a half-applied profile.
    m_instance->clearBallCalProfile();

    m_emptyFrames.clear();
    m_ballFrames.clear();
    m_candidate = BallCalProfile{};
    m_session   = CalibSession{};
    m_capturingBall = false;
    emit sessionChanged();

    setPhase(QStringLiteral("promptEmpty"));
}

void BallCalibrationController::confirm()
{
    if (m_phase == QLatin1String("promptEmpty")) {
        setPhase(QStringLiteral("captureEmpty"));
        startCapture(kEmptyFrames);
    } else if (m_phase == QLatin1String("promptBall")) {
        setPhase(QStringLiteral("settle"));
        m_settleTimer.start(kSettleMs);
    } else if (m_phase == QLatin1String("promptRemove")) {
        // The ball is gone (the user just confirmed) — NOW start watching.
        setPhase(QStringLiteral("validateRemove"));
    } else if (m_phase == QLatin1String("promptPlace")) {
        m_holdFound = m_holdTotal = 0;
        setPhase(QStringLiteral("validatePlace"));
        m_acquireTimeout.start(kAcquireTimeoutMs);
    }
}

void BallCalibrationController::cancel()
{
    if (m_instance && m_instance->ballDetector()) {
        BallDetector *det = m_instance->ballDetector();
        QMetaObject::invokeMethod(det, [det]() { det->cancelCalibCapture(); },
                                  Qt::QueuedConnection);
        // Revert to the persisted profile (or none).
        BallCalProfile saved;
        if (loadProfile(profilePathFor(m_cameraKey).toStdString(), saved))
            m_instance->applyBallCalProfile(saved);
        else
            m_instance->clearBallCalProfile();
        m_instance->setBallEnabled(m_priorBallEnabled);
    }
    m_settleTimer.stop(); m_frameTimeout.stop();
    m_holdTimer.stop();   m_acquireTimeout.stop();
    m_stillnessTimeout.stop();
    m_countdownTimer.stop(); m_countdownDelay.stop();
    setPhase(QStringLiteral("idle"));
    emit cancelled();
}

void BallCalibrationController::acceptMarginal()
{
    if (!canAcceptMarginal()) return;
    persistAndFinish();
}

void BallCalibrationController::clearSaved()
{
    QFile::remove(profilePathFor(m_cameraKey));
    if (m_instance)
        m_instance->clearBallCalProfile();
    setPhase(QStringLiteral("idle"));
}

QVariantMap BallCalibrationController::savedProfileInfo() const
{
    BallCalProfile p;
    if (!loadProfile(profilePathFor(m_cameraKey).toStdString(), p))
        return {};
    return {
        {QStringLiteral("margin"),         p.margin},
        {QStringLiteral("calibratedAtMs"), static_cast<qlonglong>(p.calibratedAtMs)},
        {QStringLiteral("roi"),            QRectF(p.roiX, p.roiY, p.roiW, p.roiH)},
    };
}

QString BallCalibrationController::profilePathFor(const QString &cameraKey)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/ballcal");
    QDir().mkpath(dir);
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(cameraKey.toUtf8(), QCryptographicHash::Sha1)
            .toHex().left(12));
    return dir + QLatin1Char('/') + hash + QStringLiteral(".yml.gz");
}

// ── Capture ─────────────────────────────────────────────────────────────────

void BallCalibrationController::startCapture(int frames)
{
    m_progress = 0.0;
    emit progressChanged();
    BallDetector *det = m_instance ? m_instance->ballDetector() : nullptr;
    if (!det) { failNoFrames(); return; }
    m_captureTarget = frames;
    m_gateOpen      = false;
    m_stillRun      = 0;
    m_prevCalibFrame.release();
    // Open-ended stream: the stillness gate decides when frames start
    // counting, so this side owns the target and stops the stream itself.
    QMetaObject::invokeMethod(det, [det]() { det->beginCalibCapture(-1); },
                              Qt::QueuedConnection);
    m_frameTimeout.start(kFrameTimeoutMs);
    m_stillnessTimeout.start(kStillnessTimeoutMs);
}

void BallCalibrationController::onCalibFrame(const cv::Mat &roiBgr, int /*have*/,
                                             int /*target*/)
{
    m_frameTimeout.start(kFrameTimeoutMs);   // frames are flowing — keep alive

    const bool emptyPhase = m_phase == QLatin1String("captureEmpty") && !m_capturingBall;
    const bool ballPhase  = m_phase == QLatin1String("captureBall")  && m_capturingBall;
    if (!emptyPhase && !ballPhase)
        return;                              // stale frame from a cancelled phase

    // Scene-stillness gate: don't start feeding frames into the models while
    // anything is still moving through the hitting area (the user stepping
    // clear, a club waving past). Gate frames are never stored. The frame
    // that completes the still run is itself clean, so it falls through and
    // is stored as the first capture frame.
    if (!m_gateOpen) {
        const auto s = pinpoint::ballcal::frameStillness(m_prevCalibFrame, roiBgr);
        m_prevCalibFrame = roiBgr;
        m_stillRun = s.still ? m_stillRun + 1 : 0;
        if (m_stillRun < kStillFramesNeeded)
            return;
        m_gateOpen = true;
        m_stillnessTimeout.stop();
        emit phaseChanged();                 // instruction: settling → learning
    }

    auto &store = emptyPhase ? m_emptyFrames : m_ballFrames;
    store.push_back(roiBgr);

    m_progress = m_captureTarget > 0
        ? static_cast<double>(store.size()) / m_captureTarget : 0.0;
    emit progressChanged();

    if (static_cast<int>(store.size()) >= m_captureTarget) {
        m_frameTimeout.stop();
        if (BallDetector *det = m_instance ? m_instance->ballDetector() : nullptr)
            QMetaObject::invokeMethod(det, [det]() { det->cancelCalibCapture(); },
                                      Qt::QueuedConnection);
        if (emptyPhase)
            setPhase(QStringLiteral("promptBall"));
        else
            runFit();
    }
}

void BallCalibrationController::failNoFrames()
{
    m_settleTimer.stop(); m_holdTimer.stop(); m_acquireTimeout.stop();
    m_stillnessTimeout.stop();
    // The capture stream is open-ended now — disarm it so a camera that
    // recovers later doesn't keep cloning ROI mats into a failed session.
    if (BallDetector *det = m_instance ? m_instance->ballDetector() : nullptr)
        QMetaObject::invokeMethod(det, [det]() { det->cancelCalibCapture(); },
                                  Qt::QueuedConnection);
    setPhase(QStringLiteral("failed"),
             tr("No camera frames are arriving — check the camera is connected "
                "and started, and ball detection is enabled."));
}

// ── Fit ─────────────────────────────────────────────────────────────────────

void BallCalibrationController::runFit()
{
    setPhase(QStringLiteral("fit"));

    // Value-capture the frames; the fit runs detached from this object's state.
    const std::vector<cv::Mat> empties = m_emptyFrames;
    const std::vector<cv::Mat> balls   = m_ballFrames;
    const QRectF roi  = m_instance ? m_instance->roi() : QRectF();

    m_fitWatcher.setFuture(QtConcurrent::run([empties, balls, roi]() {
        BallCalProfile p;
        if (empties.size() < 8 || balls.empty()) return p;

        // First half of the empty block fits the background…
        const size_t half = empties.size() / 2;
        for (size_t i = 0; i < half; ++i)
            p.background.accumulate(empties[i]);
        if (!p.background.finalize()) return p;

        p.ball = fitBallModel(balls, p.background);
        if (!p.ball.valid) return p;

        // …the second half (never seen by the fit) provides honest empty scores.
        // Ball scores come from the consensus-inlier frames ONLY: a frame the
        // fit itself rejected (hand still leaving, ball not yet placed) scores
        // ~0 through the scorer and would collapse minBall — turning transient
        // motion during capture into a bogus "doesn't stand out" failure.
        std::vector<double> ballScores, emptyScores;
        for (int idx : p.ball.inlierFrames)
            ballScores.push_back(detect(balls[static_cast<size_t>(idx)],
                                        p.background, p.ball, 2.0).score);
        for (size_t i = half; i < empties.size(); ++i)
            emptyScores.push_back(detect(empties[i], p.background, p.ball, 2.0).score);

        const ThresholdResult t = deriveThreshold(ballScores, emptyScores);
        p.theta  = t.theta;
        p.margin = t.margin;
        p.roiX = roi.x(); p.roiY = roi.y(); p.roiW = roi.width(); p.roiH = roi.height();
        p.roiPxW = p.background.meanGray.cols;
        p.roiPxH = p.background.meanGray.rows;
        p.valid  = t.pass;
        return p;
    }));
}

void BallCalibrationController::onFitDone()
{
    const BallCalProfile p = m_fitWatcher.result();

    if (!p.background.valid()) {
        setPhase(QStringLiteral("failed"),
                 tr("Couldn't learn the empty hitting area — something kept "
                    "moving through it. Clear the area and try again."));
        return;
    }
    if (!p.ball.valid) {
        QString reason = tr("Couldn't isolate the ball — check it sits fully inside "
                            "the hitting area and is visible to the camera, then "
                            "try again.");
        if (!p.ball.diag.empty())
            reason += QStringLiteral("\n(") + QString::fromStdString(p.ball.diag)
                      + QLatin1Char(')');
        setPhase(QStringLiteral("failed"), reason);
        return;
    }
    if (!p.valid) {
        m_candidate = p;
        m_candidate.valid = true;   // usable, but below the robustness floor
        // Which condition failed? minBall recovers from theta + margin
        // (theta = maxEmpty + bias*margin).
        const double minBall = p.theta
            + (1.0 - pinpoint::ballcal::tuning::kThetaBias) * p.margin;
        QString reason = p.margin < pinpoint::ballcal::tuning::kMinMargin
            ? tr("The ball doesn't stand out enough from the empty hitting "
                 "area (margin %1, need %2). Improve the lighting or contrast "
                 "— or accept the marginal calibration.")
                  .arg(p.margin, 0, 'f', 2)
                  .arg(pinpoint::ballcal::tuning::kMinMargin, 0, 'f', 2)
            : tr("Separation is fine (margin %1) but the ball's own match "
                 "score is weak (worst frames %2, need %3) — usually a frame "
                 "where the ball was occluded or lighting flickered during "
                 "capture. Try again, or accept the marginal calibration.")
                  .arg(p.margin, 0, 'f', 2)
                  .arg(minBall, 0, 'f', 2)
                  .arg(pinpoint::ballcal::tuning::kMinBallScore, 0, 'f', 2);
        const size_t dropped = m_ballFrames.size() - p.ball.inlierFrames.size();
        if (dropped >= 2)
            reason += QLatin1Char('\n')
                + tr("%1 of %2 capture frames showed something else moving in "
                     "the hitting area and were ignored — stay fully out of "
                     "the camera's view while the ball is being learned.")
                      .arg(dropped).arg(m_ballFrames.size());
        setPhase(QStringLiteral("failed"), reason);
        return;
    }

    m_candidate = p;

    // Seed the session's score sets with the fit-time anchors: theta and
    // margin pin down (minBall, maxEmpty); the validation rounds append the
    // real evidence on top.
    const double minBall  = p.theta + (1.0 - pinpoint::ballcal::tuning::kThetaBias) * p.margin;
    const double maxEmpty = p.theta - pinpoint::ballcal::tuning::kThetaBias * p.margin;
    m_session.seed({minBall}, {maxEmpty});
    emit sessionChanged();

    pushCandidateProfile();
    startValidateRemove();
}

void BallCalibrationController::pushCandidateProfile()
{
    if (!m_instance) return;
    BallCalProfile live = m_candidate;
    live.theta  = m_session.current.theta;
    live.margin = m_session.current.margin;
    live.valid  = true;
    m_instance->applyBallCalProfile(live);
}

// ── Validation rounds ───────────────────────────────────────────────────────

void BallCalibrationController::startValidateRemove()
{
    m_roundEmpty.clear();
    m_roundBall.clear();
    m_roundRemoveClean = false;
    m_sawFalsePositive = false;
    // Confirm-gated (countdown): watching starts only after the user has had
    // time to actually remove the ball — a reaching arm mid-removal must
    // never count as a false positive.
    setPhase(QStringLiteral("promptRemove"));
}

void BallCalibrationController::startValidatePlace()
{
    // Confirm-gated likewise — placement happens during the prompt.
    setPhase(QStringLiteral("promptPlace"));
}

void BallCalibrationController::onLiveDetection(const BallDetection &det)
{
    if (m_phase == QLatin1String("validateRemove")) {
        if (!det.found && !m_holdTimer.isActive()) {
            // Ball gone — begin the sustained-clean window.
            m_holdTimer.start(kRemoveHoldMs);
        } else if (det.found && m_holdTimer.isActive()) {
            // Something scored ball-like while the area should be empty.
            m_sawFalsePositive = true;
            m_falsePosX = det.x; m_falsePosY = det.y;
            m_holdTimer.stop();
            finishRound(false, true /*place half not reached; not its fault*/);
            return;
        }
        if (!det.found)
            m_roundEmpty.push_back(det.score);
    } else if (m_phase == QLatin1String("validatePlace")) {
        if (det.found) {
            if (!m_holdTimer.isActive()) {
                m_acquireTimeout.stop();
                m_holdTimer.start(kPlaceHoldMs);
            }
            m_roundBall.push_back(det.score);
            ++m_holdFound;
        }
        if (m_holdTimer.isActive())
            ++m_holdTotal;
    }
}

void BallCalibrationController::finishRound(bool removeClean, bool placeAcquired)
{
    m_holdTimer.stop();
    m_acquireTimeout.stop();

    m_session.addRound(removeClean, placeAcquired, m_roundEmpty, m_roundBall);
    emit sessionChanged();

    if (m_session.passed()) {
        persistAndFinish();
        return;
    }
    if (m_session.exhausted()) {
        QString reason;
        if (m_sawFalsePositive)
            reason = tr("Something in the empty hitting area keeps scoring "
                        "ball-like near (%1%, %2%) — a reflection or bright "
                        "object. Adjust the area or the scene and recalibrate.")
                         .arg(int(m_falsePosX * 100)).arg(int(m_falsePosY * 100));
        else if (!placeAcquired)
            reason = tr("The ball wasn't recognised after placing it. Improve "
                        "the lighting or contrast and try again.");
        else
            reason = tr("Validation kept failing (margin %1). Improve lighting "
                        "or contrast and recalibrate — or accept the marginal "
                        "calibration.").arg(m_session.current.margin, 0, 'f', 2);
        setPhase(QStringLiteral("failed"), reason);
        return;
    }

    // Another round: theta has been re-derived with the new evidence.
    pushCandidateProfile();
    startValidateRemove();
}

void BallCalibrationController::persistAndFinish()
{
    m_candidate.theta  = m_session.current.theta;
    m_candidate.margin = m_session.current.margin;
    m_candidate.valid  = true;
    m_candidate.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();

    if (!saveProfile(profilePathFor(m_cameraKey).toStdString(), m_candidate))
        ppWarn() << "[BallCalibration] failed to persist profile for" << m_cameraKey;

    if (m_instance) {
        m_instance->applyBallCalProfile(m_candidate);
        m_instance->setBallEnabled(true);   // calibrated implies the user wants it on
    }
    setPhase(QStringLiteral("done"));
    emit completed();
}

void BallCalibrationController::setPhase(const QString &phase, const QString &failReason)
{
    if (m_phase == phase && m_failReason == failReason) return;
    m_phase = phase;
    m_failReason = failReason;

    // The presence ting is feedback chrome, not protocol — during the
    // validate rounds every place/remove would chime, which is just noise
    // while the user is following prompts.
    if (m_instance)
        m_instance->setBallTingSuppressed(busy());

    // Awaiting-confirm phases run the auto-press countdown; everything else
    // stops it (incl. an early manual Continue). The number shows at once,
    // but ticking waits out the read-the-prompt grace.
    if (awaitingConfirm()) {
        m_countdown = kConfirmCountdownSecs;
        m_countdownTimer.stop();
        m_countdownDelay.start();
    } else {
        m_countdownDelay.stop();
        m_countdownTimer.stop();
        m_countdown = -1;
    }
    emit countdownChanged();

    emit phaseChanged();
}

#endif // HAVE_OPENCV
