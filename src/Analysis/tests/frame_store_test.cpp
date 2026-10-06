// frame_store_test — the analysis' one decode per camera (frame_store.h; decode.frameStore,
// analysis_dag_design.md step E), over a synthetic SwingWindow:
//   1. for BayerRG8, BGR24 and Mono8 the store's grey is decodeToBgr → BGR2GRAY (the bytes
//      decodeGrayFromBytes gives the shaft trackers, and Ball computes) and its BGR is the
//      decodeToBgr output — byte for byte, every frame; a null payload is an empty frame;
//   2. the caps: grey + BGR over decode.frameStoreMaxMiB keeps grey only, grey over it builds
//      nothing; the RAM floor refuses BGR;
//   3. the lease: off ⇒ nothing; a store that holds what is asked ⇒ the store, no lock; no store
//      (or no BGR when BGR is asked) ⇒ the slot's reader lock, held until the lease dies; a slot
//      whose analysis is gone ⇒ nothing.

#include "../frame_store.h"
#include "../../Buffer/swing_window.h"
#include "../../Buffer/swing_payload_source.h"
#include "../../Export/frame_decode.h"

#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <thread>
#include <vector>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// Camera frames held in RAM, one lane per source. Sequence = index; an empty payload is "absent".
class StubFrames final : public SwingPayloadSource {
public:
    void add(SourceId id, CameraFormat cf, std::vector<std::vector<std::byte>> frames)
    {
        FormatDescriptor fd;
        fd.format = cf;
        lanes_[id] = Lane{ fd, std::move(frames) };
    }
    SourceRing::ReadHandle payloadOf(SourceId id, uint64_t seq) const noexcept override
    {
        const auto it = lanes_.find(id);
        if (it == lanes_.end() || seq >= it->second.frames.size() || it->second.frames[seq].empty())
            return {};
        SourceRing::ReadHandle h;
        h.data  = it->second.frames[seq].data();
        h.bytes = it->second.frames[seq].size();
        return h;
    }
    const FormatDescriptor &formatOf(SourceId id) const noexcept override
    {
        const auto it = lanes_.find(id);
        if (it != lanes_.end()) return it->second.fd;
        static const FormatDescriptor kEmpty{};
        return kEmpty;
    }
    bool validate(SourceId, const SourceRing::ReadHandle &) const noexcept override { return true; }
private:
    struct Lane { FormatDescriptor fd; std::vector<std::vector<std::byte>> frames; };
    std::map<SourceId, Lane> lanes_;
};

static std::vector<std::byte> randomBytes(size_t n, unsigned seed)
{
    cv::Mat m(1, int(n), CV_8UC1);
    cv::RNG rng(seed);
    rng.fill(m, cv::RNG::UNIFORM, 0, 256);
    std::vector<std::byte> v(n);
    std::memcpy(v.data(), m.data, n);
    return v;
}

static bool sameMat(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty() || b.empty()) return a.empty() && b.empty();
    if (a.size() != b.size() || a.type() != b.type()) return false;
    return cv::norm(a, b, cv::NORM_INF) == 0.0;
}

struct Built {
    std::unique_ptr<SwingWindow> window;
    std::vector<std::vector<std::byte>> frames;
    CameraFormat cf;
};

static Built makeWindow(PixelFormat pf, int w, int h, int bpp, int n, SourceId id, int nullAt = -1)
{
    Built b;
    b.cf.pixel_format = pf;
    b.cf.width = uint32_t(w);
    b.cf.height = uint32_t(h);
    for (int i = 0; i < n; ++i)
        b.frames.push_back(i == nullAt ? std::vector<std::byte>() : randomBytes(size_t(w) * h * bpp, 17u + unsigned(i)));
    auto src = std::make_unique<StubFrames>();
    src->add(id, b.cf, b.frames);
    std::vector<IndexEntry> entries;
    for (int i = 0; i < n; ++i) {
        IndexEntry e;
        e.timestamp_us = 1000 + 5000LL * i;
        e.source_id = id;
        e.source_sequence = uint64_t(i);
        e.global_sequence = uint64_t(i);
        entries.push_back(e);
    }
    b.window = std::make_unique<SwingWindow>(std::move(src), std::move(entries), 0, 1000 + 5000LL * n);
    return b;
}

static FrameStoreConfig cfgAll()
{
    FrameStoreConfig c;
    c.enabled = true;
    c.maxMiB = 4096;
    c.bgrMinRamGiB = 0;
    return c;
}

