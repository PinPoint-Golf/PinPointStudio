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
