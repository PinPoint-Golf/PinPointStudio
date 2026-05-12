#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "pp_debug.h"
#include "SecretsManager.h"
#include "film_controller.h"
#include "imu_controller.h"
#include "transcription_controller.h"
#include "tts_controller.h"
#include "camera_manager.h"

int main(int argc, char *argv[])
{
    PinPointDebug::install();
    QGuiApplication app(argc, argv);
    SecretsManager::initializeDefaults();

    ImuController           imuController;
    TranscriptionController controller;
    TtsController           ttsController;
    CameraManager           cameraManager;
    FilmController          filmController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("imuController"),    &imuController);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"),       &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("ttsController"),    &ttsController);
    engine.rootContext()->setContextProperty(QStringLiteral("cameraManager"),    &cameraManager);
    engine.rootContext()->setContextProperty(QStringLiteral("filmController"),   &filmController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("PinPoint", "Main");

    return QCoreApplication::exec();
}
