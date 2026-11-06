# libbuffer: splitting the capture ring buffer into a sibling repository

**Audience**: whoever builds the split. The API in §4 is approved before any code moves
**Code today**: `src/Buffer/` (`pinpoint_buffer`): 4,101 lines, plus 3,590 of tests
**Code after**: `github.com/PinPoint-Golf/libbuffer` (MIT, sibling `../libbuffer`), and a thin golf
layer left in `src/Buffer/` (GPL, unchanged licence)
**Status**: DESIGN, nothing built. `PinPoint-Golf/libbuffer` does not exist yet
**Written**: 2026-11-06

---

## 1. Decisions

| Decision | Choice |
|---|---|
| Scope | Clean split: no golf, camera or IMU vendor names in the library |
| Licence | MIT for the library. The files that stay in Studio stay GPL |
| Behaviour | No change. Same bytes, same locks, same lifetimes, same `swing.json` |
| Studio call sites | No change outside `src/Buffer/`, apart from build files, `app_info.cpp` and test wiring |
| Format metadata | The library stores none. Studio keeps it, keyed by `SourceId` (§4.3) |
| Names | `libbuffer` everywhere: repo, namespace, include prefix, `LIBBUFFER_` macros and options |

**Deliverables**

1. Repository `PinPoint-Golf/libbuffer`, tagged `v0.1.0`, with history, tests and CI.
2. Studio built against it, with `src/Buffer/` reduced to the golf layer.
3. This note, updated with the measured results of §8.

**Done means** every gate in §8 has passed, or has failed and is written up here. No gate is skipped.

---

## 2. What the code does today

The library is already nearly independent. It uses no Qt and no PinPoint code, and it has its own CMake
project. Three things tie it to golf.

**The format types.** `types.h` holds `DeviceKind` (with `IMU_WitMotion`, `IMU_HackMotion`) and
`PixelFormat`. `format_descriptor.h` holds `CameraFormat`, `ImuFormat` and the `swing.json` exposure
strings. `SourceDescriptor` embeds a `FormatDescriptor`.

The core barely reads them. Only `computeSlotBytes()` and `computeSlotCount()` look inside the format,
to size the ring. `EventBuffer` otherwise just stores the descriptor, hands it back from `formatOf()`,
overwrites it in `updateSourceFormat()`, and defaults `device_serial` from `identifier`.

**The window.** `event_buffer.h` includes `swing_window.h`, and `captureSwingWindow()` is on the public
API. `SwingWindow` is generic except for `interpolateImu()`, which needs `ImuSample`, and `formatOf()`.

**The payload interface.** `SwingPayloadSource` mixes byte access (`payloadOf`, `validate`) with format
access (`formatOf`).

What Studio touches, outside `src/Buffer/`:

| Name | Lines | Files |
|---|---:|---:|
| `SwingWindow` | 157 | 62 |
| `formatOf(` | 58 | 19 |
| `FormatDescriptor` | 49 | 19 |
| `nowMicros(` | 45 | 29 |
| `registerSource(` | 25 | 12 |
| `SourceDescriptor` | 15 | 11 |
| `updateSourceFormat(` | 8 | 3 |
| `captureSwingWindow(` | 5 | 3 |
| `makeRingPayloadSource(` | 1 | 1 |

These counts include tests and comments. Almost every line reads a type or a method that §4 keeps exactly
as it is, which is why the split can leave them alone.

---

## 3. Where each file goes

| File | Goes to | Change |
|---|---|---|
| `source_ring.{h,cpp}` | library | `SourceDescriptor` helpers removed |
| `timeline_index.{h,cpp}`, `wait_flag.{h,cpp}`, `thread_policy.{h,cpp}` | library | Namespace only |
| `source_stats.h`, `platform.h`, `event_buffer_config.h` | library | Namespace and macro prefix only |
| `device_clock_mapper.{h,cpp}` | library | Namespace, comments reworded |
| `event_buffer.{h,cpp}` | library | Takes `SourceConfig`, no format storage, window renamed |
| `swing_window.{h,cpp}` | **split** | Generic part becomes library `CaptureWindow` |
| `swing_payload_source.h` | **split** | Byte access becomes library `PayloadSource` |
| `types.h` | **split** | `SourceId`, `IndexEntry`, `IndexEntryFlags` move |
| `format_descriptor.h`, `source_descriptor.h`, `imu_sample.h` | Studio | Slot helpers move here from `source_ring.cpp` |
| `pixel_format_names.h`, `ram_payload_source.h`, `composite_payload_source.h`, `deferred_stitch.h` | Studio | None |

