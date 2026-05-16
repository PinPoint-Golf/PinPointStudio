# Pinpoint Central Event Buffer — Design Document

**Version**: 6 (Final)
**Status**: Implementation Specification — Locked
**Supersedes**: v1, v2, v3, v4, v5

---

## Revision History

| Version | Changes |
|---|---|
| v1 | Initial design — per-source rings, unified index, seqlock SPSC, merger thread |
| v2 | Raw device-format storage; timestamping strategy; producer capture protocol |
| v3 | Chameleon3 validation; DMA transport abstraction per OS; hardware sync placeholders; C++20 toolchain validation; unprivileged thread priority strategy |
| v4 | Performance/security/robustness review. Incorporated Rec-1,2,3,6,8,9,10,11,13,15,16. Desktop-first defaults locked. Lock-free on Linux/Windows; condition_variable fallback on macOS (accepted). Crash forensics deferred. |
| v5 | Application lifecycle design. Added `BufferState` formal state machine, `pause()`/`resume()` API, `SwingWindow` protected time-range view, `WriteSlot::valid` flag in producer protocol, lifecycle section (Section 20), updated architectural decisions and open configuration parameters. |
| v6 | Dynamic source registration. Lifted Idle-only registration constraint; added `deregisterSource()` with immediate memory release; sparse slot array with `slot_hwm_` / `active_sources_`; `SwingWindow` live-flag safety; buffer lifetime = app lifetime; `start()` valid with zero sources; `pause()`/`resume()` as the capture gate; `startAll()`/`stopAll()` control camera pipelines only. |

---

## 1. Design Goals

1. **Central registration** — sources register; consumers subscribe to a unified timeline
2. **Pre-allocated** — fixed 5s window per source, zero allocation on hot path after registration
3. **Circular / drop-oldest** — overwrite tail when full, no producer backpressure
4. **Lock-free on hot paths** — Linux/Windows native; macOS uses `condition_variable` for waits (accepted degradation)
5. **Zero-copy where the OS API permits**, one copy where it does not
6. **Raw device fidelity** — store bytes exactly as the device delivered them
7. **Real-time timestamps** — sub-100µs accuracy, single shared monotonic clock
8. **Production observability** — per-source statistics, liveness monitoring, overrun detection
9. **Desktop-first** — Windows and Linux desktop are primary targets; mobile platforms supported via configuration profiles
10. **Application lifecycle** — buffer lifetime equals app lifetime; `pause()`/`resume()` gate capture windows; device selection drives memory allocation
11. **Dynamic device registration** — sources can be registered while `Idle` or `Paused` and deregistered while `Paused`; memory freed immediately on deregistration

---

## 2. Architectural Decisions (Locked)

| # | Decision |
|---|---|
| 1 | **Per-source storage + unified index** — heterogeneous payloads in typed rings, lightweight index for temporal ordering |
| 2 | **SPSC per source, SPMC index** — no MPMC anywhere |
| 3 | **Seqlock-protected slots** — generation counters frame writes, consumers validate before/after |
| 4 | **Sequence-number-based reads** — consumers track position, detect overruns explicitly |
| 5 | **Dedicated merger thread** — k-way timestamp merge into the index; runs for app lifetime |
| 6 | **Raw bytes + FormatDescriptor** — buffer never interprets payloads |
| 7 | **Single shared `nowMicros()` monotonic clock** — used by all capture threads |
| 8 | **Three transport classes** — `DirectDma`, `SingleCopy`, `StreamingRead` |
| 9 | **Hardware sync placeholders** — `SyncSource` enum and `sync_group`, no active sync logic in v1 |
| 10 | **C++20 target** — Win10 1809+, macOS 11+, Ubuntu 20.04+, iOS 14+, Android API 24+ |
| 11 | **`WaitFlag` abstraction** — lock-free on Linux/Windows, `condition_variable` on macOS/iOS (accepted degradation) |
| 12 | **Unprivileged thread priority elevation** — graceful fallback where elevation requires admin |
| 13 | **Desktop-first defaults** — 5s window, 1080p60 expected; mobile is a configuration profile, not a separate code path |
| 14 | **Dynamic source lifecycle** — `registerSource()` callable while `Idle` or `Paused`; `deregisterSource()` callable while `Paused`; memory freed immediately on deregistration; `SourceId` is stable and may be reused after deregistration |
| 15 | **Hard bounds enforcement in release builds** — defence in depth on the producer/consumer contract |
| 16 | **Formal `BufferState` machine** — `Idle → Capturing → Paused → Capturing → ...`; transitions are explicit, invalid transitions assert |
| 17 | **`pause()` does not tear down DMA** — producer capture threads quiesce via atomic flag; merger drains reorder heap; rings freeze. `resume()` reverses with no kernel calls |
| 18 | **`SwingWindow` is only valid after `pause()`** — analysis reads a frozen buffer; `swing_window_live_` atomic flag prevents deregistration while a window is live |
| 19 | **`WriteSlot::valid` flag** — `acquireWriteSlot()` returns `valid = false` when paused or when the source has been deregistered; capture threads check this flag and return early from callbacks; no blocking in capture path |
| 20 | **Buffer lifetime = app lifetime** — `start()` called once at app launch with zero sources; `stop()` called on app exit via `aboutToQuit`; `pause()`/`resume()` are the capture gate driven by swing/ball detection; `startAll()`/`stopAll()` control camera pipelines only |
| 21 | **Sparse slot array** — `sources_[MAX_SOURCES]` may contain null entries (deregistered slots); `slot_hwm_` is the iteration ceiling; `active_sources_` is the live count; null slots skipped atomically in all merger, watchdog, and diagnostics loops |
| 21 | **Sparse slot array** — `sources_[MAX_SOURCES]` may contain null entries (deregistered slots); `slot_hwm_` is the high-water mark iterated by the merger; `active_sources_` is the live count; null slots skipped in all loops |

---

## 3. Component Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                       EventBuffer (central)                      │
│                                                                  │
│  ┌────────────────┐  ┌────────────────┐  ┌──────────────────┐    │
│  │ SourceRing     │  │ SourceRing     │  │ SourceRing       │    │
│  │ Cam0 (raw)     │  │ Cam1 (raw)     │  │ IMU0 (raw)       │    │
│  │ Mono8/12/...   │  │ Mono8/12/...   │  │ device packet    │    │
│  │ + SourceStats  │  │ + SourceStats  │  │ + SourceStats    │    │
│  └────────────────┘  └────────────────┘  └──────────────────┘    │
│       ▲ 1 producer        ▲ 1 producer        ▲ 1 producer       │
│       │ zero/single-copy  │                   │                  │
│   [Cam0 capture]      [Cam1 capture]      [IMU0 capture]         │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Merger Thread                                              │  │
│  │  - Hybrid wait (spin → WaitFlag with timeout)              │  │
│  │  - Fixed-size sorted merge buffer (no allocation)          │  │
│  │  - Monotonicity enforcement per source                     │  │
│  │  - Liveness watchdog checks                                │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ TimelineIndex (SPMC ring of IndexEntry)                    │  │
│  └────────────────────────────────────────────────────────────┘  │
│       │                                                          │
│       ▼ multiple consumers                                       │
│  [Swing Analyser]  [Recorder]  [UI Preview]  [Inference]         │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ DiagnosticsView — read-only per-source stats               │  │
│  │ SourceStalled events emitted into timeline                 │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 4. Core Data Structures

### 4.1 `FormatDescriptor`

```cpp
enum class DeviceKind {
    Camera_UVC,           // V4L2 / MediaFoundation / AVFoundation UVC webcams
    Camera_GenICam,       // USB3 Vision / GigE Vision (e.g. Chameleon3)
    Camera_AVFoundation,  // iOS/macOS built-in
    Camera_Camera2,       // Android
    IMU_WitMotion,
    IMU_Bosch,
    IMU_Custom,
};

enum class PixelFormat {
    Unknown,
    Mono8, Mono12, Mono12Packed, Mono16,
    BayerRG8, BayerRG12, BayerRG16,
    YUV422, YUYV, UYVY,
    NV12, YUV420P,
    BGR24, RGB24, BGRA32, RGBA32,
    MJPEG, H264_NAL,   // future / variable-size formats
};

struct CameraFormat {
    PixelFormat pixel_format;
    uint32_t width, height;
    uint32_t fps_numerator, fps_denominator;
    uint32_t max_payload_bytes;       // used to size ring slots
    uint32_t typical_payload_bytes;   // used for statistics / diagnostics
    std::array<uint32_t, 4> plane_strides;   // 0 if not planar
};

struct ImuFormat {
    DeviceKind device;
    uint32_t sample_rate_hz;
    uint32_t packet_bytes;        // fixed size for all IMU packets
    std::string packet_schema;    // parser identifier e.g. "wt9011_v2"
};

struct FormatDescriptor {
    DeviceKind device;
    std::string device_serial;
    std::variant<CameraFormat, ImuFormat> format;
};
```

