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

#include "rig.h"

#include "../Core/pp_debug.h"
#include "../IMU/imu_role_map.h"                    // the one claim per role rule (invariant 2; §6 rule 4, ruled)
#include "../Vocabulary/Rig/camera_assumptions.h"   // what each assumed record says (AD-7)

#include <QDateTime>

#include <algorithm>
#include <optional>

namespace pinpoint {

namespace {

const int kImpactCode = recording::perspectiveCode(recording::Perspective::Impact);
const int kNoneCode   = recording::perspectiveCode(recording::Perspective::None);

// The camera's entry in `s`, made when it has none.
rig::CameraSetup &entryOf(rig::RigState &s, const rig::DeviceKey &camera)
{
    for (rig::CameraSetup &c : s.cameras)
        if (c.key() == camera) return c;
    s.cameras.append(rig::CameraSetup(camera));
    return s.cameras.last();
}

// An entry with every versioned fact at its default is the same as no entry (rig_commands.h).
bool isDefaultCamera(const rig::CameraSetup &c)
{
    return c.perspectiveCode() == kNoneCode && !c.mirrored && !c.fixedInPlace && !c.hittingArea && c.calibrations.isEmpty();
}
void dropDefaultCameras(rig::RigState &s) { s.cameras.removeIf(isDefaultCamera); }

// ── The two set-up assumptions, as stated records (RP-8; rig_design.md §4.3, §4.8; information_model.md §2.3) ──
//
// The analysis makes two assumptions about the cameras' placement that nothing measures yet. Each is written into the
// rig state as an `extrinsics` record with method `assumed`, on the camera it describes, so each Recording's rig
// says the orientation was assumed, not measured; the camera calibration programme (#74) later replaces the record.
// The analysis never reads these records (RP-9); it reads the down-the-line yaw sigmas from the same header. What
// each record says is Vocabulary/Rig/camera_assumptions.h's (the Solution Architect, 27 Nov 2026, the rulings for
// Step 10 on #377); which camera gets which is decided here (ruling 1):
//   - a face-on camera fixed in place: square and level (rig::kFaceOnSquare; AD-7's wall-mounted camera);
//   - a down-the-line camera with no measured extrinsics, fixed in place or not: the analysis's assumed placement, the
//     yaw uncertain by 9° (rig::kDownTheLineUncalibrated);
//   - any other camera (None, Impact, Other, a code this build does not know, and a face-on camera not fixed in place):
//     no such record.
// Each record states yaw, pitch and roll, and a sigma only for an axis that claims one (an axis with none is absent
// from the uncertainty, never 0). Neither record has a time observed (it is no observation), so the digest holds only
// its content and putting a camera back gives back its earlier RigStateId.
// THE SEAM: a camera holding an extrinsics record of any other method (a measured one; Stage C makes none) gets no
// assumed record: the measured record replaces it.

const QString kYaw   = QStringLiteral("yawDeg");
const QString kPitch = QStringLiteral("pitchDeg");
const QString kRoll  = QStringLiteral("rollDeg");

rig::CalibrationRecord assumedRecord(const rig::DeviceKey &camera, const rig::AssumedOrientation &o)
{
    rig::NamedValues sigmas;
    if (o.yawSigmaDeg) sigmas.set(kYaw, *o.yawSigmaDeg);
    if (o.pitchSigmaDeg) sigmas.set(kPitch, *o.pitchSigmaDeg);
    if (o.rollSigmaDeg) sigmas.set(kRoll, *o.rollSigmaDeg);
    return rig::CalibrationRecord::forCamera(camera, rig::CalibrationKind::Extrinsics, rig::CalibrationMethod::Assumed,
                                             rig::NamedValues{ { kYaw, o.yawDeg }, { kPitch, o.pitchDeg }, { kRoll, o.rollDeg } },
                                             rig::Uncertainty::sigmas(std::move(sigmas)), QDateTime());
}

std::optional<rig::CalibrationRecord> assumedExtrinsicsFor(const rig::CameraSetup &c)
{
    const bool measured = std::any_of(c.calibrations.cbegin(), c.calibrations.cend(), [](const rig::CalibrationRecord &r) {
        return r.kind == rig::CalibrationKind::Extrinsics && r.method != rig::CalibrationMethod::Assumed;
    });
    if (measured) return std::nullopt;
    switch (c.perspective()) {
    case recording::Perspective::FaceOn:
        if (!c.fixedInPlace) return std::nullopt;
        return assumedRecord(c.key(), rig::kFaceOnSquare);
    case recording::Perspective::DownTheLine:
        return assumedRecord(c.key(), rig::kDownTheLineUncalibrated);
    default:
        return std::nullopt;
    }
}

// Each camera's assumed record made again from its set-up: the one it held goes, the one that applies now (if any)
// comes; every other record is kept as it is. A camera that the removal leaves with every fact at its default (it held
// nothing but an assumed record that no longer applies) is no entry; any other entry is left as it is.
void deriveAssumedExtrinsics(rig::RigState &s)
{
    QList<rig::CameraSetup> kept;
    for (rig::CameraSetup c : s.cameras) {
        const bool held = c.calibrations.removeIf([](const rig::CalibrationRecord &r) {
            return r.kind == rig::CalibrationKind::Extrinsics && r.method == rig::CalibrationMethod::Assumed;
        }) > 0;
        if (auto r = assumedExtrinsicsFor(c)) c.calibrations.append(std::move(*r));
        if (!(held && isDefaultCamera(c))) kept.append(std::move(c));
    }
    s.cameras = std::move(kept);
}

// ── The sensors' roles (invariant 2; rig_commands.h) ──
//
// A sensor placement with no role and no alignment record is the same as no entry (rig_commands.h), so a claim then a
// release gives back the earlier RigStateId.
bool isEmptySensor(const rig::SensorSetup &x) { return x.role == analysis::SegmentRole::Unknown && !x.calibration; }
void dropEmptySensors(rig::RigState &s) { s.sensors.removeIf(isEmptySensor); }

// The map today's rule answered, back into the state: one placement per key, its role by its settings name. Each
// placement's alignment record is carried over; a placement whose role went but which holds a record stays, with no
// role (the record is the Rig's for the run, rig_design.md §4.2); dropEmptySensors then drops the rest.
void setSensorsFromRoles(rig::RigState &s, const QVariantMap &roles)
{
    QList<rig::SensorSetup> next;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        rig::SensorSetup x;
        x.key  = rig::PlacementKey::fromPersisted(it.key());
        x.role = analysis::segmentRoleForPlacementRole(it.value().toString());
        next.append(x);
    }
    for (const rig::SensorSetup &old : s.sensors) {
        if (!old.calibration) continue;
        auto it = std::find_if(next.begin(), next.end(), [&](const rig::SensorSetup &x) { return x.key == old.key; });
        if (it != next.end()) {
            it->calibration = old.calibration;
        } else {
            rig::SensorSetup kept;
            kept.key         = old.key;
            kept.calibration = old.calibration;
            next.append(kept);
        }
    }
    s.sensors = std::move(next);
}

// The alias map's own spelling of a device (decision 9): never key equality, so two descriptions of one sensor id stay
// two entries, as in imu/alias.
QString aliasSpelling(const rig::DeviceKey &k)
{
    return k.isCamera() ? k.persistedCameraKey() : k.persistedWithDescription();
}

} // namespace