`CompositePayloadSource` is generic in spirit, but it routes `formatOf`, so it stays. Moving it would give
the library a class with no user there.

Library layout, following `libwrist`:

```
libbuffer/
  CMakeLists.txt  CMakePresets.json  LICENSE  README.md  .clang-format
  .github/workflows/ci.yml
  docs/design.md
  include/libbuffer/   version.h types.h platform.h source_config.h source_ring.h source_stats.h
                       timeline_index.h wait_flag.h thread_policy.h payload_source.h
                       capture_window.h event_buffer.h event_buffer_config.h device_clock_mapper.h
  src/                 source_ring.cpp timeline_index.cpp wait_flag.cpp thread_policy.cpp
                       event_buffer.cpp capture_window.cpp device_clock_mapper.cpp
  tests/
```

---

## 4. The API

### 4.1 Library

```cpp
namespace libbuffer {

struct SourceConfig {
    std::string name;
    std::string identifier;   // stable per device; keys the lifetime counters; "" = not tracked
    size_t      slot_count = 0;   // the ring rounds this up to a power of two
    size_t      slot_bytes = 0;
    std::chrono::microseconds expected_interarrival_us{0};   // 0 = no stall watchdog
};

class PayloadSource {
public:
    virtual ~PayloadSource() = default;
    virtual SourceRing::ReadHandle payloadOf(SourceId, uint64_t sequence) const noexcept = 0;
    virtual bool validate(SourceId, const SourceRing::ReadHandle&) const noexcept = 0;
};

class CaptureWindow {
public:
    CaptureWindow(std::unique_ptr<const PayloadSource>, std::vector<IndexEntry>,
                  int64_t start_us, int64_t end_us);
    // move-only; startTimestampUs(), endTimestampUs(), duration(), entries(), entriesFor(id),
    // entryCount(id), payloadOf(entry): all as SwingWindow does today

    bool validate(SourceId, const SourceRing::ReadHandle&) const noexcept;

    struct Bracket { const IndexEntry* before; const IndexEntry* after; };
    std::optional<Bracket> bracket(SourceId, int64_t target_us) const noexcept;
};

class EventBuffer {
public:
    SourceId registerSource(SourceConfig cfg);
    bool     windowLive() const noexcept;
    CaptureWindow captureWindow(int64_t t_start_us, int64_t t_end_us);
    CaptureWindow captureWindow(std::chrono::milliseconds trailing_duration);
    std::unique_ptr<const PayloadSource> makeRingSource();
    // Everything else as it is today: deregisterSource, the producer and consumer APIs, lifecycle,
    // setLogCallback, setThreadRegisterHook, statsFor, stalledSources, diagnostics, nowMicros.
    // No formatOf, no updateSourceFormat.
};

} // namespace libbuffer
```

`bracket()` is the one new method. It lifts the search out of `interpolateImu`, so its tie-break moves
with it unchanged: `after` is the first entry strictly later than the target, and `before` is the **first**
entry among those sharing the largest timestamp at or before the target. Using `after - 1` would pick the
last of them and silently change the interpolation on disk and stitched lanes.

`SourceDescriptor::computeSlotCount()` stays in Studio because the ring only needs a number. The logs and
the sizing comments in `hm_instance.h` and `imu_instance.h` depend on it staying where they can see it.

### 4.2 Studio's golf layer

`src/Buffer/` keeps its flat headers, so **no `#include` line changes outside it**. `pinpoint_buffer`
keeps its name and links the library publicly, so its 24 link lines stay too.

