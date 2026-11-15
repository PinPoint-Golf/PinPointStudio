/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "shot.h"
#include "session.h"
#include "recording_repository.h"

namespace pinpoint {

Shot::Shot(const ShotRef &ref, std::optional<recording::ShotKind> kind, const ShotDoors &doors, Session *parent)
    : QObject(parent), m_ref(ref), m_doors(doors), m_kind(kind)
{
}

recording::ShotKind Shot::kind() const
{
    // Read once, then kept: the kind never changes (CLS-SHOT). Without a door it is not known to be
    // device-only, which is what the classification answers for a shot it cannot find.
    if (!m_kind)
        m_kind = (m_doors.recordings && m_doors.recordings->isDeviceOnly(m_ref)) ? recording::ShotKind::DeviceOnly
                                                                                : recording::ShotKind::Captured;
    return *m_kind;
}

} // namespace pinpoint