Rig::Rig(RigRepository &repository, QObject *parent) : QObject(parent), m_repo(repository) {}

void Rig::load()
{
    RigLoad loaded = m_repo.load();
    m_parts    = std::move(loaded.parts);
    m_readOnly = loaded.source == RigLoad::Source::ReadOnly;
    const bool minted = loaded.id.isNull();
    m_id = minted ? RigId::create() : loaded.id;
    if (minted && !m_parts.state.sinceUtc.isValid()) m_parts.state.sinceUtc = QDateTime::currentDateTimeUtc();
    // The assumed records are made again from the set-up as loaded: a file from before them, a set-up copied from
    // settings, or a record a build with other constants wrote. When that changes the state, it is the state in force
    // from now, saved once, silently: a launch's load emits nothing.
    const RigStateId asStored = rig::rigStateIdOf(m_parts);
    deriveAssumedExtrinsics(m_parts.state);
    m_stateId = rig::rigStateIdOf(m_parts);
    const bool derived = m_stateId != asStored;
    if (derived && !minted) m_parts.state.sinceUtc = QDateTime::currentDateTimeUtc();
    ppInfo() << "[Rig] loaded" << m_id.toString() << "state" << m_stateId.toString() << "cameras"
             << m_parts.state.cameras.size() << "sensors" << m_parts.state.sensors.size()
             << (minted ? "(a new RigId)" : "") << (derived ? "(the assumed camera records made again)" : "");
    // A new RigId, a group the store copied at this load, or the assumed records made again, is written once,
    // silently. A read-only file never is.
    if (loaded.source != RigLoad::Source::ReadOnly && (minted || loaded.saveNeeded || derived)) save();
}