```cpp
namespace pinpoint {

using libbuffer::SourceId;  using libbuffer::IndexEntry;  using libbuffer::SourceRing;
using libbuffer::BufferState;  using libbuffer::LogSeverity;  // ...plus the other moved names

class SwingPayloadSource : public libbuffer::PayloadSource {
public:
    virtual const FormatDescriptor& formatOf(SourceId) const noexcept = 0;
};

class SwingWindow : public libbuffer::CaptureWindow {
public:
    SwingWindow(std::unique_ptr<const SwingPayloadSource>, std::vector<IndexEntry>,
                int64_t start_us, int64_t end_us);
    size_t frameCount(SourceId) const noexcept;        // both forward to entryCount()
    size_t imuSampleCount(SourceId) const noexcept;
    const FormatDescriptor& formatOf(SourceId) const noexcept;
    bool interpolateImu(SourceId, int64_t target_us, std::byte* out, size_t out_bytes) const noexcept;
private:
    const SwingPayloadSource* formats_;   // the object the base owns; survives a move
};

class EventBuffer : public libbuffer::EventBuffer {
public:
    using libbuffer::EventBuffer::EventBuffer;
    SourceId registerSource(SourceDescriptor desc);     // hides the base method
    void     deregisterSource(SourceId id);
    const FormatDescriptor& formatOf(SourceId id) const;
    void     updateSourceFormat(SourceId id, const FormatDescriptor& fmt);
    SwingWindow captureSwingWindow(int64_t t_start_us, int64_t t_end_us);
    SwingWindow captureSwingWindow(std::chrono::milliseconds trailing_duration);
    std::unique_ptr<const SwingPayloadSource> makeRingPayloadSource();
    bool     swingWindowLive() const noexcept;
private:
    std::array<std::optional<FormatDescriptor>, MAX_SOURCES> formats_;
};

} // namespace pinpoint
```

`src/Buffer/source_ring.h` and `device_clock_mapper.h` become one-line forwarding headers, because Studio
includes them (1 and 6 lines). The other moved headers have no includers outside `src/Buffer/`, so they
need no forwarder.

Studio's `event_buffer.cpp` keeps the behaviour the library no longer owns:

- `registerSource` defaults `device_serial` from `identifier`, sizes the slots, calls the base and stores
  the format.
- `updateSourceFormat` preserves a registered serial when the new one is empty, and is a no-op for an
  unknown id.
- `formatOf` throws `std::out_of_range` for an unknown id.
- `deregisterSource` calls the base, then clears the entry.
- `captureSwingWindow` keeps today's order: snapshot the index, then raise the guard.

The ring backing for a window is a small adapter:

```cpp
class RingSwingSource final : public SwingPayloadSource {
    std::unique_ptr<const libbuffer::PayloadSource> ring_;   // owns the resume guard
    const EventBuffer* buffer_;                              // answers formatOf
};
```

The guard is still raised by the library's ring source constructor and cleared by its destructor, so the
protection `makeRingPayloadSource()` exists for is unchanged. A window read gains one virtual call per
frame fetch, and the producer path gains nothing.

### 4.3 Why Studio holds the format table

| Option | Verdict |
|---|---|
| **Table in Studio, keyed by `SourceId`** | **Chosen.** The library has no type erasure and nothing to explain. `SourceId` is already the slot index, so a fixed array gives the same stable reference and in-place update as today |
| `EventBuffer<Meta>` template | Rejected. `event_buffer.cpp` moves into a header, and every consumer recompiles the merger |
| `std::any` on the descriptor | Rejected. `SwingWindow` has to wrap `formatOf` anyway for the cast, so this saves only the array and adds a feature the library does not use itself |

---

## 5. Build and release wiring

**Library CMake.** It follows the `libwrist` rule that embedding must change nothing about the host: no
cache `FORCE`, and tests only when top-level. Three things differ from today's `src/Buffer/CMakeLists.txt`:

- `target_compile_features(libbuffer PUBLIC cxx_std_20)` replaces `CMAKE_CXX_STANDARD`, so the requirement
  reaches consumers.
- `find_package(Threads)` and a public `Threads::Threads` link. Today nothing declares it, and the host
  supplies it.
- `LIBBUFFER_SANITIZE` replaces `PINPOINT_ENABLE_{ASAN,UBSAN,TSAN}`.

**Studio CMake.** Resolve `libbuffer` before line 710's `add_subdirectory(src/Buffer)`, using the libwrist
block as the pattern: `PP_LIBBUFFER_LOCAL`, sibling wins, otherwise FetchContent. Differences:

- **Pin `GIT_TAG v0.1.0`, not `main`.** libwrist tracks `main` because bugs were expected during
  integration. This code is stable, it sits on the capture path, and a release should be reproducible.
- Publish `PP_LIBBUFFER_VERSION` to `app_info.cpp` so the About box shows it, as for the other sisters.
- Add a `tests/cmake/PinPointTests.cmake` resolver matching the `PP_LIBWRIST_DIR` one, with the library's
  include directory on the `pp_add_test` path.
- Add the library's include directory wherever `src/Buffer` or `${PP_BUFFER}` is listed directly. That is
  about 24 lines, because `types.h` now includes `<libbuffer/types.h>`. Targets such as `regrade_ledger`
  and `ppcp_conform_host` see `types.h` without linking `pinpoint_buffer`.
