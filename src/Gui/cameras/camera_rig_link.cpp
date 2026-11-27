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

#include "camera_rig_link.h"

namespace pp_camrig {

namespace {

const int kNone   = pinpoint::recording::perspectiveCode(pinpoint::recording::Perspective::None);
const int kImpact = pinpoint::recording::perspectiveCode(pinpoint::recording::Perspective::Impact);

const pinpoint::rig::CameraSetup *entryOf(const RigState &s, const DeviceKey &camera)
{
    for (const pinpoint::rig::CameraSetup &c : s.cameras)
        if (c.key() == camera) return &c;
    return nullptr;
}

// The alias map's own spelling (decision 9): a camera's is its camera key.
QString aliasOfCamera(const RigParts &parts, const DeviceKey &camera)
{
    const QString spelling = camera.persistedCameraKey();
    for (const auto &a : parts.other.aliases)
        if (a.first.isCamera() && a.first.persistedCameraKey() == spelling) return a.second;
    return {};
}

} // namespace

CameraFacts factsOf(const RigParts &parts, const DeviceKey &camera)
{
    CameraFacts f;
    if (const auto *c = entryOf(parts.state, camera)) {
        f.perspectiveCode = c->perspectiveCode();
        f.mirrored        = c->mirrored;
        f.fixedInPlace    = c->fixedInPlace;
        f.hittingArea     = c->hittingArea;
    }
    f.alias = aliasOfCamera(parts, camera);
    for (const auto &e : parts.other.enabledOnThisMachine)
        if (e.first == camera) f.enabledOnThisMachine = e.second;
    return f;
}

ConnectRestore restoreAtConnect(const RigParts &parts, const DeviceKey &camera)
{
    const CameraFacts f = factsOf(parts, camera);
    ConnectRestore    r;
    if (f.perspectiveCode > kNone) r.perspectiveCode = f.perspectiveCode;
    r.poseOff      = f.perspectiveCode == kImpact;
    r.mirrored     = f.mirrored;
    r.fixedInPlace = f.fixedInPlace;
    if (f.fixedInPlace && f.hittingArea && !f.hittingArea->isEmpty()) r.box = f.hittingArea;
    return r;
}

StateReaction reactionTo(const RigState &before, const RigState &after, const QList<ManagedCamera> &cameras)
{
    StateReaction out;
    for (const ManagedCamera &m : cameras) {
        const auto *b = entryOf(before, m.key);
        const auto *a = entryOf(after, m.key);
        const int   pb = b ? b->perspectiveCode() : kNone, pa = a ? a->perspectiveCode() : kNone;
        if (pb != pa) {
            out.listChanged = true;
            if (m.live) out.perspectives.append({ m.key, pa });
            if (m.selected && m.live && (pb == kImpact) != (pa == kImpact)) out.reconnect.append(m.key);
        }
        if (!m.live) continue;
        const bool mb = b && b->mirrored, ma = a && a->mirrored;
        if (mb != ma) out.mirrored.append({ m.key, ma });
        const bool fb = b && b->fixedInPlace, fa = a && a->fixedInPlace;
        if (fb != fa) out.fixedInPlace.append({ m.key, fa });
        const std::optional<QRectF> bb = b ? b->hittingArea : std::nullopt, ba = a ? a->hittingArea : std::nullopt;
        if (bb != ba) out.boxes.append({ m.key, ba });
    }
    return out;
}

EnabledReaction enabledReaction(const EnabledCamera &camera, bool inSessionExcluded, bool enabledOnThisMachine)
{
    EnabledReaction r;
    const bool excluded = !enabledOnThisMachine;
    if (camera.listed && camera.excluded == excluded) return r;   // its own flag already says so
    r.nothing       = false;
    r.excluded      = excluded;
    r.sessionAdd    = excluded && !inSessionExcluded;
    r.sessionRemove = !excluded && inSessionExcluded;
    r.deselect      = camera.listed && excluded && camera.selected;
    r.select        = camera.listed && !excluded && !camera.selected;
    return r;
}

void requestPerspective(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, int perspectiveCode)
{
    rig.assignPerspective(camera, perspectiveCode);
}

void requestMirrored(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, bool mirrored)
{
    rig.setMirrored(camera, mirrored);
}

QRectF requestBox(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, const QRectF &box)
{
    const QRectF roi = box.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (roi.isEmpty()) {
        rig.clearHittingArea(camera);
        return {};
    }
    rig.setHittingArea(camera, roi);
    return roi;
}

void requestClearBox(pinpoint::RigCameraCommands &rig, const DeviceKey &camera)
{
    rig.clearHittingArea(camera);
}

void requestAlias(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, const QString &typed)
{
    rig.setAlias(camera, typed.trimmed());
}

void requestExcluded(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, bool excluded)
{
    rig.setEnabledOnThisMachine(camera, !excluded);
}

int requestAliasSeeds(pinpoint::RigCameraCommands &rig, const RigParts &parts, const QList<DeviceKey> &cameras)
{
    int asked = 0;
    for (const DeviceKey &k : cameras) {
        if (!k.isCamera() || !aliasOfCamera(parts, k).isEmpty()) continue;
        rig.setAlias(k, pp_camroi::seededAlias(k));
        ++asked;
    }
    return asked;
}

} // namespace pp_camrig
