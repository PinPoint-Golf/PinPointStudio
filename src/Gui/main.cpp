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
#include "arm_bone_controller.h"
#include "llm_controller.h"
#include "session_controller.h"

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
    NavigationController    navController(&athleteController);
    ResourceMonitorController resourceMonitor(&eventBuffer, &cameraManager, &imuManager);
    cameraManager.setAthleteController(&athleteController);   // swing export metadata
    ArmBoneController         armBoneController;
    SessionController         sessionController;

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
    engine.rootContext()->setContextProperty(QStringLiteral("armBoneController"), &armBoneController);
    engine.rootContext()->setContextProperty(QStringLiteral("sessionController"), &sessionController);

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
