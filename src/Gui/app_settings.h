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
#include "pp_settings.h"
#include "version.h"

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
    Q_PROPERTY(QString language                    READ language                    WRITE setLanguage                    NOTIFY languageChanged)
    Q_PROPERTY(QString units                       READ units                       WRITE setUnits                       NOTIFY unitsChanged)
    Q_PROPERTY(QString athleteLibraryPath          READ athleteLibraryPath          WRITE setAthleteLibraryPath          NOTIFY athleteLibraryPathChanged)
    Q_PROPERTY(bool    autoSaveSession             READ autoSaveSession             WRITE setAutoSaveSession             NOTIFY autoSaveSessionChanged)
    Q_PROPERTY(bool    autoDetectSwing             READ autoDetectSwing             WRITE setAutoDetectSwing             NOTIFY autoDetectSwingChanged)
    Q_PROPERTY(QString swingDetectionSensitivity   READ swingDetectionSensitivity   WRITE setSwingDetectionSensitivity   NOTIFY swingDetectionSensitivityChanged)
    Q_PROPERTY(bool    aiCoachingOnSessionEnd      READ aiCoachingOnSessionEnd      WRITE setAiCoachingOnSessionEnd      NOTIFY aiCoachingOnSessionEndChanged)
    Q_PROPERTY(bool    checkForUpdates             READ checkForUpdates             WRITE setCheckForUpdates             NOTIFY checkForUpdatesChanged)
    Q_PROPERTY(bool    sendDiagnostics             READ sendDiagnostics             WRITE setSendDiagnostics             NOTIFY sendDiagnosticsChanged)

public:
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

        m_language                  = ppSettings().value(QStringLiteral("general/language"),                  QStringLiteral("en_GB")).toString();
        m_units                     = ppSettings().value(QStringLiteral("general/units"),                     QStringLiteral("mph")).toString();
        m_athleteLibraryPath        = ppSettings().value(QStringLiteral("general/athleteLibraryPath"),        QStringLiteral("")).toString();
        m_autoSaveSession           = ppSettings().value(QStringLiteral("general/autoSaveSession"),           true).toBool();
        m_autoDetectSwing           = ppSettings().value(QStringLiteral("general/autoDetectSwing"),           true).toBool();
        m_swingDetectionSensitivity = ppSettings().value(QStringLiteral("general/swingDetectionSensitivity"), QStringLiteral("Medium")).toString();
        m_aiCoachingOnSessionEnd    = ppSettings().value(QStringLiteral("general/aiCoachingOnSessionEnd"),    true).toBool();
        m_checkForUpdates           = ppSettings().value(QStringLiteral("general/checkForUpdates"),           true).toBool();
        m_sendDiagnostics           = ppSettings().value(QStringLiteral("general/sendDiagnostics"),           false).toBool();
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

    QString language()                  const { return m_language; }
    QString units()                     const { return m_units; }
    QString athleteLibraryPath()        const { return m_athleteLibraryPath; }
    bool    autoSaveSession()           const { return m_autoSaveSession; }
    bool    autoDetectSwing()           const { return m_autoDetectSwing; }
    QString swingDetectionSensitivity() const { return m_swingDetectionSensitivity; }
    bool    aiCoachingOnSessionEnd()    const { return m_aiCoachingOnSessionEnd; }
    bool    checkForUpdates()           const { return m_checkForUpdates; }
    bool    sendDiagnostics()           const { return m_sendDiagnostics; }

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

    void setLanguage(const QString &v)
    {
        if (m_language == v) return;
        m_language = v;
        ppSettings().setValue(QStringLiteral("general/language"), v);
        emit languageChanged();
    }

    void setUnits(const QString &v)
    {
        if (m_units == v) return;
        m_units = v;
        ppSettings().setValue(QStringLiteral("general/units"), v);
        emit unitsChanged();
    }

    void setAthleteLibraryPath(const QString &v)
    {
        if (m_athleteLibraryPath == v) return;
        m_athleteLibraryPath = v;
        ppSettings().setValue(QStringLiteral("general/athleteLibraryPath"), v);
        emit athleteLibraryPathChanged();
    }

    void setAutoSaveSession(bool v)
    {
        if (m_autoSaveSession == v) return;
        m_autoSaveSession = v;
        ppSettings().setValue(QStringLiteral("general/autoSaveSession"), v);
        emit autoSaveSessionChanged();
    }

    void setAutoDetectSwing(bool v)
    {
        if (m_autoDetectSwing == v) return;
        m_autoDetectSwing = v;
        ppSettings().setValue(QStringLiteral("general/autoDetectSwing"), v);
        emit autoDetectSwingChanged();
    }

    void setSwingDetectionSensitivity(const QString &v)
    {
        if (m_swingDetectionSensitivity == v) return;
        m_swingDetectionSensitivity = v;
        ppSettings().setValue(QStringLiteral("general/swingDetectionSensitivity"), v);
        emit swingDetectionSensitivityChanged();
    }

    void setAiCoachingOnSessionEnd(bool v)
    {
        if (m_aiCoachingOnSessionEnd == v) return;
        m_aiCoachingOnSessionEnd = v;
        ppSettings().setValue(QStringLiteral("general/aiCoachingOnSessionEnd"), v);
        emit aiCoachingOnSessionEndChanged();
    }

    void setCheckForUpdates(bool v)
    {
        if (m_checkForUpdates == v) return;
        m_checkForUpdates = v;
        ppSettings().setValue(QStringLiteral("general/checkForUpdates"), v);
        emit checkForUpdatesChanged();
    }

    void setSendDiagnostics(bool v)
    {
        if (m_sendDiagnostics == v) return;
        m_sendDiagnostics = v;
        ppSettings().setValue(QStringLiteral("general/sendDiagnostics"), v);
        emit sendDiagnosticsChanged();
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
    void languageChanged();
    void unitsChanged();
    void athleteLibraryPathChanged();
    void autoSaveSessionChanged();
    void autoDetectSwingChanged();
    void swingDetectionSensitivityChanged();
    void aiCoachingOnSessionEndChanged();
    void checkForUpdatesChanged();
    void sendDiagnosticsChanged();

private:
    int     m_themeIndex      = 0;
    int     m_windowWidth     = 1120;
    int     m_windowHeight    = 700;
    double  m_fontScale       = -1.0;
    QString m_density         = QStringLiteral("default");
    bool    m_reduceMotion    = false;
    double  m_overlayOpacity  = 0.7;
    bool    m_windowMaximized = false;

    QString m_language                  = QStringLiteral("en_GB");
    QString m_units                     = QStringLiteral("mph");
    QString m_athleteLibraryPath;
    bool    m_autoSaveSession           = true;
    bool    m_autoDetectSwing           = true;
    QString m_swingDetectionSensitivity = QStringLiteral("Medium");
    bool    m_aiCoachingOnSessionEnd    = true;
    bool    m_checkForUpdates           = true;
    bool    m_sendDiagnostics           = false;
};
