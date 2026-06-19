# Vendor-neutral, cross-platform GPU acceleration for the ONNX Runtime engines

**Status:** research complete, implementation not started.
**Owner:** the validation spike starts on a Windows dev box (real GPU + MSVC/CMake toolchain).
**Scope:** all desktop platforms. The ONNX-Runtime engines (Pose MoveNet/ViTPose, TTS Kokoro,
LLM ORT-GenAI) are bound to vendor-specific execution providers today — CUDA on Linux/Windows,
CoreML on macOS, and **nothing on AMD** anywhere. The goal is a single **vendor-neutral GPU EP
(WebGPU)** that runs across Windows, Linux, and macOS on any vendor's GPU, retiring the
per-platform CUDA/CoreML matrix. The immediate beachhead is **Windows AMD** (the most-visible
gap — that's where the validation spike runs); Linux AMD and macOS follow on the *same* EP. The
Linux `amdgpu` sysfs metrics are a small separate task.

> **TL;DR / decision:** Don't build production tooling around DirectML — it's frozen
> (sustained-engineering, pinned at ORT 1.24.4). The strategic path is the **WebGPU EP**,
> which is on the current version line (1.27.0) and is *one EP that also covers Linux + macOS*.
> But WebGPU's native-C++ packaging is rough and its op coverage for our models is unproven.
> With a 6-month+ runway, the immediate task is a **WebGPU validation spike** (Phase 0), not a
> production build. Build the real wiring around whichever survives the spike. DirectML is kept
> here only as a documented fallback for any single model WebGPU can't run.

---

## 1. Why

GPU acceleration is split across two unrelated stacks:

| Stack | Backend | AMD today? |
|---|---|---|
| **STT** — whisper.cpp / ggml | Vulkan (preferred), CUDA fallback — `CMakeLists.txt:119-153` | ✅ already works on AMD |
| **TTS** (Kokoro), **Pose** (MoveNet/ViTPose), **LLM** (ORT-GenAI) — ONNX Runtime | CUDA EP only | ❌ CPU fallback only |

whisper got AMD for free (ggml has a first-class Vulkan backend). ONNX Runtime has no
vendor-agnostic EP wired in, so its workloads are NVIDIA-only. On AMD the one genuinely degraded
*local* feature is **pose estimation** (60 Hz live; TTS and LLM already have cloud fallbacks).

The engines already contain a runtime EP cascade — `CoreML → CUDA → DirectML → CPU` — each
branch guarded by its own `#ifdef`, with the label flowing to the UI badge. A new EP is a new
branch + the CMake to deliver its binary. Files:
`src/Pose/pose_estimator_movenet.cpp:173-183`, `src/Pose/pose_estimator_vitpose.cpp:184-194`,
`src/TTS/KokoroTTSEngine.cpp:96-105`, `src/LLM/LocalLlmEngine.cpp`.

---

## 2. The two candidates

Both give universal Windows GPU coverage (AMD + Intel + NVIDIA via D3D12). The difference is
trajectory, packaging, and reach.

| | **WebGPU EP** | **DirectML EP** |
|---|---|---|
| Version line | **1.27.0**, active dev (newer than our ORT 1.26) | **Frozen at 1.24.4**, sustained-engineering only |
| Microsoft direction | The forward path | Superseded; "new feature work moved to WinML/WebGPU" |
| Windows GPU coverage | AMD+Intel+NVIDIA (Dawn→D3D12) | AMD+Intel+NVIDIA (DX12) |
| **Cross-platform reach** | **Same EP runs Linux (Vulkan) + macOS (Metal)** — could replace CUDA+CoreML+DirectML everywhere and fix Linux AMD too | Windows only |
| C++ prebuilt package | ❌ none — **pip wheel only** (no import lib, no headers) | ✅ NuGet (clean lib + headers) |
| Native-EP maturity | Maturing — feature request #22077 still **open**; JS kernels being ported to C++ | GA, battle-tested |
| Extra runtime DLLs to ship | `dxcompiler.dll` (18 MB) + `dxil.dll` + `onnxruntime_providers_shared.dll` | `DirectML.dll` (from a 2nd NuGet) |
| Mandatory session quirks | none documented (verify) | `DisableMemPattern()` + `ORT_SEQUENTIAL` required |
| Enable from C++ | `opts.AppendExecutionProvider("WebGPU")` (generic string; no header) | `opts.AppendExecutionProvider("DML")` or `OrtSessionOptionsAppendExecutionProvider_DML` |

**The strategic case for WebGPU:** it isn't just a Windows-AMD fix — it's the single EP that
could collapse the *entire* ORT GPU matrix (CUDA on Linux/Win, CoreML on Mac, nothing on AMD)
into one cross-vendor, cross-OS path, and fix Linux AMD as a side effect. That's worth far more
than a Windows-only DirectML patch. The risk is concentrated in two unknowns the spike must
resolve: **(a) does the native C++ EP actually run *our three models* correctly and fast on a
real AMD GPU?** and **(b) is the pip-only C++ packaging tolerable to wire into CMake?**

---

## 3. WebGPU EP — research findings (ground-truth, 2026-06-19)

- **Actively shipped, current version line.** `pip onnxruntime-webgpu` is at **1.27.0
  (2026-06-15)** — *newer* than the latest GitHub C++ release (1.26.0) and DirectML's frozen
  1.24.4. Classified `Development Status :: 5 - Production/Stable` (that's the umbrella ORT
  classifier, not a per-EP guarantee — see maturity caveat below).
- **No C++ distribution.** There is **no `Microsoft.ML.OnnxRuntime.WebGpu` NuGet** (verified:
  BlobNotFound) and **no `webgpu` GitHub release asset**. Official prebuilt binaries are
  **pip wheels only**.
- **What the Windows wheel actually contains** (`onnxruntime_webgpu-1.27.0-cp311-win_amd64.whl`,
  downloaded + inspected):
  ```
  onnxruntime/capi/onnxruntime.dll                 24.3 MB  (WebGPU + Dawn statically linked)
  onnxruntime/capi/dxcompiler.dll                  17.7 MB  (DXC — REQUIRED at runtime)
  onnxruntime/capi/dxil.dll                         1.5 MB  (DXIL signing — REQUIRED)
  onnxruntime/capi/onnxruntime_providers_shared.dll
  ```
  **No `onnxruntime.lib` (import lib). No headers.** ← the packaging friction.
- **Runtime deps:** Dawn's D3D12 backend compiles shaders via DXC, so `dxcompiler.dll` +
  `dxil.dll` **must be bundled** next to the exe (~20 MB). Confirmed present in the wheel.
- **Native EP is still "in progress."** Feature request #22077 (Native WebGPU EP) is **open**
  (updated Feb 2026); kernels are being ported from the JS/WASM implementation. → **op coverage
  is a real risk** for our models, especially Kokoro's fully-dynamic shapes (the same trait that
  breaks CoreML-on-Intel) and ViTPose. This is exactly what Phase 0 must measure.
