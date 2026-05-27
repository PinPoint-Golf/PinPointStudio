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
#include "LlmEngine.h"

#include <atomic>
#include <memory>
#include <QString>
#include <QVariantList>

// LLM engine backed by ONNX Runtime GenAI (local inference).
//
// On construction the engine probes for an NVIDIA GPU (Linux: /proc/driver/nvidia/version,
// Windows: nvcuda.dll) and records the expected backend label. The actual EP is
// selected by ORT-GenAI at model load time based on the downloaded package variant
// (CUDA build → prefers CUDA EP; CPU build → CPU only).
//
// If the detected backend is empty (CPU) the LlmController hot-swaps to the
// GeminiLlmEngine cloud backend — identical to the TtsController → AzureTTSEngine
// hot-swap used for Kokoro TTS.
//
// Build note: only functional when built with -DHAVE_ORTGENAI (set automatically
// by CMake when WITH_ORTGENAI=ON). Without it, loadModel()/chat() immediately
// emit errorOccurred().

class LocalLlmEngine : public LlmEngine
{
    Q_OBJECT

public:
    explicit LocalLlmEngine(QObject *parent = nullptr);
    ~LocalLlmEngine() override;

    bool    loadModel(const QString &modelDir) override;
    void    chat(const QVariantList &history, const QString &systemPrompt) override;
    void    stop() override;
    bool    isReady()     const override { return m_ready; }
    QString gpuBackend()  const override { return m_gpuBackend; }

private:
    // Format conversation history into Phi-4-mini chat template string.
    static QString buildPrompt(const QVariantList &history, const QString &systemPrompt);

    bool               m_ready = false;
    std::atomic<bool>  m_stopFlag { false };
    QString            m_gpuBackend;  // "CUDA", "CoreML", or "" (CPU)

#ifdef HAVE_ORTGENAI
    struct OrtGenAiState;
    std::unique_ptr<OrtGenAiState> m_oga;
#endif
};
