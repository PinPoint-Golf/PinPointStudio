/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <QObject>

// Hardware-adaptive CUDA-runtime sidecar (Windows). The companion to the WinSparkle
// app updater: WinSparkle keeps the hardware-agnostic `-core` app up to date, while
// this watches the *hardware* and offers the separately-packaged NVIDIA CUDA runtime
// (`-cuda`, its own Inno AppId) when — and only when — an NVIDIA GPU is present but
// the runtime is not yet installed. Re-evaluated every launch, so a user who ADDS a
// GPU later is offered acceleration with **no reinstall**. Authoritative design:
// docs/design/windows_update.md §4.4.
//
// Detection is local and cheap:
//   • gpuPresent      — nvcuda.dll (installed by the NVIDIA driver) loads.
//   • runtimeInstalled — cudnn64_<major>.dll sits next to the executable; the filename
//                        encodes the cuDNN major this build requires, so presence and
//                        "right major" are the same check.
//
// v1 install action is user-initiated and secure-by-construction: it opens the
// official GitHub Releases page so the user fetches the `-cuda` installer over HTTPS
// from the trusted source (no unverified bytes are ever auto-executed). A fully
// automated, EdDSA-verified in-app fetch is a GA enhancement — WinSparkle exposes no
// app-side "verify this file" API, so it needs a vendored Ed25519 verifier or an
// Authenticode-signed installer first (design §4.4, §6).
//
// Registered in main.cpp as the QML context property `cudaRuntime` on all platforms
// for QML uniformity; `supported` is true only on a Windows, CUDA-capable, installed
// build — elsewhere it is inert and `shouldOffer` is always false.
class CudaRuntimeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported        READ supported        CONSTANT)
    Q_PROPERTY(bool gpuPresent       READ gpuPresent       NOTIFY stateChanged)
    Q_PROPERTY(bool runtimeInstalled READ runtimeInstalled NOTIFY stateChanged)
    // The single QML driver: show the "enable GPU acceleration" offer iff supported,
    // a GPU is present, and the runtime is not already installed.
    Q_PROPERTY(bool shouldOffer      READ shouldOffer      NOTIFY stateChanged)

public:
    explicit CudaRuntimeController(QObject *parent = nullptr);

    bool supported()        const { return m_supported; }
    bool gpuPresent()       const { return m_gpuPresent; }
    bool runtimeInstalled() const { return m_runtimeInstalled; }
    bool shouldOffer()      const { return m_supported && m_gpuPresent && !m_runtimeInstalled; }

    // Re-probe the hardware/runtime state (call on launch and whenever GPU detection
    // may have changed — e.g. the resource monitor noticing a device).
    Q_INVOKABLE void refresh();

    // Open the official GitHub Releases page so the user obtains the `-cuda` installer
    // from the trusted HTTPS source (v1 install action).
    Q_INVOKABLE void openDownloadPage();

signals:
    void stateChanged();

private:
    bool m_supported        = false;
    bool m_gpuPresent       = false;
    bool m_runtimeInstalled = false;
};
