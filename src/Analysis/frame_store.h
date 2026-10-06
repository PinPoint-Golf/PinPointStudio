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

// ONE decode per frame per camera, shared by every stage (analysis_dag_design.md step E,
// decode.frameStore).
//
// WHY. Re-analysing a swing from its MP4s on the studio, the face-on clip (≈745 frames,
// 688×1024) was fetched and decoded by the pose pass (twice: the coarse pass over the whole
// window, then the span), by Ball (1.1 s), by the face-on shaft tracker's frame cache
// (1.45 s), by ImpactAnchor (0.4 s, a back-seek that rewinds the MP4 decoder to frame 0) and
// by AddressMarks (0.19 s); the DTL clip (≈617 frames) by DtlPose (1.2 s) and DtlShaft (1.2 s)
// — about 5 s of a 12.6 s chain, all of it the same pixels again. On a live shot the same
// repetition is one edge-aware demosaic of the Bayer ring frame per consumer.
//
// WHAT IS HELD. Every entry of the camera's window, in one forward pass (the fetch serial —
// the payload contract — the decode parallel over a chunk of owned copies, as ball_runner and
// shaft_frame_io already do it):
//   * grey CV_8UC1 for every frame — decodeToBgr then cvtColor(BGR2GRAY), the decodeToLuma
//     fallback for a payload decodeToBgr refuses: decodeGrayFromBytes's bytes exactly, which
//     is what Ball, Shaft, DtlShaft and ImpactAnchor read;
//   * the decodeToBgr output itself (owned) when BGR is kept: what the pose passes' ViTPose
//     preprocess and AddressMarks' segmenter read. BGR is kept for EVERY frame or for none —
//     the pose's frames are chosen by its schedule after its own coarse pass, so a subset
//     would send it back to the window for the rest (a back-seek on the MP4 path) — and only
//     under decode.frameStoreMaxMiB and on a machine with decode.frameStoreBgrMinRamGiB of
//     physical memory. Without it the pose passes and AddressMarks read the window as before.
// The WHOLE window, not the union of the consumers' ranges: the two-pass face-on pose already
// scans the whole window, the MP4 reader decodes every frame before the last one asked for
// anyway, and a whole-window store needs no ladder — so the DTL store is built at t = 0, beside
// the face-on pose, instead of after SegResolve.
//
// READ-ONLY after build. Two consumers may read the same store at once; nobody writes into a
// Mat they got from it (the shaft trackers' frame caches share its buffers, so the 525 MB of
// face-on grey is held once).
//
// WHO SEES IT. FrameDecodeStage (wrist_analyzer.cpp) builds it and publishes a FrameStoreSlot
// for (window, source); the slot lives as long as the analysis context. Consumers deep in the
// runners (PoseRunner, BallRunner, ShaftTracker, DtlShaftTracker) take a FrameLease for
// (window, source): the store when it holds what they need, else the slot's reader lock, held
// for as long as the lease — the executor no longer serialises a consumer that dropped
// window.<cam> from its declaration, so a consumer falling back to the window must exclude the
// others that might, itself. With decode.frameStore off no lease is ever taken.

#include <opencv2/core.hpp>

#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "types.h"   // SourceId, IndexEntry
#include "format_descriptor.h"

namespace pinpoint { class SwingWindow; }

namespace pinpoint::analysis {

struct FrameStoreConfig {
    bool enabled      = true;    // decode.frameStore (fromOverrides sets the frozen default)
    int  maxMiB       = 2048;    // decode.frameStoreMaxMiB (per camera)
    int  bgrMinRamGiB = 24;      // decode.frameStoreBgrMinRamGiB
    static FrameStoreConfig fromOverrides(const QVariantMap &ov);   // frozen defaults + keys
};

// One frame through the store's decode: exactly what decodeGrayFromBytes (shaft_frame_io.h)
// and decodeToBgr give for the same bytes. `bgr` is the owned decodeToBgr output when
// `keepBgr` (left empty otherwise); `bgrOk` is decodeToBgr's verdict — Ball skips a frame it
// refuses, the shaft cache takes the luma fallback. Pure: the unit test drives it directly.
void decodeStoreFrame(const pinpoint::CameraFormat &cfmt, const std::byte *data, size_t bytes,
                      bool keepBgr, cv::Mat &grey, cv::Mat &bgr, bool &bgrOk);

class FrameStore {
public:
    struct Stats {
        int    frames   = 0;
        double greyMiB  = 0.0, bgrMiB = 0.0;
        double fetchMs  = 0.0;   // the serial fetch (on the MP4 path, the H.264 decode)
        double decodeMs = 0.0;   // the parallel decode + grey conversion
    };

