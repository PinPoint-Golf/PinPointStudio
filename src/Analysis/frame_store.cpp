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

#include "frame_store.h"

#include <opencv2/imgproc.hpp>

#include <QElapsedTimer>

#include "analysis_tuning.h"
#include "swing_window.h"
#include "../Core/pp_tuned_constants.h"
#include "../Export/frame_decode.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#else
#  include <unistd.h>
#endif

namespace pinpoint::analysis {

FrameStoreConfig FrameStoreConfig::fromOverrides(const QVariantMap &ov)
{
    FrameStoreConfig c;
    c.enabled      = pinpoint::tuned::decode::kFrameStore;
    c.maxMiB       = pinpoint::tuned::decode::kFrameStoreMaxMiB;
    c.bgrMinRamGiB = pinpoint::tuned::decode::kFrameStoreBgrMinRamGiB;
    tuning::apply(ov, "decode.frameStore",             c.enabled);
    tuning::apply(ov, "decode.frameStoreMaxMiB",       c.maxMiB);
    tuning::apply(ov, "decode.frameStoreBgrMinRamGiB", c.bgrMinRamGiB);
    return c;
}

void decodeStoreFrame(const pinpoint::CameraFormat &cfmt, const std::byte *data, size_t bytes,
                      bool keepBgr, cv::Mat &grey, cv::Mat &bgr, bool &bgrOk)
{
    grey.release();
    bgr.release();
    bgrOk = false;
    cv::Mat view;   // fresh: decodeToBgr's BGR24 passthrough aliases `data`
    if (pinpoint::decodeToBgr(cfmt, data, bytes, view) && !view.empty()) {
        bgrOk = true;
        // decodeGrayFromBytes, verbatim: a 1-channel decode is cloned, else BGR2GRAY.
        if (view.channels() == 1) grey = view.clone();
        else                      cv::cvtColor(view, grey, cv::COLOR_BGR2GRAY);
        if (keepBgr)
            bgr = view.data == reinterpret_cast<const uchar *>(data) ? view.clone() : view;
        return;
    }
    cv::Mat luma;
    if (pinpoint::decodeToLuma(cfmt, data, bytes, luma) && !luma.empty())
        grey = luma.clone();
}

std::shared_ptr<const FrameStore> FrameStore::build(const pinpoint::SwingWindow &window,
                                                    pinpoint::SourceId source,
                                                    const FrameStoreConfig &cfg, bool wantBgr,
                                                    QString &why, Stats &stats)
{
    stats = Stats{};
    const std::vector<pinpoint::IndexEntry> entries = window.entriesFor(source);
    if (entries.empty()) { why = QStringLiteral("no frames"); return nullptr; }
    const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&window.formatOf(source).format);
    if (!cfmt || cfmt->width == 0 || cfmt->height == 0
        || !pinpoint::demosaicPlanFor(cfmt->pixel_format).supported) {
        why = QStringLiteral("no decodable camera format");
        return nullptr;
    }
    const size_t n = entries.size();
    const size_t px = size_t(cfmt->width) * size_t(cfmt->height);
    const size_t greyBytes = n * px, bgrBytes = n * px * 3;   // every supported format decodes to 3 channels
    const size_t cap = size_t(std::max(0, cfg.maxMiB)) * 1024ull * 1024ull;
    if (greyBytes > cap) {
        why = QStringLiteral("grey %1 MiB over decode.frameStoreMaxMiB %2")
                  .arg(double(greyBytes) / 1048576.0, 0, 'f', 0).arg(cfg.maxMiB);
        return nullptr;
    }
    bool keepBgr = wantBgr;
    if (keepBgr && greyBytes + bgrBytes > cap) {
        keepBgr = false;
        why = QStringLiteral("BGR not kept: grey + BGR %1 MiB over decode.frameStoreMaxMiB %2")
                  .arg(double(greyBytes + bgrBytes) / 1048576.0, 0, 'f', 0).arg(cfg.maxMiB);
    }
    if (keepBgr && cfg.bgrMinRamGiB > 0) {
        const uint64_t ram = physicalMemoryBytes();
        if (ram < uint64_t(cfg.bgrMinRamGiB) * 1024ull * 1024ull * 1024ull) {
            keepBgr = false;
            why = QStringLiteral("BGR not kept: %1 GiB physical memory < decode.frameStoreBgrMinRamGiB %2")
                      .arg(double(ram) / 1073741824.0, 0, 'f', 0).arg(cfg.bgrMinRamGiB);
        }
    }