bool Rig::save()
{
    const bool ok = m_repo.save(m_id, m_parts);   // the store logs a failure; the state in memory stands
    m_unsaved = !ok && !m_readOnly;               // a read-only file's refusal is not a failure to retry (P4)
    return ok;
}

void Rig::saveIfUnsaved()
{
    if (!m_unsaved) return;
    ppInfo() << "[Rig] a change whose save failed is saved again at quit";
    save();   // a second failure is logged by the store; nothing more can be done
}

template <typename Change> void Rig::changeState(Change change)
{
    rig::RigState next = m_parts.state;
    change(next);
    deriveAssumedExtrinsics(next);   // the assumed records follow the set-up, in the same new state
    dropDefaultCameras(next);
    dropEmptySensors(next);
    const RigStateId id = rig::rigStateIdOf(next);
    if (id == m_stateId) return;   // the same set-up: nothing saved, nothing emitted
    next.sinceUtc = QDateTime::currentDateTimeUtc();
    m_parts.state = std::move(next);
    m_stateId     = id;
    save();                         // a failure is logged; the new state stands and the next command saves again
    emit rigChanged(ref());
}

void Rig::assignPerspective(const rig::DeviceKey &camera, int perspectiveCode)
{
    if (!camera.isCamera()) return;
    changeState([&](rig::RigState &s) {
        if (perspectiveCode == kImpactCode)   // Impact is exclusive: the camera that held it becomes None
            for (rig::CameraSetup &c : s.cameras)
                if (c.key() != camera && c.perspectiveCode() == kImpactCode) c.setPerspectiveCode(kNoneCode);
        entryOf(s, camera).setPerspectiveCode(perspectiveCode);
    });
}

void Rig::setMirrored(const rig::DeviceKey &camera, bool mirrored)
{
    if (!camera.isCamera()) return;
    changeState([&](rig::RigState &s) { entryOf(s, camera).mirrored = mirrored; });
}

void Rig::setFixedInPlace(const rig::DeviceKey &camera, bool fixedInPlace)
{
    if (!camera.isCamera()) return;
    changeState([&](rig::RigState &s) { entryOf(s, camera).fixedInPlace = fixedInPlace; });   // the box is kept either way
}

void Rig::setHittingArea(const rig::DeviceKey &camera, const QRectF &box)
{
    if (!camera.isCamera()) return;
    changeState([&](rig::RigState &s) { entryOf(s, camera).hittingArea = box; });
}

void Rig::clearHittingArea(const rig::DeviceKey &camera)
{
    if (!camera.isCamera()) return;
    changeState([&](rig::RigState &s) { entryOf(s, camera).hittingArea.reset(); });
}

QVariantMap Rig::placementView() const { return imu_roles::placementViewOf(m_parts.state); }