- Link `libbuffer` into the four test targets that compile `swing_window.cpp` directly
  (`imu_refusion_check_test`, `imu_fuser_retime_test`, `capture_integrity_check_test`, `frame_store_test`).
- Delete `src/Analysis/tests/imu_test_stubs.cpp`. It defines only `EventBuffer::nowMicros()`, so that
  those targets avoid linking the buffer, and the library is small and free of Qt, so that reason has gone.
  `pipeline_test` and `analysis_profiling_test` reference nothing from it, so they just lose the source
  line. `imu_driver_frame_test` and `trunk_turn_path_test` link `libbuffer` for the one call that needs
  it, `wt9011dcl_base.cpp:229`, which picks up the inherited `nowMicros`. Nothing connects to the signal
  that timestamp feeds in those tests, so the stub's fake 5 ms tick is not lost. Its stale comments in
  `src/Analysis/tests/CMakeLists.txt` go with it.

**CI.** Studio has only `release.yml`, so `libbuffer` gets its own `ci.yml`, adapted from libwrist's: Linux
gcc and clang, ASan with UBSan, TSan, macOS, Windows.

**Licence.** `LICENSE` is MIT, `Copyright (c) 2026 Mark Liversedge`, with `SPDX-License-Identifier: MIT`
on every file, replacing the GPL header on the moved files. All 41 commits have one author.
`LICENSEDEPS.md` gains a row, marked as pinned rather than tracking `main`.

**History.** `git subtree split --prefix=src/Buffer` seeds the repo, so the commit messages and blame that
explain the ⚠ comments survive. The layout move and the relicence are then separate commits on top.

**Docs.** The library gets a short `README.md` and a `docs/design.md` taken from the generic parts of
`docs/developer/event_buffer_developer_guide.md`. In Studio, update that guide's location and path map,
`BUILDING.md` (lines 585 and 596), and `docs/developer/testing_developer_guide.md` (line 187). The locked
`docs/design/event_buffer_design.md` stays as the record. The `/growth` skill lists its repos, so it gains
the seventh, or the moved lines show as a fall.

---

## 6. Traps

1. **`registerSource` can return `kInvalidSourceId`.** In the wrong state with asserts off it returns
   rather than throws. The subclass must check before indexing `formats_`.
2. **Ids are reused.** `registerSource` fills the lowest free slot, so `deregisterSource` must clear the
   format entry, or a new source inherits the old one's format until its first update. Clear it only for a
   valid id, because an invalid one is a no-op today.
3. **`capture_window.cpp` must not reference `EventBuffer`.** The four direct-compile test targets rely on
   a window existing without a buffer.
4. **`SwingWindow::formats_` is a raw pointer to a base-owned object.** It survives a move because the
   object is on the heap, and the move operations must carry it.
5. **TSan suppressions name the namespace.** `tests/tsan_suppressions.txt` says
   `race:pinpoint::SourceRing::...`, so every line must change with it. Otherwise the deliberate seqlock
   races are reported.
6. **`PINPOINT_ENABLE_*` disappears.** `PP_SANITIZE` instruments only `pp_add_test` targets, and it does
   not instrument `pinpoint_buffer`. The umbrella must therefore pass `PP_SANITIZE` through to
   `LIBBUFFER_SANITIZE` and to `pinpoint_buffer`, or the IMU worker's TSan run looks at uninstrumented
   ring code. That is a small improvement on today, where the library needs the second spelling.
7. **Publish order.** A failed clone is a failed configure for everyone, which is why `PP_LIBPPCP_FETCH`
   defaults off. `libbuffer` and its tag must be on GitHub before the Studio commit lands. Until then the
   work runs against the sibling.
8. **The cabin WIP patch route carries Studio only.** A library change has to be pushed and the pin moved
   before the cabin PC can see it.
9. **Stray names in comments.** `event_buffer.h` names `ShotProcessor`, `CameraManager`, `main.cpp` and
   Studio's design docs, and `device_clock_mapper.h` is written about cameras and dated incidents. Reword
   them so the reasoning survives without the names. Keep the ⚠ warnings themselves.

---

## 7. Tests