    // Fetch + decode every entry of `source` in `window`. Null, with `why` set, when the camera
    // has no decodable format or the grey alone is over the cap. `wantBgr` asks for BGR; it is
    // kept only under the cap and the RAM floor (`why` then says which refused it).
    static std::shared_ptr<const FrameStore> build(const pinpoint::SwingWindow &window,
                                                   pinpoint::SourceId source,
                                                   const FrameStoreConfig &cfg, bool wantBgr,
                                                   QString &why, Stats &stats);

    pinpoint::SourceId source() const { return m_source; }
    bool hasBgr() const { return m_hasBgr; }
    int  size() const { return int(m_seq.size()); }

    // Position of an entry of this source in the store, or -1.
    int indexOf(const pinpoint::IndexEntry &e) const
    {
        if (e.source_id != m_source) return -1;
        const auto it = std::lower_bound(m_order.begin(), m_order.end(), e.source_sequence,
                                         [](const std::pair<uint64_t, int> &p, uint64_t s) { return p.first < s; });
        return (it != m_order.end() && it->first == e.source_sequence) ? it->second : -1;
    }
    // Null when the entry is not held; an EMPTY Mat when its payload did not decode — the
    // same empty Mat decodeGray / decodeToBgr gave for it.
    const cv::Mat *grey(const pinpoint::IndexEntry &e) const
    {
        const int i = indexOf(e);
        return i < 0 ? nullptr : &m_grey[size_t(i)];
    }
    const cv::Mat *bgr(const pinpoint::IndexEntry &e) const
    {
        const int i = m_hasBgr ? indexOf(e) : -1;
        return i < 0 ? nullptr : &m_bgr[size_t(i)];
    }
    bool bgrOk(const pinpoint::IndexEntry &e) const
    {
        const int i = indexOf(e);
        return i >= 0 && m_bgrOk[size_t(i)] != 0;
    }

private:
    pinpoint::SourceId                 m_source = pinpoint::kInvalidSourceId;
    bool                               m_hasBgr = false;
    std::vector<uint64_t>              m_seq;     // entry order (the window's)
    std::vector<std::pair<uint64_t, int>> m_order;   // (sequence, index), sorted by sequence
    std::vector<cv::Mat>               m_grey, m_bgr;
    std::vector<char>                  m_bgrOk;
};

// The per-analysis handle FrameDecodeStage publishes for one camera. `store` null = not built
// (over the cap, no format): every consumer then reads the window under `readerMu`.
struct FrameStoreSlot {
    std::shared_ptr<const FrameStore> store;
    std::mutex                        readerMu;
};

// The registry consumers find a slot through, keyed by (window, source). Weak: the slot dies
// with the analysis context that owns it, so a later pass over the same window (swinglab's
// --dtl block, after analyze() returned) finds nothing and reads the window as always.
void publishFrameStoreSlot(const pinpoint::SwingWindow *window, pinpoint::SourceId source,
                           const std::shared_ptr<FrameStoreSlot> &slot);
std::shared_ptr<FrameStoreSlot> findFrameStoreSlot(const pinpoint::SwingWindow *window,
                                                   pinpoint::SourceId source);

// What a consumer holds while it reads one camera's frames. store() is the store when it holds
// what the consumer needs (grey always; BGR when `needBgr`); otherwise null, and — if a slot
// was published for this analysis — the slot's reader lock is held until the lease dies, so
// the consumer's window reads cannot interleave with another consumer's. decode.frameStore
// off ⇒ an empty lease: no lookup, no lock, today's path.
class FrameLease {
public:
    FrameLease(const pinpoint::SwingWindow &window, pinpoint::SourceId source,
               const QVariantMap &overrides, bool needBgr);
    const FrameStore *store() const { return m_use ? m_slot->store.get() : nullptr; }
    FrameLease(const FrameLease &) = delete;
    FrameLease &operator=(const FrameLease &) = delete;

private:
    std::shared_ptr<FrameStoreSlot> m_slot;
    std::unique_lock<std::mutex>    m_lock;
    bool                            m_use = false;
};

// Physical memory in bytes (0 when the platform will not say).
uint64_t physicalMemoryBytes();

} // namespace pinpoint::analysis