### 4.2 `SyncSource` — Hardware Sync Placeholder

```cpp
enum class SyncSource {
    SoftwareTimestamp,   // v1 default — nowMicros() on receipt
    HardwarePts,         // device-embedded PTS (placeholder, not active in v1)
    HardwareTrigger,     // external sync signal (placeholder, not active in v1)
};
```

> **Note**: `HardwarePts` and `HardwareTrigger` are reserved for future use. In v1 all sources
> use `SoftwareTimestamp`. If a sync_group is configured, the merger records it but applies
> the standard 5ms reorder window.
>
> **Chameleon3 capability**: The Chameleon3 has opto-isolated GPIO supporting primary/secondary
> triggering. When hardware sync is activated in a future release, one camera drives a trigger
> pulse and the secondary camera uses it as its exposure trigger, yielding sub-microsecond
> inter-camera alignment.

### 4.3 `SourceDescriptor`

```cpp
struct SourceDescriptor {
    SourceId id;
    std::string name;                          // e.g. "left_camera", "club_imu"
    FormatDescriptor format;
    std::chrono::milliseconds window_duration{5000};

    // Sync hints (v1: only SoftwareTimestamp is active)
    SyncSource sync_source = SyncSource::SoftwareTimestamp;
    std::optional<std::string> sync_group;

    // Used by liveness watchdog
    std::chrono::microseconds expected_interarrival_us;

    size_t computeSlotBytes() const;    // derived from format.max_payload_bytes
    size_t computeSlotCount() const;    // ceil(rate * window), rounded to power of 2
};
```

### 4.4 `SourceStats` — Per-Source Observability

Placed on its own cache line to prevent false sharing with ring hot-path atomics.

```cpp
struct alignas(64) SourceStats {
    std::atomic<uint64_t> events_written{0};
    std::atomic<uint64_t> events_overwritten{0};       // written before merger drained
    std::atomic<uint64_t> bytes_written_total{0};
    std::atomic<int64_t>  last_write_timestamp_us{0};
    std::atomic<int64_t>  max_inter_arrival_us{0};
    std::atomic<uint64_t> bounds_violations{0};        // producer bytes_written > capacity
    std::atomic<uint64_t> monotonicity_violations{0};  // detected by merger
};
```

### 4.5 `SourceRing` — Byte-Oriented Seqlock SPSC Ring

```cpp
class SourceRing {
public:
    SourceRing(SourceId id, size_t slot_count, size_t slot_max_bytes);

    // --- Producer side (single capture thread) ---

    struct WriteSlot {
        std::byte* data;
        size_t     capacity;
        uint64_t   sequence;
        size_t*    bytes_written;   // producer writes actual size here before publish
        int64_t*   timestamp_us;    // producer writes timestamp here before publish
        bool       valid;           // false when buffer is paused — producer must not write
    };

    WriteSlot acquireWriteSlot() noexcept;               // SingleCopy / StreamingRead
    WriteSlot getSlotByIndex(size_t slot_idx) noexcept;  // DirectDma

    // Validates bounds, increments stats, advances generation counter.
    void publish(uint64_t sequence) noexcept;

    // --- Consumer side (zero-copy) ---

    struct ReadHandle {
        const std::byte* data;
        size_t           bytes;
        int64_t          timestamp_us;
        uint64_t         generation_snapshot;

        // MUST be called after consuming data.
        // Returns false if slot was overwritten during read — treat result as invalid.
        bool validate(const SourceRing& ring) const noexcept;
    };

    ReadHandle acquireReadHandle(uint64_t sequence) const noexcept;
    uint64_t   latestSequence() const noexcept;
    bool       peekTimestamp(uint64_t sequence, int64_t& out) const noexcept;

    // --- Observability ---
    const SourceStats& stats() const noexcept;
    SourceId           id()    const noexcept;

    // --- DMA registration ---
    std::vector<std::byte*> getSlotPointers()  const;
    size_t                  slotCapacity()      const noexcept;
    size_t                  slotCount()         const noexcept;

private:
    struct alignas(64) SlotHeader {
        std::atomic<uint64_t> generation{0};  // even = stable, odd = being written
        int64_t  timestamp_us{0};
        uint32_t bytes_written{0};
        // payload bytes follow immediately in storage
    };

    SourceId source_id_;
    size_t   slot_count_;      // power of 2
    size_t   slot_mask_;       // slot_count_ - 1
    size_t   slot_max_bytes_;
    size_t   slot_stride_;     // sizeof(SlotHeader) + slot_max_bytes_, cache-line aligned

    std::unique_ptr<std::byte[], AlignedDeleter> storage_;   // 64-byte aligned

    alignas(64) std::atomic<uint64_t> write_seq_{0};  // own cache line (false-sharing prevention)
    SourceStats stats_;                                // own cache line
};
```

**Seqlock correctness**:
- Writer: stores `generation` as odd before writing, increments to even (via `fetch_add`) after.
- Reader: snapshots `generation` before read, re-checks after. If either is odd or they differ, the slot was being written — caller retries or skips.
- Consumers read behind the write head by at least `REORDER_WINDOW`, so torn reads are rare in practice. `validate()` makes them detectable when they do occur.

### 4.6 `TimelineIndex`

```cpp
struct IndexEntry {
    int64_t  timestamp_us;
    SourceId source_id;
    uint64_t source_sequence;
    uint64_t global_sequence;
    uint32_t flags;           // IndexEntryFlags::SourceStalled, reserved bits
};
static_assert(sizeof(IndexEntry) <= 40);

class TimelineIndex {
public:
    explicit TimelineIndex(size_t capacity);   // default 8192 — see Section 18

    uint64_t append(const IndexEntry&) noexcept;  // merger thread only
    uint64_t latestSequence() const noexcept;
    bool     tryRead(uint64_t global_seq, IndexEntry& out) const noexcept;

    // Range query — allocates; intended for swing-analysis post-processing
    std::vector<IndexEntry> snapshot(int64_t t_start_us, int64_t t_end_us) const;
};
```

### 4.7 `WaitFlag` — Platform-Abstracted Wait-with-Timeout

```cpp
class WaitFlag {
public:
    // Block up to 'timeout' waiting for value to change from 'expected'.
    // Returns the new value (or 'expected' on timeout).
    uint64_t waitFor(uint64_t expected, std::chrono::microseconds timeout);
    void     store(uint64_t v) noexcept;
    uint64_t load()            const noexcept;
    void     notifyAll()       noexcept;

private:
    std::atomic<uint64_t> value_{0};

#if PINPOINT_PLATFORM_APPLE
    // Accepted degradation: condition_variable used for reliable timeout support.
    // std::atomic::wait on Apple uses polling-with-backoff (private API restriction).
    mutable std::mutex              mtx_;
    mutable std::condition_variable cv_;
#endif
    // Linux:   futex-backed atomic::wait + watchdog for timeout
    // Windows: WaitOnAddress + WakeByAddress
    // No additional members needed on those platforms.
};
```

### 4.8 `EventBuffer` — Central Façade