| Suite | Lines | Domain or window references | Action |
|---|---:|---:|---|
| `adversarial_fuzz_test` | 219 | 0 | Move as is |
| `device_clock_mapper_test` | 224 | 0 | Move as is |
| `timeline_index_test` | 178 | 0 | Move as is |
| `wait_flag_test` | 171 | 0 | Move as is |
| `watchdog_test` | 311 | 3 | Move, use `SourceConfig` |
| `thread_policy_test` | 164 | 6 | Move, use `SourceConfig` |
| `event_buffer_test` | 551 | 6 | Move, use `SourceConfig` and `CaptureWindow` |
| `source_ring_test` | 486 | 12 | Move; the slot-size cases stay in Studio |
| `latency_benchmark` | 380 | 6 | Move, use `SourceConfig` |
| `swing_window_test` | 906 | 75 | **Split**: lanes, ordering, bracket and move go to the library; IMU interpolation and formats stay |

New in Studio, because these behaviours leave the library: **`event_buffer_format_test`** covers the serial
default, serial preservation, `out_of_range` on an unknown id, the invalid-id path of trap 1, id reuse from
trap 2, and the guard held across `makeRingPayloadSource()` through a moved window.

New in the library: **`bracket()`** tests, including duplicate timestamps. A **`no_host_names`** check in
CI fails on `pinpoint`, `swing`, `golf`, `Witmotion`, `HackMotion`, `ShotProcessor` or `CameraManager`
anywhere in `include/` and `src/`, so the split stays clean.

---

## 8. Gates

Results are written into this note. No test run touches `qml_ui`, and Studio tests run through `ctest`.

| Gate | Check | Pass |
|---|---|---|
| G0 | Before changing anything: record `latency_benchmark` on the Mac, and `interpolateImu` output plus `swing.json` for one swing | Baselines saved under the run tree |
| G1 | Library: `ctest` on the Mac under `dev`, ASan+UBSan and TSan | All green |
| G2 | Library CI on all platforms, with `no_host_names` | All green |
| G3 | Studio: Debug build, then `ctest -R` for `Buffer` residual suites, `event_buffer_format_test`, `swing_window_parity_test`, `imu_refusion_check_test`, `imu_fuser_retime_test`, `capture_integrity_check_test`, `frame_store_test`, `imu_io_worker_test`, `shot_controller_decline_test`, `ppcp_live_session_test`, and the four former stub targets: `pipeline_test`, `analysis_profiling_test`, `imu_driver_frame_test`, `trunk_turn_path_test` | All green |
| G4 | The same `interpolateImu` sweep as G0 | Byte-identical |
| G5 | Re-analyse the G0 swing on a copy, reusing its recorded pose | `swing.json` byte-identical apart from timings |
| G6 | `latency_benchmark` as in G0 | p50 and p99 within the spread of three baseline runs |
| G7 | After `libbuffer` is public: a clean configure with `-DPP_LIBBUFFER_LOCAL=OFF` | Fetches the tag, and the About box shows it |
| G8 | Cabin PC: `tools/studio/studio-build.cmd`, the Windows route through FetchContent and MSVC | Builds and deploys |
| G9 | At the cabin: one real session, one shot | Cameras register, the swing saves and analyses, the session ledger looks normal |

G4 and G5 are the gates that matter most: re-analysis determinism depends on this code. Because no output
changes, no `k*StageVersion` is bumped and no sweep is needed. If G5 shows a difference, that is a bug in
the split and not a reason to bump one.

---

## 9. Build order

1. **G0 baselines** on the unchanged tree.
2. **Library** in `../libbuffer`: subtree split, layout, relicence, namespace, API, tests, CI, reaching G1.
3. **Studio** against the sibling: golf layer, CMake, new test, reaching G3 to G6.
4. **Push `libbuffer` and tag `v0.1.0`**, once the results have been judged, reaching G2 and G7.
5. **Commit and push Studio**, then G8 and G9.

Steps 2 and 3 can run in parallel once §4 is approved, because they are separate repos and §4 is the
contract between them. Estimated effort is 1 to 2 days.

---

## 10. Left alone

These behave oddly today, and the split preserves them exactly.

- **`updateSourceFormat` has no lock.** It assigns the descriptor in place, and it can run on the frame
  path through `refreshExposure()`, while analysis reads `formatOf()`. Whether the two can overlap has not
  been checked.
- **Unknown ids through the ring.** `RingPayloadSource::formatOf` is `noexcept` but calls a `formatOf` that
  throws, so an unknown id terminates the process. The RAM source returns an empty descriptor instead.
- **A small gap in `captureSwingWindow`.** It snapshots the index before raising the guard, the reverse of
  what `makeRingPayloadSource()`'s comment argues for.
