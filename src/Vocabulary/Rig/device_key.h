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

// The device keys: what names one physical device, and one thing that can hold a role
// (information_model.md §5.2, ID-DEVICE-KEY, ID-PLACEMENT-KEY; docs/design/rig_design.md §4.6).
// Vocabulary ring, context Acquisition. Header-only, Qt value types only: nothing of src/IMU,
// src/Vocabulary/Recording or src/Gui (the design's §6 rule 1).
//
// ⚠ EVERY SPELLING HERE IS A PERSISTED MAP KEY, AND EACH MAP KEEPS ITS OWN, BYTE FOR BYTE. A key
// spelled one byte differently is not an error anyone sees: it silently loses a camera's perspective,
// a sensor's role or an alias. Nothing is migrated; device_key_test pins every spelling against a
// settings store written by today's code. The maps and the spelling each uses:
//
//   camera/perspective, isMirrored, fixedInPlace,   DeviceKey::persistedCameraKey()
//   ballRoi, alias, targetFps, triggerMode;          "<description>|<serial, or the device id when none>"
//   camera/excluded (a list)
//   camera/roi (not a rig fact)                      DeviceKey::persistedRoiKey(CropEntry)
//                                                     the camera key, plus "#impact" for the impact strip
//   camera/exposureUs, camera/tuning                 DeviceKey::persistedTuningKey(TuningEntry)
//                                                     the camera key for Impact, plus "#wide" for every other role
//   imu/alias                                        DeviceKey::persistedWithDescription()  "<description>|<deviceId>"
//   imu/excluded (a list), imu/outputRateHz,         DeviceKey::persistedId()               "<deviceId>"
//   imu/fusionMode
//   imu/roles                                        PlacementKey::persisted()
//                                                     "<deviceId>", "<deviceId>#lowerArm", "<deviceId>#palm"
//   analysis/clubLenPrior (SET-05)                   "<athlete uuid>|<club>|" + persistedCameraKey() of the
//                                                     face-on camera (its typed key is C-1c's)
//
// A spelling reproduces today's open-coded expression exactly, whatever its parts hold: a camera with
// no description still spells "|<id>", as today's code does. Only a null key spells the null string.
// Each parser is the inverse of its spelling: spell(parse(s)) == s for every s it accepts. The
// suffixed parsers read "#impact" / "#wide" from the end only, so a key whose identifier itself ends in
// one is ambiguous, as it is in camera_roi.h today.
//
// Equality, ordering and hashing compare what identifies the device, which is what its stored forms
// carry: a camera by its description and its identifier (the serial, or the device id); a sensor by its
// device id alone. A sensor's description is carried for the alias spelling but is not its identity, so
// a key read from a bare-id map (imu/roles, imu/excluded) equals the key the enumerator builds for the
// same sensor. A camera key read back from its spelling cannot tell a serial from a device id; its
// identifier is whichever the spelling held, which is all the camera key ever compared.
//
// NEVER key imu/alias by DeviceKey equality: two descriptions of one id are two stored entries, so key
// it by persistedWithDescription(). NEVER key camera/roi, exposureUs or tuning by a DeviceKey alone:
// pair it with its CropEntry or TuningEntry, or "k", "k#impact" and "k#wide" collapse into one entry.

#include <QHashFunctions>
#include <QLatin1Char>
#include <QString>
#include <QStringView>

#include <initializer_list>
#include <utility>

namespace pinpoint::rig {

// ── DeviceKey: one physical device ──────────────────────────────────────────────────────────────

class DeviceKey
{
public:
    enum class Kind {
        None,     // the null key
        Camera,   // built from description, serial number and device id
        Sensor,   // an IMU (Witmotion or HackMotion): built from description and device id
        // A phone camera (PPCP) is named by peer_id + source_id: it gets its own kind and fields in
        // Stage F. Nothing here builds or reads one.
    };

    DeviceKey() = default;   // null

    // A camera. Its identifier is its serial number, or its device id when it reports no serial
    // (CameraManager::cameraKey's rule, the one every camera map uses).
    static DeviceKey camera(const QString &description, const QString &serialNumber, const QString &deviceId)
    {
        return DeviceKey(Kind::Camera, description, serialNumber.isEmpty() ? deviceId : serialNumber);
    }

    // A sensor (an IMU), by its description and its device id.
    static DeviceKey sensor(const QString &description, const QString &deviceId)
    {
        return DeviceKey(Kind::Sensor, description, deviceId);
    }