```cpp
enum class BufferState {
    Idle,        // constructed, not started; no merger thread
    Capturing,   // merger running, producers active, rings filling
    Paused,      // merger quiesced, rings frozen, safe for analysis
    Stopping,    // teardown in progress (transient)
};

class EventBuffer {
public:
    static constexpr size_t MAX_SOURCES = 16;

    explicit EventBuffer(EventBufferConfig cfg = {});
    ~EventBuffer();   // RAII: calls stop() if not Idle

    EventBuffer(const EventBuffer&)            = delete;
    EventBuffer& operator=(const EventBuffer&) = delete;

    // --- Registration ---
    // registerSource: callable while Idle or Paused. NOT callable while Capturing.
    // Allocates ring memory here. Reuses freed slots before extending hwm.
    // Returns kInvalidSourceId if state is wrong or MAX_SOURCES exhausted.
    SourceId registerSource(SourceDescriptor desc);

    // deregisterSource: Paused state ONLY. Asserts no SwingWindow is live.
    // Immediately frees all ring memory. SourceId becomes invalid thereafter.
    void deregisterSource(SourceId id);

    // Count of currently live (non-deregistered) sources.
    size_t activeSourceCount() const noexcept;

    // --- Producer API ---
    // acquireWriteSlot returns valid=false when state != Capturing
    // or when id has been deregistered.
    SourceRing::WriteSlot acquireWriteSlot(SourceId id) noexcept;
    SourceRing::WriteSlot getSlotByIndex(SourceId id, size_t slot_idx) noexcept;
    void                  publish(SourceId id, uint64_t sequence) noexcept;

    // DMA registration helpers (called by CaptureSource::start())
    std::vector<std::byte*> getSlotPointers(SourceId id)  const;
    size_t                  getSlotCapacity(SourceId id)  const;
    size_t                  getSlotCount(SourceId id)     const;

    // --- Consumer API ---
    class Subscription {
    public:
        bool     waitNext(IndexEntry& out, std::chrono::microseconds timeout);
        bool     tryNext(IndexEntry& out);
        void     resetToLatest();
        uint64_t overrunsSinceLastRead() const noexcept;
    };
    Subscription subscribe();

    SourceRing::ReadHandle  acquireReadHandle(SourceId id, uint64_t source_seq) const noexcept;
    const FormatDescriptor& formatOf(SourceId id) const;
    std::vector<IndexEntry> snapshot(int64_t t_start_us, int64_t t_end_us);

    // --- Lifecycle ---
    // See Section 19 for full state machine and transition rules.
    // start() is valid with zero registered sources.
    void        start();    // Idle → Capturing; launches merger thread
    void        pause();    // Capturing → Paused; quiesces producers and merger
    void        resume();   // Paused → Capturing; clears rings, restores capture
    void        stop();     // Capturing|Paused → Idle; joins merger thread
    BufferState state()       const noexcept;
    bool        isCapturing() const noexcept;

    // SwingWindow — only callable in Paused state.
    // Sets swing_window_live_ flag; deregisterSource() asserts this is false.
    SwingWindow captureSwingWindow(int64_t t_start_us, int64_t t_end_us);
    SwingWindow captureSwingWindow(std::chrono::milliseconds trailing_duration);

    // --- Observability ---
    const SourceStats&     statsFor(SourceId id) const;
    std::vector<SourceId>  stalledSources() const;
    DiagnosticsSnapshot    diagnostics() const;

    // --- Clock ---
    static int64_t nowMicros() noexcept;

private:
    friend class SwingWindow;

    struct SourceSlot {
        SourceDescriptor            desc;
        std::unique_ptr<SourceRing> ring;
        uint64_t                    next_seq = 0;
        std::atomic<bool>           stalled{false};
    };

    EventBufferConfig config_;

    // Sparse slot array — null entries are deregistered sources.
    std::array<std::unique_ptr<SourceSlot>, MAX_SOURCES> sources_;
    size_t slot_hwm_       = 0;   // highest index ever assigned + 1
    size_t active_sources_ = 0;   // count of non-null slots

    std::atomic<bool> swing_window_live_{false};  // guards deregisterSource()

    TimelineIndex index_;
    WaitFlag      index_wait_;
    alignas(64) WaitFlag source_published_;

    alignas(64) std::atomic<bool>     capturing_{false};
    std::atomic<BufferState>           state_{BufferState::Idle};
    std::atomic<bool>                  running_{false};
    std::atomic<bool>                  draining_{false};
    std::atomic<bool>                  drained_{false};
    std::atomic<uint64_t>              sub_gen_{0};

    std::thread merger_thread_;
    std::chrono::steady_clock::time_point last_watchdog_tick_;

    int  findSlotIndex(SourceId id) const noexcept;
    void mergerLoop();
    void maybeRunWatchdog();
};
```

---

## 5. Producer Protocol — Zero-Copy Contract

Every capture thread follows this exact protocol regardless of transport.

### 5.1 Four-Step Pattern

```cpp
void onDeviceDataReady(/* OS callback args */) {
    // Step 1: Timestamp FIRST — before any other work
    int64_t ts = EventBuffer::nowMicros();

    // Step 2: Acquire slot — never blocks, never fails
    auto slot = event_buffer_.acquireWriteSlot(source_id_);
    // (DirectDma: use getSlotByIndex instead)

    // Step 3: Check valid — returns false when buffer is paused
    if (!slot.valid) return;   // discard callback data cleanly, no write

    // Step 4: Write payload directly into slot.data
    size_t n = writePayloadIntoSlot(slot.data, slot.capacity /* ... */);
    *slot.bytes_written = n;
    *slot.timestamp_us  = ts;

    // Step 5: Publish — validates bounds, updates stats, releases generation
    event_buffer_.publish(source_id_, slot.sequence);
}
```

### 5.2 Hard Rules

**MUST**:
- Timestamp first — `EventBuffer::nowMicros()` is the first executable line
- Check `slot.valid` immediately after `acquireWriteSlot()` — return early if false
- Use `EventBuffer::nowMicros()` exclusively — no other clock source
- Fill both `bytes_written` and `timestamp_us` before calling `publish()`
- Publish even on short reads — record actual `bytes_written`

**MUST NOT**:
- Write to `slot.data` when `slot.valid == false`
- Call `publish()` when `slot.valid == false`
- Allocate between acquire and publish (`new`, `std::vector`, `std::string`, etc.)
- Copy data through any intermediate buffer before the slot
- Retain any pointer to slot memory after calling `publish()` — the slot may be overwritten
- Call any blocking operation other than the device read itself
- Log synchronously — use a lock-free logger or skip

### 5.3 Bounds Enforcement in `publish()` (Release Build)

```cpp
void SourceRing::publish(uint64_t sequence) noexcept {
    SlotHeader& hdr = slotHeaderAt(sequence & slot_mask_);

    // Hard clamp — defence in depth, protects consumer OOB reads
    if (hdr.bytes_written > slot_max_bytes_) {
        hdr.bytes_written = static_cast<uint32_t>(slot_max_bytes_);
        stats_.bounds_violations.fetch_add(1, std::memory_order_relaxed);
    }

    stats_.events_written.fetch_add(1, std::memory_order_relaxed);
    stats_.bytes_written_total.fetch_add(hdr.bytes_written, std::memory_order_relaxed);
    updateInterArrival(hdr.timestamp_us);

    // Release: makes all prior writes visible to consumers checking generation
    hdr.generation.fetch_add(1, std::memory_order_release);
}
```

---

## 6. Capture Transport Abstraction

### 6.1 Transport Classes

```cpp
enum class CaptureTransport {
    DirectDma,       // driver/kernel writes via DMA into ring slot memory
    SingleCopy,      // OS callback delivers buffer → memcpy into slot (one copy)
    StreamingRead,   // read(fd, slot.data, slot.capacity) — zero copy from syscall
};
```

### 6.2 Per-Platform Mapping

| Platform | Camera API | Transport | Notes |
|---|---|---|---|
| Linux | V4L2 / UVC | `DirectDma` | `V4L2_MEMORY_USERPTR` — ring slot pointers registered with driver |
| Linux | Spinnaker / Aravis (Chameleon3) | `DirectDma` | GenICam buffer pool — ring slots registered as pool |
| Windows | MediaFoundation / UVC | `SingleCopy` | `IMFSourceReader` async callback → memcpy |
| Windows | Spinnaker (Chameleon3) | `DirectDma` | Same buffer-pool mechanism as Linux |
| macOS / iOS | AVFoundation | `SingleCopy` | `CMSampleBuffer` → `CVPixelBufferGetBaseAddress` + memcpy |
| Android | Camera2 | `SingleCopy` | `ImageReader` callback → memcpy |
| All | Serial / BLE IMU | `StreamingRead` | `read(fd, slot.data, capacity)` |

### 6.3 `CaptureSource` Interface

```cpp
class CaptureSource {
public:
    virtual ~CaptureSource() = default;
    virtual CaptureTransport     transport()  const noexcept = 0;
    virtual const FormatDescriptor& format()  const noexcept = 0;

    // RAII contract: start() registers with EventBuffer.
    // stop() MUST perform DMA teardown before returning (see 6.4).
    // Destructor must call stop() if started.
    virtual void start(EventBuffer& buffer, SourceId id) = 0;
    virtual void stop() = 0;
};
```

