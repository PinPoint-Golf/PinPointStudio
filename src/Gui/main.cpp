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
#include "shot_processor.h"
#include "shot_replay_controller.h"
#include "live_wrist_angles.h"

int main(int argc, char *argv[])
{
    PinPointDebug::install();
#ifdef HAVE_OPENCV
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
    // Entering review stops live capture (which disarms the shot trigger via the
    // buffer state) and explicitly gates ShotController; the session/clock keep
    // running so resumeLive() returns to the same live session. Capture is NOT
    // auto-resumed on resumeLive — the user presses Capture again.
    QObject::connect(&sessionReviewController, &SessionReviewController::reviewActiveChanged,
                     [&sessionReviewController, &shotController, &cameraManager] {
        const bool review = sessionReviewController.reviewActive();
        shotController.setReviewActive(review);
        if (review)
            cameraManager.stopCapture();
    });
    // Declared after cameraManager so it is destroyed FIRST: ~ShotProcessor
    // joins the shot workers and destroys the SwingWindow before
    // ~CameraManager deregisters sources and ~EventBuffer frees ring memory.
    ShotProcessor             shotProcessor(&eventBuffer, &cameraManager, &imuManager,
                                            &appSettings, &athleteController,
                                            &sessionController, &shotModel);
    cameraManager.setShotProcessor(&shotProcessor);   // teardown stop-barrier
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

    // IMU impact auto-trigger (shot detection P1) — gated behind the
    // autoDetectSwing setting (default OFF until the P3 arbiter adds
    // cross-modal confirmation). Whole chain is GUI-thread, so a direct
    // connection; the armed() gate inside triggerShot already handles
    // capturing/busy/review.
    QObject::connect(&imuManager, &ImuManager::impactDetected, &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float) {
        if (appSettings.autoDetectSwing())
            shotController.triggerShot(ShotController::Source::Imu, estImpactUs);
    });

    // Acoustic onset auto-trigger (shot detection P2) — same autoDetectSwing
    // gate as the IMU path until the P3 arbiter fuses candidates.
    // TranscriptionController::impactDetected is emitted on the AUDIO thread
    // with est_t* already computed; the &shotController context makes this a
    // queued hop onto the GUI thread.
    controller.setAcousticLatencyUs(appSettings.audioDeviceLatencyUs());
    QObject::connect(&appSettings, &AppSettings::audioDeviceLatencyUsChanged,
                     &controller, [&controller, &appSettings] {
        controller.setAcousticLatencyUs(appSettings.audioDeviceLatencyUs());
    });
    QObject::connect(&controller, &TranscriptionController::impactDetected,
                     &shotController,
                     [&shotController, &appSettings](qint64 estImpactUs, float) {
        if (appSettings.autoDetectSwing())
            shotController.triggerShot(ShotController::Source::Acoustic, estImpactUs);
    });

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