- **API:** enable with the generic string `opts.AppendExecutionProvider("WebGPU")` — no special
  header needed (unlike DirectML's factory function).

**Consuming the wheel from C++** (no lib/headers in it):
1. Extract the 4 DLLs above from the wheel (a wheel is a zip).
2. **Headers:** take the public C/C++ API headers from the standard GitHub release of the
   nearest version (`onnxruntime-win-x64-1.26.0.zip` → `include/`). The stable C API is
   ABI-compatible, so 1.26 headers against the 1.27 DLL is fine for our usage.
3. **Import lib:** either reuse `onnxruntime.lib` from that same standard release zip (the C-API
   export symbols match, so it links), or generate one from the WebGPU DLL via
   `dumpbin /exports onnxruntime.dll` → `.def` → `lib /def:onnxruntime.def /machine:x64 /out:onnxruntime.lib`.
4. **Cleaner alternative:** build ORT from source with `--use_webgpu` — yields matched
   DLL + lib + headers in one go, but it's a multi-hour build that fetches Dawn. Worth it only
   once WebGPU is chosen for production; **not** for the spike.

---

## 4. DirectML EP — research findings (the documented fallback)

Kept because the facts are solid and it's the proven GA option if WebGPU fails a specific model.

- GitHub releases **no longer ship a `directml` zip** (verified v1.22/1.23/1.24.4 — only CPU /
  CUDA / CUDA13). DirectML EP comes **only via NuGet**, frozen on the 1.24.x line:
  `Microsoft.ML.OnnxRuntime.DirectML` **1.24.4** (2026-03-17, still patched). A DirectML build
  pins Windows ORT to 1.24.4.
- `DirectML.dll` is **not** in that package — it's the dependency `Microsoft.AI.DirectML`
  **1.15.4** (~193 MB nupkg; we only need `bin/x64-win/DirectML.dll`). So **two** downloads.
- The ORT DirectML nupkg (~12 MB) has clean C++ bits: `runtimes/win-x64/native/onnxruntime.dll`
  (+ `.lib`, `onnxruntime_providers_shared.dll`) and `build/native/include/*.h` incl.
  `dml_provider_factory.h`.
- **Mandatory** before appending DML (the current stub omits these → would error):
  `opts.DisableMemPattern();` and `opts.SetExecutionMode(ORT_SEQUENTIAL);`. Also: one thread per
  session for `Run()` (we're fine — per-`CameraInstance` sessions; verify nothing's shared).
- Covers DX12 GPUs up to ONNX opset 20; Windows 10 1903+.

NuGet direct URLs (flat container, no auth; `.nupkg` is a zip → `FetchContent_Declare(URL ...)`):
```
https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg
https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg
```

---

## 5. Recommendation & decision tree

1. **Do not wire DirectML into production now.** Building fresh tooling around a frozen,
   superseded EP for a release 6+ months out is wasted effort.
2. **Phase 0 — WebGPU validation spike (the immediate task).** Prove WebGPU runs our three
   models on real AMD hardware. Few hours, throwaway plumbing. Detailed steps in §6.
3. **Decide from the spike:**
   - ✅ **WebGPU runs MoveNet + ViTPose acceptably** → productionize WebGPU (§7). Skip DirectML.
     Plan the follow-on to extend WebGPU to Linux/macOS and retire the CUDA/CoreML matrix.
   - ⚠️ **WebGPU runs pose but not Kokoro** (likely — dynamic shapes) → ship WebGPU for pose;
     Kokoro stays CPU→Azure cloud (already wired). Still skip DirectML — not worth a second GPU
     stack just for TTS that already has a cloud path.
   - ❌ **WebGPU can't run pose acceptably** (op gaps / perf / native-EP immaturity) → fall back
     to the DirectML wiring in §8. It's GA and will work; accept the frozen-version tradeoff.

---

## 6. Phase 0 — WebGPU validation spike (do this first)

Goal: a yes/no on "WebGPU EP runs our models on AMD," cheaply, before touching production CMake.

- [ ] On the Windows AMD box, get the native binaries: `pip download onnxruntime-webgpu` (or pull
      the wheel) and extract `onnxruntime.dll`, `dxcompiler.dll`, `dxil.dll`,
      `onnxruntime_providers_shared.dll`.
- [ ] Headers + import lib from `onnxruntime-win-x64-1.26.0.zip` (GitHub release) — see §3.
- [ ] Quick local CMake override (a throwaway `-DWITH_WEBGPU=ON` branch, or just hand-point the
      ORT include/lib/DLLs at the extracted set) — don't merge it yet.
- [ ] Add a WebGPU branch to the EP cascade in the three engines (place it where DirectML sits):
      ```cpp
      #ifdef WITH_WEBGPU
          if (epLabel.isEmpty()) {                 // (m_gpuBackend in Kokoro)
              try {
                  m_ort->opts.AppendExecutionProvider("WebGPU");   // generic string API, no header
                  epLabel = QStringLiteral("WebGPU");
                  ppInfo() << "[<engine>] WebGPU execution provider active";
              } catch (const Ort::Exception &e) {
                  ppInfo() << "[<engine>] WebGPU unavailable:" << e.what() << "— falling back";
              }
          }
      #endif
      ```
- [ ] Ship `dxcompiler.dll` + `dxil.dll` next to the exe (Dawn needs them at first inference).
- [ ] **Run all three models and record, per model:**
      - Does the session build and `Run()` succeed (no unsupported-op / unbounded-dimension errors)?
      - Is the output numerically correct (compare a frame's keypoints / TTS audio vs the CPU path)?
      - Pose latency at 60 Hz on AMD — does the live overlay keep up?
      - Watch the log for silent CPU fallback of subgraphs (WebGPU partial offload).
- [ ] Write the verdict back into §5's decision tree.

Highest-risk model: **Kokoro** (dynamic shapes). Validate it explicitly; pose is the real target.

---

## 7. Productionize WebGPU (only if the spike passes)

- [ ] **CMake:** add `option(WITH_WEBGPU ...)`, mutually exclusive with `WITH_CUDA` on Windows
      (same one-binary-one-EP constraint as DirectML — §8). Source the binaries either by
      fetching+extracting the pip wheel (script the DLL extraction + import-lib generation) or,
      preferred for a shipping build, **build ORT from source with `--use_webgpu`** for matched
      lib/headers/DLL. Bundle `onnxruntime.dll`, `onnxruntime_providers_shared.dll`,
      `dxcompiler.dll`, `dxil.dll` via `pp_win_bundle_dll(core ...)`.
- [ ] `target_compile_definitions(... WITH_WEBGPU)`; update the EP summary string (~961-969).
- [ ] Promote the spike's WebGPU cascade branch in the three engines to permanent.
- [ ] **ORT-GenAI (LLM):** check whether a WebGPU GenAI build exists yet; if not, leave the LLM
      on its existing path (CUDA on NVIDIA / CPU elsewhere) until it does. (ORT-GenAI *does* ship
      a DirectML asset — `onnxruntime-genai-*-win-x64-dml.zip` — as a stopgap if LLM-on-AMD-GPU
      becomes a priority.)
- [ ] GPU **metrics**: no change — Windows already uses DXGI (vendor-agnostic), reports AMD VRAM.
- [ ] `cuda_runtime_controller` (`src/Update/`): gate off in `main.cpp` for non-CUDA builds —
      it offers the NVIDIA CUDA runtime, irrelevant here. Low priority (already inert without an
      NVIDIA driver).
- [ ] **Follow-on (separate doc):** extend `WITH_WEBGPU` to Linux (Dawn→Vulkan, fixes Linux AMD)
      and macOS (Dawn→Metal), then retire per-platform CUDA/CoreML. This is the real prize.

---

## 8. Fallback — DirectML wiring (only if WebGPU fails pose)

Use this only per §5's ❌ branch. Mutually-exclusive build variant (you can't have CUDA + DML in
one prebuilt DLL without building ORT from source).

- [ ] `option(WITH_DIRECTML "Enable DirectML EP (Windows, any DX12 GPU)" OFF)`; when set on
      Windows, force `WITH_CUDA OFF`.
- [ ] Windows ORT path: pin 1.24.4, fetch the two NuGets in §4. Nupkg layout differs from the
      GitHub tarball — include dir `build/native/include`, lib + DLLs under
      `runtimes/win-x64/native/`, plus `bin/x64-win/DirectML.dll` from the 2nd package. Adjust the
      `_ort_root` probe (~`CMakeLists.txt:877`) for this layout. Bundle onnxruntime.dll +
      onnxruntime_providers_shared.dll + DirectML.dll.
- [ ] `target_compile_definitions(... WITH_DIRECTML)`.
- [ ] In each engine's `#ifdef WITH_DIRECTML` block, **add the mandatory constraints** before the
      append: `opts.DisableMemPattern(); opts.SetExecutionMode(ORT_SEQUENTIAL);` then
      `opts.AppendExecutionProvider("DML")`. If that throws "not supported in this build", switch
      to `#include <dml_provider_factory.h>` + `OrtSessionOptionsAppendExecutionProvider_DML(opts, 0)`.
- [ ] ORT-GenAI: fetch `onnxruntime-genai-*-win-x64-dml.zip` instead of `-cuda`.

---

## 9. Risks / gotchas

- **WebGPU native-EP maturity.** #22077 still open; kernels ported from JS. Op gaps and
  silent partial CPU fallback are plausible — Phase 0 exists to catch this. Don't assume; measure.
- **Kokoro dynamic shapes.** Same failure class that kills CoreML-on-Intel
  (`CMakeLists.txt:859-862`). Likeliest model to be rejected by *either* EP. TTS degrades to
  CPU→Azure, not broken; pose is unaffected.
- **WebGPU C++ packaging is pip-only.** No NuGet/headers/import-lib. Either script wheel
  extraction + import-lib generation, or build from source. Budget for this in §7.
- **~20 MB of DXC DLLs** must ship with a WebGPU build (`dxcompiler.dll` + `dxil.dll`).
- **DirectML is a dead end by design** — sustained engineering, frozen 1.24.4. Only adopt it as
  the §8 fallback, never as the strategic target.
- **Single-thread-per-session** applies to DirectML (and verify for WebGPU). Per-`CameraInstance`
  pose sessions and single TTS/LLM workers should be safe — confirm no `Ort::Session` is shared.

---

## 10. Sources

- WebGPU EP / native request status: https://github.com/microsoft/onnxruntime/issues/22077 ·
  https://onnxruntime.ai/docs/tutorials/web/ep-webgpu.html
- `onnxruntime-webgpu` (1.27.0, Win x64 wheel, contents verified): https://pypi.org/project/onnxruntime-webgpu/
- DirectML EP docs (constraints, opset, sustained-engineering): https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html
- `Microsoft.ML.OnnxRuntime.DirectML` 1.24.4: https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime.directml
- ORT GitHub releases (no `directml`/`webgpu` C++ asset; latest 1.26.0): https://github.com/microsoft/onnxruntime/releases
- ORT-GenAI DirectML asset: https://github.com/microsoft/onnxruntime-genai/releases
- Package contents (both DirectML nupkgs + the WebGPU wheel) downloaded and inspected 2026-06-19.