### 6.4 V4L2 DirectDma Registration Sequence

```
1. EventBuffer::registerSource() allocates ring with N slots of slot_max_bytes.

2. CaptureSource::start():
   a. Open V4L2 device, set format matching registered FormatDescriptor.
   b. slot_ptrs = buffer.getSlotPointers(source_id_)
   c. VIDIOC_REQBUFS with V4L2_MEMORY_USERPTR, count = N
   d. For each slot_ptr: VIDIOC_QBUF with userptr = slot_ptr, length = slot_max_bytes
   e. VIDIOC_STREAMON

3. Capture thread loop:
   a. VIDIOC_DQBUF — blocks until driver has DMA'd a frame into one of our slots
   b. ts = EventBuffer::nowMicros()  (or buf.timestamp converted to monotonic)
   c. slot = buffer.getSlotByIndex(source_id_, buf.index)
   d. *slot.bytes_written = buf.bytesused
      *slot.timestamp_us  = ts
   e. buffer.publish(source_id_, slot.sequence)
   f. VIDIOC_QBUF — re-queues the slot for next DMA write

4. CaptureSource::stop():   (RAII — MUST happen before ring memory freed)
   a. VIDIOC_STREAMOFF
   b. VIDIOC_REQBUFS with count = 0
   c. close(fd)
```

The `EventBuffer` destructor must not free ring memory until all `CaptureSource` instances have called `stop()`. `EventBuffer::stop()` asserts no sources are still active before proceeding.

### 6.5 SingleCopy Pattern — Windows MediaFoundation Example

```cpp
void MediaFoundationSource::OnReadSample(IMFSample* sample) {
    int64_t ts = EventBuffer::nowMicros();   // first line

    auto slot = event_buffer_->acquireWriteSlot(source_id_);
    if (!slot.valid) return;   // buffer paused — discard frame cleanly

    IMFMediaBuffer* buf;
    sample->ConvertToContiguousBuffer(&buf);
    BYTE* src; DWORD len;
    buf->Lock(&src, nullptr, &len);

    std::memcpy(slot.data, src, std::min<size_t>(len, slot.capacity));
    *slot.bytes_written = static_cast<size_t>(len);
    *slot.timestamp_us  = ts;
    event_buffer_->publish(source_id_, slot.sequence);

    buf->Unlock();
    buf->Release();
}
```

The single `memcpy` at 1080p Mono8 (~2MB) runs in ~200µs at typical memory bandwidth. This is the floor for SingleCopy transports and is acceptable.

---

## 7. Merger Loop

### 7.1 Fixed-Size Merge Structure (No Allocation in Hot Path)

```cpp
struct PendingEvent {
    SourceId source_id;
    uint64_t source_seq;
    int64_t  timestamp_us;
    bool     valid = false;
};

class MergerState {
    std::array<PendingEvent, EventBuffer::MAX_SOURCES> heads_;
    std::array<int64_t, EventBuffer::MAX_SOURCES>      last_emitted_ts_{};

public:
    // Linear scan is faster than heap ops for N ≤ 16 (no allocation, cache-hot)
    int  findMinValidIndex() const noexcept;
    void setHead(int idx, PendingEvent ev) noexcept;
    void clearHead(int idx) noexcept;

    // Monotonicity enforcement (Rec-8)
    // Clamps ev.timestamp_us and increments stats if non-monotonic
    void enforceMonotonicity(int src_idx, PendingEvent& ev, SourceStats& stats) noexcept;
};
```

### 7.2 Hybrid Wait Strategy (Latency-Correct)

```cpp
void EventBuffer::mergerLoop() {
#if PINPOINT_PLATFORM_WINDOWS
    timeBeginPeriod(1);   // 1ms timer resolution; timeEndPeriod in RAII guard
#endif

    MergerState state;
    constexpr int SPIN_ITERATIONS = 64;   // ~10µs of spinning before sleeping

    while (running_.load(std::memory_order_acquire)) {

        // 1. Poll each source ring for new events since last drain
        bool any_progress = drainSources(state);

        // 2. Emit all events with timestamp ≤ safe_emit_until into TimelineIndex
        int64_t safe_emit_until = computeSafeEmitTimestamp(state);
        emitReadyEvents(state, safe_emit_until);

        // 3. Watchdog — runs at most once per 10ms (see Section 8)
        maybeRunWatchdog();

        // 4. Wait strategy
        if (any_progress) {
            // Hot path: spin briefly to catch burst events without sleeping
            for (int i = 0; i < SPIN_ITERATIONS; ++i) cpu_pause();
        } else {
            // Cold path: block efficiently until a source posts a new event
            auto timeout = std::min(
                std::chrono::microseconds(500),      // upper bound keeps watchdog ticking
                timeUntilNextExpectedEvent(state)
            );
            index_wait_.waitFor(last_observed_, timeout);
        }
    }

#if PINPOINT_PLATFORM_WINDOWS
    timeEndPeriod(1);
#endif
}
```

> **Why `timeBeginPeriod(1)` on Windows**: The default Windows timer resolution is 15.6ms.
> Without raising it, `sleep_for(200µs)` actually sleeps ~15ms, making the 5ms reorder
> window meaningless. `timeBeginPeriod(1)` gives 1ms granularity, enough for our purposes,
> without requiring admin rights. The call is process-wide and reversed on stop().

### 7.3 `safe_emit_until` Computation

```
safe_emit_until = min(latest_observed_ts across all sources) - REORDER_WINDOW_US
REORDER_WINDOW_US = 5000  (5ms default, configurable)
```

If `sync_group` is set on a group of sources (future hardware sync), a tighter window may be applied within that group. v1 ignores `sync_group` in this computation.

> **Latency Contribution of Cross-Source Ordering**
>
> The reorder window combined with multi-source operation creates an unavoidable
> latency floor. An event from a fast source (e.g. 200Hz IMU) cannot be emitted
> until the slowest source has produced an event with a later timestamp, plus
> the reorder window margin. Mathematically:
>
> ```
> visible_latency_floor ≈ reorder_window_us + 0.5 × slowest_source_interval_us
> ```
>
> For 5ms reorder window + 60Hz camera (16.7ms interval): IMU events are visible
> ~13ms after publish on average. This is correct by design — the reorder window
> exists to absorb cross-source clock jitter, and is the price of in-order delivery.
>
> Two distinct latency metrics matter and must not be conflated:
>
> 1. **Merger response latency** — producer publish → event observable in source
>    ring via direct read. Bounded only by merger wake speed and atomic visibility.
>    Sub-100µs achievable. Use a single-source benchmark to measure.
>
> 2. **End-to-end visible latency** — producer publish → event readable via
>    `Subscription`. Bounded by reorder window + cross-source ordering delay.
>    Multi-second-window analysis via `SwingWindow` has zero observable latency
>    since the buffer is paused.
>
> For Pinpoint's primary use case (analyse after pause), end-to-end latency is
> irrelevant — `SwingWindow` provides immediate access to the entire frozen
> buffer. Live preview latency of ~15ms is well below human perception threshold.

### 7.4 Monotonicity Enforcement

```cpp
void MergerState::enforceMonotonicity(int idx, PendingEvent& ev,
                                      SourceStats& stats) noexcept {
    if (ev.timestamp_us <= last_emitted_ts_[idx]) {
        stats.monotonicity_violations.fetch_add(1, std::memory_order_relaxed);
        ev.timestamp_us = last_emitted_ts_[idx] + 1;
    }
    last_emitted_ts_[idx] = ev.timestamp_us;
}
```

Silently clamps and increments the violation counter. The diagnostic API exposes this to callers.

---

## 8. Liveness Watchdog

### 8.1 Mechanism

Runs at most once per 10ms, piggybacking on the merger thread:

```cpp
void EventBuffer::maybeRunWatchdog() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_watchdog_tick_ < std::chrono::milliseconds(10)) return;
    last_watchdog_tick_ = now;

    int64_t now_us = EventBuffer::nowMicros();
    for (size_t i = 0; i < slot_hwm_; ++i) {
        if (!sources_[i]) continue;           // deregistered slot — skip
        auto& src       = *sources_[i];
        int64_t last_ts = src.ring->stats().last_write_timestamp_us
                                           .load(std::memory_order_relaxed);
        int64_t threshold_us = src.desc.expected_interarrival_us.count() * 5;

        bool silent = (last_ts > 0) && ((now_us - last_ts) > threshold_us);

        if (silent && !src.stalled.exchange(true)) {
            // Newly stalled — emit a marker into the timeline so consumers see it
            IndexEntry marker{};
            marker.timestamp_us  = now_us;
            marker.source_id     = src.desc.id;
            marker.flags         = IndexEntryFlags::SourceStalled;
            uint64_t seq = index_.append(marker);
            index_wait_.store(seq);
            index_wait_.notifyAll();
        } else if (!silent) {
            src.stalled.store(false);
        }
    }
}
```

### 8.2 Consumer Handling

```cpp
// Consumers check flags on every IndexEntry
while (sub.waitNext(entry, 100ms)) {
    if (entry.flags & IndexEntryFlags::SourceStalled) {
        handleSourceStalled(entry.source_id);   // log, alert UI, abort recording
        continue;
    }
    // normal processing...
}
```

Gaps in the timeline are explicit, never silent. Consumers can distinguish "no events yet" from "source died".

---

## 9. Real-Time Timestamping

### 9.1 Single Shared Monotonic Clock

```cpp
int64_t EventBuffer::nowMicros() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
```

| Platform | Underlying call | Resolution |
|---|---|---|
| Linux / Android | `clock_gettime(CLOCK_MONOTONIC)` | ~20ns |
| Windows | `QueryPerformanceCounter` | ~100ns |
| macOS / iOS | `mach_absolute_time` | ~40ns |

All are monotonic (never go backwards) and unaffected by NTP or system time changes. **Every timestamp in Pinpoint uses `EventBuffer::nowMicros()`.** Mixing clock sources is forbidden.

### 9.2 Timestamp at Earliest Observation

The golden rule: `nowMicros()` is the **first executable statement** in every device callback.

| Source type | Preferred timestamp | Fallback |
|---|---|---|
| UVC camera (V4L2) | `v4l2_buffer::timestamp` (kernel monotonic) | `nowMicros()` on callback entry |
| UVC camera (Windows MF) | `IMFSample::GetSampleTime` converted to monotonic | `nowMicros()` on callback entry |
| Chameleon3 / GenICam | Frame chunk timestamp converted to monotonic | `nowMicros()` on callback entry |
| IMU serial / BLE | `nowMicros()` immediately after `read()` returns | — |
| AVFoundation | `CMSampleBufferGetPresentationTimeStamp` + anchor offset | `nowMicros()` |

Device PTS → monotonic conversion: anchor first-packet `device_pts_0` and `monotonic_t_0 = nowMicros()`. Subsequent: `ts = monotonic_t_0 + (device_pts - device_pts_0) * scale`. Re-anchor every few seconds with a low-pass filter to prevent drift.

### 9.3 Thread Priority (Reduces Jitter)

See Section 11. Elevated capture thread priority reduces callback scheduling jitter. On Windows and macOS this is free (unprivileged). On Linux it requires `CAP_SYS_NICE`.

---

## 10. Memory Sizing — Desktop-First Defaults

### 10.1 Default Profile (2 Cameras + 3 IMUs, 5s Window)

| Source | Format | Slot bytes | Slot count | Total |
|---|---|---|---|---|
| Cam0 1080p60 Mono8 | 2.07 MB | 2.07 MB | 512 | 1.06 GB |
| Cam1 1080p60 Mono8 | 2.07 MB | 2.07 MB | 512 | 1.06 GB |
| IMU × 3 (200Hz, 32B aligned) | 32 B | 32 B | 1024 | 96 KB |
| TimelineIndex | 40 B | 40 B | 8192 | 320 KB |
| **Total** | | | | **~2.13 GB** |

### 10.2 Format Variants

| Format | Multiplier vs Mono8 | Notes |
|---|---|---|
| Mono8 | 1.0× | Recommended — sufficient for club/body tracking |
| Mono12 packed | 1.5× | Higher dynamic range |
| BayerRG8 | 1.0× | Same size as Mono8; decoded by consumer |
| YUV422 | 2.0× | Avoid unless colour required |
| MJPEG / H264 | ~0.1–0.3× | Variable size — slot sized to worst-case; future |

### 10.3 Slot Count Rule

Slot count must be a **power of 2**. Formula: `next_power_of_2(ceil(rate × window_seconds))`.
Example: 60fps × 5s = 300 → 512.

### 10.4 Mobile Profile (Deferred)

Not a v1 concern. When added: 720p30 × 3s → ~250 MB total. Implemented as a named `BufferProfile` passed to `EventBuffer` constructor. No code-path changes — only sizing.

---

## 11. Thread Priority Strategy (Unprivileged)

### 11.1 `ThreadPolicy` Helper

```cpp
enum class ThreadRole { Capture, Merger, Consumer };

class ThreadPolicy {
public:
    // Best-effort elevation. Never throws. Returns true if elevated.
    static bool apply(ThreadRole role) noexcept;

    // Pin to a CPU core. No-op on mobile platforms.
    static bool pinToCore(int core_id) noexcept;
};
```

### 11.2 Per-Platform Targets

| Platform | Capture | Merger | Core pin |
|---|---|---|---|
| Windows | `THREAD_PRIORITY_TIME_CRITICAL` | `THREAD_PRIORITY_ABOVE_NORMAL` | `SetThreadAffinityMask` |
| Linux | `SCHED_FIFO` p=50 → `nice -10` → nice 0 | `nice -5` → nice 0 | `pthread_setaffinity_np` |
| macOS / iOS | `QOS_CLASS_USER_INTERACTIVE` | `QOS_CLASS_USER_INITIATED` | not available |
| Android | `setpriority(-19)` | `setpriority(-8)` | not available |

**Linux note**: `SCHED_FIFO` requires `CAP_SYS_NICE`. Graceful fallback is automatic.
Deployment guide should offer: `sudo setcap cap_sys_nice+ep pinpoint` for users who want
RT priority without running as root.

### 11.3 CPU Affinity Policy

Optional; disabled by default. When enabled, capture threads are pinned starting from core 1
(core 0 carries OS interrupt and scheduler work on most Linux configurations). Recommended for
desktop deployments where CPU count ≥ 4.

---

## 12. C++20 Toolchain Requirements

### 12.1 Compiler Support Matrix

| Toolchain | Minimum Version | Notes |
|---|---|---|
| GCC | 11+ | Full C++20; atomic wait via futex |
| Clang | 14+ | libc++ 14+ for atomic wait |
| MSVC | VS 2022 17.0+ | Atomic wait via `WakeByAddressSingle/All` |
| Apple Clang | Xcode 14+ | Atomic wait present; uses polling (mitigated by `WaitFlag` using `condition_variable`) |
| Android NDK | r25+ | libc++ atomic wait via futex |

### 12.2 Minimum OS / SDK Versions

| Platform | Minimum |
|---|---|
| Windows | 10 1809 (`WakeByAddress` stable) |
| macOS | 11 Big Sur |
| iOS | 14 |
| Ubuntu | 20.04 / glibc 2.31 |
| Android | API 24 |

### 12.3 macOS / Apple Clang Notes

`std::atomic::wait` is available from macOS 11 / Xcode 14 but Apple's libc++ implementation
historically uses polling-with-exponential-backoff (not a native futex), because `__ulock_wait`
is a private API blocked by App Store review. This affects consumer-side `Subscription::waitNext`
latency only — the hot path (producer writes, merger emits) is unaffected.

Mitigation: `WaitFlag` uses `std::condition_variable` on Apple platforms, which maps to Grand
Central Dispatch primitives and has reliable, low-latency wakeup behaviour.

`std::atomic::wait` also has no timeout in C++20 (timed waits are proposed for C++26 in P2643).
The `WaitFlag` abstraction solves this for all platforms without exposing private APIs.

---

## 13. Camera Platform Notes — Teledyne FLIR Chameleon3

The Chameleon3 is a **USB3 Vision / GenICam** machine-vision camera. Key characteristics:

- **No MJPEG output.** Output formats: Mono8, Mono12, Mono12Packed, Mono16, BayerRG8/12/16, YUV422, RGB8.
- Complies with USB3 Vision v1.0 and uses GenICam for all feature control via an on-board XML descriptor.
- 16 MB on-camera frame buffer (for retransmission — not used by our buffer design).
- Opto-isolated GPIO with locking USB 3.0 connector — future hardware sync target.
- Sensor resolutions: 1.3 MP to 5.0 MP depending on model.
- Global shutter on all models (important for motion capture — no rolling shutter artefacts).
- Accessed via Teledyne Spinnaker SDK or open-source Aravis library (Linux).

On Windows and Linux, the Spinnaker SDK and Aravis both support buffer-pool registration
compatible with the `DirectDma` transport pattern described in Section 6.4.

The `MJPEG` and `H264_NAL` entries in `PixelFormat` are **reserved for future cameras** that
do support compressed output. The `FormatDescriptor` and slot sizing already accommodate
variable-length payloads (`max_payload_bytes` ≥ `typical_payload_bytes`) — adding such a
camera requires only a new `CaptureSource` implementation, no buffer API changes.

---

## 14. Security Posture

### 14.1 Trust Boundaries

| Surface | Trust Level | Mitigation |
|---|---|---|
| USB camera DMA writes | Untrusted (BadUSB possible) | Slots registered at exact `slot_max_bytes`; driver enforces upper bound |
| IMU raw packet bytes | Untrusted | Buffer stores verbatim; parser robustness is the consumer's responsibility |
| Device timestamps | Semi-trusted | Per-source monotonicity enforcement in merger |
| Producer `bytes_written` | Producer-controlled | Hard clamp in `publish()` in all build configurations |
| Consumer `ReadHandle::bytes` | Validated by seqlock | Consumer must call `validate()` after use |

### 14.2 Explicit Non-Goals

The buffer does not:
- Parse pixel data or IMU packets (raw storage by design)
- Validate timestamp magnitude (only monotonicity)
- Defend against malicious capture source code in-process (our own code, out of scope)

Parser robustness and payload validation belong in the consumer/analysis layer.

---

## 15. Testing Strategy

Lock-free code is uniquely difficult to validate. This test plan is **non-negotiable** — the
event buffer is the foundation of every downstream component.

### 15.1 Required Test Categories

| # | Test | Pass Criteria |
|---|---|---|
| 1 | SPSC stress (all transport types) | 10M+ events, no torn reads, correct ordering, TSAN clean |
| 2 | Overrun correctness | Slow consumer: all overruns detected via `validate()`; no crashes; stats accurate |
| 3 | Monotonicity enforcement | Injected non-monotonic timestamps → clamped; violation counters increment |
| 4 | Liveness watchdog | Stalled source → `SourceStalled` marker emitted within `5 × expected_interarrival` |
| 5 | TSAN with suppressions | Seqlock races suppressed by name; all other races clean |
| 6 | ASAN + UBSAN | Zero errors across all test categories |
| 7 | 24h soak | Zero correctness errors; zero RSS growth; monotonic overrun-counter only increase |
| 8 | Adversarial timing fuzzer | Consumer reading slot N-1 while producer writes slot N; no corruption |
| 9a | Merger latency benchmark (single-source) | p50 < 100µs (IMU), < 200µs (video); p99 < 500µs (IMU), < 1ms (video). Measures merger thread responsiveness with no cross-source ordering delay. |
| 9b | End-to-end latency benchmark (multi-source) | p50 ≤ reorder_window + 0.5 × slowest_source_interval. For 5ms reorder + 60Hz camera: p50 < 15ms (IMU), < 10ms (video). p99 < 2 × p50. Measures realistic latency including reorder window. |
| 10 | Memory bounds | Ring sizing matches formula; no overallocation; no heap growth after start() |
| 11 | Shutdown cleanliness | DMA teardown order correct; ASAN clean on stop(); V4L2 STREAMOFF before free |
| 12 | Cross-platform CI | Full suite on Linux/Windows/macOS on every commit |
| 13 | State machine transitions | All valid transitions succeed; all invalid transitions assert in debug |
| 14 | Pause / resume correctness | Pause stops all writes; resume restores them; no data written during pause appears in timeline |
| 15 | SwingWindow frozen access | Payloads readable via `SwingWindow` remain valid for its lifetime; no corruption from concurrent resume |
| 16 | Multi-swing cycle | 100 start/pause/resume/stop cycles with real data; ring clears correctly; no RSS growth; stats reset per cycle |

### 15.2 TSAN Seqlock Suppressions

The deliberate data race in the seqlock (reading payload bytes between two generation checks)
is benign but TSAN will flag it. Suppress **narrowly** by function name in `tsan_suppressions.txt`:

```
race:pinpoint::SourceRing::acquireReadHandle
race:pinpoint::SourceRing::SlotHeader::bytes_written
```

Do not use broad suppressions. All other races must be clean.

### 15.3 Additional Verification (Recommended)

- Construct litmus tests for seqlock memory ordering; run under CDSChecker or Relacy
- Run benchmarks on actual target hardware before declaring v1 done — synthetic results miss
  real cache topology effects
- On Linux, run with `SCHED_FIFO` capture thread and a CPU-stressed background to verify
  worst-case jitter is within spec

---

## 16. File Layout

```
src/Buffer/
├── types.h                    // SourceId, DeviceKind, PixelFormat, IndexEntryFlags
├── format_descriptor.h        // FormatDescriptor, CameraFormat, ImuFormat
├── source_descriptor.h        // SourceDescriptor, SyncSource
├── source_stats.h             // SourceStats (cache-line aligned)
├── source_ring.h              // SourceRing (seqlock SPSC, byte-oriented)
├── timeline_index.h           // TimelineIndex (SPMC seqlock)
├── wait_flag.h                // WaitFlag (platform-dispatched)
├── thread_policy.h            // ThreadPolicy helper
├── event_buffer.h             // EventBuffer, Subscription, SwingWindow
├── capture_source.h           // CaptureSource abstract base
├── source_ring.cpp
├── timeline_index.cpp
├── event_buffer.cpp           // merger loop, watchdog, lifecycle, registration
├── swing_window.cpp
├── wait_flag_linux.cpp        // futex-direct atomic::wait + watchdog
├── wait_flag_windows.cpp      // WaitOnAddress / WakeByAddress
├── wait_flag_apple.cpp        // condition_variable path
├── wait_flag_posix.cpp        // fallback condition_variable for other POSIX
├── thread_policy_linux.cpp
├── thread_policy_windows.cpp
├── thread_policy_apple.cpp
├── thread_policy_android.cpp
├── capture/                   // separate CMake library target: pinpoint_capture
│   ├── v4l2_capture_source.cpp               // Linux UVC — DirectDma
│   ├── spinnaker_capture_source.cpp          // Chameleon3 — DirectDma
│   ├── mediafoundation_capture_source.cpp    // Windows UVC — SingleCopy
│   ├── avfoundation_capture_source.mm        // macOS/iOS — SingleCopy
│   ├── camera2_capture_source.cpp            // Android — SingleCopy
│   └── serial_imu_capture_source.cpp         // All platforms — StreamingRead
├── CMakeLists.txt
└── tests/
    ├── CMakeLists.txt
    ├── tsan_suppressions.txt
    ├── source_ring_test.cpp
    ├── timeline_index_test.cpp
    ├── wait_flag_test.cpp
    ├── event_buffer_test.cpp
    ├── swing_window_test.cpp
    ├── adversarial_fuzz_test.cpp
    ├── soak_test.cpp              // 24h run, RSS monitoring
    ├── latency_benchmark.cpp
    └── build/                     // CMake build tree — not committed to VCS
```

**Include convention**: consumers use `#include "source_ring.h"` (or with a path prefix
matching however the parent CMakeLists.txt exposes `src/Buffer` as an include directory).
No subdirectory nesting in the include path.

**CMake targets produced:**

| Target | Sources | Links to |
|---|---|---|
| `pinpoint_buffer` | `src/Buffer/*.cpp` | (nothing external) |
| `pinpoint_capture` | `src/Buffer/capture/*.cpp` | `pinpoint_buffer` |
| `pinpoint_buffer_tests` | `src/Buffer/tests/*.cpp` | `pinpoint_buffer`, GTest |

---

## 17. Implementation Phasing

Each phase is independently testable and reviewable. Do not begin a phase until the previous
phase passes all its required tests.

