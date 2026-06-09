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

#include "live_wrist_angles.h"

#include <QQuaternion>
#include <QVariantList>
#include <QVariantMap>

#include "app_settings.h"
#include "athlete_controller.h"
#include "imu_instance.h"
#include "imu_manager.h"
#include "../Analysis/wrist_angles.h"

using namespace pinpoint::analysis;

LiveWristAngles::LiveWristAngles(ImuManager *imu, AppSettings *settings,
                                 AthleteController *athlete, QObject *parent)
    : QObject(parent), m_imu(imu), m_settings(settings), m_athlete(athlete)
{
    m_timer.setInterval(33);   // ~30 Hz — plenty for a live readout
    connect(&m_timer, &QTimer::timeout, this, &LiveWristAngles::tick);
}

void LiveWristAngles::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    if (on) { m_timer.start(); tick(); }
    else      m_timer.stop();
    emit activeChanged();
}

// Resolve the ImuInstance assigned to a placement slot ("A"/"B"/"C") — the same
// slot→instance resolution ArmVizView does in QML.
static ImuInstance *slotInstance(ImuManager *imu, AppSettings *settings, const QString &slot)
{
    if (!imu || !settings)
        return nullptr;
    const QVariantMap placement = settings->imuPlacement();
    const QVariantList list = imu->imuDeviceList();
    for (const QVariant &v : list) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (placement.value(id).toString() == slot)
            return qobject_cast<ImuInstance *>(imu->instanceFor(id));
    }
    return nullptr;
}

void LiveWristAngles::tick()
{
    ImuInstance *fore  = slotInstance(m_imu, m_settings, QStringLiteral("A"));   // forearm (at wrist)
    ImuInstance *hand  = slotInstance(m_imu, m_settings, QStringLiteral("B"));   // back of hand
    ImuInstance *upper = slotInstance(m_imu, m_settings, QStringLiteral("C"));   // upper arm (optional)

    // Lead arm is the LEFT for a right-handed golfer (matches ArmVizView). anatQuat is
    // identity at the calibration neutral, so the relative quaternion IS the posture
    // vs neutral — no address reference here (that lives in the post-shot analyzer).
    const bool leftArm = m_athlete && m_athlete->currentHandedness() != QLatin1String("Left");

    m_bowValid = fore && hand && fore->anatCalibrated() && hand->anatCalibrated();
    if (m_bowValid) {
        const QQuaternion rel = (fore->anatQuat().conjugated() * hand->anatQuat()).normalized();
        const WristAngles w = wristFlexExtDeviation(rel, leftArm);
        m_bow        = radToDeg(w.feRad);
        m_hinge      = radToDeg(w.rudRad);
        m_bowLabel   = wristMetricLabel(QStringLiteral("leadWristFlexExt"), m_bow);
        m_hingeLabel = wristMetricLabel(QStringLiteral("leadWristRadUln"),  m_hinge);
    } else {
        m_bowLabel = m_hingeLabel = QStringLiteral("—");
    }

    m_rollValid = upper && fore && upper->anatCalibrated() && fore->anatCalibrated();
    if (m_rollValid) {
        const QQuaternion rel = (upper->anatQuat().conjugated() * fore->anatQuat()).normalized();
        const ForearmElbow ef = forearmPronElbowFlex(rel, leftArm);
        m_roll      = radToDeg(ef.pronRad);
        m_rollLabel = wristMetricLabel(QStringLiteral("forearmPronation"), m_roll);
    } else {
        m_rollLabel = QStringLiteral("—");
    }

    emit changed();
}
