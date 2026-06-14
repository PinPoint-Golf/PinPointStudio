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

#include <QGuiApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <cmath>

#include "pp_debug.h"
#include "app_settings.h"
#ifdef HAVE_OPENCV
#  include <opencv2/core.hpp>
#  include <opencv2/core/utils/logger.hpp>
#endif
#include "SecretsManager.h"
#include "film_controller.h"
#include "imu_manager.h"
#include "transcription_controller.h"
#include "tts_controller.h"
#include "camera_manager.h"
#include "buffer_controller.h"
#include "event_buffer.h"
#include "athlete_controller.h"
#include "navigation_controller.h"
#include "resource_monitor_controller.h"
#include "clipboard_helper.h"
#include "llm_controller.h"
#include "session_controller.h"
#include "session_review_controller.h"
#include "shot_controller.h"
#include "shot_list_model.h"
#include "../Export/swing_doc.h"
#include "../Export/swing_zip_exporter.h"
#include "shot_processor.h"
#include "shot_replay_controller.h"
#include "live_wrist_angles.h"

int main(int argc, char *argv[])
{
    PinPointDebug::install();
#ifdef HAVE_OPENCV
    // WARN+ logger output still goes to std::cerr (no writer hook before
    // OpenCV 4.11) — captured into PpMessageLog by the cerr tee in
    // PinPointDebug::install() above.
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    cv::redirectError([](int status, const char *func, const char *msg,
                         const char * /*file*/, int /*line*/, void *) -> int {
        ppError() << "[OpenCV]" << (func ? func : "?") << "-" << (msg ? msg : "") << "(status" << status << ")";
        return 0;
    }, nullptr);
#endif
    QGuiApplication app(argc, argv);

    // Must run after the app object exists: loads the Qt Multimedia FFmpeg
    // plugin (which clobbers any earlier av_log callback) and re-installs the
    // FFmpeg → PpMessageLog capture on top of it.
    PinPointDebug::installFfmpegLogCapture();

    // Load bundled fonts so Theme.qml family names resolve on all platforms.
    const QStringList fontResources = {
        ":/fonts/Georgia.ttf",
        ":/fonts/Georgiab.ttf",
        ":/fonts/Georgiai.ttf",
        ":/fonts/Georgiaz.ttf",
        ":/fonts/DMSans-Variable.ttf",
        ":/fonts/DMSans-Italic-Variable.ttf",
        ":/fonts/DMMono-Regular.ttf",
        ":/fonts/DMMono-Medium.ttf",
        ":/fonts/DMSerifDisplay-Regular.ttf",
        ":/fonts/InstrumentSans-Variable.ttf",
        ":/fonts/JetBrainsMono-Variable.ttf",
        ":/fonts/PlayfairDisplay-Variable.ttf",
        ":/fonts/Geist-Variable.ttf",
        ":/fonts/GeistMono-Variable.ttf",
        ":/fonts/SpaceGrotesk-Variable.ttf",
        ":/fonts/SpaceMono-Regular.ttf",
        ":/fonts/SpaceMono-Bold.ttf",
    };
    for (const QString &path : fontResources) {
        if (QFontDatabase::addApplicationFont(path) < 0)
            ppWarn() << "[Fonts] failed to load" << path;
    }

    app.setWindowIcon(QIcon(":/icons/pinpointstudio_256.png"));
    app.setDesktopFileName(QStringLiteral("pinpointstudio"));
    SecretsManager::initializeDefaults();

    // EventBuffer declared first — destroyed last (stack unwinds in reverse).
    // All controllers that hold a pointer to it must be destroyed first.
    pinpoint::EventBuffer   eventBuffer;
    eventBuffer.setLogCallback([](pinpoint::LogSeverity sev, const char *msg) {
        if (sev >= pinpoint::LogSeverity::Error)
            ppError() << msg;
        else
            ppWarn() << msg;
    });
    // Merger runs for app lifetime. With no sources registered yet it
    // auto-pauses; the first registerSource() call auto-resumes it.
    eventBuffer.start();

    AppSettings              appSettings;
    ImuManager              imuManager(&eventBuffer, &appSettings);
    TranscriptionController controller;
    TtsController           ttsController;
    LlmController           llmController;
    CameraManager           cameraManager(&eventBuffer, &appSettings);
    FilmController          filmController;
    BufferController        bufferController(&eventBuffer);
    AthleteController       athleteController;
    // Live lead-wrist metrics (vs neutral) for the wizard "Check your sensor" overlay.
    LiveWristAngles         liveWrist(&imuManager, &appSettings, &athleteController);
    SessionController       sessionController;
    NavigationController    navController(&athleteController, &sessionController);
    ResourceMonitorController resourceMonitor(&eventBuffer, &cameraManager, &imuManager);
    // Registers the shot-marker EventBuffer source; registering a first source
    // auto-resumes the buffer, so restore the user capture intent right after.
    ShotController            shotController(&eventBuffer, &sessionController);
    cameraManager.applyCaptureIntent();
    ShotListModel             shotModel;
    // Bulk "export selected shots to a zip" for the carousel ⋯ menu — derives
    // everything from the shot dirs it is handed, so it needs no dependencies.
    pinpoint::SwingZipExporter swingZipExporter;
    // Reload the current athlete's most recent session so prior shots survive a
    // restart (read-only for now — SwingDocReader parses the unified swing.json;
    // rating/note aren't persisted yet so they come back cleared).
    {
        const QString sessionDir = pinpoint::SwingDocReader::latestSessionDir(
            appSettings.athleteLibraryPath(), athleteController.currentName());
        int restored = 0;
        for (const QString &sd : pinpoint::SwingDocReader::findSwingDirs(sessionDir)) {
            const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(sd);
            if (!ps.ok)
                continue;
            shotModel.addPersistedShot(ps.swingDir, ps.ordinal, ps.timestampLabel, ps.club,
                ps.hasVideo,
                ps.thumbnailPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(ps.thumbnailPath),
                ps.score, ps.rating, ps.note, ps.metrics, ps.analysisDetail);
            ++restored;
        }
        if (restored)
            ppInfo() << "[Reload] restored" << restored << "shots from" << sessionDir;
    }
    // Session review: enumerate saved sessions + load one into a private shot
    // model. Constructed after the reload above so its synthesized live-session
    // row reflects the just-restored shots.
    SessionReviewController   sessionReviewController(&shotModel, &appSettings,
                                                      &athleteController);
    // DATA-SOURCE transition (a past session opened/closed from disk). This is the
    // ONLY axis that pauses capture: opening a loaded session stops live capture
    // (which disarms the shot trigger via the buffer state) and gates ShotController;
    // the session/clock keep running so returning to live resumes the SAME session.
    // The live-capture record state is remembered here and restored on return, so
    // pressing Capture (→ resumeLive) reinstates exactly what was running — and stays
    // idle if capture was idle. The orthogonal session MODE (Capture/Replay/Analyse)
    // is owned by the SessionMode QML singleton; Main.qml lands a loaded session in
    // Replay and a return-to-live in Capture.
    QObject::connect(&sessionReviewController, &SessionReviewController::reviewActiveChanged,
                     [&sessionReviewController, &shotController, &cameraManager,
                      wasCapturing = false]() mutable {
        const bool review = sessionReviewController.reviewActive();
        shotController.setReviewActive(review);
        if (review) {
            wasCapturing = cameraManager.captureIntent();   // remember before pausing
            cameraManager.stopCapture();
        } else if (wasCapturing) {
            wasCapturing = false;
            cameraManager.startCapture();                   // restore on return to live
        }
    });
    // Declared after cameraManager so it is destroyed FIRST: ~ShotProcessor
    // joins the shot workers and destroys the SwingWindow before
    // ~CameraManager deregisters sources and ~EventBuffer frees ring memory.
    ShotProcessor             shotProcessor(&eventBuffer, &cameraManager, &imuManager,
                                            &appSettings, &athleteController,
                                            &sessionController, &shotModel);
    cameraManager.setShotProcessor(&shotProcessor);   // teardown stop-barrier
    imuManager.setShotProcessor(&shotProcessor);      // same barrier for IMU deselect
    // Disk-backed replay of saved shots (MP4 + swing.json) — independent of the
    // live SwingWindow that ShotProcessor owns for the just-captured shot.
    ShotReplayController      shotReplay;
    // Any review-state transition tears down an on-screen disk replay, so the
    // previous shot's replay stage + metric graph don't linger over the newly
    // selected (or resumed-live) session. reviewActiveChanged fires on every
    // loadSession()/resumeLive() — including session→session while already in
    // review — so this covers entering review, switching sessions, and resuming
    // live. stop() is idempotent (no-ops when nothing is replaying).
    QObject::connect(&sessionReviewController, &SessionReviewController::reviewActiveChanged,
                     &shotReplay, &ShotReplayController::stop);
    ClipboardHelper           clipboardHelper;

    // IMU source register/deregister can change the shared EventBuffer state
    // (first-source auto-resume / last-source auto-pause). Re-apply the user
    // capture intent so cameraManager.bufferState — the QML-facing buffer
    // state — stays correct and notifies.
    QObject::connect(&imuManager, &ImuManager::bufferStateChanged,
                     &cameraManager, &CameraManager::applyCaptureIntent);

    // Shot trigger arms/disarms with the buffer state. bufferStateChanged is
    // the single always-notified buffer-state signal (IMU-caused transitions
    // are forwarded through applyCaptureIntent above).
    QObject::connect(&cameraManager, &CameraManager::bufferStateChanged,
                     &shotController, &ShotController::reevaluateArmed);

    // Detected shots drive the processor pipeline (post-roll → window capture
    // → analysis ∥ export → carousel → replay); the processor's busy state
    // disarms the trigger for the whole pipeline.
    QObject::connect(&shotController, &ShotController::shotDetected,
                     &shotProcessor,  &ShotProcessor::onShotDetected);
    QObject::connect(&shotProcessor,  &ShotProcessor::busyChanged,
                     &shotController, [&shotController, &shotProcessor] {
        shotController.setProcessorBusy(shotProcessor.busy());
    });

    // IMU impact auto-detection (shot detection P1, re-pointed at the P3
    // arbiter): candidates funnel through reportCandidate's hold/fuse window
    // rather than committing directly. Gated behind the autoDetectSwing
    // setting. Whole chain is GUI-thread, so a direct connection; the
    // armed() gate inside reportCandidate handles capturing/busy/review.
    QObject::connect(&imuManager, &ImuManager::impactDetected, &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float conf) {
        if (appSettings.autoDetectSwing())
            shotController.reportCandidate(ShotController::Source::Imu,
                                           estImpactUs, conf);
    });

    // Acoustic onset auto-detection (shot detection P2, re-pointed at the P3
    // arbiter like the IMU path). TranscriptionController::impactDetected is
    // emitted on the AUDIO thread with est_t* already computed; the
    // &shotController context makes this a queued hop onto the GUI thread.
    controller.setAcousticLatencyUs(appSettings.audioDeviceLatencyUs());
    QObject::connect(&appSettings, &AppSettings::audioDeviceLatencyUsChanged,
                     &controller, [&controller, &appSettings] {
        controller.setAcousticLatencyUs(appSettings.audioDeviceLatencyUs());
    });
    // Microphone selection + acoustic sensitivity — pushed at startup and kept
    // live. setInputDevice before the first capture so the saved device is used.
    controller.setInputDevice(appSettings.audioInputDevice());
    QObject::connect(&appSettings, &AppSettings::audioInputDeviceChanged,
                     &controller, [&controller, &appSettings] {
        controller.setInputDevice(appSettings.audioInputDevice());
    });
    // Acoustic "sensitivity" [0,1] sets the absolute amplitude gate — the
    // candidate-open threshold floor that keeps quiet ticks/ambient from firing
    // (and from masking real impacts). Mapped on a log scale across the useful
    // envelope range: s=0 → 0.30 (only loud events), s=1 → 0.01 (very sensitive),
    // s=0.5 → ~0.055. The meter draws this level so the user can sit it between
    // their ambient/keyboard noise and their club impacts.
    const auto applyAcousticSensitivity = [&controller, &appSettings] {
        const double s = appSettings.acousticSensitivity();   // already clamped [0,1]
        controller.setAcousticMinLevel(0.01 * std::pow(30.0, 1.0 - s));
    };
    applyAcousticSensitivity();
    QObject::connect(&appSettings, &AppSettings::acousticSensitivityChanged,
                     &controller, applyAcousticSensitivity);

    // Run the microphone (hence the acoustic detector + its calibration) while a
    // capturing session is live. Without this the mic only ran on the Audio page
    // / calibration view, so acoustic detection — and the calibrated gate — never
    // applied during real sessions. captureIntent is session-stable (doesn't
    // toggle per-shot). Gated by the acoustic enable + autoDetectSwing so the mic
    // stays off whenever acoustic can't contribute a candidate.
    const auto applyShotAudio = [&controller, &cameraManager, &appSettings] {
        controller.setShotDetectionActive(cameraManager.captureIntent()
                                          && appSettings.acousticShotDetectionEnabled()
                                          && appSettings.autoDetectSwing());
    };
    applyShotAudio();
    QObject::connect(&cameraManager, &CameraManager::captureIntentChanged,
                     &controller, applyShotAudio);
    QObject::connect(&appSettings, &AppSettings::acousticShotDetectionEnabledChanged,
                     &controller, applyShotAudio);
    QObject::connect(&appSettings, &AppSettings::autoDetectSwingChanged,
                     &controller, applyShotAudio);
    QObject::connect(&controller, &TranscriptionController::impactDetected,
                     &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float conf) {
        // Raw detector fires always (so calibration sees them); only feed the
        // arbiter when shot detection is on AND acoustic is enabled. Voice/STT
        // is independent and unaffected by acousticShotDetectionEnabled.
        if (appSettings.autoDetectSwing() && appSettings.acousticShotDetectionEnabled())
            shotController.reportCandidate(ShotController::Source::Acoustic,
                                           estImpactUs, conf);
    });
    // Vision corroboration (P3-G5) joins the same funnel via
    // reportCandidate(Source::Ball, ...) once the ball detector grows its
    // Kalman-track ballLaunched(timestampUs) signal (ball_detector_design.md
    // §8) — today's ballPresentChanged is smoothed over a 50-frame window,
    // far too coarse for the ±40 ms match tolerance.

    // Voice input: completed STT transcription → coach chat (when voice input enabled).
    QObject::connect(&controller, &TranscriptionController::transcriptionReceived,
                     &llmController, [&llmController](const QString &text) {
        if (llmController.voiceInputEnabled())
            llmController.sendMessage(text);
    });

    // Voice output: completed LLM response → TTS (when voice output enabled).
    QObject::connect(&llmController, &LlmController::responseReady,
                     &ttsController, [&ttsController, &llmController](const QString &text) {
        if (llmController.voiceOutputEnabled())
            ttsController.speak(text);
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"),       &appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("athleteController"), &athleteController);
    engine.rootContext()->setContextProperty(QStringLiteral("navController"),     &navController);
    engine.rootContext()->setContextProperty(QStringLiteral("imuManager"),       &imuManager);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"),       &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("ttsController"),    &ttsController);
    engine.rootContext()->setContextProperty(QStringLiteral("llmController"),    &llmController);
    engine.rootContext()->setContextProperty(QStringLiteral("cameraManager"),    &cameraManager);
    engine.rootContext()->setContextProperty(QStringLiteral("filmController"),   &filmController);
    engine.rootContext()->setContextProperty(QStringLiteral("bufferController"), &bufferController);
    engine.rootContext()->setContextProperty(QStringLiteral("resourceMonitor"),  &resourceMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionController"), &sessionController);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionReviewController"), &sessionReviewController);
    engine.rootContext()->setContextProperty(QStringLiteral("shotController"),    &shotController);
    engine.rootContext()->setContextProperty(QStringLiteral("shotProcessor"),     &shotProcessor);
    engine.rootContext()->setContextProperty(QStringLiteral("shotReplay"),        &shotReplay);
    engine.rootContext()->setContextProperty(QStringLiteral("shotModel"),         &shotModel);
    engine.rootContext()->setContextProperty(QStringLiteral("swingExporter"),     &swingZipExporter);
    engine.rootContext()->setContextProperty(QStringLiteral("liveWrist"),         &liveWrist);
    engine.rootContext()->setContextProperty(QStringLiteral("clipboard"),         &clipboardHelper);

    // Clean merger shutdown before Qt tears down its event loop.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&eventBuffer]() {
        eventBuffer.stop();
    });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("PinPointStudio", "Main");

    return QCoreApplication::exec();
}