### Phase 1 — Core Ring Primitive
- `types.h`, `format_descriptor.h`, `source_descriptor.h`
- `SourceStats`, `SourceRing` (seqlock SPSC with bounds enforcement)
- **Gate**: SPSC stress, overrun, ASAN/UBSAN/TSAN clean, adversarial fuzzer

### Phase 2 — Timeline and Merger
- `TimelineIndex`, `WaitFlag` (all platforms)
- `EventBuffer` skeleton: merger loop with monotonicity enforcement and hybrid wait
- **Gate**: End-to-end timestamp ordering, reorder window behaviour, 24h soak stub

### Phase 3 — Observability
- Liveness watchdog and `SourceStalled` markers
- `ThreadPolicy` helper (all platforms)
- Full diagnostic API surface
- **Gate**: Watchdog behaviour tests, priority elevation verified on each platform

### Phase 4 — Capture Transports
- `CaptureSource` interface
- StreamingRead (serial IMU) — simplest, implement first
- SingleCopy implementations (one per OS)
- DirectDma (Linux V4L2 first, then Spinnaker)
- **Gate**: Integration tests against real hardware; DMA teardown ASAN clean

### Phase 5 — Hardening
- 24h soak on each target platform with real cameras and IMUs
- Latency benchmarks on real hardware
- Cross-platform CI passing
- Deployment guide: capability documentation, Linux `setcap` instructions, mobile notes

---

## 18. Open Configuration Decisions

These defaults are baked into the initial implementation. Override via `EventBufferConfig`
struct passed to the `EventBuffer` constructor.

| Parameter | Proposed Default | Rationale |
|---|---|---|
| `reorder_window_us` | 5000 (5ms) | Covers typical cross-source USB scheduling jitter |
| `watchdog_interval_ms` | 10 | Low overhead; responsive to source failures |
| `stall_threshold_multiplier` | 5× | `5 × expected_interarrival` before marking stalled |
| `MAX_SOURCES` | 16 | Compile-time; covers 2 cameras + up to 14 IMUs |
| `timeline_index_capacity` | 8192 | ~10s × 500 events/s aggregate; power of 2 |
| `merger_spin_iterations` | 64 | ~10µs of spinning before sleeping; tune on real HW |
| `merger_cold_timeout_us` | 500 | Upper bound on cold-path wait; keeps watchdog ticking |
| `cpu_affinity_enabled` | false | Opt-in; when enabled, capture threads start at core 1 |

| `reorder_window_us` | 5000 (5ms) | Covers typical cross-source USB scheduling jitter |
| `watchdog_interval_ms` | 10 | Low overhead; responsive to source failures |
| `stall_threshold_multiplier` | 5× | `5 × expected_interarrival` before marking stalled |
| `MAX_SOURCES` | 16 | Compile-time; covers 2 cameras + up to 14 IMUs |
| `timeline_index_capacity` | 8192 | ~10s × 500 events/s aggregate; power of 2 |
| `merger_spin_iterations` | 64 | ~10µs of spinning before sleeping; tune on real HW |
| `merger_cold_timeout_us` | 500 | Upper bound on cold-path wait; keeps watchdog ticking |
| `cpu_affinity_enabled` | false | Opt-in; when enabled, capture threads start at core 1 |
| `pause_drain_timeout_ms` | 20 | Max time to wait for merger reorder heap to drain on pause |
| `resume_clear_rings` | true | Whether resume() resets ring write heads to avoid stale pre-pause data appearing in new swing |

---

## 19. Application Lifecycle

### 19.1 State Machine

The `EventBuffer` has four externally visible states. Invalid transitions assert in debug
builds and are no-ops in release.

```
  ┌─────────────────────────────────────────────────────────┐
  │                                                         │
  │  [constructed]                                          │
  │       │                                                 │
  │    start()                                              │
  │       │                                                 │
  │       ▼                                                 │
  │   CAPTURING ◄──────── resume() ────────┐                │
  │       │                                │                │
  │    pause()                             │                │
  │       │                                │                │
  │       ▼                                │                │
  │    PAUSED ──── captureSwingWindow() ───┘                │
  │       │         (analysis happens here)                 │
  │    stop()  ◄─── also callable from CAPTURING            │
  │       │                                                 │
  │       ▼                                                 │
  │     IDLE                                                │
  │       │                                                 │
  │    start() (next swing session)                         │
  │       │                                                 │
  │       ▼                                                 │
  │   CAPTURING                                             │
  │                                                         │
  └─────────────────────────────────────────────────────────┘
```

### 19.2 State Transition Semantics

**`start()` — Idle → Capturing**

1. Asserts state is `Idle`.
2. Sets `capturing_.store(true, memory_order_release)`.
3. Sets `state_` to `Capturing`.
4. Launches merger thread (if not already running — merger thread persists across pause/resume).
5. CaptureSource threads begin delivering callbacks; `acquireWriteSlot()` returns `valid = true`.

**`pause()` — Capturing → Paused**

1. Asserts state is `Capturing`.
2. Sets `capturing_.store(false, memory_order_release)` — capture threads see this on next
   callback and return early from `acquireWriteSlot()`.
3. Waits for in-flight `publish()` calls to complete — spin on a per-source in-flight counter
   (or simply wait one full expected-interarrival cycle per source).
4. Signals merger to drain: sets `draining_ = true`; merger runs until its reorder heap is
   empty, then sets `drained_` and blocks on `WaitFlag`.
5. Waits for `drained_` with `pause_drain_timeout_ms` timeout. Logs warning if timeout exceeded.
6. Sets `state_` to `Paused`. Rings are now frozen — no producer writes, merger quiesced.
7. **DMA queues remain registered.** No kernel calls. Camera hardware keeps running (frames
   are delivered to callbacks, callbacks return early via `valid = false`). This is intentional —
   it avoids camera re-initialisation latency on `resume()`.

**`resume()` — Paused → Capturing**

1. Asserts state is `Paused`.
2. If `resume_clear_rings == true`: resets each ring's `write_seq_` to avoid stale pre-pause
   data polluting the new swing's timeline. Consumers' existing subscriptions see a discontinuity
   (gap in sequence numbers) — this is expected and correct.
3. Clears the `TimelineIndex` write position (new swing, clean timeline).
4. Sets `capturing_.store(true, memory_order_release)`.
5. Signals merger to resume (clears `draining_`).
6. Sets `state_` to `Capturing`.

**`stop()` — Capturing|Paused → Idle**

1. If `Capturing`: calls `pause()` internally first (cleanly quiesces).
2. Sets `running_.store(false)` and wakes merger via `WaitFlag`.
3. Joins merger thread.
4. Sets `state_` to `Idle`.
5. **Does not stop CaptureSource instances** — those are owned by the caller and must be
   stopped separately before `EventBuffer` is destroyed. `~EventBuffer()` asserts no
   sources are active.

### 19.3 Typical Application Flow

The buffer runs for the lifetime of the application. Memory is allocated once
per device selection and freed on deselection — never freed between swings.

```cpp
// ── App startup ──────────────────────────────────────────────────────────
EventBuffer buffer;
buffer.start();   // zero sources — valid; merger runs idle, negligible cost

// Controllers constructed after start(). Sources register when user selects
// devices from the UI — each registerSource() call allocates ring memory once.
CameraManager cameraManager(&buffer);
ImuController imuController(&buffer);

// Clean shutdown wired to app exit signal
QObject::connect(&app, &QCoreApplication::aboutToQuit, [&buffer]() {
    buffer.stop();
});

// ── User selects Camera 0 ─────────────────────────────────────────────────
// cameraManager.setSelected(0, true) internally calls:
//   buffer.pause()
//   buffer.registerSource(cam0_desc)   ← GB allocated HERE, ONCE
//   buffer.resume()
// Camera frame callbacks begin publishing immediately.

// ── User selects IMU 0 ───────────────────────────────────────────────────
// Similar: pause → registerSource(imu0_desc) → resume

// ── Swing capture loop (no start/stop between swings) ────────────────────
while (session_active) {

    // Buffer is Capturing continuously. Rings fill and wrap silently.

    // Ball detection fires swing-complete event (future feature):
    buffer.pause();   // rings frozen — last 5s intact

    // Grab swing window — zero copy into frozen ring
    auto window = buffer.captureSwingWindow(std::chrono::milliseconds(4000));

    // Analyse — window guaranteed valid while buffer is Paused
    auto result = swing_analyser.analyse(window);
    ui.showAnalysis(result, window);

    // User dismisses — destroy window BEFORE resume
    window = {};   // ring memory stays allocated — only positions reset

    buffer.resume();   // rings cleared, capturing resumes immediately
}

// ── User deselects Camera 0 ──────────────────────────────────────────────
// cameraManager.setSelected(0, false) internally calls:
//   buffer.pause()
//   buffer.deregisterSource(cam0_id)   ← GB freed HERE
//   buffer.resume()

// ── App exit ─────────────────────────────────────────────────────────────
// aboutToQuit fires → buffer.stop() → merger joined
// ~EventBuffer() frees any remaining registered sources
```

