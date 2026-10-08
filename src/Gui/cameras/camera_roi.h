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

// A camera's crop is per ROLE, not per camera (flir_camera_settings.md §2.1).
// AppSettings::cameraRoi keeps the crop every non-Impact role uses under the
// camera key, and the impact strip under "<key>#impact". They used to share
// one entry, so choosing an impact mode overwrote the down-the-line crop and a
// camera moved out of Impact came back as a 640x240 strip at 611 fps — and one
// moved into Impact kept its full-height crop at 150 fps (CameraRoleProbe,
// 2026-10-08). Every reader goes through cropFor(); QML, which needs a binding
// on appSettings.cameraRoi, spells the same key inline (CamerasPanel.qml,
// PpCameraTiles.qml).

#include "../../Video/camera_capabilities.h"

#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>

namespace pp_camroi {

// CameraInstance::Impact (static_assert'ed in camera_instance.cpp; this header
// stays free of the instance's).
inline constexpr int kImpactPerspective = 4;

inline QString key(const QString &cameraKey, int perspective)
{
    return perspective == kImpactPerspective ? cameraKey + QStringLiteral("#impact") : cameraKey;
}

inline QRectF rectFrom(const QVariantMap &r)
{
    return QRectF(r.value(QStringLiteral("x")).toDouble(), r.value(QStringLiteral("y")).toDouble(),
                  r.value(QStringLiteral("w")).toDouble(), r.value(QStringLiteral("h")).toDouble());
}

// The impact role's default crop: the recommended mode the enumerate probe
// published (impact.modes[0], 640x240 on a Chameleon3), centred. Empty when
// the camera published none.
inline QRectF recommendedImpactCrop(const CameraCapabilities &caps)
{
    const QVariantList modes = caps.extensions.value(QStringLiteral("impact.modes")).toList();
    const double sw = caps.resolution.widthRange.max, sh = caps.resolution.heightRange.max;
    if (modes.isEmpty() || !(sw > 0) || !(sh > 0))
        return {};
    const QVariantMap m = modes.first().toMap();
    const double w = std::min(1.0, m.value(QStringLiteral("w")).toDouble() / sw);
    const double h = std::min(1.0, m.value(QStringLiteral("h")).toDouble() / sh);
    if (!(w > 0) || !(h > 0))
        return {};
    return QRectF((1.0 - w) / 2.0, (1.0 - h) / 2.0, w, h);
}

// The crop this role uses: the stored one, or — for Impact with none stored —
// the recommended impact crop (so the impact role never runs full frame at the
// full-frame rate). Empty = full sensor.
inline QRectF cropFor(const QVariantMap &roiMap, const QString &cameraKey, int perspective,
                      const CameraCapabilities *caps = nullptr)
{
    const QString k = key(cameraKey, perspective);
    if (roiMap.contains(k)) {
        const QRectF r = rectFrom(roiMap.value(k).toMap());
        if (r.width() > 0 && r.height() > 0)
            return r;
    }
    if (perspective == kImpactPerspective && caps)
        return recommendedImpactCrop(*caps);
    return {};
}

} // namespace pp_camroi
