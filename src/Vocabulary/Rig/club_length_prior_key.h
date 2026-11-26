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

// The club-length prior's key (SET-05, `analysis/clubLenPrior`; information_model.md §5.2, "Device keys
// and their spellings"; docs/design/rig_design.md §4.6). Vocabulary ring, context Acquisition.
// Header-only: the identity types and the device key, nothing of src/Gui.
//
// One prior per athlete, club and face-on camera: a length in pixels means nothing once the camera
// moves, so the camera is part of the key.
//
// ⚠ THE SPELLING IS A PERSISTED MAP KEY, KEPT BYTE FOR BYTE: "<athlete uuid>|<club>|<camera key>", the
// camera key being DeviceKey::persistedCameraKey(). Spelled one byte differently, every learned prior
// is silently lost. device_key_test pins it against a store written by the expression it replaced.
//
// The spelling is today's expression whatever the parts hold: a null athlete spells "" (so the key
// starts "|<club>|"), and the club is the session's own string, never canonicalised. The athlete's
// part is AthleteId::toString(), the form the capture path's uuid already has (it comes from
// AthleteController::currentUuid(), itself an AthleteId's toString()). The key is null only when it was
// never given a camera, which is how its holder says "nothing to learn for this shot".
//
// No parser: nothing reads the key back into its parts, and a camera key holds '|' itself.

#include "device_key.h"
#include "../Identity/identity.h"

#include <QLatin1Char>
#include <QString>

#include <utility>

namespace pinpoint::rig {

class ClubLengthPriorKey
{
public:
    ClubLengthPriorKey() = default;   // null: no prior for this shot

    // Null unless `faceOnCamera` is a camera key.
    ClubLengthPriorKey(const AthleteId &athlete, QString club, const DeviceKey &faceOnCamera)
        : m_athlete(athlete),
          m_club(faceOnCamera.isCamera() ? std::move(club) : QString()),
          m_camera(faceOnCamera.isCamera() ? faceOnCamera : DeviceKey()) {}

    bool      isNull()       const noexcept { return m_camera.isNull(); }
    AthleteId athlete()      const noexcept { return m_athlete; }
    QString   club()         const { return m_club; }
    DeviceKey faceOnCamera() const { return m_camera; }

    // analysis/clubLenPrior: "<athlete uuid>|<club>|<camera key>". The null string for a null key.
    QString persisted() const
    {
        if (isNull()) return QString();
        return m_athlete.toString() + QLatin1Char('|') + m_club + QLatin1Char('|') + m_camera.persistedCameraKey();
    }

    friend bool operator==(const ClubLengthPriorKey &a, const ClubLengthPriorKey &b) noexcept
    {
        return a.m_athlete == b.m_athlete && a.m_club == b.m_club && a.m_camera == b.m_camera;
    }
    friend bool operator!=(const ClubLengthPriorKey &a, const ClubLengthPriorKey &b) noexcept { return !(a == b); }

private:
    AthleteId m_athlete;
    QString   m_club;
    DeviceKey m_camera;
};

} // namespace pinpoint::rig
