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

#pragma once

#include "types.h"
#include "source_ring.h"
#include "format_descriptor.h"

#include <cstdint>

namespace pinpoint {

// Pluggable backing for a SwingWindow's payload + format access. It decouples the
// window from the live EventBuffer ring so the SAME concrete SwingWindow type can
// be fed by either:
//   - RingPayloadSource  — the in-RAM ring of a paused EventBuffer (live capture,
//     export); zero-copy reads, seqlock generation validation.
//   - SwingDiskSource    — an exported swing folder streamed from disk (offline
//     re-analysis); one decoded frame per camera resident at a time, IMU in RAM.
//
// The window owns its source for its whole lifetime (unique_ptr). Access is
// read-only. The bytes returned by payloadOf() for a given source must stay valid
// until the next payloadOf() call on that SAME source id — the analysis stages read
// camera frames strictly one-at-a-time, so a single reusable buffer per source
// satisfies this contract.
class SwingPayloadSource {
public:
    virtual ~SwingPayloadSource() = default;

    // Payload bytes + length for (source, sequence). data == nullptr if absent.
    virtual SourceRing::ReadHandle  payloadOf(SourceId id, uint64_t sequence) const noexcept = 0;

    // Format descriptor for a source (pixel format / dims / stride / rate).
    virtual const FormatDescriptor& formatOf(SourceId id) const noexcept = 0;

    // Confirm a handle returned by payloadOf() was not clobbered mid-read. The ring
    // backing checks the seqlock generation against the live ring; an on-disk /
    // in-RAM backing is always valid (its bytes are stable).
    virtual bool validate(SourceId id, const SourceRing::ReadHandle& h) const noexcept = 0;
};

} // namespace pinpoint
