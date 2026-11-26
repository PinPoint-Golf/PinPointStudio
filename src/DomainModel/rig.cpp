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

#include <QDateTime>

#include <algorithm>

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
void dropDefaultCameras(rig::RigState &s)
{
    s.cameras.removeIf([](const rig::CameraSetup &c) {
        return c.perspectiveCode() == kNoneCode && !c.mirrored && !c.fixedInPlace && !c.hittingArea && c.calibrations.isEmpty();
    });
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
    m_parts = std::move(loaded.parts);
    const bool minted = loaded.id.isNull();
    m_id = minted ? RigId::create() : loaded.id;
    if (minted && !m_parts.state.sinceUtc.isValid()) m_parts.state.sinceUtc = QDateTime::currentDateTimeUtc();
    m_stateId = rig::rigStateIdOf(m_parts);
    ppInfo() << "[Rig] loaded" << m_id.toString() << "state" << m_stateId.toString() << "cameras"
             << m_parts.state.cameras.size() << "sensors" << m_parts.state.sensors.size()
             << (minted ? "(a new RigId)" : "");
    // A new RigId, or a group the store copied at this load, is written once, silently. A read-only file never is.
    if (loaded.source != RigLoad::Source::ReadOnly && (minted || loaded.saveNeeded)) save();
}

bool Rig::save()
{
    return m_repo.save(m_id, m_parts);   // the store logs a failure; the state in memory stands
}

template <typename Change> void Rig::changeState(Change change)
{
    rig::RigState next = m_parts.state;
    change(next);
    dropDefaultCameras(next);
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

} // namespace pinpoint
