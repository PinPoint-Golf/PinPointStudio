# Third-Party Dependency Licences

This document records the third-party components distributed with or linked into
PinPoint Studio, their licences, and the basis on which each is compatible with
the project licence. It is maintained alongside the source and updated whenever a
dependency, version, or bundled model changes.

## Project licence

PinPoint Studio is licensed under the **GNU General Public License, version 2 or
(at your option) any later version** (GPL-2.0-or-later), as stated in the header
of every source file.

Because PinPoint Studio links espeak-ng (GPL-3.0-or-later), Qt 6 (LGPL-3.0), and
FFmpeg with libx264 (GPL-2.0-or-later, GPL by virtue of x264), **the distributed
binary is a combined work conveyed under the GNU General Public License, version
3**. The "or any later version" grant in the source headers permits this. A
GPL-2.0-only project could not lawfully combine with these components, nor with
the Apache-licensed dependencies and model assets listed below.

## Compatibility

All third-party components are compatible with the project licence, with one
exception: the optional Teledyne FLIR Spinnaker SDK, which is proprietary and
must not be distributed in a GPL binary. See **Known conflict — Teledyne FLIR
Spinnaker SDK** below.

### Linked and bundled libraries

| Component | Version | Licence | Basis of compatibility |
|---|---|---|---|
| Qt 6 — Quick, QuickControls2, Quick3D, SerialPort, Bluetooth, Multimedia, Network, WebSockets, Concurrent, ShaderTools, Gui/CorePrivate | 6.x | LGPL-3.0 (GPLv2+/GPLv3/commercial options) | LGPL-3.0 combines with GPL-3.0. Qt is dynamically linked and its licence notices are reproduced; users may relink against their own Qt build. |
| OpenCV | ≥ 4.5.0 | Apache-2.0 | Apache-2.0 is compatible with GPL-3.0. (OpenCV 4.4 and earlier are BSD-3-Clause.) |
| whisper.cpp (bundles ggml) | v1.7.2 | MIT | Permissive; GPL-compatible. |
| libsamplerate | 0.2.2 | BSD-2-Clause | Permissive; GPL-compatible. |
| Eigen | 3.4.0 | MPL-2.0 | MPL-2.0 carries an explicit GPL-2.0-or-later compatibility clause. |
| ONNX Runtime | 1.26.0 (Windows/Linux), 1.20.1 (macOS x86-64) | MIT | Permissive; GPL-compatible. |
| onnxruntime-genai | 0.13.1 | MIT | Permissive; GPL-compatible. |
| espeak-ng | 1.52.0 | GPL-3.0-or-later | Same copyleft family; conveyed under GPL-3.0. The library linked is GPL-3.0 (the repository's Android APK glue is separately Apache-2.0). |
| FFmpeg + libx264 (export encoder) | system | GPL-2.0-or-later | libx264 promotes FFmpeg from LGPL to GPL; conveyed under GPL-3.0. Qt Multimedia's own FFmpeg is LGPL and unaffected. H.264 additionally carries MPEG-LA patent terms (see Patents). |
| WinSparkle (Windows updater) | pinned | MIT | Permissive; GPL-compatible. |
| Sparkle (macOS updater) | pinned | MIT | Permissive; GPL-compatible. |
| Vulkan loader / headers (optional) | system | Apache-2.0 | Compatible with GPL-3.0; ordinarily a System Library. |
| CUDA Toolkit / cuDNN / TensorRT (optional GPU execution providers) | user-installed | Proprietary (NVIDIA) | User-installed platform runtime, not shipped in the binary — used under the GPL System Library exception. |
| GenICam runtime (shipped with Spinnaker) | system | GenICam Licence (permissive) | Permissive; GPL-compatible in itself. Bundled only when the Spinnaker backend is built — see Known conflict. |
| Teledyne FLIR Spinnaker SDK (optional, `HAVE_SPINNAKER`) | system | Proprietary | **Not compatible — see Known conflict.** |

### Model and data assets

Model weights are distributed as data, not linked object code. Each is used under
its own terms.

| Asset | Licence | Notes |
|---|---|---|
| Kokoro-82M (TTS weights) | Apache-2.0 | Inference code originally MIT. |
| Whisper models (STT) | MIT | — |
| ViTPose / ViTPose++ | Apache-2.0 (code) | Confirm terms for the exact checkpoint shipped; some published weights inherit training-dataset (e.g. COCO) terms. |
| RTMPose | Apache-2.0 | MMPose lineage. |

### Cloud services

The Azure (TTS/STT) and AssemblyAI (STT) backends are REST clients. They link no
third-party code; their use is governed by the respective providers' service
terms, not by a copyright licence.

## Known conflict — Teledyne FLIR Spinnaker SDK

The optional high-speed camera backend (`src/Video/VideoInputSpinnaker.cpp`,
guarded by `HAVE_SPINNAKER`) links Teledyne FLIR's **proprietary** Spinnaker SDK.
The GPL requires the entire combined work to be distributable under the GPL, which
Spinnaker's terms do not permit. A binary that links Spinnaker therefore cannot be
distributed under the GPL. This is a copyright-level incompatibility and is
unaffected by the choice of GPL version.

**PinPoint Studio does not distribute Spinnaker-enabled binaries.** Release builds
are produced with `HAVE_SPINNAKER` disabled. The Spinnaker backend is available
only in locally compiled builds, which are not distributed and so incur no GPL
distribution obligation — for example, a personal capture rig.

The intended resolution is to isolate the Spinnaker backend behind the existing
`VideoInput` factory as a separately loaded plugin (via `dlopen` or a separate
process), so that the proprietary SDK is not part of the linked GPL work. Until
that boundary exists, Spinnaker support remains local-build-only and is excluded
from all distributed builds.

## Mobile distribution

Android builds are distributed under the GPL as normal.

Distribution of PinPoint Studio through the **Apple App Store is not supported
under the current licence**. iOS statically links Qt, and the GPL is incompatible
with the App Store's distribution and usage restrictions; GPL-3.0's provisions on
installation of modified versions sharpen this conflict. Shipping to the App Store
would require a commercial Qt licence and a separate licensing decision for that
target.

## Patents

Distribution of H.264 encoding via FFmpeg/libx264 is subject to MPEG-LA patent
licensing terms. This is a patent obligation, separate from and additional to the
copyright licences recorded here.

## Attribution and licence texts

Distributed builds reproduce the full licence text and required attribution for
each component above: Qt (LGPL-3.0), OpenCV (Apache-2.0), Eigen (MPL-2.0),
espeak-ng (GPL-3.0), FFmpeg and libx264 (GPL-2.0-or-later), whisper.cpp and ggml
(MIT), ONNX Runtime and onnxruntime-genai (MIT), libsamplerate (BSD-2-Clause),
WinSparkle and Sparkle (MIT), the Vulkan loader (Apache-2.0), and the model assets
listed above. For LGPL Qt, distributed builds link Qt dynamically and provide the
means for a user to relink against a modified Qt.

---

*This file is an engineering record of dependency licensing, maintained with the
source. It is informational and not legal advice.*
