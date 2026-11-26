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

// GuiApp — `app` as QML reaches it (refactor step 5.8b; R-QML-1: "context globals are only devices, settings, logging
// and `app`"). It IS the App (src/App/app.h: state, athlete, session, sessionAt and the shot command door), made by the
// composition root, with the app-wide objects that were context globals of their own hung from it under the same
// names: QML reads `app.sessionReviewController` where it read `sessionReviewController`.
//
// WHY A SUBCLASS IN Gui. The objects are the Adapters ring's (src/Gui); App is the Application ring's (src/App), and
// rings point inward (information_model.md §4), so App cannot name them. This class adds no object, owner, fact or
// lifecycle: each member is an object main() already makes and owns, handed over once before the QML engine loads
// (setMembers), so every property is CONSTANT. They are QObject *: QML reads each through its own meta-object, as it
// read the context property.
//
// THE STUDIO PROFILE (`app.studioProfile`) joined them in the platform refactor, when it got its repository: it was a
// context global of its own, and the information model reaches it through `app` (OBJ-STUDIO-PROFILE). Its class is the
// Application ring's (src/App/studio_profile.h); it hangs here, as a QObject * like the others, so `App` and every test
// that builds it need not link it.
//
// WHAT STAYS A CONTEXT GLOBAL (main.cpp): the devices (cameraManager, imuManager, launchMonitor, trunkCeremony,
// bufferController, cudaRuntime, resourceMonitor, liveWrist, ppcpHost, ppcpOffers, the voice in and out: controller,
// ttsController), the settings (appSettings, secrets) and the logging (appLog, profiler).

#include "../../App/app.h"

#include <QObject>

namespace pinpoint {

class GuiApp : public App
{
    Q_OBJECT
    Q_PROPERTY(QObject *athleteController       READ athleteController       CONSTANT)
    Q_PROPERTY(QObject *sessionController       READ sessionController       CONSTANT)
    Q_PROPERTY(QObject *sessionReviewController READ sessionReviewController CONSTANT)
    Q_PROPERTY(QObject *navController           READ navController           CONSTANT)
    Q_PROPERTY(QObject *diagnosticsAdapters     READ diagnosticsAdapters     CONSTANT)
    Q_PROPERTY(QObject *workOns                 READ workOns                 CONSTANT)
    Q_PROPERTY(QObject *shotController          READ shotController          CONSTANT)
    Q_PROPERTY(QObject *shotProcessor           READ shotProcessor           CONSTANT)
    Q_PROPERTY(QObject *shotModel               READ shotModel               CONSTANT)
    Q_PROPERTY(QObject *reanalysisController    READ reanalysisController    CONSTANT)
    Q_PROPERTY(QObject *markupController        READ markupController        CONSTANT)
    Q_PROPERTY(QObject *sessionReport           READ sessionReport           CONSTANT)
    Q_PROPERTY(QObject *sessionReportList       READ sessionReportList       CONSTANT)
    Q_PROPERTY(QObject *reportMailer            READ reportMailer            CONSTANT)
    Q_PROPERTY(QObject *swingExporter           READ swingExporter           CONSTANT)
    Q_PROPERTY(QObject *archiveController       READ archiveController       CONSTANT)
    Q_PROPERTY(QObject *libraryConverter        READ libraryConverter        CONSTANT)
    Q_PROPERTY(QObject *updateController        READ updateController        CONSTANT)
    Q_PROPERTY(QObject *llmController           READ llmController           CONSTANT)
    Q_PROPERTY(QObject *notifications           READ notifications           CONSTANT)
    Q_PROPERTY(QObject *motionCaptureProbe      READ motionCaptureProbe      CONSTANT)
    Q_PROPERTY(QObject *appInfo                 READ appInfo                 CONSTANT)
    Q_PROPERTY(QObject *clipboard               READ clipboard               CONSTANT)
    Q_PROPERTY(QObject *studioProfile           READ studioProfile           CONSTANT)
public:
    // The objects main() makes, by their QML names. Set once, before the engine loads anything.
    struct Members {
        QObject *athleteController       = nullptr;
        QObject *sessionController       = nullptr;
        QObject *sessionReviewController = nullptr;
        QObject *navController           = nullptr;
        QObject *diagnosticsAdapters     = nullptr;
        QObject *workOns                 = nullptr;
        QObject *shotController          = nullptr;
        QObject *shotProcessor           = nullptr;
        QObject *shotModel               = nullptr;
        QObject *reanalysisController    = nullptr;
        QObject *markupController        = nullptr;
        QObject *sessionReport           = nullptr;
        QObject *sessionReportList       = nullptr;
        QObject *reportMailer            = nullptr;
        QObject *swingExporter           = nullptr;
        QObject *archiveController       = nullptr;
        QObject *libraryConverter        = nullptr;
        QObject *updateController        = nullptr;
        QObject *llmController           = nullptr;
        QObject *notifications           = nullptr;
        QObject *motionCaptureProbe      = nullptr;
        QObject *appInfo                 = nullptr;
        QObject *clipboard               = nullptr;
        QObject *studioProfile           = nullptr;
    };

    using App::App;

    void setMembers(const Members &m) { m_m = m; }

    QObject *athleteController() const       { return m_m.athleteController; }
    QObject *sessionController() const       { return m_m.sessionController; }
    QObject *sessionReviewController() const { return m_m.sessionReviewController; }
    QObject *navController() const           { return m_m.navController; }
    QObject *diagnosticsAdapters() const     { return m_m.diagnosticsAdapters; }
    QObject *workOns() const                 { return m_m.workOns; }
    QObject *shotController() const          { return m_m.shotController; }
    QObject *shotProcessor() const           { return m_m.shotProcessor; }
    QObject *shotModel() const               { return m_m.shotModel; }
    QObject *reanalysisController() const    { return m_m.reanalysisController; }
    QObject *markupController() const        { return m_m.markupController; }
    QObject *sessionReport() const           { return m_m.sessionReport; }
    QObject *sessionReportList() const       { return m_m.sessionReportList; }
    QObject *reportMailer() const            { return m_m.reportMailer; }
    QObject *swingExporter() const           { return m_m.swingExporter; }
    QObject *archiveController() const       { return m_m.archiveController; }
    QObject *libraryConverter() const        { return m_m.libraryConverter; }
    QObject *updateController() const        { return m_m.updateController; }
    QObject *llmController() const           { return m_m.llmController; }
    QObject *notifications() const           { return m_m.notifications; }
    QObject *motionCaptureProbe() const      { return m_m.motionCaptureProbe; }
    QObject *appInfo() const                 { return m_m.appInfo; }
    QObject *clipboard() const               { return m_m.clipboard; }
    QObject *studioProfile() const           { return m_m.studioProfile; }

private:
    Members m_m;
};

} // namespace pinpoint
