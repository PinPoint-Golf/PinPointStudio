# Lessons

Engineering lessons: traps that cost real time, what was happening, and what to do instead. They are not rules; guidance that becomes mandatory moves to the stage where it first applies, as a control. Where a test now catches the trap, the lesson names it.

## QML and Qt

**L-1. A revision counter named on a line of its own in a binding is dead code.** The QML compiler drops the expression statement, so the binding subscribes to nothing and answers once, forever. Use the value (`if (root._revision < 0) return …`). It is invisible to review, to qmllint and to any test that loads QML through `QQmlComponent::setData`, because the interpreter keeps what the compiler removes.[^l-1] *Check:* `qml_reactivity_test`.

**L-2. Every QML read of a `QVariantMap` property deep-copies it into a new JS tree** (`a && a.series ? a.series : []` is three reads). Never hand a large analysis map to QML as a `QVariantMap`: convert it once to plain JS and read each property once per binding. Time a Windows-debug claim on a Windows debug build.[^l-2] (The typed QML boundary is the information model's P9 and lint A6.)

**L-3. A Repeater over a JS array destroys its delegates whenever a row's content changes.** Never write a settings-backed property from inside a gesture whose handler lives in a delegate: defer it to the release. Put a change that only moves a reading on its own signal, so a field being typed into isn't torn down. A test that rebuilds the list with identical rows asserts nothing: move a reading on purpose and check the delegate changed.[^l-3]

**L-4. Panels are rebuilt on every mode switch, the new one built before the old one is destroyed:** save state when it changes, never on teardown. Never walk `children` inside a binding.[^l-4]

**L-5. Offscreen rendering differs from the real window:** the offscreen screen is 800 × 800 and the window fills it (size it with the platform's config file); `grabToImage` on the content item misses popups; shapes draw with the software renderer and ignore clips; `ShapePath.fillItem` draws nothing; 3-D views render nothing; HiDPI and garbage-collection defects don't show. Keep one screen for before and after, and do one windowed run before calling a GUI build done.[^l-5]

**L-6. Small QML traps that fail silently:** `print` is an illegal property name; `font:` plus `font.letterSpacing:` on one Text stops the whole app loading; a `var` list from C++ isn't `isArray()`; a property read through the wrong id is `undefined`, not an error; a disk-loaded QML module must be flat.[^l-6]

**L-7. A fully static scene stops the render loop and animation-driven Timers, and an occluded window does the same:** a "hung" probe in a covered window is starved, not broken.[^l-7]

**L-8. A run from Qt Creator is not a run from a shell:** Creator injects `DYLD_*` paths from the target's link directories, and they outrank the binary's rpath. When a fault reproduces only for the owner, ask for the run environment, then bisect the variable.[^l-8]

**L-9. A C++-created model handed to QML can be collected by the QML garbage collector on a windowed launch:** give it C++ ownership explicitly.[^l-9]

**L-45. `QQuickRhiItem` traps each give a black frame with no error:** the clip-space correction flips winding (set culling off); pin the colour buffer to the camera size on the first frame; defer texture deletion to the next `render()`; use `float`, not `int`, after a `mat4` in a uniform block; `Qt6::GuiPrivate` is needed for `rhi/qrhi.h`. Tell them apart by counting `initialize()` calls.[^l-45]

**L-50. QML scope traps:** never give an id the name of a child component's property (`catalog: catalog` binds the property to itself); inside a Repeater delegate only the component root's id resolves, and a composite that declares its own `id: root` hides the host's; a popup is reparented to the window overlay and inherits no scale; a `ScrollView` without `contentWidth: availableWidth` pushes right-aligned content off-screen; a QML quaternion's W is `.scalar`, not `.w` (`.w` is undefined, and the rotation silently becomes identity).[^l-50]

**L-51. C++ `QObject*` lists in a Repeater:** expose them as a `QVariantList`, declare the delegate's property as `property QtObject` (never `required` or `var`), and on removal drop the object from the list and emit the change before `deleteLater()`, so the delegate goes while the object is still valid.[^l-51]

**L-52. `QSettings` writes a `General/` group as `[%General]` and reads it back as `General/`,** so lowercase `general/` keys never read back after a restart; a same-process reread hides it. Test a settings round trip in a separate process.[^l-52]

**L-53. Create a `QWebSocket` in the thread it will run in,** never in a constructor that runs before the object moves to its worker.[^l-53]

**L-61. Debugging a 3-D view:** anchor every direction report to a fixed world-axis gizmo, never to "left" or "out of the screen" under an orbit camera; Qt Quick 3D's cylinder is centred on its origin but its cone has its base there.[^l-61]

**L-62. Qt 6.11 Bluetooth on Linux:** the address type is supplied by BlueZ (the setter exists only on Android); an UnknownError after about 25 s is an address-type mismatch, a fast failure is a refusal; embedded UART bridges advertise no name or service, so filter on the low-energy configuration too.[^l-62]

## C++ and libraries

**L-10. A pointer-authentication trap validating an object's type pointer means the object was already freed:** the first thing to touch it is the victim, and "fixing" the victim only moves the crash. (Here: Qt up to 6.11.1 released a colour space it had handed to Core Graphics.)[^l-10]

**L-11. A millisecond counter in an `int` on the host-boot clock overflows after 24.86 days of uptime:** count in `qint64`. A hung Windows app is read with a non-invasive debugger attach; the app log is in memory, so only the system's hang events survive.[^l-11]

**L-12. `QStandardPaths::AppLocalDataLocation` is named after the executable,** so a tool binary looks in a different folder from the app: shared assets use the generic location plus the app's folder, with a warning once when missing.[^l-12]

**L-13. `std::fmod` truncates where Python's `%` floors;** the most vexing parse (`std::vector<double> y(size_t(K))`) keeps recurring.[^l-13]

**L-14. A resumed TLS handshake skips the pre-shared-key identity callbacks:** suspect resumption whenever a PSK link authenticates but the application can't say which peer it is.[^l-14]

**L-15. OpenSSL reports a socket reset and an unclean end of file as `SSL_ERROR_SYSCALL`:** read `errno` straight after the call and log the real cause, not "orderly shutdown". A link needs both ends' logs: the host alone sees only that the far end went away.[^l-15]

**L-16. The phone protocol's wire ids are capped at 64 bytes and refused, not truncated:** an id built from real peer and source ids can exceed it where test-stub ids never do. A test asserting an id calls the production id builder.[^l-16]

**L-17. An acknowledgement carries the current observation, never a consumed "changed since last poll" flag;** a library-issued ack is not an application ack; `try?` on a delivery path is a defect, not a style; a green suite can be testing the guard that is firing. When a pipeline shows zero at the far end, add a counter per stage on the producing side: the first zero names the break. Never infer from a missing log line.[^l-17]

**L-18. If a number involving a device's instant looks wrong on hardware but right in tests, suspect a mix of host clock and device clock first:** tests in one process share one clock.[^l-18]

**L-19. Filter a device on what it can do, not its name, and only on a confirmed value:** 0 means "not probed yet", and a wrong filter hides a real device until restart.[^l-19]

**L-20. Read the whole function before saying what a library cannot do,** and pin "does it actually do X" with a test against the real library.[^l-20]

**L-48. A `string(REPLACE)` patch of a fetched dependency must match the original upstream text,** because the patch runs on a fresh clone; a patch made mid-session must also be applied by hand to the current build's copy.[^l-48]

**L-54. A GPU build flag is not a GPU:** the GPU ONNX Runtime binary defines its CUDA and DirectML flags on machines with no such GPU, and falls back to the CPU silently; decide a GPU-dependent fallback from the runtime backend, never `#ifdef`. A secret's environment variable is copied into the settings store on first launch and the stored copy wins afterwards: removing the variable does not disable the feature.[^l-54]

**L-56. A sibling library with no pinned minimum version fails as dozens of compile errors in our code,** not a version error: fetch and check the sibling before blaming the code. In CMake, a `set(... CACHE ... FORCE)` in a subproject rewrites the consumer's settings, because the cache is not directory-scoped.[^l-56]

**L-57. Binding a reference into a temporary dangles** (`const Pack &p = make()->pack()`): a flaky test is a defect report; reproduce it alone.[^l-57]

## Tests and builds

**L-21. An intermittent failure needs several samples on each side before it is blamed on a change;** only an assertion failure is evidence of a regression.[^l-21]

**L-22. A failed chained build silently reruns the old binaries:** read the error count before trusting a run. Test binaries built before a toolchain move abort under ctest: rebuild them. Prove a library or toolchain change with a full corpus sweep, not a list of operations.[^l-22]

**L-23. Scaled clocks in the session-setup UI suite:** a 100 ms hold leaves a window a poll can step over; a `tryVerify` message is built at call time.[^l-23]

**L-24. `pgrep -f '<text>'` matches its own waiting shell and every watchdog subshell whose command line holds the text:** wait on the real process (`pgrep -x ninja`), keep and kill the watchdog's sleeper, and cap every wait loop. `pkill -f <target>` also kills the make jobs building it; a watchdog of the form `( cmd & (sleep N; kill) & wait )` can hold a pipe open.[^l-24]

**L-25. Shell traps:** in zsh, `log` is a builtin, so call `/usr/bin/log`; an unquoted `$ARGS` doesn't word-split (use `${=ARGS}`). In Windows `cmd`, the current directory is searched before `PATH` (never leave a helper named `reg`, `find`, `sort`, `set` or `where` in a working directory); `%TIME%` doesn't update inside a loop (`!TIME!`); a script stays ASCII. `make -n` runs any recipe line that contains `$(MAKE)`. `cp -p` keeps old modification times, so Ninja sees no work. A bare `cmake -S -B` defaults to Makefiles; a cached `Qt6_DIR` survives a re-configure.[^l-25]

**L-26. On macOS, a process launched from a shell inherits the terminal's permission grants:** test camera and microphone permission by launching with `open -n` and reading the permission daemon's log; verify a launchd job's fix from its own runs. A locked screen seals the notarisation credentials mid-release. An unsigned arm64 bundle is always killed at launch (see the macOS release runbook).[^l-26]

**L-27. A slow first pose load after a macOS update is the Neural Engine recompiling its cache,** not contention; two app instances share one compiler, so don't run your own while the owner tests on the same machine.[^l-27]

**L-28. Render before-and-after comparisons at 100 % font scale from a frozen settings file,** with the BEFORE copy under its own bundle id; Windows offscreen sees no system fonts.[^l-28]

**L-46. A test suite can shrink without saying so** (now control BT-22): before calling a run green, compare the ctest total with the expected count and grep the configure log for SKIPPED (a missing OpenSSL drops the phone-link transport tests). On Windows, an open `QFile` handle blocks a later write, `.sh` test rows fail, and an MSVC Debug assertion opens a modal dialog that looks like a slow test: check the process's window title, then attach a debugger without stopping it; stdout is block-buffered, so its last line is not where the process died.[^l-46]

**L-47. MSVC Debug analysis runs about 8–12× slower than a Linux debug build** (debug OpenCV and checked iterators, not the hardware): never quote a Debug timing.[^l-47]

**L-49. A liveness check is fooled by an error dialog:** a process that is "running" may be sitting on a missing-DLL or assertion dialog.[^l-49]

## The analysis pipeline and its data

**L-29. A matching score is not evidence the camera stages ran:** judge a run by its metric count against another run of the same swing, and by its pose frame count; judge a pass by the per-swing diff against a backup, not a total.[^l-29]

**L-30. Which re-analysis path a run takes decides what it re-runs:** an `--out` run always re-runs pose (only `--write-back` reuses); `--params` re-runs the shaft and ladder; an empty `--params` takes the reuse path and is not a control; a full-window write-back on a live-captured swing re-tracks everything. Compare runs on the same path, and read the first swing's reuse line.[^l-30]

**L-31. The tool's trace file comes from a second tracker run:** per-frame trace columns are faithful, but claims about anchors need the result's events.[^l-31]

**L-32. When a swing's top sits at a run boundary and the downswing is all predicted, check the frame timestamps for holes before suspecting vision:** the hands-only phase model works in frame index, so a capture stall chains the finish onto the downswing.[^l-32]

**L-33. A verdict written once at capture rides through every re-analysis forever:** recompute such a block through its single producer, and remove it when no claim can be made.[^l-33]

**L-34. The phase-model heuristics are tuned to the pose's own flicker:** a cleaner grip is not safer unless they are retuned; judge tracker changes on P1's time against the mark too, not just the angle.[^l-34]

**L-35. The swing document is tens of megabytes: never parse or materialise it on the GUI thread;** a session list loads from the summary sidecar.[^l-35]

**L-36. Read the truth before calling a position wrong,** look at full-resolution crops before describing a failure, and when a picture and a number disagree, suspect the tool first.[^l-36]

**L-37. Library re-analysis is not idempotent, and errors shared by every swing don't average out.** Before calling a pose-level fix good, diff every metric against a control, not just the reading being chased.[^l-37]

**L-38. The analysis DAG only adds edges from earlier-authored stages to later ones:** a declared read can't move a stage later, so reorder the profile. Grep the header before claiming a default.[^l-38]

**L-39. A tool binary run outside its full build folder silently degrades** (missing runtime libraries, or models it can't find). On a network share, the Mac's client shows deleted files for minutes: run bulk file work on the machine that holds the share.[^l-39]

**L-40. When analysis slows late in a session, read the GPU memory log line first:** a GPU inference session called from short-lived threads never reuses freed memory (information model DEC-194).[^l-40]

**L-41. Re-filing a session recorded under the wrong athlete** means fixing each swing document's athlete block, session folder and club-length prior, restoring the wrong athlete's club-length prior, fixing the sidecars' session ids and removing a prepared report, then re-analysing and regrading. Qt reads a backslash-zero followed by a digit in an INI variant as octal.[^l-41]

**L-44. In a latest-frame-wins drain, reset the pending flag before reading the latest frame, never after:** resetting after leaves a window where a new frame sees "pending", posts no drain, and sticks until the next frame. Any queued connection that receives every camera frame builds a backlog when its consumer is slower: throttle before the expensive preprocessing, and count camera frames per second on the capture thread.[^l-44]

**L-55. A video view that binds one sink lets a hidden screen steal the frames** (the last binder wins): fan every frame out to all subscribed views, and gate per-method work inside its consumer, never at the shared throttle.[^l-55]

**L-58. Two OpenCV versions decode the same H.264 to different pixels,** so end-to-end byte parity across hosts is impossible: test the state machine on dumped responses, and diff runs only on one host.[^l-58]

**L-59. Motion-sensor timing:** BLE batches arrival stamps (about 1 ms apart, then about 90 ms between batches at a true 100 Hz), so a gap test needs a tolerance of max(40 ms, 1.8 × the 90th percentile of the deltas). An event-buffer merger that meets a slot mid-write must retry it, not count an overrun. A clock-sync figure from a link that isn't demonstrably stable is not a measurement.[^l-59]

**L-60. A capture gate on a debounced, latched "stable" flag stalls a held pose:** gate on the instantaneous rate polled each tick, reset on movement, and never bypass a stalled gate with a timed average. Never hand-derive a mounting quaternion (pin its signs from measured gravity); a calibration checked against the signal it was fitted on is circular.[^l-60]

## Reasoning and evidence

**L-42. Reasoning about a 3-D pose from source doesn't work: render it.** A check every candidate passes is a falsification test, never a selector: select on sign against a motion whose direction was recorded. An absent fact is omitted, never written as 0, which would be a clean bill of health issued over no evidence.[^l-42]

**L-43. The Qt message handler sees only Qt and third-party messages, so reading it says nothing about the app's own log calls;** never "upgrade" an info line to a warning to make it visible.[^l-43]

[^l-1]: Incident 31 Jul 2026 (the diagnostic model's inspector).
[^l-2]: Incident 16 Sep 2026 (the end-of-shot freeze).
[^l-3]: Incidents 31 Aug 2026 (crop drags; the phone alias field).
[^l-4]: Incident 8 Oct 2026.
[^l-5]: Incidents 9 Oct, 21 Oct, 7 Nov and 10 Nov 2026.
[^l-6]: Incidents 26 Aug, 9 Oct, 7 Nov and 10 Nov 2026.
[^l-7]: Incidents Aug 2026 and 18 Aug 2026.
[^l-8]: Incident Aug 2026 (three weeks of a start failure).
[^l-9]: Incident 21 Oct 2026 (the session report).
[^l-10]: Incident 20 Nov 2026.
[^l-11]: Incident 13 Oct 2026.
[^l-12]: Incident 29 Sep 2026.
[^l-13]: Incidents 11 Aug and 1 Oct 2026.
[^l-14]: Incident 29 Aug 2026.
[^l-15]: Incident 1 Sep 2026.
[^l-16]: Incident 25 Aug 2026.
[^l-17]: Incidents 28 Aug, 1 Sep and 2 Sep 2026 (the phone preview and clip paths).
[^l-18]: Incident 26 Aug 2026.
[^l-19]: Incident 25 Aug 2026.
[^l-20]: Incident 27 Aug 2026.
[^l-21]: Incident 27 Aug 2026.
[^l-22]: Incidents 5 Sep and 5 Nov 2026.
[^l-23]: Incident 5 Oct 2026.
[^l-24]: Incidents 14 Nov and 31 Oct 2026.
[^l-25]: Incidents 18 Aug, 5 Sep, 10 Sep, 2 Oct, 11 Oct, 12–17 Oct and 19 Oct 2026.
[^l-26]: Incidents 4 Aug, 29 Sep and 6 Nov 2026.
[^l-27]: Incident 17 Oct 2026.
[^l-28]: Incidents 10 Oct and 1 Nov 2026.
[^l-29]: Incidents 20 Aug and 4 Sep 2026.
[^l-30]: Incidents 18 Sep, 6 Oct, 6 Nov and 19 Oct 2026.
[^l-31]: Incident 10 Aug 2026.
[^l-32]: Incident 6 Sep 2026.
[^l-33]: Incident 20 Aug 2026.
[^l-34]: Incident 30 Sep 2026.
[^l-35]: Incident 16 Sep 2026.
[^l-36]: Incidents 20 Sep, 6 Oct and 6 Nov 2026.
[^l-37]: Incidents 2 Oct and 9 Nov 2026.
[^l-38]: Incident 6 Oct 2026.
[^l-39]: Incidents 23 Sep and 24 Nov 2026.
[^l-40]: Incident 21 Nov 2026.
[^l-41]: Incident 2 Nov 2026.
[^l-42]: Incidents Aug 2026 (the wrist-sensor integration).
[^l-43]: Incident 16 Sep 2026.
[^l-44]: Incident, 12 May 2026 (the camera display drain and the pose pipeline backlog). Owner (proposed): Product Owner.
[^l-45]: Incident, May 2026 (the Bayer video item drew black frames).
[^l-46]: Incidents, Sep–Oct 2026 (the Windows test gate).
[^l-47]: Incident, 21 Jul 2026 (Debug timings quoted for analysis).
[^l-48]: Incident, undated 2026 (a dependency patch that matched no upstream text).
[^l-49]: Incident, undated 2026 (a launch check passed on an error dialog).
[^l-50]: Incidents, 2026 (a dashboard rendered empty; a delegate handler saw the wrong root; a quaternion that never moved).
[^l-51]: Incidents, 2026 (camera and sensor lists in a Repeater).
[^l-52]: Incident, undated 2026 (settings lost after a restart).
[^l-53]: Incident, undated 2026 (the speech backend's socket).
[^l-54]: Incidents, undated 2026 (a silent CPU fallback; a cloud feature still on after its variable was removed).
[^l-55]: Incident, undated 2026 (a hidden screen took the camera frames).
[^l-56]: Incidents, undated 2026 (a stale sibling library; the wrist library's split).
[^l-57]: Incident, undated 2026 (a flaky diagnostics content test).
[^l-58]: Incident, undated 2026 (the ball-detector port).
[^l-59]: Incidents, undated 2026 (sensor gaps flagged at every batch; a merger that counted overruns; a sync figure from an unstable link).
[^l-60]: Incidents, undated 2026 (the arm calibration stall; a mounting quaternion 180° off).
[^l-61]: Incident, undated 2026 (the arm view's orientation).
[^l-62]: Incident, undated 2026 (Bluetooth sensors on Linux).