**Key differences from earlier versions:**
- `buffer.start()` is called once at app launch, not per feature session
- `buffer.stop()` is called once on app exit, not per feature session
- `pause()`/`resume()` gate each capture window (driven by ball/swing detection)
- `registerSource()` / `deregisterSource()` are the device selection API
- `startAll()` / `stopAll()` in `CameraManager` control camera pipelines only

### 19.4 Thread Safety of State Transitions

State transitions (`start`, `pause`, `resume`, `stop`) are **not** thread-safe with respect
to each other. They are called from the application's main thread only. The `capturing_`
atomic flag is the only cross-thread communication between lifecycle management and the
capture/merger threads — it is explicitly `memory_order_acquire/release`.

`isCapturing()` is safe to call from any thread (reads `capturing_` with `memory_order_acquire`).
`state()` is safe to call from any thread (reads `state_` with `memory_order_acquire`).

### 19.5 What Happens to Consumers During Pause

`Subscription::waitNext()` blocks on `WaitFlag`. While paused, no new `IndexEntry` records
are appended, so waiting consumers remain blocked until `resume()` starts producing again.
Consumers that have already read past the swing window will see no new events — this is
correct and expected. Consumers that are mid-analysis should use `SwingWindow` (see
Section 20) rather than a live `Subscription`.

---

## 20. SwingWindow

### 20.1 Purpose

A `SwingWindow` is a **frozen, zero-copy, bounded view** of a time range within the buffer.
It is the primary interface for swing analysis consumers. It provides:

- An ordered list of `IndexEntry` records for the requested time range
- Zero-copy `ReadHandle` access to payloads, guaranteed valid for the `SwingWindow` lifetime
- Source-specific helpers (frame count, IMU sample range)

`SwingWindow` is only valid after `pause()`. This eliminates the need for slot pinning or
reference counting — the buffer is frozen so no overwrite can occur.

### 20.2 Class Definition

```cpp
class SwingWindow {
public:
    // Non-copyable. Movable.
    SwingWindow(const SwingWindow&) = delete;
    SwingWindow& operator=(const SwingWindow&) = delete;
    SwingWindow(SwingWindow&&) noexcept;
    SwingWindow& operator=(SwingWindow&&) noexcept;

    // RAII — releasing the SwingWindow does not resume the buffer.
    // The application calls buffer.resume() explicitly when ready.
    // Destructor clears EventBuffer::swing_window_live_ — this unblocks
    // any pending deregisterSource() call which asserts the flag is false.
    ~SwingWindow();

    // Time range
    int64_t startTimestampUs() const noexcept;
    int64_t endTimestampUs()   const noexcept;
    std::chrono::microseconds duration() const noexcept;

    // All events in timestamp order across all sources
    std::span<const IndexEntry> entries() const noexcept;

    // Events filtered by source
    std::vector<IndexEntry> entriesFor(SourceId) const;

    // Convenience counts
    size_t frameCount(SourceId camera_id) const noexcept;
    size_t imuSampleCount(SourceId imu_id) const noexcept;

    // Zero-copy payload access.
    // Valid for the lifetime of this SwingWindow (buffer is frozen).
    // Returns a handle with data=nullptr if the entry is not in this window.
    SourceRing::ReadHandle payloadOf(const IndexEntry&) const noexcept;

    // Format of a given source (for consumers to interpret raw bytes)
    const FormatDescriptor& formatOf(SourceId) const noexcept;

    // IMU interpolation helper — returns interpolated IMU state at a given timestamp.
    // Interpolates linearly between the two nearest IMU samples for the given source.
    // Returns false if insufficient IMU data exists around the requested timestamp.
    bool interpolateImu(SourceId imu_id, int64_t timestamp_us,
                        std::byte* out, size_t out_bytes) const noexcept;

private:
    friend class EventBuffer;

    // Constructed only by EventBuffer::captureSwingWindow()
    SwingWindow(const EventBuffer* buffer,
                std::vector<IndexEntry> entries,
                int64_t start_us,
                int64_t end_us);

    const EventBuffer*       buffer_;
    std::vector<IndexEntry>  entries_;    // snapshot of IndexEntry records — tiny
    int64_t                  start_us_;
    int64_t                  end_us_;
    // Payloads are NOT copied — they live in the frozen SourceRing slots.
    // Buffer must remain in Paused state while SwingWindow is in use.
};
```

### 20.3 `captureSwingWindow()` Implementation Notes

```cpp
// Convenience overload: trailing_duration before the pause timestamp
SwingWindow EventBuffer::captureSwingWindow(
    std::chrono::milliseconds trailing_duration)
{
    int64_t end_us   = nowMicros();   // paused at this point — no new writes
    int64_t start_us = end_us - trailing_duration.count() * 1000;
    return captureSwingWindow(start_us, end_us);
}

SwingWindow EventBuffer::captureSwingWindow(
    int64_t t_start_us, int64_t t_end_us)
{
    assert(state() == BufferState::Paused);   // enforced in all builds
    auto entries = index_.snapshot(t_start_us, t_end_us);
    return SwingWindow(this, std::move(entries), t_start_us, t_end_us);
}
```

The `snapshot()` call allocates a `std::vector<IndexEntry>` — these are 40-byte records, so
a 4s window at 600 events/s produces ~96 KB of index data. Negligible. The frame payloads
themselves are **not copied** — `payloadOf()` returns a zero-copy `ReadHandle` into the
frozen ring.

### 20.4 Lifetime Contract

```
buffer.pause()
    │
    ▼
window = buffer.captureSwingWindow(4000ms)
    │
    ├── window.entries()       → span into window's internal vector (no alloc)
    ├── window.payloadOf(e)    → ReadHandle into frozen SourceRing slot (zero copy)
    ├── window.interpolateImu() → computed on the fly from IMU slots
    │
    ▼
analysis complete
    │
window goes out of scope (or explicitly reset)
    │
    ▼
buffer.resume()   ← safe to call at any point after pause(); SwingWindow need
                    not be destroyed first, but payload access after resume()
                    is undefined (rings may be overwritten). Best practice:
                    destroy SwingWindow before calling resume().
```

### 20.5 SwingWindow and Multi-Swing Sessions

For applications that want to **compare swings** or keep a recent window accessible during
the next capture, `SwingWindow` can be moved into a persistent store before `resume()` is
called — but the caller takes responsibility for not accessing payloads after `resume()`.
A safe pattern: call `window.copyPayloads()` (future deferred feature) to materialise
frame data into owned memory before resuming, or simply don't access raw payloads after
`resume()` (only use the index metadata for comparison).

---

## 21. Deferred Items

Revisit when the listed condition is met. Do not implement speculatively.

| Item | Condition to Revisit |
|---|---|
| Crash-time forensics dump | Production deployment with end users in the field |
| Adaptive merger interval for mobile power | Mobile platform launch |
| `ParsedView` payload wrapper | Multiple consumers found duplicating parse logic |
| Double-pool DMA slots | Profiling shows torn-read rate > 0.01% |
| Merger sharding | Source count exceeds 10 |
| `std::variant` / `std::function` elimination | Profiling shows > 2% time in dispatch |
| Active hardware sync (HardwarePts / HardwareTrigger) | Hardware sync acquisition confirmed |
| Hot-plug source support (re-registration while Capturing) | UX requirement defined — deregister/register while Paused is supported; Capturing-state hot-plug requires locking not yet present |
| Mobile buffer profile | Mobile platform launch |
| MJPEG / variable-payload validation path | First MJPEG-capable camera integrated |
| `SwingWindow::copyPayloads()` — materialise frames to owned memory | Multi-swing comparison feature requested |
| Thread-safe state transitions | Multiple application threads need to manage lifecycle |
