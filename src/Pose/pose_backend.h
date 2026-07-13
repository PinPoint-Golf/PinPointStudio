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

#include <QString>

// Shared ONNX-Runtime execution-provider detection, factored so the pose
// estimators and the MotionCaptureProbe agree on "what backend would run" on
// this machine — without any of them creating an Ort::Session.
//
// The cascade (CoreML → CUDA → DirectML → CPU) and its compile guards
// (WITH_COREML / WITH_CUDA / WITH_DIRECTML) mirror the per-estimator EP setup
// in pose_estimator_{movenet,vitpose,mediapipe}.cpp exactly; the runtime
// presence checks are the same cheap probes (nvcuda.dll on Windows,
// /proc/driver/nvidia/version on Linux). The estimators still own their own
// (session-creating) cascade for now; they can adopt this helper later to kill
// the duplication.

namespace pinpoint::pose {

// The accelerated backend ORT would pick, by presence only:
//   "" (CPU only) | "CoreML" | "CUDA" | "DirectML".
// Does NOT create an Ort::Session — compile guards + a dll-load / procfs check.
QString bestAvailableAcceleratedBackend();

// Convenience: any non-empty backend string is GPU-accelerated.
inline bool isAccelerated(const QString &b) { return !b.isEmpty(); }

} // namespace pinpoint::pose
