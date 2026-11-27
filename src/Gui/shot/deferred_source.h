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

// A DEFERRED SOURCE: a device whose best data for a swing arrives after the swing (deferred_sources_design.md §4.1).
// The shot processor reserves at the trigger, polls during its gather and collects the block at the window's
// construction; this is all it asks of one. HmInstance (a HackMotion wG3) is today's only implementation, and the
// capture test rig's stand-in is the other (#394, step 81; docs/implementation/platform_d_capture_seam.md, T2, ruled
// 25 Nov 2026). The shot processor is handed its sources through ShotProcessor::setDeferredSources.
//
// The block is the HackMotion's own type (src/IMU/hm_session_types.h): it is the one deferred source there is, and a
// second kind of device would need its own block in any case. A source stitches into at most two lanes, the lower arm
// (0) and the palm (1), each a buffer source the live stream already registered.

#include "../../IMU/hm_session_types.h"   // pinpoint::hm::HistoryResult
#include "../../Buffer/types.h"           // pinpoint::SourceId

#include <QString>
#include <QtGlobal>

namespace pinpoint {

class DeferredSource
{
public:
    virtual ~DeferredSource() = default;

    // The device, by the id its provenance is keyed by in the swing document.
    virtual QString deviceId() const = 0;
    // Reserve at DETECTION: the range the swing will want. Never blocks; a refusal leaves nothing pending.
    virtual void reserveHistory(qint64 impactUs, qint64 deadlineUs) = 0;
    // True while a reservation is outstanding. The gather polls this.
    virtual bool historyPending() const = 0;
    // The collected block, moved out; a second call returns an empty one.
    virtual hm::HistoryResult takeHistoryResult() = 0;
    // The buffer source of lane 0 (the lower arm) or 1 (the palm); kInvalidSourceId when it has none.
    virtual SourceId deferredLane(int lane) const = 0;
};

} // namespace pinpoint