int main()
{
    std::printf("1. grey and BGR equal decodeToBgr / BGR2GRAY\n");
    struct Fmt { PixelFormat pf; int bpp; const char *name; };
    for (const Fmt f : { Fmt{ PixelFormat::BayerRG8, 1, "BayerRG8" }, Fmt{ PixelFormat::BGR24, 3, "BGR24" },
                         Fmt{ PixelFormat::Mono8, 1, "Mono8" } }) {
        const SourceId id = 7;
        Built b = makeWindow(f.pf, 64, 48, f.bpp, 40, id, /*nullAt*/ 5);   // 40 > one 32-frame chunk
        QString why;
        FrameStore::Stats st;
        const auto store = FrameStore::build(*b.window, id, cfgAll(), true, why, st);
        bool built = store && store->hasBgr() && store->size() == 40 && st.frames == 40 && why.isEmpty();
        bool greyOk = true, bgrOk = true, nullOk = true;
        for (const IndexEntry &e : b.window->entriesFor(id)) {
            const std::vector<std::byte> &raw = b.frames[size_t(e.source_sequence)];
            cv::Mat refBgr, refGrey;
            const bool ok = pinpoint::decodeToBgr(b.cf, raw.empty() ? nullptr : raw.data(), raw.size(), refBgr);
            if (ok) cv::cvtColor(refBgr, refGrey, cv::COLOR_BGR2GRAY);
            if (!store) break;
            if (raw.empty()) {
                nullOk = store->grey(e) && store->grey(e)->empty() && !store->bgrOk(e);
                continue;
            }
            greyOk = greyOk && store->grey(e) && sameMat(*store->grey(e), refGrey) && store->bgrOk(e);
            bgrOk  = bgrOk && store->bgr(e) && sameMat(*store->bgr(e), refBgr)
                     && store->bgr(e)->data != reinterpret_cast<const uchar *>(raw.data());   // owned, not aliased
        }
        std::printf("  %s:\n", f.name);
        check(built, "store built over every entry, BGR kept");
        check(greyOk, "grey == decodeToBgr then cvtColor(BGR2GRAY), every frame");
        check(bgrOk, "BGR == decodeToBgr, every frame, owned");
        check(nullOk, "a null payload is an empty grey frame, BGR refused");
        // decodeStoreFrame on its own, without BGR: the grey only.
        cv::Mat g, bgr;
        bool ok = false;
        decodeStoreFrame(b.cf, b.frames[0].data(), b.frames[0].size(), false, g, bgr, ok);
        cv::Mat rb, rg;
        pinpoint::decodeToBgr(b.cf, b.frames[0].data(), b.frames[0].size(), rb);
        cv::cvtColor(rb, rg, cv::COLOR_BGR2GRAY);
        check(ok && bgr.empty() && sameMat(g, rg), "decodeStoreFrame without BGR: grey only");
        IndexEntry other;
        other.source_id = id + 1;
        check(!store || (store->grey(other) == nullptr && store->indexOf(other) < 0), "an entry of another source is not held");
    }

    std::printf("2. caps\n");
    {
        const SourceId id = 3;
        Built b = makeWindow(PixelFormat::BGR24, 512, 512, 3, 10, id);   // grey 2.5 MiB, BGR 7.5 MiB
        QString why;
        FrameStore::Stats st;
        FrameStoreConfig c = cfgAll();
        c.maxMiB = 5;
        auto s = FrameStore::build(*b.window, id, c, true, why, st);
        check(s && !s->hasBgr() && st.bgrMiB == 0.0 && why.contains("BGR not kept"), "grey + BGR over the cap: grey only, and says so");
        c.maxMiB = 2;
        s = FrameStore::build(*b.window, id, c, true, why, st);
        check(!s && why.contains("over decode.frameStoreMaxMiB"), "grey over the cap: no store");
        c = cfgAll();
        c.bgrMinRamGiB = 1 << 20;   // a petabyte of RAM
        s = FrameStore::build(*b.window, id, c, true, why, st);
        check(s && !s->hasBgr() && why.contains("physical memory"), "below the RAM floor: grey only");
        check(physicalMemoryBytes() > (1ull << 30), "physical memory is read");
    }

    std::printf("3. lease\n");
    {
        const SourceId id = 9;
        Built b = makeWindow(PixelFormat::BGR24, 32, 32, 3, 4, id);
        QVariantMap on{ { QStringLiteral("decode.frameStore"), true } };
        // Explicit, not an empty map: the key's default is ON (pp_tuned_constants.h decode::kFrameStore).
        QVariantMap off{ { QStringLiteral("decode.frameStore"), false } };
        {
            const FrameLease l(*b.window, id, on, false);
            check(l.store() == nullptr, "no slot published: no store");
        }
        auto slot = std::make_shared<FrameStoreSlot>();
        QString why;
        FrameStore::Stats st;
        FrameStoreConfig c = cfgAll();
        slot->store = FrameStore::build(*b.window, id, c, false, why, st);   // grey only
        publishFrameStoreSlot(b.window.get(), id, slot);
        const auto tryLock = [&slot]() {
            bool got = false;
            std::thread t([&] { got = slot->readerMu.try_lock(); if (got) slot->readerMu.unlock(); });
            t.join();
            return got;
        };
        {
            const FrameLease l(*b.window, id, off, false);
            check(l.store() == nullptr && tryLock(), "decode.frameStore off: no store, no lock");
        }
        {
            const FrameLease l(*b.window, id, on, false);
            check(l.store() == slot->store.get() && tryLock(), "grey asked of a grey store: the store, no lock");
        }
        {
            const FrameLease l(*b.window, id, on, true);
            check(l.store() == nullptr && !tryLock(), "BGR asked of a grey store: the reader lock, held");
        }
        check(tryLock(), "the lock goes with the lease");
        slot.reset();
        {
            const FrameLease l(*b.window, id, on, true);
            check(l.store() == nullptr && findFrameStoreSlot(b.window.get(), id) == nullptr,
                  "the analysis gone (slot released): nothing found, nothing locked");
        }
    }

    std::printf("frame_store_test: %s (%d failure%s)\n", g_fail ? "FAIL" : "PASS", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