    auto s = std::make_shared<FrameStore>();
    s->m_source = source;
    s->m_hasBgr = keepBgr;
    s->m_seq.resize(n);
    s->m_order.resize(n);
    s->m_grey.assign(n, cv::Mat());
    s->m_bgr.assign(keepBgr ? n : 0, cv::Mat());
    s->m_bgrOk.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        s->m_seq[i] = entries[i].source_sequence;
        s->m_order[i] = { entries[i].source_sequence, int(i) };
    }
    std::sort(s->m_order.begin(), s->m_order.end());

    // Chunked: a serial fetch into owned copies (the payload is valid only until the next
    // payloadOf on this source), then a parallel decode of the chunk — the shape of
    // shaft_frame_io's buildFrameCache and ball_runner's replay. 32 frames of BGR24 is 67 MB
    // of copies in flight at 688×1024.
    constexpr size_t kChunk = 32;
    std::vector<std::vector<std::byte>> buf(kChunk);
    std::vector<char> have(kChunk, 0);
    QElapsedTimer t;
    qint64 fetchNs = 0, decodeNs = 0;
    for (size_t base = 0; base < n; base += kChunk) {
        const size_t cnt = std::min(kChunk, n - base);
        t.start();
        for (size_t j = 0; j < cnt; ++j) {
            const pinpoint::SourceRing::ReadHandle h = window.payloadOf(entries[base + j]);
            have[j] = (h.data != nullptr && h.bytes > 0) ? 1 : 0;
            if (have[j]) buf[j].assign(h.data, h.data + h.bytes);
            else         buf[j].clear();
        }
        fetchNs += t.nsecsElapsed();
        t.start();
        cv::parallel_for_(cv::Range(0, int(cnt)), [&](const cv::Range &r) {
            for (int j = r.start; j < r.end; ++j) {
                const size_t i = base + size_t(j);
                cv::Mat g, b;
                bool ok = false;
                // A null payload decodes to nothing on every path (decodeToBgr/decodeToLuma
                // refuse a null pointer) — the empty Mat each consumer saw.
                decodeStoreFrame(*cfmt, have[size_t(j)] ? buf[size_t(j)].data() : nullptr,
                                 buf[size_t(j)].size(), keepBgr, g, b, ok);
                s->m_grey[i] = std::move(g);
                if (keepBgr) s->m_bgr[i] = std::move(b);
                s->m_bgrOk[i] = ok ? 1 : 0;
            }
        });
        decodeNs += t.nsecsElapsed();
    }
    stats.frames   = int(n);
    stats.greyMiB  = double(greyBytes) / 1048576.0;
    stats.bgrMiB   = keepBgr ? double(bgrBytes) / 1048576.0 : 0.0;
    stats.fetchMs  = double(fetchNs) / 1e6;
    stats.decodeMs = double(decodeNs) / 1e6;
    return s;
}

namespace {
struct RegEntry {
    const pinpoint::SwingWindow   *window;
    pinpoint::SourceId             source;
    std::weak_ptr<FrameStoreSlot>  slot;
};
std::mutex &regMu() { static std::mutex m; return m; }
std::vector<RegEntry> &reg() { static std::vector<RegEntry> r; return r; }
} // namespace

void publishFrameStoreSlot(const pinpoint::SwingWindow *window, pinpoint::SourceId source,
                           const std::shared_ptr<FrameStoreSlot> &slot)
{
    std::lock_guard<std::mutex> lk(regMu());
    std::vector<RegEntry> &r = reg();
    r.erase(std::remove_if(r.begin(), r.end(), [&](const RegEntry &e) {
                return e.slot.expired() || (e.window == window && e.source == source); }),
            r.end());
    r.push_back(RegEntry{ window, source, slot });
}

std::shared_ptr<FrameStoreSlot> findFrameStoreSlot(const pinpoint::SwingWindow *window,
                                                   pinpoint::SourceId source)
{
    std::lock_guard<std::mutex> lk(regMu());
    for (const RegEntry &e : reg())
        if (e.window == window && e.source == source)
            return e.slot.lock();
    return nullptr;
}

FrameLease::FrameLease(const pinpoint::SwingWindow &window, pinpoint::SourceId source,
                       const QVariantMap &overrides, bool needBgr)
{
    bool on = pinpoint::tuned::decode::kFrameStore;
    tuning::apply(overrides, "decode.frameStore", on);
    if (!on) return;
    m_slot = findFrameStoreSlot(&window, source);
    if (!m_slot) return;
    if (m_slot->store && (!needBgr || m_slot->store->hasBgr())) {
        m_use = true;
        return;
    }
    m_lock = std::unique_lock<std::mutex>(m_slot->readerMu);
}

uint64_t physicalMemoryBytes()
{
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? uint64_t(ms.ullTotalPhys) : 0;
#elif defined(__APPLE__)
    uint64_t mem = 0;
    size_t sz = sizeof(mem);
    return sysctlbyname("hw.memsize", &mem, &sz, nullptr, 0) == 0 ? mem : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? uint64_t(pages) * uint64_t(page) : 0;
#endif
}

} // namespace pinpoint::analysis