    bool    isNull()      const noexcept { return m_kind == Kind::None; }
    Kind    kind()        const noexcept { return m_kind; }
    bool    isCamera()    const noexcept { return m_kind == Kind::Camera; }
    bool    isSensor()    const noexcept { return m_kind == Kind::Sensor; }
    QString description() const { return m_description; }
    // A camera: its serial, or its device id when it has none. A sensor: its device id.
    QString id()          const { return m_id; }

    // ── Spellings. Each answers the null string for a key of another kind, or a null key. ──

    // The camera form: "<description>|<serial, or the device id when none>".
    QString persistedCameraKey() const
    {
        return isCamera() ? m_description + QLatin1Char('|') + m_id : QString();
    }

    // camera/roi: a camera keeps one crop for every role but Impact under its camera key, and the
    // impact strip under "<camera key>#impact" (camera_roi.h).
    enum class CropEntry { Shared, ImpactStrip };
    QString persistedRoiKey(CropEntry entry) const
    {
        if (!isCamera()) return QString();
        return entry == CropEntry::ImpactStrip ? persistedCameraKey() + QStringLiteral("#impact")
                                               : persistedCameraKey();
    }

    // camera/exposureUs and camera/tuning: a locked exposure and gain are per role too. The Impact
    // role keeps the camera key, as it always has; every other role keeps its own under
    // "<camera key>#wide" (camera_roi.h, tuningKey). §4.6's list omitted this spelling; it is persisted
    // today, so it is reproduced (the Rig design's rule: every persisted spelling, byte for byte).
    enum class TuningEntry { ImpactRole, OtherRoles };
    QString persistedTuningKey(TuningEntry entry) const
    {
        if (!isCamera()) return QString();
        return entry == TuningEntry::OtherRoles ? persistedCameraKey() + QStringLiteral("#wide")
                                                : persistedCameraKey();
    }

    // A sensor with its description (imu/alias): "<description>|<deviceId>".
    QString persistedWithDescription() const
    {
        return isSensor() ? m_description + QLatin1Char('|') + m_id : QString();
    }

    // A sensor's bare device id (imu/excluded; imu/roles through PlacementKey).
    QString persistedId() const { return isSensor() ? m_id : QString(); }

    // ── Parsers: the inverse of each spelling. Null when the text is not that spelling. ──
    //
    // The description and the identifier are split at the LAST '|': an id never holds one, and a
    // description that did stays whole. Any '#' in a camera key is ordinary text (a Windows webcam's
    // device id is a path full of them); only fromPersistedRoiKey and fromPersistedTuningKey read a suffix.

    static DeviceKey fromPersistedCameraKey(QStringView text)
    {
        const qsizetype sep = text.lastIndexOf(QLatin1Char('|'));
        if (sep < 0) return {};
        return DeviceKey(Kind::Camera, text.left(sep).toString(), text.mid(sep + 1).toString());
    }

    // camera/roi: the camera key, and which crop entry the text names.
    static DeviceKey fromPersistedRoiKey(QStringView text, CropEntry *entry = nullptr)
    {
        const QStringView impact = u"#impact";
        const bool isImpact = text.endsWith(impact);
        const DeviceKey key = fromPersistedCameraKey(isImpact ? text.chopped(impact.size()) : text);
        if (entry && !key.isNull()) *entry = isImpact ? CropEntry::ImpactStrip : CropEntry::Shared;
        return key;
    }

    // camera/exposureUs, camera/tuning: the camera key, and which role's entry the text names.
    static DeviceKey fromPersistedTuningKey(QStringView text, TuningEntry *entry = nullptr)
    {
        const QStringView wide = u"#wide";
        const bool isWide = text.endsWith(wide);
        const DeviceKey key = fromPersistedCameraKey(isWide ? text.chopped(wide.size()) : text);
        if (entry && !key.isNull()) *entry = isWide ? TuningEntry::OtherRoles : TuningEntry::ImpactRole;
        return key;
    }

    static DeviceKey fromPersistedWithDescription(QStringView text)
    {
        const qsizetype sep = text.lastIndexOf(QLatin1Char('|'));
        if (sep < 0) return {};
        return DeviceKey(Kind::Sensor, text.left(sep).toString(), text.mid(sep + 1).toString());
    }

    // A bare device id: a sensor with no description (the map it came from holds none).
    static DeviceKey fromPersistedId(QStringView text)
    {
        if (text.isEmpty()) return {};
        return DeviceKey(Kind::Sensor, QString(), text.toString());
    }

    // ── Identity ──

