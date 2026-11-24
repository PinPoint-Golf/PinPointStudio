# PinPoint Studio — Testing Developer Guide

**Audience**: Developers adding or running unit tests in PinPoint Studio
**Location**: `tests/` (umbrella + shared CMake module + presets), `src/<Sub>/tests/` (the suites)
**Language**: CMake + C++17/20 (GoogleTest or a hand-rolled `main()`)
**Status**: 9 suites, runnable as one umbrella build or individually

---

## Contents

1. [Philosophy — why tests are decoupled from the app](#1-philosophy--why-tests-are-decoupled-from-the-app)
2. [The big picture](#2-the-big-picture)
3. [How the umbrella orchestration works](#3-how-the-umbrella-orchestration-works)
4. [The shared module — `tests/cmake/PinPointTests.cmake`](#4-the-shared-module--testscmakepinpointtestscmake)
5. [`pp_add_test` reference](#5-pp_add_test-reference)
6. [Running the tests](#6-running-the-tests)
7. [Adding a test to an existing suite](#7-adding-a-test-to-an-existing-suite)
8. [Adding a brand-new suite](#8-adding-a-brand-new-suite)
9. [Conventions and gotchas](#9-conventions-and-gotchas)
10. [File map](#10-file-map)

---

## 1. Philosophy — why tests are decoupled from the app

The app target is heavy: configuring it pulls in whisper.cpp/ggml, FFmpeg,
espeak-ng (built from source), OpenCV and ONNX Runtime — on the order of 20+
seconds just to *configure*, before a single object compiles. A unit test for,
say, pure shaft-kinematics math has no business waiting on any of that.

⚠ **The QML module is no longer part of that argument, and the difference matters.**
The Gui suite declares the `PinPointStudio` QML module itself, from the same lists
the app builds it from (`cmake/PinPointQmlModule.cmake`), so its offscreen UI tests
can press real components. It still depends on no app target — the module's C++
needs only Qt, and its link closure stays inside sources other suites already
compile. The invariant is *decoupled from the app*; *never builds QML* was only a
proxy for it, and holding the proxy is what kept the only UI coverage in the repo
out of every release gate on every platform.

So **the test suites are not part of the app build.** The root `CMakeLists.txt`
forces `BUILD_TESTING OFF`; building the app never compiles a test. Instead each
suite is a small, self-contained CMake project that **recompiles only the handful
of `.cpp` it needs** and stubs out anything that would drag in the heavy
dependencies (most notably `PpLogStream`, whose real implementation in
`pp_debug.cpp` pulls in whisper/ggml).

Two consequences worth internalising:

- There is **no `pinpoint_analysis` / `pinpoint_core` / `pinpoint_gui` library.**
  Tests list source files directly. The only real library target is
  `pinpoint_buffer` (`src/Buffer`), which the EventBuffer and IMU suites link.
- Tests must be **buildable offline-ish and fast.** Anything a test needs that
  the app would normally provide (a log sink, `EventBuffer::nowMicros`, …) is
  supplied by a local stub file in the suite.

This decoupling is the property to preserve. Do not "simplify" by folding the
suites into the app build under `BUILD_TESTING ON` — that reintroduces the heavy
configure on every test iteration.

---

## 2. The big picture

```
tests/
├── CMakeLists.txt            # the umbrella: builds all suites at once
├── CMakePresets.json         # tests / tests-asan / tests-tsan presets
├── cmake/
│   └── PinPointTests.cmake   # shared infra: Qt prefix, Eigen, sanitizers, pp_add_test
└── MIGRATION.md              # how the suites were consolidated (historical record)

src/<Sub>/tests/
└── CMakeLists.txt            # one suite; uses pp_add_test; works standalone too
```

Every suite runs **two ways**, from the same `src/<Sub>/tests/CMakeLists.txt`:

- **Standalone** — `cmake -S src/<Sub>/tests -B <build>` for fast single-suite
  iteration. A small bootstrap block at the top of each suite pulls in the shared
  module.
- **Under the umbrella** — `cmake -S tests -B build/tests` configures *all* suites
  in one shot; the bootstrap block is skipped (the shared module is already
  loaded) and the suite is `add_subdirectory`'d.

This is the same file in both cases — there is no duplication and no "umbrella
copy" of a suite to keep in sync.

---

## 3. How the umbrella orchestration works

`tests/CMakeLists.txt` is a thin driver:

```cmake
cmake_minimum_required(VERSION 3.16)
project(pinpoint_tests LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PinPointTests)        # Qt prefix, Eigen locate, sanitizers, gtest, pp_add_test
enable_testing()

# Buffer is special — see below.
add_subdirectory("${PP_SRC}/Buffer"       "${CMAKE_BINARY_DIR}/Buffer-lib")
add_subdirectory("${PP_SRC}/Buffer/tests" "${CMAKE_BINARY_DIR}/Buffer")

foreach(suite Analysis Audio Core Gui IMU LaunchMonitor Pose Update)
    add_subdirectory("${PP_SRC}/${suite}/tests" "${CMAKE_BINARY_DIR}/${suite}")
endforeach()
```

Three details make this work:

1. **The shared module is included once, at the top.** It defines `pp_add_test`,
   `pp_find_eigen`, the sanitizer knob and the repo-layout variables (`PP_SRC`,
   `PP_BUFFER`, `PP_CORE`, `PP_THIRD`). Because it is loaded before any
   `add_subdirectory`, every suite sees `pp_add_test` already defined — which is
   exactly the signal each suite's bootstrap block uses to decide it is running
   under the umbrella (see §7).

2. **Buffer is special-cased.** Its tests live under `src/Buffer/tests` but are
   gated on `PROJECT_IS_TOP_LEVEL` *and* need the real `pinpoint_buffer` library
   target. So the umbrella adds the Buffer library first, then pulls in
   `src/Buffer/tests` directly. The IMU suite also needs `pinpoint_buffer`; it
   guards its own `add_subdirectory(Buffer)` with `if(NOT TARGET pinpoint_buffer)`
   so the umbrella and standalone paths don't collide ("binary dir already used").

3. **A default C++20 standard** is set by the shared module. Suites added as
   sibling directories don't inherit a parent suite's scope-local
   `CMAKE_CXX_STANDARD`, so the shared module pins the app's standard (C++20)
   globally. Individual targets override with `STD 17` / `STD 20` on
   `pp_add_test` where they care.

One configure produces one CTest registry spanning all suites:

```bash
cmake -S tests -B build/tests
cmake --build build/tests -j6
ctest --test-dir build/tests --output-on-failure -LE qml_ui                    # every suite but the QML files
ctest --test-dir build/tests --output-on-failure -L qml_ui --no-tests=error   # then the QML files (label qml_ui), in their own pass
```

---

## 4. The shared module — `tests/cmake/PinPointTests.cmake`

`include(PinPointTests)` sets up everything a suite needs. It is idempotent
(`include_guard(GLOBAL)`), so the umbrella and a standalone bootstrap can both
include it safely.

What it provides:

| Thing | Detail |
|---|---|
| **Repo layout** | `PP_REPO_ROOT`, `PP_SRC`, `PP_BUFFER`, `PP_CORE`, `PP_THIRD` |
| **Qt prefix** | The **newest** `~/Qt/<version>/<abi>` carrying a `Qt6Config.cmake` (`macos`, `gcc_64`, `msvc2022_64`), unless `-DCMAKE_PREFIX_PATH` / env already set. Compared by version, not sorted, so 6.10 beats 6.9. If none is found it says so rather than silently letting CMake find some other Qt. |
| **Qt + Threads** | `find_package(Qt6 Core Gui)` and `find_package(Threads)` once. Suites needing more (Qml, Bluetooth, OpenCV, Test for `QSignalSpy`) add their own — cheap and additive. |
| **C++ standard** | Default C++20 (app/Buffer parity); per-target `STD` overrides. |
| **`pp_find_eigen(<out>)`** | One Eigen locator: explicit `-DPP_EIGEN_DIR`, then any app build's `build/*/_deps/eigen-src` (matched regardless of build-dir name), then fetch 3.4.0. |
| **`pp_apply_sanitizers(<tgt>)`** | Applied automatically by `pp_add_test` from the `PP_SANITIZE` cache var. |
| **`pp_require_gtest()`** | Lazily fetches GoogleTest the first time a suite links it — gtest-free suites never pay for it. Called automatically by `pp_add_test` when `GTEST`/`GTEST_NO_MAIN` is requested. |
| **`pp_add_test(...)`** | The canonical test-target helper (see §5). |

### Eigen — the bug this fixed

Before consolidation, `src/Analysis/tests` globbed `build/*/_deps/eigen-src`
(correct) while `src/IMU/tests` **hardcoded the Linux build-dir name**
(`build/Desktop_Qt_6_11_0-Debug/...`). On macOS that path never matched, so
`eskf_gyro_units_test` was silently *skipped* — not failed, just absent. The
single `pp_find_eigen()` routine removed that whole class of per-suite path drift.
Always resolve Eigen through it.

### Sanitizers — one knob

All suites share `-DPP_SANITIZE`:

```bash
cmake -S tests -B build/tests-asan -DPP_SANITIZE="address;undefined"
cmake -S tests -B build/tests-tsan -DPP_SANITIZE=thread
```

Use a **separate build dir per sanitizer config** (the flags bake into objects).
`pp_add_test` applies them to every target automatically.

Two gotchas that have each cost an afternoon:

- **The Buffer suite does not use this knob.** It predates `pp_add_test` and keeps
  its own `-DPINPOINT_ENABLE_ASAN` / `_UBSAN` / `_TSAN` options (see
  `src/Buffer/CMakeLists.txt`). `-DPP_SANITIZE` configures the umbrella without
  touching Buffer's targets — which reads as "TSan found nothing" when TSan never
  ran.
- **Never set `detect_leaks=1` on arm64 macOS.** LeakSanitizer is unsupported
  there, and every test aborts at exit — a whole suite reporting as failing, with
  nothing wrong. ASan itself is fine; it is only the leak detector.

---

## 5. `pp_add_test` reference

```cmake
pp_add_test(<name>
    SOURCES <files...>            # required — test .cpp + the production .cpp it needs
    [LINK <libs...>]              # extra link libs (Qt6::Qml, ${OpenCV_LIBS}, pinpoint_buffer, …)
    [INCLUDE <dirs...>]           # extra include dirs (PP_BUFFER is always added)
    [DEFINES <defs...>]           # extra compile definitions
    [COMPILE_OPTIONS <opts...>]   # e.g. -O2 for timing-sensitive tests
    [STD 17|20]                   # CXX_STANDARD for this target (default: C++20)
    [AUTOMOC]                     # the test or its sources contain Q_OBJECT / QML_ELEMENT
    [GTEST]                       # link GTest::gtest_main (gtest supplies main())
    [GTEST_NO_MAIN]               # link GTest::gtest (the test supplies its own main())
    [NO_QT])                      # do NOT auto-link Qt6::Core/Gui (e.g. OpenCV-only math)
```

Behaviour:

- By default the target links **Qt6::Core + Qt6::Gui** and adds **`PP_BUFFER`**
  to the include path (`types.h` is near-ubiquitous). Pass `NO_QT` for a pure
  OpenCV or header-only math test.
- `GTEST` / `GTEST_NO_MAIN` trigger the lazy GoogleTest fetch. Use
  `GTEST_NO_MAIN` when the test needs its own `main()` — e.g. to spin a
  `QCoreApplication` for a `Q_OBJECT` + `QTimer` class.
- Every target is registered with CTest (`add_test`) and gets sanitizer flags
  from `PP_SANITIZE`.

Examples (real, from the suites):

```cmake
# Pure header-only math.
pp_add_test(wrist_angles_test SOURCES wrist_angles_test.cpp)

# OpenCV-only, no Qt, optimised hot loop.
pp_add_test(shaft_tracker_test
    SOURCES shaft_tracker_test.cpp ${ANALYSIS}/shaft_tracker_math.cpp
    INCLUDE ${OpenCV_INCLUDE_DIRS}  LINK ${OpenCV_LIBS}
    COMPILE_OPTIONS -O2  NO_QT)

# GoogleTest + profiler core + a log stub (no whisper).
pp_add_test(pp_profiler_test
    SOURCES pp_profiler_test.cpp ${PROFILER_CORE}
    INCLUDE ${CORE}  LINK Threads::Threads
    DEFINES PINPOINT_PROFILE_BASELINE=1 PINPOINT_PROFILE=1  GTEST)

# Q_OBJECT controller with its own main().
pp_add_test(profiler_controller_test
    SOURCES profiler_controller_test.cpp ${MONITOR}/profiler_controller.cpp ${PROFILER_CORE}
    INCLUDE ${CORE} ${MONITOR}  LINK Threads::Threads
    GTEST_NO_MAIN  AUTOMOC)
```

Platform-specific bits that don't fit the keyword form (Apple `.mm` Metal
backends, `dxgi` on Windows) are applied **after** the `pp_add_test` call with
plain `target_sources` / `target_link_libraries` — see the GPU-metrics targets in
`src/Core/tests/CMakeLists.txt`.

---

## 6. Running the tests

### Everything at once (umbrella)

```bash
cmake -S tests -B build/tests
cmake --build build/tests -j6
ctest --test-dir build/tests --output-on-failure
```

### A single suite (standalone — fastest iteration)

```bash
cmake -S src/IMU/tests -B build/imu-tests
cmake --build build/imu-tests -j6
ctest --test-dir build/imu-tests --output-on-failure
```

(The Qt prefix is auto-resolved; pass `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.x/<abi>`
only if Qt is installed somewhere non-standard.)

### Via presets

`tests/CMakePresets.json` defines `tests`, `tests-asan`, `tests-tsan`. CMake
reads `CMakePresets.json` from the current directory, so run them **from
`tests/`**:

```bash
cd tests
cmake --preset tests
cmake --build --preset tests
ctest --preset tests
```

### Useful CTest filters

```bash
ctest --test-dir build/tests -R shaft        # only tests whose name matches /shaft/
ctest --test-dir build/tests -j6 -LE qml_ui  # run tests in parallel (keep the QML files in their own pass: -L qml_ui)
ctest --test-dir build/tests --rerun-failed --output-on-failure
```

### On the studio PC — `tools/studio/studio-build.cmd`

The studio PC's test tree is `build\tests-vs18` (Debug, NMake JOM, the `tests/` umbrella). Two modes of `tools/studio/studio-build.cmd` use it; the script's header has the details and the exit codes.

- **`tests`** builds the tree, then runs both passes (`-LE qml_ui`, then `-L qml_ui`), with the real stores checked around them. `tools/method/cabin_run.sh` runs it.
- **`build`** does the same steps up to and including the build, then stops: no ctest pass, no settings snapshot. `tools/method/build_times.sh` times it. With nothing changed it takes about 1–2 minutes, e.g. `studio-build.cmd build pull jobs 16`.

Both refuse while PinPoint Studio, Foresight, FSX or GSPro runs, take `patch <file>` and `jobs N` (1 to 24, default 16), and write `tests_cmake.log`, `tests_build.log` and `tests_summary.log` under `C:\PinPointStudio\scratch\studio-build\`.

### The QML UI suite — the `qml_ui` label

Each `tst_*.qml` in `src/Gui/tests/qml/` is its own ctest test, `qml_ui.tst_<file>`: the one
`qml_ui_test` binary run with `-input <that file>`, offscreen, with its own `TIMEOUT`
(`src/Gui/CMakeLists.txt`, set from the times measured on the Mac and on the studio PC's Debug build).
All of them carry the label `qml_ui`. A new file is registered at the next configure.

```bash
ctest --test-dir build/tests -L qml_ui -j3 --output-on-failure    # every QML file, in parallel
ctest --test-dir build/tests -LE qml_ui -j3                       # everything except the QML files
ctest --test-dir build/tests -R '^qml_ui\.tst_session_report$' --output-on-failure   # one file
```

Until platform refactor B0.2b (#251) the whole suite was one test, `qml_ui`, running every file in one
process, so one slow or stuck file hid every file after it. The old name maps like this:
`-R '^qml_ui$'` → `-L qml_ui`, and `-E '^qml_ui$'` → `-LE qml_ui`. The old name no longer exists, and
ctest exits 0 when `-R` matches nothing, so a script still using it silently runs no QML. Run the label
as a pass of its own, not mixed into a `-j` run of the compute-bound suites: a starved render loop is
what once made this suite flaky. The binary still runs a file directly, for its full report:
`build/tests/Gui/qmlui/qml_ui_test -platform offscreen -input src/Gui/tests/qml/tst_pp_chip.qml`
(add `-v2` for every check, `-o out.xml,junitxml` for per-function times).

**Wait for layout with `waitForPolish`, never `waitForRendering` (#254).** `waitForRendering(item)`
returns on the next rendered frame or after its timeout (5 s by default). When a change draws
nothing new, no frame comes, so the call sleeps the whole 5 s. That was 400 s of the old
`tst_session_diagnostics.qml`'s 427 s on the Mac (80 of its 974 calls). A Row, Column or ListView
lays out on polish, so a test that is about to read geometry waits for polish on the window. See
`laidOut()` in `src/Gui/tests/qml/diag/SessionDiagnosticsCase.qml`: `wait(0)`, then
`waitForPolish(item.Window.window)` while `isPolishScheduled` says one is pending, and `verify` the
result so that a layout which never settles fails rather than passing late. Wait on the window
rather than an item, because an item's own flag says nothing about the positioners inside it. The
session diagnostics tests are five files, `tst_session_diagnostics_<part>.qml`, split along the old
file's section headers. They share a harness in `src/Gui/tests/qml/diag/`:
`SessionDiagnosticsProbe.qml` holds the body, fakes, spies and fixtures, and
`SessionDiagnosticsCase.qml` holds the helpers and the one `init()`.

---

## 7. Adding a test to an existing suite

1. Drop `my_thing_test.cpp` into `src/<Sub>/tests/`.
2. Add one `pp_add_test` call to that suite's `CMakeLists.txt`, listing the test
   plus any production `.cpp` it must compile (remember: there is no library to
   link — name the sources). Reach for the right flags from §5.
3. If the production code touches `PpLogStream`, add `src/Core/pp_log_stream.cpp`
   and `src/Core/PpMessageLog.cpp` to `SOURCES` — that is the real application
   log and it costs Qt and nothing else. **Do not** pull in `pp_debug.cpp`: that
   one still drags in whisper/ggml, because it also installs the message handler
   and silences whisper, ggml, OpenCV and FFmpeg.
4. Build + run standalone, then once under the umbrella.

For reference, every suite's `CMakeLists.txt` begins with this bootstrap so it
works standalone *and* under the umbrella — you won't normally touch it:

```cmake
# Standalone bootstrap: pull in the shared module when configured on our own.
# Under the umbrella pp_add_test already exists, so this block is skipped.
if(NOT COMMAND pp_add_test)
    cmake_minimum_required(VERSION 3.16)
    project(pinpoint_<sub>_tests LANGUAGES CXX)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../tests/cmake)
    include(PinPointTests)
    enable_testing()
endif()
```

---

## 8. Adding a brand-new suite

Put tests **next to the code they exercise**, in a `tests/` subfolder of that
subsystem — `src/<NewSub>/tests/`. This keeps the relative `../foo.cpp` includes
short and the co-located stubs/fixtures obvious. (Don't create a single global
`tests/` dump of all test sources — only the *orchestration* is centralised, not
the test files.)

**A `tests/` folder does not have to mean a new suite.** `src/Diagnostics/tests/`,
`src/Export/tests/` and `src/Metrics/tests/` hold sources with no `CMakeLists.txt`
of their own — the Analysis suite compiles them by absolute path
(`SOURCES ${SRC}/Export/tests/swing_doc_test.cpp …`), because each links
`swing_analysis.h` or the pack value types and a separate project would duplicate
that whole dependency set for a handful of targets. Prefer this when the new tests
would pull in the same dependencies as an existing suite; add a real suite only
when the dependency set genuinely differs.

1. Create `src/<NewSub>/tests/CMakeLists.txt` starting with the bootstrap block
   from §7 (substitute the project name).
2. Add your `pp_add_test` calls.
3. Register the suite in the umbrella — add its name to the `foreach` list in
   `tests/CMakeLists.txt` (or, if it needs a library like Buffer, add a
   dedicated `add_subdirectory` with the same special-casing).
4. Verify standalone, then under the umbrella.
5. Add a one-line entry to the suite catalog in `BUILDING.md`.

If the new suite needs Eigen, call `pp_find_eigen(<var>)`. If it needs a
dependency the shared module doesn't `find_package` (OpenCV, Qt6::Bluetooth,
Qt6::Qml), `find_package` it in the suite — that is additive and cached.

⚠ **The QML UI suite is the one target that does not follow step 2**, and it is worth
knowing why before copying it. `pp_add_test` cannot express it — that helper uses plain
`add_executable` and hardcodes `Qt6::Core`/`Qt6::Gui`, with no hook for a test
`ENVIRONMENT` — so `qml_ui_test` is spelled out by hand. It also lives in
`src/Gui/CMakeLists.txt` rather than `src/Gui/tests/`, pulled in by `add_subdirectory`:
`qt_add_qml_module` names each file's qmlcachegen output directory after the path
*relative to `CMAKE_CURRENT_SOURCE_DIR`*, so declaring it from `tests/` yields
`qml_ui_test_../theme` — which POSIX resolves and ninja on Windows rejects outright.
`src/Gui` is an ancestor of all 157 QML files, so the paths come out clean. Anything else
declaring a QML module from a test directory will hit the same wall.

---

## 9. Conventions and gotchas

- **Test framework is per-suite, not enforced.** Buffer, Core, Update and
  LaunchMonitor use GoogleTest; Analysis, Audio, Gui, IMU and Pose use a hand-rolled
  `main()` + `CHECK`/`CHECK_NEAR` macros. Both are fine — match the suite you're
  editing.
- **`AUTOMOC` is per-target now.** The old suites set a global `CMAKE_AUTOMOC ON`;
  with `pp_add_test` you pass `AUTOMOC` on the targets that have `Q_OBJECT` /
  `QML_ELEMENT`. Forgetting it shows up as undefined-symbol link errors for the
  class's signals/vtable.
- **A `Q_OBJECT` header with no paired `.cpp` must be listed in `SOURCES`.** AUTOMOC
  moc's a header automatically when it shares a basename with a compiled `.cpp`
  (e.g. `foo.h` ↔ `foo.cpp`). A pure-interface or test-double header that has *no*
  matching `.cpp` (e.g. an abstract base, or a `FakeXxx` test double) is **not**
  picked up just by being `#include`d — list the `.h` itself in `SOURCES` so AUTOMOC
  generates its `moc_*.cpp`. Symptom otherwise: `undefined reference to vtable` /
  `staticMetaObject` for that class. (The Update suite lists `update_backend.h` and
  `fake_update_backend.h` for exactly this reason.)
- **`QSignalSpy` lives in `Qt6::Test`, not Core/Gui.** A suite that spies on signal
  emissions/ordering must `find_package(Qt6 COMPONENTS Test)` and `LINK Qt6::Test`.
- **Compiling a real settings-backed class? Make an `IsolatedStores` first.** The Update policy
  test compiles the real `AppSettings`/`SessionController` (they expose exactly the
  API the controller touches) rather than maintaining doubles. `AppSettings` reads and
  writes through `ppSettings()`, so the test's `main()` makes a
  `pinpoint::test::IsolatedStores` **before** `QCoreApplication`, and it never reads or
  writes the developer's real settings file. Test mode alone does not do that (see
  "Per-user stores" below).
- **Never link `pp_debug.cpp`** — it pulls in whisper/ggml for `PinPointDebug::install()`,
  which silences whisper, ggml, OpenCV and FFmpeg at startup. **Link
  `src/Core/pp_log_stream.cpp` + `src/Core/PpMessageLog.cpp` instead**: that is
  `ppWarn()`/`ppInfo()` itself, and it needs Qt and nothing more.

  Until 31 Aug 2026 the log primitive lived in `pp_debug.cpp`, so every suite
  linked a stub that satisfied the linker and **threw every log line away** —
  `Core/tests/pp_log_stub.cpp` (six suites), plus copies in
  `Analysis/tests/imu_test_stubs.cpp` and `Pose/tests/pose_test_stubs.cpp`. All of
  them now carry the real log, so a test can assert on a line instead of
  discarding it. `Core/tests/pp_log_stub.cpp` is kept for anything that
  deliberately wants silence; nothing in the tree needs it.
  (`Gui/tests/reanalysis_stubs.cpp` is a different thing: it stubs the re-analysis
  worker body, not logging.)
- **C++20 unless you say otherwise.** Anything pulling in `event_buffer.h` →
  `swing_window.h` (`std::span`) needs C++20 — that's the default, so it just
  works; only set `STD 17` to deliberately pin an older standard.
- **Sanitizers need their own build dir.** Don't reconfigure an existing dir with
  `PP_SANITIZE` — make `build/tests-asan` / `build/tests-tsan`.
- **Windows/MSVC:** the OpenCV-using suites (Analysis, Pose) need
  `-DOpenCV_DIR=...` at configure (the app's auto-probe isn't in the test
  projects), and CTest needs Qt's `bin` + OpenCV's `bin` on `PATH` or the test
  exes fail with `0xc0000135` (DLL not found).
- **`build/` is gitignored.** Build dirs are disposable; never commit them.
- **Never name a lambda `emit`.** It is a Qt keyword macro. The symptom is not "redefinition" but a
  wall of `left operand of comma operator has no effect` on every call site, because the macro
  expands to nothing and `push(a, b, c)` becomes a comma expression.
- **A `find()` helper returns a pointer INTO a vector — bind the vector.**
  `find(buildXSeries(...), "key")` dangles the moment the full expression ends, and the crash is a
  null deref somewhere later that looks nothing like the cause. `club_delivery_test` was written
  that way first and segfaulted; every call there now goes through a named `const auto`.
- **Make a synthetic swing look like a swing, in TIME as well as geometry.** Producers resolve their
  address reference over a ±250 ms window about the Address event. A fixture compressed into 160 ms
  puts the TOP inside that window and contaminates the very reference the metric is measured
  against — which reads as a producer bug and is not one. `body_rotation_test` builds ~1.6 s at 240
  fps for exactly this reason.
- **A decision that reads live app state cannot be tested — extract the decision.**
  Not the same as "add a fake": if a policy reads five things off four controllers, the
  testable part is the JUDGEMENT, and the untestable part is the GATHERING, which is also
  the part that is hard to get wrong. `decideStandalone()` (`src/LaunchMonitor`) takes
  seven booleans and returns a verdict; the controller only fills the struct. The same
  split is why `linux_update_logic.cpp` exists. The tell that you need it: the policy has
  already behaved differently on a real machine than it did on paper — the launch
  monitor's did that twice, once because a precondition was missing entirely and once
  because the whole path sat behind a setting that was off, and neither was a coding
  error that any amount of care in the controller would have caught.
- **Enumerate the state space when it is small enough to enumerate.** Seven booleans is
  128 states; `standalone_gate_test` walks all of them and asserts exactly one records.
  Sampling the interesting-looking ones would not have caught a precondition added later
  without a thought about ordering. Where the combination count is small, exhaustive is
  cheaper to write than choosing representatives and arguing about the choice.
- **Return a verdict, not a bool, when several things can be false at once.** Which reason
  gets reported is a decision somebody should be able to read and revisit, and it belongs
  in a test rather than in the order the `if`s happened to be written. `standalone_gate`
  orders configuration before current state so a user still setting up hears about the
  setting rather than about capture — and there is a test asserting exactly that
  precedence, plus one asserting every refusal carries a reason.
- **Pin the REAL captured bytes, not a tidied version of them.** `gcquad_csv_parser_test`
  embeds the actual FSX2020 row verbatim — trailing comma, CRLF and all — because the
  format's quirks are the thing most likely to break, and a fixture retyped into what the
  format "obviously" is tests the author's belief rather than the device. The same reason
  `fake_shot.py` copies the exemplar's header byte for byte instead of composing one.
- **Assert SIGNS on synthetic tracks, accuracy on a corpus.** A sign is the one thing a synthetic
  fixture can pin exactly, and it is also the thing that silently grades every swing backwards when
  it is wrong. `upper_body_metrics_test` runs its whole sign suite twice — once normally, once
  through a MIRRORED camera with a left-handed golfer — because a convention that only holds for a
  right-hander filmed from one side is not a convention.

---

### The session-setup UI suite — `session_setup_ui`

`session_setup_ui_test` (declared beside `qml_ui_test` in `src/Gui/CMakeLists.txt`; tests, fakes and drivers in `src/Gui/tests/setup/`) loads the real session-setup and calibration QML over fake cameras, sensors and a fake HackMotion device. `session_setup_lint_test` reads the same sources statically. Design and results: `docs/design/session_wizard_refactor_design.md` §7 and §11.

- **Opt-in — not in the full suite or the release gate (2026-10-17).** It passes on an idle machine but crashes in Qt's render path on a loaded one: on Windows at the v0.1-beta5 gate, and on Linux the same week (offscreen + RHI got a GL 2.0 context — 43 101 "No GLSL shader code found" warnings, enough to crash QTest's failure formatter — then a render-thread race in `QQuickWindow::afterFrameEnd`). Configure with `-DPP_SESSION_SETUP_UI_TEST=ON` to build and register it. `session_setup_lint_test` still reads its sources and stays in the gate.
- **Run:** `cmake -S tests -B build/tests -DPP_SESSION_SETUP_UI_TEST=ON`, then `ctest --test-dir build/tests -R '^session_setup'` (about nine minutes, on an otherwise idle machine). One file by hand:
  `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi QSG_USE_SIMPLE_ANIMATION_DRIVER=1 build/tests/Gui/qmlui/session_setup_ui_test -input src/Gui/tests/setup/<file>.qml` (`windows` instead of `offscreen` on Windows). Test QML is read from the source tree at run time; production QML is compiled in.
- **It cannot make a sound.** The real `TingPlayer` is filtered out of the target and `tests/setup/silent_ting_player.h` takes its QML name. Keep it that way: the suite completes dozens of calibrations.
- **Offscreen draws nothing.** Behaviour cases run headless; the render cases in `tst_setup_guide_render.qml` skip there and need `QT_QPA_PLATFORM=cocoa` (or `windows`) on a visible desktop. `SETUP_GUIDE_GRABS=<dir>` saves the frames.
- **Every case fails on an unexpected QML warning** (`testLog`). A binding that breaks at run time has no build error; this is what catches it.
- **Never construct `AppSettings` in a test before the suite's `IsolatedStores` (or a `QSettings::setPath(IniFormat, UserScope, <scratch>)`).** `QStandardPaths::setTestModeEnabled` does not move the app's ini, and the constructor now writes (the one-time `imu/placement` → `imu/roles` migration). `qml_ui_test`'s set-up object makes one as its first member.
- **Each test file gets its own settings store (2026-11-11, #171), in this harness and in `qml_ui_test`.** Quick Test gives every `tst_*.qml` its own engine, so `Theme` starts fresh, but a shared ini did not: `Theme` writes the theme, density and the like back through `appSettings`, the fakes write `imuRoles`, and the next file read them, so a test could pass in the full run and fail alone. `qmlEngineAvailable()` now points QSettings at `<scratch>/file<N>` per file and stops if the scratch directory is invalid. A file that changes the theme or a setting still puts it back in `cleanup()`, so tests within one file stay independent, and a test that needs a theme sets it.
- **On Windows it runs in a real window** (`QT_QPA_PLATFORM=windows`, set by the ctest environment; by hand use `windows` in the line above). Offscreen + RHI has no working Quick 3D on Windows (2026-10-08): D3D11 cannot create a swapchain for an offscreen window ("Access is denied", then a warning every frame) and OpenGL aborts. The suite's window shows on screen while it runs — keep it unoccluded, or its render loop and Timers starve.
- **Every test window holds a `View3DKeepAlive`** (in `support/CalibDriver.qml`, `support/SetupDriver.qml` and the files that use neither). Qt 6.11.0's DEBUG Quick 3D crashes the render thread when a window's LAST View3D is destroyed — every time, on a plain View3D unloaded from a Loader; Qt's release libraries survive it. That was the suite's crash in the HackMotion calibration file (`QSSGRenderCamera::markDirty` + `0xfeeefeee`). The app holds one in each top-level window for the same reason; `src/Gui/viz/View3DKeepAlive.qml` has the bisection, `src/Gui/tests/probes/view3d_unload.qml` checks the app.
- **The test target embeds the shaders the app's QML loads** (view gain, topo background, Bayer demosaic) at the app's resource paths. A missing one is an "Empty shader passed to graphics pipeline" warning on a real D3D11 window — and a failed case.

### Per-user stores — `IsolatedStores` and `store_isolation_lint_test`

Every suite that can reach a per-user store makes a `pinpoint::test::IsolatedStores` (`src/Core/tests/isolated_stores.h`, header-only; #187) before it touches one, and keeps it alive for the whole run:

- **A `main()` suite:** the first line of `main()`, before `QCoreApplication`: `pinpoint::test::IsolatedStores stores;`
- **A QTest suite:** the first member of the test class, so it is made before every other member and before `initTestCase`.
- **A gtest suite:** the first line of its own `main()`; with `gtest_main`, a file-scope `static pinpoint::test::IsolatedStores g_stores;`.
- **Include it by relative path**, e.g. `#include "../../Core/tests/isolated_stores.h"` from `src/<Dir>/tests/`. `pp_add_test` adds no include directory for it, so adding it rebuilds only the suite's own file.

What it does, against one fresh `QTemporaryDir` per process, so two runs at once or two build trees never share a file:

- **The settings.** `QSettings::setPath` for `IniFormat` and `NativeFormat`, `UserScope` and `SystemScope`. `ppSettings()` (the settings and the phone pairings) is then `<tmp>/settings/PinPointStudio/PinPointStudio.ini` on every platform. Qt honours `NativeFormat`'s path only on Linux; no product code uses it.
- **Test mode.** `QStandardPaths::setTestModeEnabled(true)`. Qt cannot point the standard locations into a temporary directory on macOS or Windows; only Linux honours `XDG_*`. So they move to Qt's test folders: `~/.qttest/…` on macOS and Linux, `%APPDATA%\qttest\…` and `%LOCALAPPDATA%\qttest\…` on Windows. Those are off the real profile but shared by every run of the same suite. On macOS, Home, Documents and the Desktop move too (`~/.qttest`); on Windows they do not.
- **The app's own folders**, through their seams:
  - `PINPOINT_DIAGNOSTICS_DIR=<tmp>/diagnostics` is `diagnosticsDataDir()`: every user pack, norm set, screen, drill, gate-word and reference layer.
  - `PINPOINT_STUDIO_DIR=<tmp>/studio` is the studio profile's folder.
  - A suite that sets either afterwards wins (`report_studio_test`).
- **The library.** `libraryRoot()` is `<tmp>/library`, for a suite to hand to a store. The stores' no-root fallback (`AppDataLocation/swings`) has no seam; under the helper it is the test folder.
- **Checking a path.** `contains(path)` says whether a path lies inside the temporary directory. Both sides are canonicalised (macOS `/var` and `/private/var`, Windows case), and a path not yet written counts too.
- **No restore.** It never restores anything when it is destroyed. If no temporary directory can be made, it stops the run (`qFatal`) rather than fall back to a real store.

A suite that wants a finer store of its own (one settings file per test, `ppcp_*`, `athlete_repository_test`, `qml_ui_test`'s one per QML file) still makes the helper first, then calls `QSettings::setPath` again: the later path wins.

**`isolated_stores_test`** (Core suite) proves the redirections through the app's own path helpers, so a renamed seam fails there. With the helper on:

- `ppSettings().fileName()`, a value written through it, `diagnosticsDataDir()`, `StudioProfile::studioDir()` and `libraryRoot()` lie inside the temporary directory;
- the app-data, cache and config locations, and the library's fallback, are not the real ones.

It prints where Home, Documents and the Desktop point.

**`store_isolation_lint_test`** (registered by the umbrella, `tests/CMakeLists.txt`) stops a new suite forgetting it:

- **The rule:** a suite whose target compiles a source that calls `ppSettings()` must make an `IsolatedStores`, or the lint fails and names the suite and the files that reach the settings.
- **The manifest:** at the end of every configure, the umbrella writes `store_isolation_manifest.txt`, with each executable target's sources and every test name. It is plain CMake, so it works under Ninja and NMake JOM.
- **Comments and strings don't count.** The sources are read at run time with them blanked. The rule runs on fixtures first.
- **What it does not see:**
  - an inline `ppSettings()` in a header the target does not list;
  - the two tests that drive a tool through `cmake -P` (`athlete_dump_ids_test`, `regrade_ledger_new_needs_ini_test`). Their tools read the real settings by design, so the studio PC still excludes them;
  - code that reaches only `QStandardPaths` (the pack readers). That is a folder named after the test binary, never the app's;
  - **a call in a linked library.** It scans only each executable's own listed sources, and follows no `target_link_libraries`. Clean today, because no in-tree library is linked into a suite; it stops being sound when Stage B Part 2 makes the rings libraries (#258);
  - **a suite whose test has another name** than its target, or `<target less _test>.<file>`. The opt-in `session_setup_ui_test` reaches the settings and is unchecked (its settings are isolated by its own `setPath`, but test mode is not on);
  - **the order.** It checks that an `IsolatedStores` appears, not that it comes first. The review of 24 Nov 2026 checked all 67 by hand: 46 as `main()`'s first line, 18 as a class's first member, 3 as a file-scope static.
- **Why a lint and not a hash:** a hash of the real store around the run cannot see a read, which is what kept 45 suites off the studio PC, and it cannot say which suite changed the store.

### The packs and norms — `RULE_DATA_SOURCES` and `installProcessRuleData()`

Since refactor step 3.2 the packs, norms and content sets have one owner, `RuleData`
(`src/DomainModel/rule_data.h`), and one door to their files, `RuleDataStore`
(`src/Export/rule_data_store.h`). Nothing assembles a private pack any more: every reader takes
`RuleData::current()`'s snapshot, directly or through the interim accessors
(`makeCharacteristicPackProvider()`, `sharedDrillSet()`, …). So a test that reads them:

- links `${RULE_DATA_SOURCES}` (defined in `src/Analysis/tests/CMakeLists.txt` and `src/Gui/tests/CMakeLists.txt`)
  and is `AUTOMOC` (`RuleData` is a QObject);
- calls `pinpoint::installProcessRuleData()` (`src/Export/rule_data_install.h`) on the main thread, after it
  has set its `PINPOINT_CORE_*` seams and moved its data location (`QStandardPaths::setTestModeEnabled`,
  `setApplicationName`), and before anything reads. With none installed every accessor answers from an
  empty snapshot;
- after changing a seam or a data location mid-run, calls `installProcessRuleData().reload(parts)`: one
  snapshot per change, so a test that swaps the core pack is a change RuleData must be told about;
- likewise after removing or hand-writing a user file (`user.json`, `user.norms.json`, …) once RuleData is
  installed: since step 3.9 the pack and corridor editor (`ModelBrowser`) reads RuleData's snapshot, never
  the files, so `model_browser_test` and `inspector_refresh_test` call `RuleData::installed()->reload()`
  after clearing their scratch files.

A probe of the running app keeps its saves off the real user files with `PINPOINT_DIAGNOSTICS_DIR`
(`docs/developer/feature_switches_developer_guide.md`); on macOS the app data location ignores
`XDG_DATA_HOME`.

A save through RuleData's commands publishes at once (EVT-12 / EVT-13); there is no "written but not yet
reset" state. `shared_norm_provider_test` deliberately installs none: it is the headless norm slot's test.
`rule_data_test` is the owner's own.

### The headless start for probes — `PINPOINT_HEADLESS_PROBE=1` and `headless_probe_test`

**Every agent probe uses `PINPOINT_HEADLESS_PROBE=1`, and every probe launch goes through `tools/probe/run_headless.sh`** (the one exception is `headless_probe_test`, below, which builds its own scratch and passes each variable as a separate argument). Environment assignments typed before a command can reach the app as one word or not at all, and the app then starts for real against the real home, settings and library, so the launcher builds the environment itself, refuses a home, settings file or library outside scratch, and kills within a second an app whose process lacks the switch.

    tools/probe/run_headless.sh --app <PinPointStudio> --home <scratch>/home \
      --settings <scratch>/cfg/PinPointStudio/PinPointStudio.ini --probe <abs path to the probe> [--timeout 120] \
      [--env PINPOINT_<seam>=<value>]... -- [app args]

- **Refused (exit 2, nothing launched)** unless `--home`, `--settings`, `--log` and the settings file's `[%General]` `athleteLibraryPath` are strictly beneath `/mnt/swingdata/scratch/`, the repository's `build/`, or the session scratchpad `/private/tmp/claude-<uid>/`, after symlinks are resolved (a `..` component or whitespace is refused; so are `/mnt/swingdata` itself, an athlete folder, and any other setting pointing into the share). The app reads its settings as `$XDG_CONFIG_HOME/PinPointStudio/PinPointStudio.ini`, so `--settings` must be named that way; the launcher sets `XDG_CONFIG_HOME` to the folder above it. A binary named PinPointStudio that lacks the switch's log text is refused too.
- **The environment** is passed through `/usr/bin/env -i`, one argument each: `PINPOINT_HEADLESS_PROBE=1`, `QT_QPA_PLATFORM=offscreen`, `PINPOINT_LOG_STDERR=1`, `HOME` and `CFFIXED_USER_HOME` (the scratch home), `XDG_CONFIG_HOME`, `PINPOINT_DIAGNOSTICS_DIR=<home>/diagnostics`, `TMPDIR=<home>/tmp`, a system `PATH`, `LANG`, and each `--env` (`PINPOINT_*` names only, for seams such as `PINPOINT_WORKONS_DIR`; a path value must be scratch or inside the repository).
- **The check (exit 3).** Within a second of the launch the running process's own environment is read (macOS `ps -E`, Linux `/proc/<pid>/environ`); without `PINPOINT_HEADLESS_PROBE=1` and the scratch `HOME`, or if it cannot be read, the process is killed with SIGKILL.
- **The run.** stderr is teed to a log whose path is printed first (default `<home>/run_headless-<time>-<pid>.log`); `--timeout` (default 120 s) stops the app and exits 124; otherwise the app's exit code passes through.
- **`run_headless_selftest`** (ctest, Gui suite; `tools/probe/run_headless_selftest.sh`, about 10 s) proves the refusals, the kill path (through the `--_test_drop_switch` and `--_test_wrong_home` hooks, which the launcher refuses for the real app), the exact environment, the exit code and the timeout, with a stand-in binary it compiles. It never launches the app.

A plain launch of PinPoint Studio, offscreen included, starts the device side at once, and on macOS that raises the Bluetooth, Speech Recognition and Microphone permission prompts on whoever's screen the session is on. With `PINPOINT_HEADLESS_PROBE=1` the app starts with none of it (refactor 4-c; `src/Core/headless_probe.h`):

- **Not started**, each said once in the app log as `[HeadlessProbe] not started: <part>`: Bluetooth (the IMU manager's adapter list and scan), the microphone and speech recognition (the transcription controller's permissions and threads), speech output (the TTS controller's speaker, worker and voice), the cameras (the camera manager's device list), the launch monitor (its connector: `Kind::None`), the phone link (the PPCP host's listener, discovery and advertising), the pose model's launch warm-up and the pose timing benchmark.
- **The controllers QML binds to are still made**, inert, so every screen loads and reads empty device lists.
- **A backstop below them.** The macOS permission requests and the Bluetooth adapter list and scan refuse under the switch and log `[HeadlessProbe] REFUSED <call>`, so a path the switch missed raises no prompt either.
- **Only the exact value `1` turns it on.** Unset, or any other value, it changes nothing. A golfer never sets it.

What the launcher sets up, by hand (for reading, not for launching): a probe run, with its saves kept off the real files (`HOME`, `CFFIXED_USER_HOME` and `XDG_CONFIG_HOME` at a scratch home whose `PinPointStudio/PinPointStudio.ini` points `athleteLibraryPath` (under `[%General]`: AppSettings' `General/` keys) at a scratch library, and `PINPOINT_DIAGNOSTICS_DIR`, above):

    PINPOINT_HEADLESS_PROBE=1 QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
      HOME=<scratch> CFFIXED_USER_HOME=<scratch> XDG_CONFIG_HOME=<scratch>/.config PINPOINT_DIAGNOSTICS_DIR=<scratch>/diag \
      <PinPointStudio> --probe-qml <abs path to the probe>

**`headless_probe_test`** (ctest, Gui suite; `src/Gui/tests/headless_probe_check.cmake`) launches the app binary that way with `src/Gui/tests/probes/headless_start.qml`, which visits every screen and Settings panel and exits. It fails on a missing "not started" line, any `REFUSED` line, a QML `ReferenceError` or `TypeError`, or, on macOS, a `display_prompt` in `tccd`'s log over the run, or a `tccd` log that cannot be read. The binary is `-DPP_APP_BINARY=<path>` at configure, else the first that exists of the app tree beside the tests tree (a worktree's `build/app`) and the main tree's Qt Creator build (`build/Qt_*_for_macOS_Debug`); configure prints which. A missing binary, or one built before the switch (the script looks for the switch's own log text in it first), **fails the test and is never launched**. The only skip is an explicit `-DPP_APP_BINARY=OFF`, which ctest reports as Not Run (exit 77), never as passed. About 35 s. A shell launch is attributed to the terminal by TCC, so the test proves no permission is asked for, not how a granted app behaves. `src/Gui/tests/probes/app_state_on_screen.qml` (refactor 4-c) drives 4-a's application state the same way over a scratch library: a pick kept across a reload of the same session and a return to the session screen, the Consistency glance following the selection, the Motion popup back on Presets on a switch to Capture, and an empty session's End with no "not being saved" notice. It needs the stage panel in the Replay layout (the default layouts have none: copy a `[view]` section into the scratch settings).

### The architecture lint — `architecture_lint_test`

`src/Gui/tests/architecture_lint_test.cpp` is the guard for the refactor (`docs/implementation/swe-audit/03-target-architecture.md` §2 and §11). It is shaped like `design_system_lint_test`: it reads the source tree at run time (every C++ and QML/JS file under `src/`, skipping `tests/`, `Testing/`, `third_party/` and `build/`), so it needs no rebuild when the code changes, and it is plain C++ on Qt Core, so it runs on every builder without Python.

- **The rules.** A1, an `#include` from an inner ring into an outer one (Platform · Vocabulary · Rules · DomainModel · Application · Adapters), and, since step 2's S1-g (DEC-146), an Adapters-ring file outside `src/Gui` (or a `src/Recording` file) including a `src/Gui` header. A2, QML creating a session- or athlete-scoped C++ type. A3, folder-name parsing outside `SwingPaths` and `SessionIdentity` (`src/DomainModel/session_identity.*`, step 2.1). A4, a session or athlete file name spelled outside its owner — every FILE-SESS and FILE-ATH name of the information model (§8.1), owned by `src/Export/session_files.h` or `src/Export/athlete_files.h` (step 2.8b: `session.json`, `session_input.json`, `diagnostics.json`, `club_baseline.json`, `work_ons.json`, `archived.json`, the report's `report_meta.json`, `*_session-report.pdf` and `report-stills/`, `.pinpoint-last-swing`, `trunk_ceremony*.json`; `athlete.json`, `tendencies.json`, `swing_themes*.json`, `outcome_corrections.jsonl`; `fault_profile.json` left the list when step 3.8-ii retired it); a writer elsewhere calls the owner's name function. A5, P11 (DEC-161; widened in S1-g from the rules directories): file or directory access (`QFile`, `QSaveFile`, `QDir`, `QDirIterator`, `QFileInfo`, `QTemporaryFile`, `std::fstream`, `fopen`, `atomicWrite`, `FolderLock`) or an include of a repository (the `DomainModel` interfaces and the stores and file doors of `src/Export`) anywhere outside `src/Export` and the Platform ring; `src/DomainModel` may include its repositories. Path text is not counted (`QDir::cleanPath`, a temporary `QFileInfo(p).fileName()`, …), nor an `#include <QFile>` line. A6, a `Q_INVOKABLE`, `Q_PROPERTY`, public slot or signal with a `QVariantMap`/`QVariantList` in any header. A7, a function over 300 lines in the Vocabulary and Rules rings. A8, `src/Analysis` including the ring buffer, device classes or app state. The header comment holds the details and the known limits; the ring table (directory defaults plus a per-file list for the directories that straddle) is in the test.
- **The allow-list.** `src/Gui/tests/architecture_allowlist.txt` holds today's violations, one line per file and rule: `path | rule | count | WHY  # step N`. The tag is the refactor step that removes the entry (`# step 1` … `# step 8`, or a sub-step such as `# step 3.0`; `# platform refactor (capture)` or `# platform refactor (settings)` for the refactor after step 8, 05 §1 / §2; `# not domain (§8.3)` for a file the information model classes as not domain, kept allowlisted by the Solution Architect's answer; `# unplanned` if nothing places it yet, which is a question for the Solution Architect; `# kept` or `# kept (reason)` if it is deliberate). A file over its count fails. A file under its count is printed as a `note:` so the count can be lowered in the same change. The test ends with the size of today's debt by step.
- **Never add a line to make it pass.** A new entry is an architecture decision for the Solution Architect, exactly as for the design-system allow-list. When a refactor step removes a violation, lower or delete its line in the same commit.
- **A new directory under `src/` fails the test** until it is given a ring in `dirRings()`. That forces the choice to be made on purpose.
- **Run:** `cmake --build build/tests --target architecture_lint_test -j 6`, then `ctest --test-dir build/tests -R '^architecture_lint_test$' --output-on-failure` (about 2 s).

### The post-shot bench — `post_shot_bench`, and reading `[PostShot]` and `[GpuMem]`

Two app-log lines and one headless tool measure what happens after a live shot (refactor 3.7-S and 3.7-I; `build/refactor/step-3/3.7/NOTES-S.md`, `NOTES-G.md`). Both lines are `ppInfo`, so they reach the one app log (`PINPOINT_LOG_STDERR=1` echoes it), and both are always on: they change no behaviour and write nothing else.

- **`[PostShot]` (`src/Core/post_shot_trace.h`).** One line per live shot: what the GUI thread spent, step by step, from `ShotProcessor::onSwingDocWritten` until the next shot lands or 4 s pass with no timed step. A step is a `PostShotTrace::Scope("name")`. It records only on the thread that opened the window and only while it is open; anywhere else (a worker, a tool, a test, Home) it costs two relaxed atomic loads. A window keeps at most 256 steps (the line ends `(+N steps not kept)`). A window the next shot's `begin()` closes while a step still runs is written, ending `(window cut: N steps still running)`, never dropped. **Pinned by `post_shot_trace_test`** (`src/Core/tests/`, ctest): the format (nesting, `timed`, one decimal under 10), the thread gate, the cap, the quiet end and the cut.
  - **Reading it.** `[PostShot] <shot> span S ms, timed T ms: join … (evt01, gpumem, notify, addShot, announce (lm.ball (lm.write (lock, doc (load, save)), refresh (read, review, emit)))), lm.club (lm.write …, refresh …, lm.rewritten …), owner, settle (rebuild (emit), persist (read, lock, json, commit), ingested), charts`. Nested steps are in parentheses; a step repeated in a window appears once per run. `span` runs from the window's start to the end of its last step, `timed` is the sum of the top-level steps, and **`span − timed` is GUI-thread time no scope names** (QML polish and layout, other events).
  - **Where each name is.** `shot_processor.cpp` (`join`, `evt01`, `gpumem`, `notify`, `addShot`, `announce`); `launch_monitor_controller.cpp` (`lm.ball`, `lm.club`, `lm.write`, `lm.applied`, `lm.rewritten`); `swing_store_repository.cpp` (`lock`, `doc`); `swing_doc_writer.cpp` (`load`, `save`); `shot_list_model.cpp` (`refresh`, `read`, `review`, `emit`); `session_diagnostics_model.cpp` (`settle`, `rebuild`, `emit`, `persist`, `ingested`); `session_diagnostics_io.h` (`read`, `lock`, `json`); `atomic_write.h` (`commit`); `session_diagnostics.cpp` (`owner`); `session_series_model.cpp` (`charts`). The steps that run QML synchronously inside an emit are `notify`, `rebuild/emit`, `refresh/emit`, `lm.rewritten`, `ingested` and `charts`.
  - **No `hook` step any more.** The parallel run's comparison hook and its switch (`PINPOINT_DIAG_PARALLEL=1`) were deleted in refactor 3.10: since 3.7-e no old model was left in the app to compare, and refactor 5.4b deleted the old engine. Parity is `regrade_ledger --dump` against a baseline (`step5c-baseline`).
- **`[GpuMem]` (`ProfilerController::logGpuMem`, `src/Gui/monitor/profiler_controller.cpp`).** One line every 30 s from the profiler's 1 s sampler (`at=tick`), one at each session start (`at=session-start`, written before the profiler's reset, so its peak is the window just closed), one at each session end (`at=session-end`), and one at each shot's EVT-01 (`at=EVT-01 shot=<ref>`: `ShotProcessor::shotDocumentAnnounced`, a relay main.cpp wires to the logger; timed as `gpumem` in that shot's `[PostShot]` line):
  `[GpuMem] at=tick dedicated_mb=1712 shared_mb=128 peak_dedicated_mb=1712 private_mb=4450 working_set_mb=3900 source=DXGI`
  - `dedicated_mb` is this process's GPU memory (Windows: DXGI's local segment, the same figure as typeperf's `\GPU Process Memory(pid_*)\Dedicated Usage` within a few MB; macOS: Metal's `currentAllocatedSize`, unified memory, `source=Metal(unified)`). `shared_mb` is DXGI's non-local segment, expected to equal typeperf's `Shared Usage` (not yet measured; the 3.7 combined cabin run checks it); `-` off Windows. `peak_dedicated_mb` is the high-water since the last session start. `private_mb` and `working_set_mb` are `osmetrics::processMemory()` (Windows `PrivateUsage` and `WorkingSetSize`; macOS `phys_footprint` and resident size). A figure the platform cannot give is `-`, never 0. The text is `ProfilerController::gpuMemLine()` (pure), pinned by `profiler_controller_test` (DXGI, Metal unified, unavailable); `osmetrics::processMemory()` by `pp_os_metrics_test`.
  - **Cost.** Two DXGI queries and one `GetProcessMemoryInfo`, microseconds, so it is not behind a switch. The sampler runs only when the profiler's baseline tier is compiled in (`PINPOINT_PROFILE_BASELINE`, on by default); the session and EVT-01 lines do not depend on it.
  - **Reading it.** NOTES-G's ORT bench puts PinPoint at about 1.4 GB between sessions and 1.7 GB inside one, with ORT alone. A climb per session or per shot is the window's (D3D11/Qt Quick) side; flat but high is its steady state.
- **`post_shot_bench` (`tools/diagnostics/post_shot_bench.cpp`).** The post-shot GUI path, headless, for N shots of one session on a scratch library. It uses main.cpp's composition: the store, the swing door and reader, an Athlete, `app`, the carousel's `ShotListModel` and the panel's thin adapter over the session's owner, with the app's doors, as the app has run since 3.7-a. It has no other path[^r53-bench]. Each shot follows the cabin's order: EVT-01, the replay detail converted to JS once, the row, the ball reading through `app.completeCapture` and the refreshes, then the previous shot's club data. It prints each shot's `[PostShot]` line and a table of every step's cost against the shot count (shot 1, the mean of shots 2–6, the mean of the last five, the slope in ms per shot). It reads each panel property and the card's roles once inside the emits (`qml.panel`, `qml.row`) as a floor for QML; the real QML costs more. Its line differs from the app's: it prints `notify.js` (the JS conversion alone) where the app prints `notify` (the emit and its bindings), it adds the `qml.*` floors, and it has no `gpumem` and no `charts` step.
  - **Build and run.** `cmake --build build/tests --target post_shot_bench -j 6`, then `post_shot_bench --fixtures <folder of swing.ppsw copies> --library <empty scratch folder> [--shots 40] [--view 1|0] [--cards 12] [--csv out.csv] [--quiet] [--gc]`. `--cards N` (3.7-c) reads the pip rows of the newest N carousel cards on each of the panel's `surfaceChanged`, as `PpShotCard` does (`qml.pips`, inside the emit of the model they read: the panel's with `--view 1`; with `--view 0` the session's cards adapter, under `owner/pips`). After the run it prints J-46's check: each late reading's row as the controller's one refresh left it, compared with the same row after a second refresh. Fixtures are real capture documents copied into a scratch folder (NOTES-S used the ten `swing_000N/swing.ppsw` of 20 Nov's `Wrist_01`); they are rotated over the shots.
  - **Not a ctest.** It times rather than checks, and it writes the library it is given, so it refuses a `--library` that is not empty or new. It sets `QStandardPaths` test mode and the eight `PINPOINT_CORE_*` variables to the shipped content when they are unset. It drives no QML, no cameras, no analysis and no GPU.

### Parity dumps — `regrade_ledger --dump` and `athlete_dump`

The refactor's parity baseline (`build/refactor/step-G/BRIEF.md` §2) records what today's code produces so each later step can show it changed nothing it did not mean to. Two headless tools write the diagnostics and athlete halves of it. Both are built in the tests tree (`src/Analysis/tests/CMakeLists.txt`), are **not** registered with ctest because they write into the folders they are given, and share one canonical JSON writer, `tools/diagnostics/parity_json.h`: keys sorted, numbers `%.17g`, NaN as null, no timestamps or cache fingerprints, folders as `<session>` / `<athlete>`. Two runs on the same data diff byte for byte, even from two scratch copies at different paths.

- **The Charts series, old against new, `charts_series_dump [--out <json>] <session-dir>…`** (refactor 5.5a; tests tree, not a ctest: it holds each session, whose owner writes, so it runs on a scratch copy only). For each session it dumps, in one canonical form, the snapshot the Charts tabs read two ways: the session's own series (`SessionSeries`) after its `SessionDiagnostics` has judged and written, and the view-owned loader it replaced reading the file that owner wrote. Every field of every shot is compared and each difference printed, classed against the list written before the run (`build/refactor/step-5/DUTIES-5.5.md` §D, D1–D5); exit 1 on any other. The old loader is kept for it in the tests tree as a test double (`FolderSeries`, `src/Gui/tests/folder_series.h`).
- **Session level, `regrade_ledger --dump <out.json> <session-dir>…`.** It regrades each session through its own `SessionDiagnostics` (`--engine new`, the only engine)[^r53-regrade]; `--engine old` was removed with the old engine in refactor 5.4b and is refused. The engine must tell the golfers apart when each session's club baseline is frozen. It reads each session's athlete from its athlete folder's `athlete.json` (FILE-ATH-01), through `AthleteRepository` (since refactor 4-c, J-61); a folder with no `athlete.json` from `athletes.json` beside the session (`pinpoint.athletes/1`; the corpus's is `corpus/swings/athletes.json`, matched by the golfer's name in the session folder's name, and such a session freezes no club baseline: "club baseline not frozen: corpus run"; an entry's optional `aliases` are other ids the golfer's swings record, and a session whose swings record one is said to be the same golfer, while an id the file does not list is said too, not refused[^r53c]); `--athletes-ini <a copy of the app's settings>` only as the last fallback, for a library M1 has not migrated. **A session whose athlete no source names is refused** (SKIP, exit 1, nothing written), so no golfer is pooled into another's baseline (`regrade_ledger_new_needs_ini_test`, 3.10 review B1, which also checks the new default and two golfers in one folder). It writes, per session, the parallel run's form P (`src/Gui/diagnostics/diag_parallel_run.h`, `dumpVersion` 2): the stage, the pattern set, every condition's ledger figures, every shot's rows, its outcome's facts and three factors, the shot and club mixes, carry today, the reasons, the work-ons and the usuals, in ids and numbers (no words, no view state). Until 3.10 a plain regrade dumped the old engine's form v1 (rows with every field, the composed outcome's words, the card order, the coverage line); a v1 baseline does not compare with a dump from 3.10 on. The first new-engine baseline is `step3.10-baseline`; `step5-baseline` (5.3c) was the first on the migrated library (`build/refactor/step-5/5.3c/BASELINE.md`)[^r53c]; the reference since 5.5c is `step5c-baseline`, whose summaries carry `dayReadings` (`build/refactor/step-5/5.5c/BASELINE.md`).
- **Athlete level, `athlete_dump --fresh --out <out.json> <athlete-dir>…`.** By default (`--engine new`) it runs the catch-up service and the new owners, described below; `--engine old` runs the app's work-ons catch-up as Home did (`WorkOnsController` over the folder's `AthleteDiagnostics`, synchronous; since 3.7-f it also dumps the consistency marks and the scoped views — the first club group, the first club and the first direction filter — and `--bench-themes` times Home's swing summary on the old fan-out and on the owner's lane) and writes `tendencies.json`, the club distances and each session's `work_ons.json` and `club_baseline.json`. `--fresh` deletes those caches first so this build derives them again. The tool refuses any folder without a `scratch` component in its real path. Under either engine each athlete's id is the one its sessions' `session.json` files name, read through the session repository (refused, before anything is deleted, when they name more than one; only a folder with no session file keeps an id made from its name), and a folder holding sessions that leaves 0 counted fails the run with words (`athlete_dump_ids_test`)[^r53c]. Step 3.6 added `--engine new`, the default since refactor 5.3[^r53-regrade] (`--engine old` is `WorkOnsController`, Home's path before 3.7-f): the catch-up service and the new owners (`SessionDiagnostics` headless, every shot fired again, each club baseline frozen from the predecessors' summaries; `AthleteDiagnostics` for the tendencies, distances, themes and athlete work-ons, from the summaries' compact per-shot facts). Both engines' dumps carry `themes`, `athleteWorkOns` and each session's `boardAnnotations`; the old engine's carries `doWell` (`themes` before the praise rule, `praised` after).
- **Content.** Both need all eight `PINPOINT_CORE_*` variables (PACK, NORMS, FLIGHT_NORMS, CONTEXTS, SCREENS, DRILLS, GATE_WORDS, REFERENCES) set to `src/Resources/diagnostics/*.json`. The tools carry no Qt resources, so without the flight table no flight would be read. Each dump records the SHA-1 of every content file.
- **Order for a parity run, and the corpus sweep.** Copy the sessions under `/mnt/swingdata/scratch/` (`cp -R`, never the live library or the corpus). Copy each athlete folder's `athlete.json` with them; a copy of `corpus/swings` carries its `athletes.json`. Then run `regrade_ledger --tag parity --dump` (the new engine, the default) on every session (with `--athletes-ini <a copy of the settings>` only for a library not yet migrated), and then `athlete_dump --fresh` on each athlete folder. `--help` on either tool gives the JSON shape.
- **The writer is pinned** by `parity_json_test` (ctest): sorted keys, full precision, run keys dropped and folders tokenised. `club_baseline.json`'s `rewrites[].at` (when a rewrite ran) is dropped from each rewrite only, as `frozenAt` is everywhere; an `at` anywhere else is kept (refactor 3.10).

### The step-2 migrations — `migrate_step2_test` and `pps_migrate_step2`

`pps_migrate_step2` (`tools/migrate/`, refactor step 2.8a) runs the four step-2 data migrations M1–M4: the athlete record from the settings into `athlete.json`, a `SessionId` and `session.json` for every old session, the golfer's input out of `diagnostics.json`, and the Wrist ★ onto the athlete's record. The tool's header (`tools/migrate/pps_migrate_step2.cpp`) has the whole story. It is a dry run by default. `--write` refuses a library that is not under `/mnt/swingdata/scratch/` unless the Solution Architect's approval flag is given.

- **The tool** is built in the tools tree (root `CMakeLists.txt`, `PINPOINT_BUILD_STORAGE_TOOLS`): `cmake --build build/tools-parity-ninja --target pps_migrate_step2 -j 6`. On the cabin PC, `tools/studio/studio-build.cmd migrate` builds it in the same tree. That is a mode of its own, so a failure there never fails `tool` or `both`.
- **The test** is in the tests tree (Analysis suite): `cmake --build build/tests --target migrate_step2_test -j 6`, then `ctest --test-dir build/tests -R '^migrate_step2_test$' --output-on-failure` (under a second). It compiles the tool's engine (`tools/migrate/migrate_step2.*`) and drives it on a fixture library in a `QTemporaryDir`. The fixture's settings file is its own, and `QSettings::setPath` points `ppSettings()` at it, so the real ones are never reached. The fixture holds legacy sessions (two on one day whose name order is not their start order, one whose name and shots disagree on the type, one with no swing), a renamed athlete, two uuids in one folder, two machines' records, an app-backfilled `session.json` (J-19), golfer input in the older keys (one entry for a shot that is gone), golfer input only a backup holds, and the ★ setting.
- **What it checks.** The dry-run plan, before and after the Solution Architect's decisions. That a dry run writes nothing: every file of the fixture is hashed before and after. Every athlete-id decision is honoured or refused (this machine's id, the other machine's, or a uuid the folder's swings record), and M1 blocks when no swing records the id it would settle (J-58). The guards: a write outside the scratch roots, a held folder lock, the app running and backups inside an athlete folder are each refused, and nothing is written. Every path is compared resolved, so a symlink out of scratch is refused, whether it is the library or an athlete folder in it. An archive mirror outside scratch is refused, a lock in it refuses the write, and its old `session.json` is backed up. A failed review write, or a failed M3 check, leaves `diagnostics.json` byte-identical. A scratch write, read back through the app's own doors (`AthleteStore`, `SessionStore`, `SessionDiagnosticsFiles::golferInput`, the reviews). The backups' manifest. And that a second run plans nothing and leaves every file as it was.
- **Dry runs on live trees.** `build/refactor/step-2/2.8a/dry_run_proof.sh <label> <library> [<settings>]` runs the tool twice. It proves the run wrote nothing: a listing of path, size and mtime before and after; the SHA-256 of every file the tool read, which the tool lists with `--read-list`; and the settings file and PinPoint plists, hashed and diffed.

### Parity baseline — `tools/parity/`

The runner around the parity dumps. It takes one baseline run under a label, and compares two runs. Each refactor step re-runs it and diffs against the baseline taken at the commit Stage 0 lands on (`build/refactor/step-G/BRIEF.md` §2). The scripts are standard-library Python, run with `python3 -I`, and each has `--help`. A run reads the library and the corpus and writes only under `/mnt/swingdata/scratch/parity/<label>/` (`C:\PinPointStudio\scratch\parity\<label>\` on the cabin PC). It refuses any destination whose real path (symlinks resolved) is not inside that root, and it refuses a label that is already used.

- **The set, `parity_set.json`** (written by `parity_select.py`, which reads the library and the corpus without writing them). It holds eight whole sessions, one per category, as paths relative to the library root: camera + IMU + launch monitor (18 Aug), launch monitor only (4 Aug), the Premier launch monitor (2 Nov), the untaped 6 iron (9 Sep), Jess and Nick (20 Oct), Chris, and one multi-club session. The file says why each one was chosen. It also lists the 61 pinned-pose swings of `corpus/pose2`, each with its pinned face-on pose and, for the 34 two-camera swings, its pinned DTL pose from `corpus/pose2_dtl`. ViTPose on CUDA is not deterministic, so both views must be pinned. Re-run `parity_select.py` only to change the set on purpose. A category with no candidate stops it.
- **Mac half, `parity_baseline.py <label>`.** It copies the eight sessions to `<label>/lib/`, with each athlete folder's `athlete.json` (the record the new engine reads: refactor 4-c, J-61). It then runs `regrade_ledger --engine new --tag parity --dump` over them (the session's own `SessionDiagnostics`, since refactor 3.10), then `athlete_dump --fresh` over their athlete folders, checks that the athlete dump counted every session copied (`run_mac.json` `athleteSessions`; one short exits 1)[^r53c], and deletes the copies (`--keep-copies` keeps them). `--athletes-ini <settings>` is only for a library not yet migrated: the file is copied beside the session copies, passed on and deleted with them; there is no default, since the app's settings hold no athletes after the 22 Nov migrations. `--athlete-files` (step 2.8b) also copies every other plain file directly in each athlete folder (its caches); off by default. Output goes to `out/diagnostics.json`, `out/athlete.json` and `out/run_mac.json` (commit, binary hashes, exit codes, seconds per step), with the tools' logs in `out/logs/`. Build `regrade_ledger` and `athlete_dump` in the tests tree at the commit under test first. About 50 s in all: the copy takes 22 s, the session dump 18 s and the athlete dump 5–6 s.
- **Analysis half, on the cabin PC only** (it is a 61-swing sweep). First build `swinglab_run` with `tools/studio/studio-build.cmd tool`. Then run `ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\parity\run_analysis_golfsimpc.cmd <label>"`. It runs `parity_baseline.py --half analysis`: `swinglab_run --out --doc-blocks` with `--pose` (and `--dtl-pose`) per swing, in place in `build\Release-Installer`. It writes `out/analysis.json`, one digest per swing, and `out/run_analysis.json` (per-swing seconds and exit codes), then deletes the result trees. It takes about 6–8 s a swing. The digest is `result.json` minus three things: the wall times (`analysis.timings`, and `skeleton3d.diagnostics.ms` and `.plane.branchMs`, the only fields that moved between two runs of one swing), the source path, and the pose arrays, which become `{count, sha1}`. It also keeps runmeta's verdict (`ok`, `error`, `score`, …). `--doc-blocks` adds the swing document's `summary` block and this pass's `imuIntegrity`/`captureIntegrity` verdicts, as a write-back of the same analysis would store them. The verdicts are reached as `reanalyzeSwingDir` reaches them, and the source document goes through the production writer (`RecordingRepository::create`) and is read back. The cabin's `swinglab_run` must be built with that option. A swing fails, and the run exits 1, when it has no `result.json`, a non-zero exit, or a `result.json` with no `summary` (the doc-blocks write failed). Its runner log is kept as `out/logs/<id>.runner.log`. `parity_baseline.py --self-test` checks that rule. `--only <id>` runs a subset, for a one-swing smoke test on the Mac.
- **Compare, `parity_diff.py <labelA> <labelB>`.** It compares `analysis.json`, `diagnostics.json` and `athlete.json`.
  - Whole numbers are compared exactly when both sides are whole: a JSON integer, or a float with no fraction. That covers counts, ids and ms or µs timestamps, which a relative tolerance would let move by milliseconds. `1` and `1.0` are equal.
  - Other numbers are equal within a relative tolerance, `--rtol 1e-9` by default, or an absolute `--atol 1e-12` near zero.
  - `inf` equals only the same-signed `inf`, and NaN only NaN. Neither ever passes the tolerance.
  - States, labels and types are compared exactly.
  - A key or a file in one run only is reported.
  - Arrays of objects are matched by their identity (`session`, `swing`, `shotId`, `conditionId`, `id`, `key`, …), so one extra shot does not shift every row after it. An order change is reported once.
  - The output is a Markdown list, first by field (path, count, largest relative change, one example) and then by swing or session, with an empty box on each line for "explained". `--out` writes it to a file.
  - Exit 0 means no difference and 1 means at least one. Exit 2 is a usage error, or nothing compared: both runs hold none of the three files, or a file is empty or not JSON. So a run that never wrote its outputs cannot pass.
  - `parity_diff.py --self-test` checks these rules on 27 built-in cases.
- **Order for a step's check.** If `tools/parity/` is not committed, copy it to `/mnt/swingdata/scratch/parity/_tools/` and run `C:\PinPointStudio\scratch\parity\_tools\run_analysis_golfsimpc.cmd` instead; the `.cmd` finds its Python beside itself. Run both halves under the same label at the step's commit. Then run `parity_diff.py <baseline> <label> --out <step folder>/parity-differences.md`. Delete the run folder once its differences are judged, keeping the baseline.

---

## 10. File map

| Path | Role |
|---|---|
| `tests/CMakeLists.txt` | Umbrella — builds all suites in one configure |
| `tests/cmake/PinPointTests.cmake` | Shared infra: Qt prefix, `pp_find_eigen`, sanitizers, lazy gtest, `pp_add_test` |
| `tests/CMakePresets.json` | `tests` / `tests-asan` / `tests-tsan` presets (run from `tests/`) |
| `tests/MIGRATION.md` | Historical record of the consolidation + remaining cleanup |
| `src/Analysis/tests/` | **84** — by far the largest. Wrist-assessment engine, the stage mechanism and one test per stage, segmentation/metrics, shaft + clubhead + ball tracking, pose helpers, scoring and norms, the diagnostics/characteristic packs, orientation filters, IMU driver parse, the shot arbiter, and the Export round-trip. Reach for `ctest -R <pattern>` here, not the whole suite. |
| `src/Buffer/tests/` | Lock-free ring, timeline merge, watchdog, thread-policy, fuzz (8, GoogleTest, links `pinpoint_buffer`). **Not migrated to `pp_add_test`** — own gtest fetch, own `PINPOINT_ENABLE_*` sanitizer options. |
| `src/Core/tests/` | Resource profiler (core, concurrency/TSan, compile-out), OS + GPU metrics, stats log, profiler controller (7, GoogleTest; refactor 3.7-I adds `processMemory()` and the `[GpuMem]` line's text to the OS-metrics and controller tests); the standard club table and the Height field (2, GoogleTest); `post_shot_trace_test` — the `[PostShot]` step timer (`post_shot_trace.h`, refactor 3.7-I; GoogleTest with its own `main`): see "The post-shot bench" in §9; the two persistence mechanisms and the one worker pool (3, hand-rolled `main`, Qt Core only): `atomic_write_test` — the one atomic write (`atomic_write.h`, R-PERS-2): a file replaced whole, a missing folder made (or refused with `intoExistingFolder`), the original byte-identical after an unwritable folder, a read-only file or a refused rename, no temporary left, the streaming forms (a writer callback and `AtomicFile` held across calls), text mode; moved here from `Diagnostics/tests/pack_io_test` with the helper (refactor step 2.0), which now pins only `pack_io`'s forwarding call — and `folder_lock_test` — the advisory folder lock (`folder_lock.*`, R-PERS-6): two lock objects (held → `Locked`, naming the holder, then free once let go), a lock left by another machine freed only by age (stale after 60 s), a missing folder not made, and (step 2.8b) `lockOrJoin` joining a hold the same thread already has — never another thread's, while a plain `lock()` on the same thread still answers `Locked` — and a hold taken through a symlink whose folder vanishes mid-hold leaving no stale entry behind (the registry key kept from acquisition) — and `work_pool_test` — the one worker pool, its serial lanes and the post-to-owner receiver (`work_pool.*`, R-CON-5; refactor step 3.1, D15): a lane keeps order (60 jobs on four workers, never two at once); two lanes do not block each other; a cancelled lane drops its waiting jobs and releases their values at once while its running job finishes, and a submit after cancelling a running lane waits behind the running job (the lane stays serial); destroying the pool with a job running and jobs waiting never runs the waiting ones, releases their values, and returns once the running one finishes; priorities (a higher priority first, then the foreground lane, then the earliest; a Low job with its thread set to `LowPriority` — Qt's stored value, not the OS's — and the worker back at `NormalPriority` after it, D24); the receiver drops a result whose session is not loaded, is another instance of it, or whose shot is gone, drops a superseded ask, and keeps only the shot's exact latest, telling the owner of every drop but the first two so its batch settles; a job posted through the pool arrives on the receiver's thread, delivery stays first in, first out when an owner spins an event loop inside its `deliver`, a throwing job still posts (its error set), and a released session's running job is dropped at the receiver; the process-wide counter never repeats a number across sessions or threads. Also clean under ThreadSanitizer (`-DPP_SANITIZE=thread`); `isolated_stores.h`, the one test-isolation helper (#187), with `isolated_stores_test` (hand-rolled `main`) and `store_isolation_lint_test` (registered by the umbrella): see "Per-user stores" in §9 |
| `src/Gui/tests/` | TimelineLabels, chart metrics, SwingSeriesModel, ShotListModel, ReanalysisController, QML reactivity (6). Step 2.3: `athlete_repository_test` (`AthleteStore`, the `Athlete` and `AthleteController` on a temp library and a scratch INI — swing_export_developer_guide.md §15) beside `athlete_session_guard_test` (no switch mid-session; an edit never emits `currentAthleteChanged`), both from `athlete_session_guard_tests.cmake`, sharing `athlete_test_library.h`. Step 2.7b extends three: `charts_traces_presenter_test` (`shotNumberAfterAGap`: after a trashed swing the kicker reads "Shot 9 · 8 of 15", the against-the-previous label "shot 8", a shot before the gap "Shot 3 of 15"), `charts_delivery_presenter_test` (`golfersDayNameDateType`: under `name-date-type` the day's sibling is pooled and named "session 1" by its n-of-day; the kicker and the shape marks carry the swing number; `golfersDay` checks the scatter's `sessionLabel` and `shotNo`) and `report_lm_test` (section I: the launch monitor's rows, left-out list and notes by swing number after a gap). |
| `src/IMU/tests/` | Impact detector, ImuIoWorker, ESKF gyro-unit pin (3) |
| `src/LaunchMonitor/tests/` | `LastShot.CSV` parsing (columns by name not position, metric **and** imperial units as FSX2020 declares them, closure rate in dps **or** rpm, the derived values, malformed/torn/short input), shot attribution, and the standalone gate exhaustive over all 2^7 precondition states (3, GoogleTest; the pairing test writes its own fixtures into a `QTemporaryDir` and needs Qt6 Test) |
| `src/Pose/tests/` | v2 temporal ball tracker + its live/offline parity, BallDetector throttle contract, heatmap decode, pose-model selection (5) |
| `src/Audio/tests/` | Acoustic onset detector (1) |
| `src/Diagnostics/tests/`, `src/Export/tests/`, `src/Metrics/tests/`, `src/Identity/tests/`, `src/DomainModel/tests/` | **Sources only — no suite of their own.** Compiled by the Analysis suite (counted in its 84) because they link `swing_analysis.h` / the pack types. See below. The last two (refactor step 2.1): `identity_test` — `AthleteId`, `SessionId`, `ShotRef` (`src/Identity/identity.h`): each reads back the text it writes and nothing else (no folder name or path reads as one; no constructor from text, by `static_assert`), hashing, order, a `QVariant` round trip — and `session_identity_test` — `SessionIdentity`: a legacy folder name read under every naming pattern against the names `SwingPaths` composes, D3's name-based id against golden values from Python's `uuid.uuid5`, n-of-day by start time (by folder name when a day lacks start times — the app's order, not the per-type `_NN`: step 2.7a's N12, N13), the local day in a given time zone, the label. Step 2.2 adds `session_repository_test` (`src/Export/tests/`) — `SessionStore`, the adapter of `SessionRepository`: `session.json`'s keys, a newer schema read-only, resume (today's session of the same type, same `SessionId`), End (which never discards from step 3.4a-ii: the empty discard is `Session::end`'s), a legacy folder read without a write, enumerate, one in-progress session per athlete, the allocator never reissuing a number, D7's store half (the old session ended where it is, or left in progress when unreachable, and a new one in the new library) and a held `FolderLock` refusing the write; and (step 2.8b, section J, J-18) a crash-left session ended by the next Start gets the latest of its swings' capture ends, marked `endInferred`, no end when none records a capture time, and an End is never marked; and (section G, D11: DEC-158) `SessionStore::fileSaysInProgress`, the tools' check: a session being recorded is in progress, an ended one, a legacy one, an unreadable file and no folder are not, and the check writes nothing. `swing_paths_test`'s allocator sections (N, A, R, B, E, K) moved into it with the allocator; `swing_paths_test` keeps the names, `sanitise` and the trash. Step 2.5 adds `golfer_input_test` (`src/Diagnostics/tests/`, audit H-04's missing test, on the diagnostics model's link line): a focus, a run, a screen answer, a miss, a shot intent and a correction live in `session_input.json` and the shots' reviews, and survive `diagnostics.json` being deleted; a legacy session (its input in `diagnostics.json`'s older keys only) reads identically, keeps the keys byte for byte through a regrade (`setRebuildCache`, as `regrade_ledger` runs it), takes an edit in the new home and holds a cleared answer explicitly; an envelope written before step 2.5 reads as its moved copy does; a newer envelope is read-only and the golfer told; `session_input.json` keeps unknown keys and refuses a newer schema; the older keys survive the cache being deleted mid-activation and a catch-up activation; a club-baseline freeze read off the GUI thread gives the synchronous freeze's baseline, grades nothing before it, and a golfer command or a re-activation during it waits for it or lets it go. Step 2.7a adds `shot_number_test` (`src/Diagnostics/tests/`, the same link line): after a trashed swing and one with no document, the ledger's "shot N" — the header's label, the review badge, every readout — is the swing number, a session with no gap reads as before; under the `name-date-type` pattern a session still has its day (a live panel freezes its club baseline over the day before: F-06), and two sessions of one day are placed by their session files' start times, which the tendencies "as of" a session follow. Step 3.3 adds two (`src/Diagnostics/tests/`, on `FIRE_FINDINGS_TEST_SOURCES`, the measure-dump link line plus `fire_findings.cpp`): `fire_findings_test` — `fireFindings()` and `deriveShotData()` (`src/Diagnostics/engine/fire_findings.*`) on one swing, `lm_7iron`: the same readings and inputs give the same record and stamp, and every field of `FiringInputs` moves `firingStamp()` — the pack, the norms, the cohort (a women's row read only for a woman) and the club baseline move the findings too, the flight table the derived data, SET-06 nothing else (recorded, not applied); a broken recording withholds every row; the derived data is `composeShotOutcome` + `shotFactors` — and `rule_version_golden_test` (R-STAMP-3, audit C-05) — `fireFindings()` on the four `live_swings` fixtures, against a **frozen** copy of the pack, norms and contexts (`src/Diagnostics/tests/data/rule_version_golden/`, never the shipped files, so a content edit cannot move it or hide a rule change), with its digest in `rule_version_golden/golden.json` keyed by `kDiagSchemaVersion`, `kRowWordsVersion`, `kRowGradingVersion` and `kPhaseGridSchemaVersion`: it fails when an output moves and no version did, when a version moved until the golden is re-recorded, and (its own check, `frozenContent`) when the frozen copy was edited (`PP_RECORD_RULE_GOLDEN=1 ctest --test-dir build/tests -R '^rule_version_golden_test$'`); each number is snapped to 0 below 1e-9 (−0 too) and rounded to 9 significant digits before the digest, and a mismatching record is written beside the build for diffing. Step 3.4a adds `session_test` (`src/DomainModel/tests/`, on the Recording store's link line plus the store, the `Athlete`, `Session`, `Shot` and `app`), over the real session store, swing door and reading door on a temporary library: a start ends the session running here first (EVT-09 before EVT-17); resume only today's session of the same type, with its own id (EVT-23), the session running here ended first, another type or yesterday's refused; an empty session ended is trashed by its `Athlete` (EVT-20, no EVT-09), one with a swing or a resumed one ends (EVT-09) and stays; `addShot` allocates (the folder, the high-water) then makes the `Shot`, with no EVT-01 until capture's document write is reported, then exactly one; `trashShot` (EVT-03) never reissues the number and deletes the `Shot` only when the event loop runs (`deleteLater`); the events in command order; a `Shot` and a `Session` have no public constructor (`static_assert`), one instance per ref and per id; a `RecordingSource` reads nothing once its session is unpublished; `app.sessionAt` through the current athlete, none for another athlete's folder; the id-keyed diagnostics files (J-42) written and read back where `SessionFiles` names them, an id not held refused; `trashSession` refuses a session in progress (J-46); and (step 3.4a-ii) a session started on demand is never the empty discard's and the store's own End never discards (S12), resuming the session already running here writes and emits nothing and leaves it resumed work (S13), `addShot` hands back the whole allocation with the caller's kind (S14). Both suites set their stores with `SessionStore::setOsTrashForTest(false)`, so every trash lands in the library's own `.pinpoint-trash` inside the temporary folder (asserted), never in the OS trash. Step 3.4b-i adds `shot_commands_test` (`src/DomainModel/tests/`, on the diagnostics model's link line plus the `Athlete`, `Session`, `Shot`, the athlete store, `app` and `ShotListModel`), over the real athlete store, session store and swing door on a temporary library and temporary settings (`QSettings::setPath`): each of the `Shot`'s commands (`rate`, `note`, `setDeclaredClub`, `correctClub`, `excludeReading`, `setShotIntent` — now and on a worker —, `correctLabel`, `completeCapture` for a reading and a phone clip's stream, `setMarkupTruth`) saves to today's key, then emits its event once (none for a rating, a note or the truth), and a save that changes nothing emits nothing; the `Athlete` appends one tuning-log line on EVT-06; on a shot with no document every command is refused with its reason and emits nothing (no tuning line); the `Session`'s `setSessionIntent` / `setIntentRun` write field by field (the caller's value only for a field the file lacks) and emit EVT-10 / EVT-08; the door (`app`) reaches a held shot and not a folder no `Session` holds; the carousel's edits through the door save the one fact (a rating writes the rating alone) and emit EVT-21 / 04 / 05, and without a door keep today's write-through; the old diagnostics model's `setShotIntent`, `correctOutcome`, `declareMiss`, `recordScreenResult`, `declareFocusFrom` and `setIntentFrom` emit the same events, saved where they always were. Its stores trash nothing outside the temporary folder. Step 3.4b-ii adds three sections (C14–C16): `Shot::applyAnalysis` writes re-analysis's analysis back and emits EVT-02 once (nothing for a launch-monitor-only shot whose rows are current; `NoDocument` for a shot with none); the `Shot`'s summary is its repository's cheap read field for field, follows its own saves and is read again after a re-analysis; the carousel's rows over the `Shot`s — a session load lists the `Session`'s shots (newest first, the swingDir as today's text), an edit's row shows what the `Shot` holds after its save (a save that fails, its folder made unwritable, leaves the row as the shot is), a live row's number and review facts are its `Shot`'s, and the carousel's delete is `Session::trashShot` (EVT-03, the `Shot` let go, the number never reissued), with today's paths when no door is set; and `ReanalysisController` reads a held shot through its `Shot`'s `RecordingSource` (the analysis is stubbed in the test and records what it was handed) and writes back through `app` (EVT-02 once), a launch-monitor-only shot's rows through the `Shot` too, and no door keeps today's path. Round 2 (review 1) adds C15k (a session its `Athlete` holds is answered by `Athlete::sessionAt` with its session file unreadable: nothing read again), C16f (a rescued camera shot — allocated captured, its document written device-only by the launch monitor — is routed by its document's classification: its rows refreshed through the `Shot`, never analysed) and C16g (the `Shot`'s write-back of a camera shot writes the same document and the same swing-file bytes as HEAD's path, `persistReanalysis` straight to the repository). `reanalysis_controller_test` and `recording_repository_test` (Test 10, the write-back) take the new `reanalyzeShot(RecordingSource)` / `reanalyseAndPersist(write, source, deviceOnly, …)` forms. Step 3.5-i adds `session_diagnostics_test` (`src/DomainModel/tests/`, on `FIRE_FINDINGS_TEST_SOURCES` plus the Recording store's line, the session store, the `Athlete`, `Session`, `Shot`, `SessionDiagnostics`, the firing reader and the pool), over the real store, swing door, pool, receiver, the process's `RuleData` and the real `fireFindings` on a temporary library (every trash into its own `.pinpoint-trash`): most cases read the shots through a fake `FiringReader` that lays a club and a launch angle over `lm_7iron`'s grid and can hold one read at a gate, so each race is staged on purpose — a club correction fires the shot once and derives both clubs' shots once each (J-2); a shot of another club in flight while a usual changes is kept (REVIEW-1); a superseded result is dropped and the batch settles once; a trashed shot's result is dropped (ItemGone) and settles; a session released and loaded again while its re-fire runs keeps none of the old instance's results (T1); one new shot derives exactly its club's shots once, none of another club, and a shot that leaves the usual unchanged asks nothing more (T2), with every earlier finding exactly as fired (a usual's change re-derives only, DEC-191); a read that fails mid-run (the session unpublished) is unread, the kept findings untouched, the mark persisted in `diagnostics.json`'s `unread` and the next load re-asks exactly those (T3); a release writes what is kept first and a load re-asks exactly the stale and missing shots (T4); the real reader, headless, keeps what `fireFindings` gives on the document's own read (R); and a sweep holds at most one released session at a time, the running one and one in use refused (Q18). Round 2 (review 1) adds: a shot with a label correction and a shot intent, released and loaded again, derives the same outcome, its review read on the worker (WORD); golfer input only in `diagnostics.json`'s older keys gives the same derived data as the same input in its new homes, by the one J-33 rule (`golfer_input_rules.h`) the old model's `golferInput` uses too (LEG); a derive that throws is marked and not asked again until an input moves, on the pool and headless (FAIL); the cohort is the record's on each shot's own date, and a sex / date-of-birth change re-fires every shot (COH). Also clean under ThreadSanitizer (`build/tests-tsan`). Step 3.5-ii adds (with `session_reduction.cpp` on its line): each shot's derived data is persisted with its findings, ids and numbers only, and a load takes it back — nothing fired or derived, no review read, each shot's facts as before — until a layer-3 input moves (DER; WORD and FAIL now load a file of the older shape, its `derived` taken out, so they still prove the load-time derive); the diagnosis on the lane, headless over the real reader: one reduction per settled batch, the typed results (three shots, three "because", the work-ons keyed by the folder's name, the owner's usuals), no "because" on a shot's own outcome (J-14 (b)), `work_ons.json` read by today's reader as today and by the typed reader with every DEC-192 field, the stage and `closed` written, an ended session closed from its `Session`'s status, a declared focus (EVT-10) re-reduced with no shot asked (DIAG); the typed reader strict, lenient (an older schema read as it stands, without DEC-192's keys) and never for a newer schema (RJ). `regrade_ledger --engine=old|new` (tools, not ctest) is run P's tool: the two engines dumped in the parallel run's form for `tools/parity/parity_diff.py`. Step 3.6 adds `athlete_diagnostics_test` (`src/DomainModel/tests/`, on `session_diagnostics_test`'s line plus `AthleteDiagnostics` and the catch-up service), over the real store, `Athlete`, `Session` and `SessionDiagnostics` on a temporary library: the praise rule (Repeatable or Grooved only; ungraded, grade 0, 3 and below and a measurement-limited grade not praised: PRA); the board's annotations (BRD); `AthleteDiagnostics` reads summaries only — a spying session repository counts no ledger, club baseline or session-input read (SUM) — and is lazy: stale until read, an event marks it stale once, a marked session's summary alone is read again (LZY); the catch-up writes every ended session oldest first through its `SessionDiagnostics` (the summary with DEC-192's growth), freezes each club baseline from the predecessors' summaries (equal to the ledger path's: D7) and releases each session (CUP); the summaries honour a left-out reading and a club override (S05); a baseline is frozen once, an older rule's rewritten with the rewrite recorded, and none frozen while an earlier session is not summed up (FRZ); a write block is reported with where (BLK); a pause lets go of the session in hand and takes it again first, cancel drops the queue (D15). Round 2 adds TND: the tendencies (with their distances) and the themes `AthleteDiagnostics` reduces from the summaries alone (their compact per-shot facts, DEC-192 as the Solution Architect amended it) equal the ledger path's field for field, and reading them reads no ledger. Round 3 adds S05d (a shot re-labelled on the board is composed under its new club: its own factors are its tendency facts' — it failed before the fix) and BLK b (a write that fails at the release of a session a pause let go of is reported). `swing_themes_test` and `swing_themes_golden_test` hold the themes reduction, which now reads per-shot facts (`ThemeShot`), to the Python reference unchanged. And `reduction_rule_golden_test` (`src/Diagnostics/tests/`, on `FIRE_FINDINGS_TEST_SOURCES`): the session and athlete reductions (work-ons, the aggregate, tendencies, the outcome scope, distances and club baseline, themes, the praise rule, the board rule) on the four fired `live_swings` fixtures arranged as three sessions, over 3.3's frozen content copy, each digest keyed by its rule versions in `rule_version_golden/reductions.json` (re-record as for `rule_version_golden_test`). `work_ons_controller_test` gains the praise rule on Home's path and Stage 0.5's follow-up on Tendencies and Your distances. Step 3.8-i adds `housekeeping_test` (`src/App/tests/`, on the Recording store's line plus the session, athlete and swing stores, the archiver, the `Athlete`, `Session`, `Shot` and `src/App/housekeeping.*`), over the parts the app wires — one session store with its worker-safe archive job, one athlete store and the athlete callables over it, the purge and the size (`SwingPaths`), the Archiving tab's walk — on a temporary library and temporary settings: the one purge (an entry trashed before the cutoff goes, a newer one stays, no cutoff empties the trash; the retention pass goes through the same purge door once and lists no athlete when no archiving is on: H1); archiving by age with the pass on a worker thread — the old ended sessions archived, mirrored and thinned, EVT-18 heard on the app's one `Session` on its own thread, every athlete load answered by the one instance, a `Session` the pass loaded itself let go again; a newer one and today's left alone (H2); a session in progress refused by `Session::archiveJob` / `archive` (no marker, no event) and left alone by the pass (H3); below the floor the oldest first until there is room, the live session skipped (H4); SET-14 off leaves the raw frames out of the archive and the library (H5); the library's size, hidden files included, -1 when aborted (H6); nothing switched on does nothing (H7); a no-golfer (`unknown`) session archived by today's repository path, in the one oldest-first order with the athletes' (H8). #242 (platform A6) adds H11 (the Archiving tab's walk, `scanLibrary`, lists nothing in the library's `.pinpoint-trash`), and `swing_paths_test` W5–W8 (the library's top level listed with `QDir::Hidden`, as Windows lists it: `SwingPaths::libraryFolders` skips the trash by name, `isTrashFolder`; red on the Mac before the fix). Clean under ThreadSanitizer (`build/tests-tsan`). It purges only its temporary library's own `.pinpoint-trash` and never trashes. Step 3.7-0 adds `diag_parallel_live_test` (`src/Diagnostics/tests/`, on `regrade_ledger`'s link line less the tool): the parallel run's in-memory doors (`pinpoint::parallel::doors`; the app's switch went in 3.10) over a live-shaped session on a temporary library — an earlier session reduced by the old model; ten shots, each document written without its launch-monitor reading and with no sidecar (EVT-01), the previous shot's reading folded in late (EVT-22); End; the old model run as the catch-up ran it: the new owner keeps every shot with its readings, club and assessable rows, writes no file (no sidecar, no diagnostics, summary or baseline file; a re-fire leaves the tree's sizes and mtimes as they were), reads the baseline the old model froze (`reloadClubBaseline`, as the deleted hook did), and its form equals the old model's (0 differences); the parallel reader is `SidecarPolicy::ParseNoWrite` and parses a missing or stale sidecar, writing nothing. Every live document is a capture-shaped `.ppsw`. (Round 2's HOOK, CRASH and GONE tests drove the app's comparison hook and were deleted with it in 3.10; a crash-left session is still covered by `diag_stage_panel_test`'s CRASH.) On HEAD's wiring it fails (every shot read as an empty grid: 8023 differences). `measure_sample_test` (case f) and `swing_doc_test` (JSON-era and `.ppsw` summaries) pin the three policies of `readPhaseGrid` / `readSwingSummary`, the bool forms unchanged. Step 3.7-a adds `diag_stage_panel_test` (`src/Diagnostics/tests/`, on `diag_parallel_live_test`'s line plus `session_diagnostics_adapter.cpp`): the stage panel's cut-over with the app's own doors (`pinpoint::appDiagnosticsDoors`) on a temporary library — with no panel every live shot is judged by the session's owner and `diagnostics.json` written by it, `work_ons.json` not (ALWAYS); the panel's thin adapter, made by C++ after the shots, shows them all, the same object per session and surface, another for the cast over the same owner, each holding a use (PANEL); the newest shot is graded again when its launch-monitor reading is written, and the panel's row shows it (READING); Closing right after End, not reviewing (CLOSING), and for a crash-left session not recorded here (CRASH); a folder in the golfer's folder no `Session` holds keeps today's engine (UNHELD); a live panel freezes the club baseline through `Session::freezeClubBaseline` and the owner reads it (FREEZE); no `Session` (no golfer) gives an empty adapter that judges and writes nothing (NOGOLF); the carousel's pips (3.7-c): `SessionDiagnosticsAdapters::shownFor` answers the adapter a panel shows for the session (the stage panel's, else the cast's), the engine model a panel shows for a folder no `Session` holds, or nothing, and never makes one; each card's pips through it are the owner's, and a panel rebuilt on a mode switch tells the cards nothing; with no panel up the session's cards adapter gives every shot its pips with no view half, a new shot's pips arrive with the owner's diagnosis, and a shown adapter destroyed is no longer answered (PIPS); an adapter made while its owner is still reading a reviewed session is `loading` until the first diagnosis (LOADING). Step 3.7-d adds: the session report's adapter, set up as the report sets it, shows the owner's rows as the golfer annotated them on the board (a re-labelled shot's club, a left-out reading's monitor rows not assessable) and the work-on list over them, holds a use of the `Session` only while held, and writes nothing (REPORT); the content stamp made with no model equals a model's, so a report made before keeps its fingerprint (STAMP); the gate report's σ switch through the owner's doors moves the findings stamp and leaves no row with a σ (SIGMA). `diag_rank_diff` and `diag_uncertainty_report` (tools, not ctest) read each session's owner through `tools/diagnostics/owner_harness.h`, in memory. Step 3.7-e (the catch-up's cut-over; the suite links the catch-up service): the live session's summary waits for its End and is written by its owner then, fresh by the scan's rule (ALWAYSc/d); each adapter holds a use only while a view is bound (PANELf; USE: made and not shown it holds none and the `Session` can be released, shown it cannot, a rebuild counts once); FREEZE is the one freeze rule — a panel's pointing freezes nothing, the catch-up service sums up an earlier session (its owner's summary fresh by `summaryFingerprintOf`) and takes the ended live session in hand, whose owner freezes its baseline from that summary, releasing the session it loaded and keeping the one it did not; a summary whose facts' rule is one version behind, and nothing else, is stale and the catch-up writes it again (CATCHUP, CARRY 3.6 review 2); a pack edit through `RuleData`'s command (on a temporary data folder) publishes one EVT-13 and every loaded session re-fires every shot (EVT13, 3.9's test). `session_diagnostics_test` adds ARCR (review R3-M1): an archive that goes out after the reduction ran but before it was delivered, with an ask waiting and nothing of the lane running, leaves nothing outstanding and the owner settles again. `work_ons_controller_test` drives `WorkOnsController` through a stand-in for the catch-up service (its fixtures are ledgers with no session file, which no `Session` can hold), and adds the facts' rule bump at the scan: that session alone is asked again. Step 3.7-f (Home and the report read the athlete level): `athlete_diagnostics_test` adds JOB (the tendencies, the themes and the marks asked of `AthleteDiagnostics` land what its lazy reads give; asked again with nothing moved, Current; a club choice with no shots grades nothing), PER (FILE-ATH-02/03 written by the owner through `AthleteRepository`, stamped with their inputs; nothing new written twice; a fresh owner reads the current `swing_themes.json` back; a write block said and reported by `persist()`), ASOF (the tendencies as of a session), SCP (DEC-195: a club choice's views from each session's judged rows read through its own `SessionDiagnostics`, the sessions loaded for it released; its own file read back by a fresh owner), CNT/OUT (a summary of another rule written outside is read again after `rereadAll()` and counted), each inline and on the pool (the owner's own lane, routed by the receiver: J-53). `work_ons_controller_test` gives each controller an `Athlete` of its own over the fixture folder (a cache-only athlete repository, `src/Export/athlete_cache_repository.h`; its sessions' `SessionDiagnostics` in memory) and its stand-in writes each summary with the per-shot facts and club windows the owner writes; a step that edits a ledger by hand removes that session's summary too, as the owner rewrites both. `diag_stage_panel_test` adds LIVEBASE g (J-49's residual: a session resumed while loaded is asked for its baseline's predecessors, `AthleteCatchUp::followResumes`). |
| `src/Update/tests/` | Linux updater pure logic (version compare, arch-aware AppImage asset selection, GPG VALIDSIG parse, placeholder-key refusal), `PlatformTarget` arch tokens, and `UpdateController` state-machine + relaunch session-safety policy via a `FakeUpdateBackend` (3, GoogleTest; policy test needs Qt6 Qml + Test) |
| `tools/migrate/tests/` | **Sources only.** `migrate_step2_test` (refactor step 2.8a), compiled by the Analysis suite: `pps_migrate_step2`'s engine on a fixture library (see "The step-2 migrations" in §9). |

See also: the per-subsystem developer guides in this folder (e.g.
`shot_detector_developer_guide.md`, `resource_profiler_developer_guide.md`) for
what each suite actually asserts.

[^r53-regrade]: The defaults of `regrade_ledger` and `athlete_dump` moved from `old` to `new` in refactor item 5.3, 22 Nov 2026, which also added the corpus's `athletes.json` (the Product Owner's answer 1 to step 5's questions).
[^r53c]: Refactor item 5.3c, 22 Nov 2026, after 5.3's review (REVIEW-5.3 R1, R2): on the migrated library `athlete_dump` made each athlete's id from the folder's name, matched none of the sessions' files, and dumped 0 sessions with exit 0.
[^r53-bench]: `--path old` (the panel's own old engine) and its `--parallel` were removed in refactor item 5.3, 22 Nov 2026, ahead of step 5.4 deleting that engine.
