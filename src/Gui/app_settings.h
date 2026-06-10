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
#include <QStringList>
#include <QVariantMap>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include "pp_settings.h"
#include "version.h"

// Must be declared outside AppSettings — moc does not support Q_GADGET in nested classes.
struct StorageInfo {
    Q_GADGET
    Q_PROPERTY(qint64  totalBytes   MEMBER totalBytes)
    Q_PROPERTY(qint64  freeBytes    MEMBER freeBytes)
    Q_PROPERTY(qint64  sessionBytes MEMBER sessionBytes)
    Q_PROPERTY(QString volumeName   MEMBER volumeName)
public:
    qint64  totalBytes   = 0;
    qint64  freeBytes    = 0;
    qint64  sessionBytes = 0;
    QString volumeName;
};
Q_DECLARE_METATYPE(StorageInfo)

class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appVersion    READ appVersion    CONSTANT)
    Q_PROPERTY(int     themeIndex   READ themeIndex   WRITE setThemeIndex   NOTIFY themeIndexChanged)
    Q_PROPERTY(int     windowWidth  READ windowWidth  WRITE setWindowWidth  NOTIFY windowWidthChanged)
    Q_PROPERTY(int     windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(double  fontScale    READ fontScale    WRITE setFontScale    NOTIFY fontScaleChanged)
    Q_PROPERTY(QString density      READ density      WRITE setDensity      NOTIFY densityChanged)
    Q_PROPERTY(bool    reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    Q_PROPERTY(double  overlayOpacity  READ overlayOpacity  WRITE setOverlayOpacity  NOTIFY overlayOpacityChanged)
    Q_PROPERTY(bool    windowMaximized READ windowMaximized WRITE setWindowMaximized NOTIFY windowMaximizedChanged)
    Q_PROPERTY(int     windowX         READ windowX         WRITE setWindowX         NOTIFY windowXChanged)
    Q_PROPERTY(int     windowY         READ windowY         WRITE setWindowY         NOTIFY windowYChanged)
    Q_PROPERTY(QString language                    READ language                    WRITE setLanguage                    NOTIFY languageChanged)
    Q_PROPERTY(QString units                       READ units                       WRITE setUnits                       NOTIFY unitsChanged)
    Q_PROPERTY(QString athleteLibraryPath          READ athleteLibraryPath          WRITE setAthleteLibraryPath          NOTIFY athleteLibraryPathChanged)
    Q_PROPERTY(bool    autoSaveSession             READ autoSaveSession             WRITE setAutoSaveSession             NOTIFY autoSaveSessionChanged)
    Q_PROPERTY(bool    autoDetectSwing             READ autoDetectSwing             WRITE setAutoDetectSwing             NOTIFY autoDetectSwingChanged)
    Q_PROPERTY(QString swingDetectionSensitivity   READ swingDetectionSensitivity   WRITE setSwingDetectionSensitivity   NOTIFY swingDetectionSensitivityChanged)
    Q_PROPERTY(int     audioDeviceLatencyUs        READ audioDeviceLatencyUs        WRITE setAudioDeviceLatencyUs        NOTIFY audioDeviceLatencyUsChanged)
    Q_PROPERTY(bool    aiCoachingOnSessionEnd      READ aiCoachingOnSessionEnd      WRITE setAiCoachingOnSessionEnd      NOTIFY aiCoachingOnSessionEndChanged)
    Q_PROPERTY(bool    checkForUpdates             READ checkForUpdates             WRITE setCheckForUpdates             NOTIFY checkForUpdatesChanged)
    Q_PROPERTY(bool    sendDiagnostics             READ sendDiagnostics             WRITE setSendDiagnostics             NOTIFY sendDiagnosticsChanged)
    Q_PROPERTY(QString mainDisplayMode             READ mainDisplayMode             WRITE setMainDisplayMode             NOTIFY mainDisplayModeChanged)
    Q_PROPERTY(bool    rememberWindowGeometry      READ rememberWindowGeometry      WRITE setRememberWindowGeometry      NOTIFY rememberWindowGeometryChanged)
    Q_PROPERTY(QString secondaryDisplayMode        READ secondaryDisplayMode        WRITE setSecondaryDisplayMode        NOTIFY secondaryDisplayModeChanged)
    Q_PROPERTY(QString postShotContent             READ postShotContent             WRITE setPostShotContent             NOTIFY postShotContentChanged)
    Q_PROPERTY(double  postShotDelay               READ postShotDelay               WRITE setPostShotDelay               NOTIFY postShotDelayChanged)
    Q_PROPERTY(bool    postShotMirror              READ postShotMirror              WRITE setPostShotMirror              NOTIFY postShotMirrorChanged)
    Q_PROPERTY(QString uiFrameRateCap              READ uiFrameRateCap              WRITE setUiFrameRateCap              NOTIFY uiFrameRateCapChanged)
    Q_PROPERTY(bool    hardwareAcceleration        READ hardwareAcceleration        WRITE setHardwareAcceleration        NOTIFY hardwareAccelerationChanged)
    Q_PROPERTY(QStringList cameraExcluded  READ cameraExcluded  WRITE setCameraExcluded  NOTIFY cameraExcludedChanged)
    Q_PROPERTY(QVariantMap cameraTargetFps READ cameraTargetFps WRITE setCameraTargetFps NOTIFY cameraTargetFpsChanged)
    Q_PROPERTY(QVariantMap cameraTriggerMode READ cameraTriggerMode WRITE setCameraTriggerMode NOTIFY cameraTriggerModeChanged)
    Q_PROPERTY(QVariantMap cameraRoi         READ cameraRoi         WRITE setCameraRoi         NOTIFY cameraRoiChanged)
    Q_PROPERTY(QVariantMap cameraPerspective READ cameraPerspective WRITE setCameraPerspective NOTIFY cameraPerspectiveChanged)
    Q_PROPERTY(QVariantMap cameraIsMirrored  READ cameraIsMirrored  WRITE setCameraIsMirrored  NOTIFY cameraIsMirroredChanged)
    Q_PROPERTY(double      cameraPreroll       READ cameraPreroll       WRITE setCameraPreroll       NOTIFY cameraPrerollChanged)
    Q_PROPERTY(bool        cameraSyncEnabled   READ cameraSyncEnabled   WRITE setCameraSyncEnabled   NOTIFY cameraSyncEnabledChanged)
    Q_PROPERTY(QVariantMap cameraFixedInPlace  READ cameraFixedInPlace  WRITE setCameraFixedInPlace  NOTIFY cameraFixedInPlaceChanged)
    Q_PROPERTY(QVariantMap cameraAlias         READ cameraAlias         WRITE setCameraAlias         NOTIFY cameraAliasChanged)
    Q_PROPERTY(QStringList imuExcluded            READ imuExcluded            WRITE setImuExcluded            NOTIFY imuExcludedChanged)
    Q_PROPERTY(QVariantMap imuAlias               READ imuAlias               WRITE setImuAlias               NOTIFY imuAliasChanged)
    Q_PROPERTY(QVariantMap imuCalibration         READ imuCalibration         WRITE setImuCalibration         NOTIFY imuCalibrationChanged)
    Q_PROPERTY(QVariantMap imuPlacement           READ imuPlacement           WRITE setImuPlacement           NOTIFY imuPlacementChanged)
    Q_PROPERTY(QVariantMap imuOutputRateHz        READ imuOutputRateHz        WRITE setImuOutputRateHz        NOTIFY imuOutputRateHzChanged)
    Q_PROPERTY(QVariantMap imuFusionMode          READ imuFusionMode          WRITE setImuFusionMode          NOTIFY imuFusionModeChanged)
    Q_PROPERTY(QVariantMap imuMountOrientation    READ imuMountOrientation    WRITE setImuMountOrientation    NOTIFY imuMountOrientationChanged)
    Q_PROPERTY(bool        imuAutoConnect         READ imuAutoConnect         WRITE setImuAutoConnect         NOTIFY imuAutoConnectChanged)
    Q_PROPERTY(bool        imuAutoReconnect       READ imuAutoReconnect       WRITE setImuAutoReconnect       NOTIFY imuAutoReconnectChanged)
    Q_PROPERTY(bool        imuSaveCalibrationToFlash READ imuSaveCalibrationToFlash WRITE setImuSaveCalibrationToFlash NOTIFY imuSaveCalibrationToFlashChanged)
    Q_PROPERTY(QString     imuDefaultFusionMode   READ imuDefaultFusionMode   WRITE setImuDefaultFusionMode   NOTIFY imuDefaultFusionModeChanged)
    // Software orientation-fusion algorithm: "Madgwick" (default) or "ESKF".
    // Distinct from imuFusionMode/imuDefaultFusionMode (the device's 6/9-axis
    // magnetometer mode). See iorientation_filter.h.
    Q_PROPERTY(QString     imuOrientationFilter   READ imuOrientationFilter   WRITE setImuOrientationFilter   NOTIFY imuOrientationFilterChanged)
    Q_PROPERTY(QVariantMap sessionGoalsByType READ sessionGoalsByType WRITE setSessionGoalsByType NOTIFY sessionGoalsByTypeChanged)
    Q_PROPERTY(int         lastSessionType   READ lastSessionType   WRITE setLastSessionType   NOTIFY lastSessionTypeChanged)

    Q_PROPERTY(QString sessionNamingPattern  READ sessionNamingPattern  WRITE setSessionNamingPattern  NOTIFY sessionNamingPatternChanged)
    Q_PROPERTY(QString videoResolutionMode   READ videoResolutionMode   WRITE setVideoResolutionMode   NOTIFY videoResolutionModeChanged)
    Q_PROPERTY(QString videoCodec            READ videoCodec            WRITE setVideoCodec            NOTIFY videoCodecChanged)
    Q_PROPERTY(QString videoQuality          READ videoQuality          WRITE setVideoQuality          NOTIFY videoQualityChanged)
    Q_PROPERTY(QString videoContainer        READ videoContainer        WRITE setVideoContainer        NOTIFY videoContainerChanged)
    Q_PROPERTY(bool    saveRawFrames         READ saveRawFrames         WRITE setSaveRawFrames         NOTIFY saveRawFramesChanged)
    Q_PROPERTY(bool    savePoseKeypoints     READ savePoseKeypoints     WRITE setSavePoseKeypoints     NOTIFY savePoseKeypointsChanged)
    Q_PROPERTY(bool    saveImuStreams        READ saveImuStreams        WRITE setImuStreams            NOTIFY saveImuStreamsChanged)
    Q_PROPERTY(QString imuDataFormat         READ imuDataFormat         WRITE setImuDataFormat         NOTIFY imuDataFormatChanged)
    Q_PROPERTY(bool    saveLaunchMonitorData READ saveLaunchMonitorData WRITE setSaveLaunchMonitorData NOTIFY saveLaunchMonitorDataChanged)

public:
    Q_INVOKABLE StorageInfo queryStorageInfo() const;
    explicit AppSettings(QObject *parent = nullptr) : QObject(parent)
    {
        m_themeIndex      = ppSettings().value(QStringLiteral("ui/themeIndex"),      0).toInt();
        m_windowWidth     = ppSettings().value(QStringLiteral("ui/windowWidth"),     1120).toInt();
        m_windowHeight    = ppSettings().value(QStringLiteral("ui/windowHeight"),    700).toInt();
        m_fontScale       = ppSettings().value(QStringLiteral("ui/fontScale"),       -1.0).toDouble();
        m_density         = ppSettings().value(QStringLiteral("ui/density"),         QStringLiteral("default")).toString();
        m_reduceMotion    = ppSettings().value(QStringLiteral("ui/reduceMotion"),    false).toBool();
        m_overlayOpacity  = ppSettings().value(QStringLiteral("ui/overlayOpacity"),  0.7).toDouble();
        m_windowMaximized = ppSettings().value(QStringLiteral("ui/windowMaximized"), false).toBool();
        m_windowX         = ppSettings().value(QStringLiteral("ui/windowX"),         -1).toInt();
        m_windowY         = ppSettings().value(QStringLiteral("ui/windowY"),         -1).toInt();

        // NOTE: these keys use the capitalised "General/" group on purpose. QSettings
        // serialises any "General/..." key to the INI's [%General] section (to keep it
        // out of the reserved [General] section used for top-level keys). It always
        // un-escapes [%General] back to the canonical capital "General" on the next
        // launch — so a lowercase "general/..." key is written but read back as
        // "General/...", and never round-trips. Writing "General/..." matches what
        // Qt reads back, so values persist across restarts.
        m_language                  = ppSettings().value(QStringLiteral("General/language"),                  QStringLiteral("en_GB")).toString();
        m_units                     = ppSettings().value(QStringLiteral("General/units"),                     QStringLiteral("mph")).toString();
        m_athleteLibraryPath        = ppSettings().value(QStringLiteral("General/athleteLibraryPath"),        QStringLiteral("")).toString();
        m_autoSaveSession           = ppSettings().value(QStringLiteral("General/autoSaveSession"),           true).toBool();
        // Default OFF while the IMU auto-trigger is single-modality (shot
        // detection P1); flips back ON when the P3 arbiter adds cross-modal
        // confirmation.
        m_autoDetectSwing           = ppSettings().value(QStringLiteral("General/autoDetectSwing"),           false).toBool();
        m_swingDetectionSensitivity = ppSettings().value(QStringLiteral("General/swingDetectionSensitivity"), QStringLiteral("Medium")).toString();
        // Mic capture-chain delay used to back-date acoustic shot onsets
        // (shot detection P2); a fixed estimate until P4 auto-calibration.
        m_audioDeviceLatencyUs      = ppSettings().value(QStringLiteral("General/audioDeviceLatencyUs"),      20000).toInt();
        m_aiCoachingOnSessionEnd    = ppSettings().value(QStringLiteral("General/aiCoachingOnSessionEnd"),    true).toBool();
        m_checkForUpdates           = ppSettings().value(QStringLiteral("General/checkForUpdates"),           true).toBool();
        m_sendDiagnostics           = ppSettings().value(QStringLiteral("General/sendDiagnostics"),           false).toBool();

        m_mainDisplayMode        = ppSettings().value(QStringLiteral("display/mainDisplayMode"),        QStringLiteral("primary")).toString();
        m_rememberWindowGeometry = ppSettings().value(QStringLiteral("display/rememberWindowGeometry"), true).toBool();
        m_secondaryDisplayMode   = ppSettings().value(QStringLiteral("display/secondaryDisplayMode"),   QStringLiteral("none")).toString();
        m_postShotContent        = ppSettings().value(QStringLiteral("display/postShotContent"),        QStringLiteral("replay")).toString();
        m_postShotDelay          = ppSettings().value(QStringLiteral("display/postShotDelay"),          0.5).toDouble();
        m_postShotMirror         = ppSettings().value(QStringLiteral("display/postShotMirror"),         false).toBool();
        m_uiFrameRateCap         = ppSettings().value(QStringLiteral("display/uiFrameRateCap"),         QStringLiteral("display")).toString();
        m_hardwareAcceleration   = ppSettings().value(QStringLiteral("display/hardwareAcceleration"),   true).toBool();

        m_cameraExcluded    = ppSettings().value(QStringLiteral("camera/excluded"),    QStringList{}).toStringList();
        m_cameraTargetFps   = ppSettings().value(QStringLiteral("camera/targetFps"),   QVariantMap{}).toMap();
        m_cameraTriggerMode = ppSettings().value(QStringLiteral("camera/triggerMode"), QVariantMap{}).toMap();
        m_cameraRoi         = ppSettings().value(QStringLiteral("camera/roi"),         QVariantMap{}).toMap();
        m_cameraPerspective = ppSettings().value(QStringLiteral("camera/perspective"), QVariantMap{}).toMap();
        m_cameraIsMirrored  = ppSettings().value(QStringLiteral("camera/isMirrored"),  QVariantMap{}).toMap();
        m_cameraPreroll     = ppSettings().value(QStringLiteral("camera/preroll"),     1.0).toDouble();
        m_cameraSyncEnabled  = ppSettings().value(QStringLiteral("camera/syncEnabled"),    true).toBool();
        m_cameraFixedInPlace = ppSettings().value(QStringLiteral("camera/fixedInPlace"), QVariantMap{}).toMap();
        m_cameraAlias        = ppSettings().value(QStringLiteral("camera/alias"),        QVariantMap{}).toMap();

        m_imuExcluded             = ppSettings().value(QStringLiteral("imu/excluded"),             QStringList{}).toStringList();
        m_imuPlacement            = ppSettings().value(QStringLiteral("imu/placement"),            QVariantMap{}).toMap();
        m_imuOutputRateHz         = ppSettings().value(QStringLiteral("imu/outputRateHz"),         QVariantMap{}).toMap();
        m_imuFusionMode           = ppSettings().value(QStringLiteral("imu/fusionMode"),           QVariantMap{}).toMap();
        m_imuMountOrientation     = ppSettings().value(QStringLiteral("imu/mountOrientation"),     QVariantMap{}).toMap();
        m_imuAutoConnect          = ppSettings().value(QStringLiteral("imu/autoConnect"),          true).toBool();
        m_imuAutoReconnect        = ppSettings().value(QStringLiteral("imu/autoReconnect"),        true).toBool();
        m_imuSaveCalibrationToFlash = ppSettings().value(QStringLiteral("imu/saveCalibrationToFlash"), false).toBool();
        m_imuDefaultFusionMode    = ppSettings().value(QStringLiteral("imu/defaultFusionMode"),    QStringLiteral("9axis")).toString();
        m_imuOrientationFilter    = ppSettings().value(QStringLiteral("imu/orientationFilter"),    QStringLiteral("Madgwick")).toString();
        m_imuAlias                = ppSettings().value(QStringLiteral("imu/alias"),                QVariantMap{}).toMap();
        m_imuCalibration          = ppSettings().value(QStringLiteral("imu/calibration"),          QVariantMap{}).toMap();

        m_sessionGoalsByType = ppSettings().value(QStringLiteral("session/goalsByType"), QVariantMap{}).toMap();
        m_lastSessionType    = ppSettings().value(QStringLiteral("session/lastType"), 0).toInt();

        m_sessionNamingPattern  = ppSettings().value(QStringLiteral("storage/sessionNamingPattern"),  QStringLiteral("date-name-type")).toString();
        m_videoResolutionMode   = ppSettings().value(QStringLiteral("storage/videoResolutionMode"),   QStringLiteral("native")).toString();
        m_videoCodec            = ppSettings().value(QStringLiteral("storage/videoCodec"),            QStringLiteral("h264")).toString();
        // Codecs were rationalised to the cross-platform set; coerce a persisted
        // retired value ("prores"/"raw") so the UI selection and exporter agree.
        if (m_videoCodec != QLatin1String("h264") && m_videoCodec != QLatin1String("h265"))
            m_videoCodec = QStringLiteral("h264");
        m_videoQuality          = ppSettings().value(QStringLiteral("storage/videoQuality"),          QStringLiteral("medium")).toString();
        m_videoContainer        = ppSettings().value(QStringLiteral("storage/videoContainer"),        QStringLiteral("mp4")).toString();
        m_saveRawFrames         = ppSettings().value(QStringLiteral("storage/saveRawFrames"),         false).toBool();
        m_savePoseKeypoints     = ppSettings().value(QStringLiteral("storage/savePoseKeypoints"),     true).toBool();
        m_saveImuStreams         = ppSettings().value(QStringLiteral("storage/saveImuStreams"),        true).toBool();
        m_imuDataFormat         = ppSettings().value(QStringLiteral("storage/imuDataFormat"),         QStringLiteral("json")).toString();
        m_saveLaunchMonitorData = ppSettings().value(QStringLiteral("storage/saveLaunchMonitorData"), true).toBool();
    }

    QString appVersion()     const { return QStringLiteral(PINPOINT_VERSION_STRING); }
    int     themeIndex()    const { return m_themeIndex; }
    int     windowWidth()   const { return m_windowWidth; }
    int     windowHeight()  const { return m_windowHeight; }
    double  fontScale()     const { return m_fontScale; }
    QString density()       const { return m_density; }
    bool    reduceMotion()  const { return m_reduceMotion; }
    double  overlayOpacity()  const { return m_overlayOpacity; }
    bool    windowMaximized() const { return m_windowMaximized; }
    int     windowX()         const { return m_windowX; }
    int     windowY()         const { return m_windowY; }

    // Returns the index into Qt.application.screens of the screen containing the cursor.
    Q_INVOKABLE int cursorScreenIndex() const
    {
        const QPoint pos = QCursor::pos();
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) {
            if (screens[i]->geometry().contains(pos))
                return i;
        }
        return 0;
    }

    QString language()                  const { return m_language; }
    QString units()                     const { return m_units; }
    QString athleteLibraryPath()        const { return m_athleteLibraryPath; }
    bool    autoSaveSession()           const { return m_autoSaveSession; }
    bool    autoDetectSwing()           const { return m_autoDetectSwing; }
    QString swingDetectionSensitivity() const { return m_swingDetectionSensitivity; }
    int     audioDeviceLatencyUs()      const { return m_audioDeviceLatencyUs; }
    bool    aiCoachingOnSessionEnd()    const { return m_aiCoachingOnSessionEnd; }
    bool    checkForUpdates()           const { return m_checkForUpdates; }
    bool    sendDiagnostics()           const { return m_sendDiagnostics; }

    QString mainDisplayMode()        const { return m_mainDisplayMode; }
    bool    rememberWindowGeometry() const { return m_rememberWindowGeometry; }
    QString secondaryDisplayMode()   const { return m_secondaryDisplayMode; }
    QString postShotContent()        const { return m_postShotContent; }
    double  postShotDelay()          const { return m_postShotDelay; }
    bool    postShotMirror()         const { return m_postShotMirror; }
    QString uiFrameRateCap()         const { return m_uiFrameRateCap; }
    bool    hardwareAcceleration()   const { return m_hardwareAcceleration; }

    QStringList cameraExcluded()    const { return m_cameraExcluded; }
    QVariantMap cameraTargetFps()   const { return m_cameraTargetFps; }
    QVariantMap cameraTriggerMode() const { return m_cameraTriggerMode; }
    QVariantMap cameraRoi()         const { return m_cameraRoi; }
    QVariantMap cameraPerspective() const { return m_cameraPerspective; }
    QVariantMap cameraIsMirrored()  const { return m_cameraIsMirrored; }
    double      cameraPreroll()      const { return m_cameraPreroll; }
    bool        cameraSyncEnabled()  const { return m_cameraSyncEnabled; }
    QVariantMap cameraFixedInPlace() const { return m_cameraFixedInPlace; }
    QVariantMap cameraAlias()        const { return m_cameraAlias; }

    QVariantMap sessionGoalsByType() const { return m_sessionGoalsByType; }
    int         lastSessionType()    const { return m_lastSessionType; }

    QString sessionNamingPattern()  const { return m_sessionNamingPattern; }
    QString videoResolutionMode()   const { return m_videoResolutionMode; }
    QString videoCodec()            const { return m_videoCodec; }
    QString videoQuality()          const { return m_videoQuality; }
    QString videoContainer()        const { return m_videoContainer; }
    bool    saveRawFrames()         const { return m_saveRawFrames; }
    bool    savePoseKeypoints()     const { return m_savePoseKeypoints; }
    bool    saveImuStreams()        const { return m_saveImuStreams; }
    QString imuDataFormat()         const { return m_imuDataFormat; }
    bool    saveLaunchMonitorData() const { return m_saveLaunchMonitorData; }

    QStringList imuExcluded()             const { return m_imuExcluded; }
    QVariantMap imuPlacement()            const { return m_imuPlacement; }
    QVariantMap imuOutputRateHz()         const { return m_imuOutputRateHz; }
    QVariantMap imuFusionMode()           const { return m_imuFusionMode; }
    QVariantMap imuMountOrientation()     const { return m_imuMountOrientation; }
    bool        imuAutoConnect()          const { return m_imuAutoConnect; }
    bool        imuAutoReconnect()        const { return m_imuAutoReconnect; }
    bool        imuSaveCalibrationToFlash() const { return m_imuSaveCalibrationToFlash; }
    QString     imuDefaultFusionMode()    const { return m_imuDefaultFusionMode; }
    QString     imuOrientationFilter()    const { return m_imuOrientationFilter; }
    QVariantMap imuAlias()               const { return m_imuAlias; }
    QVariantMap imuCalibration()         const { return m_imuCalibration; }

    void setThemeIndex(int v)
    {
        if (m_themeIndex == v) return;
        m_themeIndex = v;
        ppSettings().setValue(QStringLiteral("ui/themeIndex"), v);
        emit themeIndexChanged();
    }

    void setWindowWidth(int v)
    {
        if (m_windowWidth == v) return;
        m_windowWidth = v;
        ppSettings().setValue(QStringLiteral("ui/windowWidth"), v);
        emit windowWidthChanged();
    }

    void setWindowHeight(int v)
    {
        if (m_windowHeight == v) return;
        m_windowHeight = v;
        ppSettings().setValue(QStringLiteral("ui/windowHeight"), v);
        emit windowHeightChanged();
    }

    void setFontScale(double v)
    {
        if (qFuzzyCompare(m_fontScale, v)) return;
        m_fontScale = v;
        ppSettings().setValue(QStringLiteral("ui/fontScale"), v);
        emit fontScaleChanged();
    }

    void setDensity(const QString &v)
    {
        if (m_density == v) return;
        m_density = v;
        ppSettings().setValue(QStringLiteral("ui/density"), v);
        emit densityChanged();
    }

    void setReduceMotion(bool v)
    {
        if (m_reduceMotion == v) return;
        m_reduceMotion = v;
        ppSettings().setValue(QStringLiteral("ui/reduceMotion"), v);
        emit reduceMotionChanged();
    }

    void setOverlayOpacity(double v)
    {
        if (qFuzzyCompare(m_overlayOpacity, v)) return;
        m_overlayOpacity = v;
        ppSettings().setValue(QStringLiteral("ui/overlayOpacity"), v);
        emit overlayOpacityChanged();
    }

    void setWindowMaximized(bool v)
    {
        if (m_windowMaximized == v) return;
        m_windowMaximized = v;
        ppSettings().setValue(QStringLiteral("ui/windowMaximized"), v);
        emit windowMaximizedChanged();
    }

    void setWindowX(int v)
    {
        if (m_windowX == v) return;
        m_windowX = v;
        ppSettings().setValue(QStringLiteral("ui/windowX"), v);
        emit windowXChanged();
    }

    void setWindowY(int v)
    {
        if (m_windowY == v) return;
        m_windowY = v;
        ppSettings().setValue(QStringLiteral("ui/windowY"), v);
        emit windowYChanged();
    }

    void setLanguage(const QString &v)
    {
        if (m_language == v) return;
        m_language = v;
        ppSettings().setValue(QStringLiteral("General/language"), v);
        emit languageChanged();
    }

    void setUnits(const QString &v)
    {
        if (m_units == v) return;
        m_units = v;
        ppSettings().setValue(QStringLiteral("General/units"), v);
        emit unitsChanged();
    }

    void setAthleteLibraryPath(const QString &v)
    {
        if (m_athleteLibraryPath == v) return;
        m_athleteLibraryPath = v;
        ppSettings().setValue(QStringLiteral("General/athleteLibraryPath"), v);
        emit athleteLibraryPathChanged();
    }

    void setAutoSaveSession(bool v)
    {
        if (m_autoSaveSession == v) return;
        m_autoSaveSession = v;
        ppSettings().setValue(QStringLiteral("General/autoSaveSession"), v);
        emit autoSaveSessionChanged();
    }

    void setAutoDetectSwing(bool v)
    {
        if (m_autoDetectSwing == v) return;
        m_autoDetectSwing = v;
        ppSettings().setValue(QStringLiteral("General/autoDetectSwing"), v);
        emit autoDetectSwingChanged();
    }

    void setSwingDetectionSensitivity(const QString &v)
    {
        if (m_swingDetectionSensitivity == v) return;
        m_swingDetectionSensitivity = v;
        ppSettings().setValue(QStringLiteral("General/swingDetectionSensitivity"), v);
        emit swingDetectionSensitivityChanged();
    }

    void setAudioDeviceLatencyUs(int v)
    {
        if (m_audioDeviceLatencyUs == v) return;
        m_audioDeviceLatencyUs = v;
        ppSettings().setValue(QStringLiteral("General/audioDeviceLatencyUs"), v);
        emit audioDeviceLatencyUsChanged();
    }

    void setAiCoachingOnSessionEnd(bool v)
    {
        if (m_aiCoachingOnSessionEnd == v) return;
        m_aiCoachingOnSessionEnd = v;
        ppSettings().setValue(QStringLiteral("General/aiCoachingOnSessionEnd"), v);
        emit aiCoachingOnSessionEndChanged();
    }

    void setCheckForUpdates(bool v)
    {
        if (m_checkForUpdates == v) return;
        m_checkForUpdates = v;
        ppSettings().setValue(QStringLiteral("General/checkForUpdates"), v);
        emit checkForUpdatesChanged();
    }

    void setSendDiagnostics(bool v)
    {
        if (m_sendDiagnostics == v) return;
        m_sendDiagnostics = v;
        ppSettings().setValue(QStringLiteral("General/sendDiagnostics"), v);
        emit sendDiagnosticsChanged();
    }

    void setMainDisplayMode(const QString &v)
    {
        if (m_mainDisplayMode == v) return;
        m_mainDisplayMode = v;
        ppSettings().setValue(QStringLiteral("display/mainDisplayMode"), v);
        emit mainDisplayModeChanged();
    }

    void setRememberWindowGeometry(bool v)
    {
        if (m_rememberWindowGeometry == v) return;
        m_rememberWindowGeometry = v;
        ppSettings().setValue(QStringLiteral("display/rememberWindowGeometry"), v);
        emit rememberWindowGeometryChanged();
    }

    void setSecondaryDisplayMode(const QString &v)
    {
        if (m_secondaryDisplayMode == v) return;
        m_secondaryDisplayMode = v;
        ppSettings().setValue(QStringLiteral("display/secondaryDisplayMode"), v);
        emit secondaryDisplayModeChanged();
    }

    void setPostShotContent(const QString &v)
    {
        if (m_postShotContent == v) return;
        m_postShotContent = v;
        ppSettings().setValue(QStringLiteral("display/postShotContent"), v);
        emit postShotContentChanged();
    }

    void setPostShotDelay(double v)
    {
        if (qFuzzyCompare(m_postShotDelay, v)) return;
        m_postShotDelay = v;
        ppSettings().setValue(QStringLiteral("display/postShotDelay"), v);
        emit postShotDelayChanged();
    }

    void setPostShotMirror(bool v)
    {
        if (m_postShotMirror == v) return;
        m_postShotMirror = v;
        ppSettings().setValue(QStringLiteral("display/postShotMirror"), v);
        emit postShotMirrorChanged();
    }

    void setUiFrameRateCap(const QString &v)
    {
        if (m_uiFrameRateCap == v) return;
        m_uiFrameRateCap = v;
        ppSettings().setValue(QStringLiteral("display/uiFrameRateCap"), v);
        emit uiFrameRateCapChanged();
    }

    void setHardwareAcceleration(bool v)
    {
        if (m_hardwareAcceleration == v) return;
        m_hardwareAcceleration = v;
        ppSettings().setValue(QStringLiteral("display/hardwareAcceleration"), v);
        emit hardwareAccelerationChanged();
    }

    void setCameraExcluded(const QStringList &v)
    {
        if (m_cameraExcluded == v) return;
        m_cameraExcluded = v;
        ppSettings().setValue(QStringLiteral("camera/excluded"), v);
        emit cameraExcludedChanged();
    }

    void setCameraTargetFps(const QVariantMap &v)
    {
        if (m_cameraTargetFps == v) return;
        m_cameraTargetFps = v;
        ppSettings().setValue(QStringLiteral("camera/targetFps"), v);
        emit cameraTargetFpsChanged();
    }

    void setCameraTriggerMode(const QVariantMap &v)
    {
        if (m_cameraTriggerMode == v) return;
        m_cameraTriggerMode = v;
        ppSettings().setValue(QStringLiteral("camera/triggerMode"), v);
        emit cameraTriggerModeChanged();
    }

    void setCameraRoi(const QVariantMap &v)
    {
        if (m_cameraRoi == v) return;
        m_cameraRoi = v;
        ppSettings().setValue(QStringLiteral("camera/roi"), v);
        emit cameraRoiChanged();
    }

    void setCameraPerspective(const QVariantMap &v)
    {
        if (m_cameraPerspective == v) return;
        m_cameraPerspective = v;
        ppSettings().setValue(QStringLiteral("camera/perspective"), v);
        emit cameraPerspectiveChanged();
    }

    void setCameraIsMirrored(const QVariantMap &v)
    {
        if (m_cameraIsMirrored == v) return;
        m_cameraIsMirrored = v;
        ppSettings().setValue(QStringLiteral("camera/isMirrored"), v);
        emit cameraIsMirroredChanged();
    }

    void setCameraPreroll(double v)
    {
        if (qFuzzyCompare(m_cameraPreroll, v)) return;
        m_cameraPreroll = v;
        ppSettings().setValue(QStringLiteral("camera/preroll"), v);
        emit cameraPrerollChanged();
    }

    void setCameraSyncEnabled(bool v)
    {
        if (m_cameraSyncEnabled == v) return;
        m_cameraSyncEnabled = v;
        ppSettings().setValue(QStringLiteral("camera/syncEnabled"), v);
        emit cameraSyncEnabledChanged();
    }

    void setCameraFixedInPlace(const QVariantMap &v)
    {
        if (m_cameraFixedInPlace == v) return;
        m_cameraFixedInPlace = v;
        ppSettings().setValue(QStringLiteral("camera/fixedInPlace"), v);
        emit cameraFixedInPlaceChanged();
    }

    void setCameraAlias(const QVariantMap &v)
    {
        if (m_cameraAlias == v) return;
        m_cameraAlias = v;
        ppSettings().setValue(QStringLiteral("camera/alias"), v);
        emit cameraAliasChanged();
    }

    void setImuExcluded(const QStringList &v)
    {
        if (m_imuExcluded == v) return;
        m_imuExcluded = v;
        ppSettings().setValue(QStringLiteral("imu/excluded"), v);
        emit imuExcludedChanged();
    }

    void setImuPlacement(const QVariantMap &v)
    {
        if (m_imuPlacement == v) return;
        m_imuPlacement = v;
        ppSettings().setValue(QStringLiteral("imu/placement"), v);
        emit imuPlacementChanged();
    }

    void setImuOutputRateHz(const QVariantMap &v)
    {
        if (m_imuOutputRateHz == v) return;
        m_imuOutputRateHz = v;
        ppSettings().setValue(QStringLiteral("imu/outputRateHz"), v);
        emit imuOutputRateHzChanged();
    }

    void setImuFusionMode(const QVariantMap &v)
    {
        if (m_imuFusionMode == v) return;
        m_imuFusionMode = v;
        ppSettings().setValue(QStringLiteral("imu/fusionMode"), v);
        emit imuFusionModeChanged();
    }

    void setImuMountOrientation(const QVariantMap &v)
    {
        if (m_imuMountOrientation == v) return;
        m_imuMountOrientation = v;
        ppSettings().setValue(QStringLiteral("imu/mountOrientation"), v);
        emit imuMountOrientationChanged();
    }

    void setImuAutoConnect(bool v)
    {
        if (m_imuAutoConnect == v) return;
        m_imuAutoConnect = v;
        ppSettings().setValue(QStringLiteral("imu/autoConnect"), v);
        emit imuAutoConnectChanged();
    }

    void setImuAutoReconnect(bool v)
    {
        if (m_imuAutoReconnect == v) return;
        m_imuAutoReconnect = v;
        ppSettings().setValue(QStringLiteral("imu/autoReconnect"), v);
        emit imuAutoReconnectChanged();
    }

    void setImuSaveCalibrationToFlash(bool v)
    {
        if (m_imuSaveCalibrationToFlash == v) return;
        m_imuSaveCalibrationToFlash = v;
        ppSettings().setValue(QStringLiteral("imu/saveCalibrationToFlash"), v);
        emit imuSaveCalibrationToFlashChanged();
    }

    void setImuDefaultFusionMode(const QString &v)
    {
        if (m_imuDefaultFusionMode == v) return;
        m_imuDefaultFusionMode = v;
        ppSettings().setValue(QStringLiteral("imu/defaultFusionMode"), v);
        emit imuDefaultFusionModeChanged();
    }

    void setImuOrientationFilter(const QString &v)
    {
        if (m_imuOrientationFilter == v) return;
        m_imuOrientationFilter = v;
        ppSettings().setValue(QStringLiteral("imu/orientationFilter"), v);
        emit imuOrientationFilterChanged();
    }

    void setImuAlias(const QVariantMap &v)
    {
        if (m_imuAlias == v) return;
        m_imuAlias = v;
        ppSettings().setValue(QStringLiteral("imu/alias"), v);
        emit imuAliasChanged();
    }

    void setImuCalibration(const QVariantMap &v)
    {
        if (m_imuCalibration == v) return;
        m_imuCalibration = v;
        ppSettings().setValue(QStringLiteral("imu/calibration"), v);
        emit imuCalibrationChanged();
    }

    void setSessionGoalsByType(const QVariantMap &v)
    {
        if (m_sessionGoalsByType == v) return;
        m_sessionGoalsByType = v;
        ppSettings().setValue(QStringLiteral("session/goalsByType"), v);
        emit sessionGoalsByTypeChanged();
    }

    void setLastSessionType(int v)
    {
        if (m_lastSessionType == v) return;
        m_lastSessionType = v;
        ppSettings().setValue(QStringLiteral("session/lastType"), v);
        emit lastSessionTypeChanged();
    }

    void setSessionNamingPattern(const QString &v)
    {
        if (m_sessionNamingPattern == v) return;
        m_sessionNamingPattern = v;
        ppSettings().setValue(QStringLiteral("storage/sessionNamingPattern"), v);
        emit sessionNamingPatternChanged();
    }

    void setVideoResolutionMode(const QString &v)
    {
        if (m_videoResolutionMode == v) return;
        m_videoResolutionMode = v;
        ppSettings().setValue(QStringLiteral("storage/videoResolutionMode"), v);
        emit videoResolutionModeChanged();
    }

    void setVideoCodec(const QString &v)
    {
        if (m_videoCodec == v) return;
        m_videoCodec = v;
        ppSettings().setValue(QStringLiteral("storage/videoCodec"), v);
        emit videoCodecChanged();
    }

    void setVideoQuality(const QString &v)
    {
        if (m_videoQuality == v) return;
        m_videoQuality = v;
        ppSettings().setValue(QStringLiteral("storage/videoQuality"), v);
        emit videoQualityChanged();
    }

    void setVideoContainer(const QString &v)
    {
        if (m_videoContainer == v) return;
        m_videoContainer = v;
        ppSettings().setValue(QStringLiteral("storage/videoContainer"), v);
        emit videoContainerChanged();
    }

    void setSaveRawFrames(bool v)
    {
        if (m_saveRawFrames == v) return;
        m_saveRawFrames = v;
        ppSettings().setValue(QStringLiteral("storage/saveRawFrames"), v);
        emit saveRawFramesChanged();
    }

    void setSavePoseKeypoints(bool v)
    {
        if (m_savePoseKeypoints == v) return;
        m_savePoseKeypoints = v;
        ppSettings().setValue(QStringLiteral("storage/savePoseKeypoints"), v);
        emit savePoseKeypointsChanged();
    }

    void setImuStreams(bool v)
    {
        if (m_saveImuStreams == v) return;
        m_saveImuStreams = v;
        ppSettings().setValue(QStringLiteral("storage/saveImuStreams"), v);
        emit saveImuStreamsChanged();
    }

    void setImuDataFormat(const QString &v)
    {
        if (m_imuDataFormat == v) return;
        m_imuDataFormat = v;
        ppSettings().setValue(QStringLiteral("storage/imuDataFormat"), v);
        emit imuDataFormatChanged();
    }

    void setSaveLaunchMonitorData(bool v)
    {
        if (m_saveLaunchMonitorData == v) return;
        m_saveLaunchMonitorData = v;
        ppSettings().setValue(QStringLiteral("storage/saveLaunchMonitorData"), v);
        emit saveLaunchMonitorDataChanged();
    }

signals:
    void themeIndexChanged();
    void windowWidthChanged();
    void windowHeightChanged();
    void fontScaleChanged();
    void densityChanged();
    void reduceMotionChanged();
    void overlayOpacityChanged();
    void windowMaximizedChanged();
    void windowXChanged();
    void windowYChanged();
    void languageChanged();
    void unitsChanged();
    void athleteLibraryPathChanged();
    void autoSaveSessionChanged();
    void autoDetectSwingChanged();
    void swingDetectionSensitivityChanged();
    void audioDeviceLatencyUsChanged();
    void aiCoachingOnSessionEndChanged();
    void checkForUpdatesChanged();
    void sendDiagnosticsChanged();
    void mainDisplayModeChanged();
    void rememberWindowGeometryChanged();
    void secondaryDisplayModeChanged();
    void postShotContentChanged();
    void postShotDelayChanged();
    void postShotMirrorChanged();
    void uiFrameRateCapChanged();
    void hardwareAccelerationChanged();
    void cameraExcludedChanged();
    void cameraTargetFpsChanged();
    void cameraTriggerModeChanged();
    void cameraRoiChanged();
    void cameraPerspectiveChanged();
    void cameraIsMirroredChanged();
    void cameraPrerollChanged();
    void cameraSyncEnabledChanged();
    void cameraFixedInPlaceChanged();
    void cameraAliasChanged();
    void imuExcludedChanged();
    void imuPlacementChanged();
    void imuOutputRateHzChanged();
    void imuFusionModeChanged();
    void imuMountOrientationChanged();
    void imuAutoConnectChanged();
    void imuAutoReconnectChanged();
    void imuSaveCalibrationToFlashChanged();
    void imuDefaultFusionModeChanged();
    void imuOrientationFilterChanged();
    void imuAliasChanged();
    void imuCalibrationChanged();
    void sessionGoalsByTypeChanged();
    void lastSessionTypeChanged();
    void sessionNamingPatternChanged();
    void videoResolutionModeChanged();
    void videoCodecChanged();
    void videoQualityChanged();
    void videoContainerChanged();
    void saveRawFramesChanged();
    void savePoseKeypointsChanged();
    void saveImuStreamsChanged();
    void imuDataFormatChanged();
    void saveLaunchMonitorDataChanged();

private:
    int     m_themeIndex      = 0;
    int     m_windowWidth     = 1120;
    int     m_windowHeight    = 700;
    double  m_fontScale       = -1.0;
    QString m_density         = QStringLiteral("default");
    bool    m_reduceMotion    = false;
    double  m_overlayOpacity  = 0.7;
    bool    m_windowMaximized = false;
    int     m_windowX         = -1;
    int     m_windowY         = -1;

    QString m_language                  = QStringLiteral("en_GB");
    QString m_units                     = QStringLiteral("mph");
    QString m_athleteLibraryPath;
    bool    m_autoSaveSession           = true;
    bool    m_autoDetectSwing           = false;   // P1 default; ON again at P3
    QString m_swingDetectionSensitivity = QStringLiteral("Medium");
    int     m_audioDeviceLatencyUs      = 20000;
    bool    m_aiCoachingOnSessionEnd    = true;
    bool    m_checkForUpdates           = true;
    bool    m_sendDiagnostics           = false;

    QString m_mainDisplayMode        = QStringLiteral("primary");
    bool    m_rememberWindowGeometry = true;
    QString m_secondaryDisplayMode   = QStringLiteral("none");
    QString m_postShotContent        = QStringLiteral("replay");
    double  m_postShotDelay          = 0.5;
    bool    m_postShotMirror         = false;
    QString m_uiFrameRateCap         = QStringLiteral("display");
    bool    m_hardwareAcceleration   = true;

    QStringList m_cameraExcluded;
    QVariantMap m_cameraTargetFps;
    QVariantMap m_cameraTriggerMode;
    QVariantMap m_cameraRoi;
    QVariantMap m_cameraPerspective;
    QVariantMap m_cameraIsMirrored;
    double      m_cameraPreroll     = 1.0;
    bool        m_cameraSyncEnabled = true;
    QVariantMap m_cameraFixedInPlace;
    QVariantMap m_cameraAlias;

    QStringList m_imuExcluded;
    QVariantMap m_imuPlacement;
    QVariantMap m_imuOutputRateHz;
    QVariantMap m_imuFusionMode;
    QVariantMap m_imuMountOrientation;
    QVariantMap m_imuAlias;
    QVariantMap m_imuCalibration;
    bool        m_imuAutoConnect          = true;
    bool        m_imuAutoReconnect        = true;
    bool        m_imuSaveCalibrationToFlash = false;
    QString     m_imuDefaultFusionMode    = QStringLiteral("9axis");
    QString     m_imuOrientationFilter    = QStringLiteral("Madgwick");

    QVariantMap m_sessionGoalsByType;
    int         m_lastSessionType = 0;

    QString m_sessionNamingPattern  = QStringLiteral("date-name-type");
    QString m_videoResolutionMode   = QStringLiteral("native");
    QString m_videoCodec            = QStringLiteral("h264");
    QString m_videoQuality          = QStringLiteral("medium");
    QString m_videoContainer        = QStringLiteral("mp4");
    bool    m_saveRawFrames         = false;
    bool    m_savePoseKeypoints     = true;
    bool    m_saveImuStreams        = true;
    QString m_imuDataFormat         = QStringLiteral("json");
    bool    m_saveLaunchMonitorData = true;
};