rig::RoleClaim Rig::claimRole(const rig::DeviceKey &sensor, analysis::SegmentRole role, const rig::SensorFactsTable &facts)
{
    // A role with no settings name (Unknown, T12, the thighs, the club) is today's caller error, never a release: its
    // own name goes in, which the rule refuses as "not a placement role". Only releaseRole hands the rule "".
    const QString name = analysis::placementRoleName(role);
    return claim(sensor, name.isEmpty() ? analysis::segmentRoleName(role) : name, facts);
}

rig::RoleClaim Rig::releaseRole(const rig::DeviceKey &sensor) { return claim(sensor, QString(), rig::SensorFactsTable()); }

rig::RoleClaim Rig::claim(const rig::DeviceKey &sensor, const QString &roleName, const rig::SensorFactsTable &facts)
{
    // A key that is not a sensor's spells no bare id, which the rule answers as "no device id".
    const imu_roles::DeviceFactsFn fn = [&facts](const QString &deviceId) { return facts.factsFor(deviceId); };
    const imu_roles::ClaimResult r = imu_roles::claimRole(imu_roles::rolesMapOf(m_parts.state), sensor.persistedId(), roleName, fn);
    rig::RoleClaim out;
    out.ok          = r.ok;
    out.error       = r.error;
    out.refusedRole = analysis::segmentRoleForPlacementRole(r.refusedRole);
    if (!r.holderId.isEmpty()) out.holder = rig::DeviceKey::fromPersistedId(r.holderId);
    for (const QString &k : r.displaced) out.displaced.append(rig::PlacementKey::fromPersisted(k));
    // Refused, or a caller error: the state as it was, nothing saved, nothing emitted. A claim that leaves the roles as
    // they were makes the same digest, so changeState saves and emits nothing for it either.
    if (r.ok) changeState([&](rig::RigState &s) { setSensorsFromRoles(s, r.roles); });
    return out;
}

void Rig::setAlias(const rig::DeviceKey &device, const QString &alias)
{
    if (device.isNull()) return;
    auto &aliases = m_parts.other.aliases;
    const QString spelling = aliasSpelling(device);
    const auto it = std::find_if(aliases.begin(), aliases.end(),
                                 [&](const auto &a) { return aliasSpelling(a.first) == spelling; });
    if (alias.isEmpty()) {
        if (it == aliases.end()) return;
        aliases.erase(it);
    } else if (it != aliases.end()) {
        if (it->second == alias) return;
        it->second = alias;
    } else {
        aliases.append({ device, alias });
    }
    save();
    emit rigDeviceLabelChanged(device, RigDeviceLabel::Alias);
}

void Rig::setEnabledOnThisMachine(const rig::DeviceKey &device, bool enabled)
{
    if (device.isNull()) return;
    auto &flags = m_parts.other.enabledOnThisMachine;
    const auto it = std::find_if(flags.begin(), flags.end(), [&](const auto &f) { return f.first == device; });
    const bool now = it == flags.end() ? true : it->second;   // enabled is the default, and is not stored
    if (now == enabled) return;
    if (enabled) flags.erase(it);
    else if (it != flags.end()) it->second = false;
    else flags.append({ device, false });
    save();
    emit rigDeviceLabelChanged(device, RigDeviceLabel::EnabledOnThisMachine);
}

void Rig::learnClubLength(const rig::ClubLengthPriorKey &key, const QVariantMap &learned)
{
    if (key.isNull()) return;
    auto &priors = m_parts.other.clubLengthPriors;
    const QString spelling = key.persisted();   // by its spelling, as the reader finds it (rig_state.h)
    auto it = std::find_if(priors.begin(), priors.end(), [&](const auto &p) { return p.first.persisted() == spelling; });
    QVariantMap entry = it == priors.end() ? QVariantMap() : it->second;
    for (auto m = learned.cbegin(); m != learned.cend(); ++m) entry.insert(m.key(), m.value());
    if (it != priors.end()) {
        if (entry == it->second) return;   // nothing learned that the entry did not hold
        it->second = entry;
    } else {
        if (entry.isEmpty()) return;
        priors.append({ key, entry });
    }
    save();   // learned state: no event (rig_commands.h); a failure is remembered and saved again (P4)
}

} // namespace pinpoint