    friend bool operator==(const DeviceKey &a, const DeviceKey &b) noexcept
    {
        return a.m_kind == b.m_kind && a.identityDescription() == b.identityDescription() && a.m_id == b.m_id;
    }
    friend bool operator!=(const DeviceKey &a, const DeviceKey &b) noexcept { return !(a == b); }
    friend bool operator<(const DeviceKey &a, const DeviceKey &b) noexcept
    {
        if (a.m_kind != b.m_kind) return a.m_kind < b.m_kind;
        const int byDescription = a.identityDescription().compare(b.identityDescription());
        if (byDescription != 0) return byDescription < 0;
        return a.m_id < b.m_id;
    }
    friend size_t qHash(const DeviceKey &k, size_t seed = 0) noexcept
    {
        return qHashMulti(seed, static_cast<int>(k.m_kind), k.identityDescription(), k.m_id);
    }

private:
    DeviceKey(Kind kind, QString description, QString id)
        : m_kind(kind), m_description(std::move(description)), m_id(std::move(id)) {}

    // What of the description identifies the device: all of it for a camera, none for a sensor.
    QString identityDescription() const { return m_kind == Kind::Camera ? m_description : QString(); }

    Kind    m_kind = Kind::None;
    QString m_description;
    QString m_id;
};

// ── PlacementKey: one thing that can hold a role ────────────────────────────────────────────────
//
// A sensor's DeviceKey plus an optional lane. A Witmotion has no lane; a HackMotion wG3 has two, lower
// arm and palm, so one device holds two roles. The lanes are spelled as src/IMU/hm_unit_id.h spells
// them ("#lowerArm", "#palm"). That header is in the Rules ring, so this one cannot include it: the two
// strings are repeated below and device_key_test pins them equal to hm_unit_id::suffix().

class PlacementKey
{
public:
    enum class Lane { None, LowerArm, Palm };

    static QString laneSuffix(Lane lane)
    {
        switch (lane) {
        case Lane::LowerArm: return QStringLiteral("#lowerArm");
        case Lane::Palm:     return QStringLiteral("#palm");
        case Lane::None:     break;
        }
        return QString();
    }

    PlacementKey() = default;   // null

    // Null unless `device` is a sensor: a camera holds no role.
    explicit PlacementKey(const DeviceKey &device, Lane lane = Lane::None)
        : m_device(device.isSensor() ? device : DeviceKey()), m_lane(device.isSensor() ? lane : Lane::None) {}

    bool      isNull()  const noexcept { return m_device.isNull(); }
    DeviceKey device()  const { return m_device; }
    Lane      lane()    const noexcept { return m_lane; }
    bool      hasLane() const noexcept { return m_lane != Lane::None; }

    // imu/roles: "<deviceId>", "<deviceId>#lowerArm" or "<deviceId>#palm".
    QString persisted() const
    {
        return isNull() ? QString() : m_device.persistedId() + laneSuffix(m_lane);
    }

    // The inverse, as hm_unit_id::parse reads a unit id: split at the last '#', and accept the lane
    // only when the text regenerates exactly. Anything else, an unknown suffix too, is a whole bare
    // device id, never a guessed lane.
    static PlacementKey fromPersisted(QStringView text)
    {
        if (text.isEmpty()) return {};
        const qsizetype sep = text.lastIndexOf(QLatin1Char('#'));
        if (sep > 0) {
            const QStringView id = text.left(sep);
            for (Lane lane : { Lane::LowerArm, Lane::Palm }) {
                if (text == id.toString() + laneSuffix(lane))
                    return PlacementKey(DeviceKey::fromPersistedId(id), lane);
            }
        }
        return PlacementKey(DeviceKey::fromPersistedId(text));
    }

    friend bool operator==(const PlacementKey &a, const PlacementKey &b) noexcept
    {
        return a.m_device == b.m_device && a.m_lane == b.m_lane;
    }
    friend bool operator!=(const PlacementKey &a, const PlacementKey &b) noexcept { return !(a == b); }
    friend bool operator<(const PlacementKey &a, const PlacementKey &b) noexcept
    {
        if (a.m_device != b.m_device) return a.m_device < b.m_device;
        return a.m_lane < b.m_lane;
    }
    friend size_t qHash(const PlacementKey &k, size_t seed = 0) noexcept
    {
        return qHashMulti(seed, qHash(k.m_device), static_cast<int>(k.m_lane));
    }

private:
    DeviceKey m_device;
    Lane      m_lane = Lane::None;
};

} // namespace pinpoint::rig
