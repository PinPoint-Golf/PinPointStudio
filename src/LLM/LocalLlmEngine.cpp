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

#include "LocalLlmEngine.h"
#include "pp_debug.h"

#include <QFile>
#include <QLibrary>
#include <QVariantMap>

#ifdef HAVE_ORTGENAI
#  include <ort_genai.h>

// ORT-GenAI state isolated in a pimpl so its headers don't leak into LocalLlmEngine.h.
struct LocalLlmEngine::OrtGenAiState {
    std::unique_ptr<OgaModel>           model;
    std::unique_ptr<OgaTokenizer>       tokenizer;
    std::unique_ptr<OgaTokenizerStream> tokStream;
};
#endif

// ---------------------------------------------------------------------------

LocalLlmEngine::LocalLlmEngine(QObject *parent)
    : LlmEngine(parent)
{
    // Probe for GPU at construction time, before model load.
    // Same driver-file approach as KokoroTTSEngine so the hot-swap trigger
    // fires at model-load rather than waiting for inference to be slow.
#ifdef HAVE_ORTGENAI
    m_oga = std::make_unique<OrtGenAiState>();

#if defined(WITH_CUDA)
    // Check for NVIDIA driver before assuming CUDA is usable.
#  if defined(Q_OS_LINUX)
    const bool hasNvidiaDriver = QFile::exists(QStringLiteral("/proc/driver/nvidia/version"));
#  elif defined(Q_OS_WIN)
    QLibrary nvcuda(QStringLiteral("nvcuda.dll"));
    const bool hasNvidiaDriver = nvcuda.load();
    if (hasNvidiaDriver) nvcuda.unload();
#  else
    const bool hasNvidiaDriver = false;
#  endif
    if (hasNvidiaDriver)
        m_gpuBackend = QStringLiteral("CUDA");
    else
        ppInfo() << "[LocalLLM] No NVIDIA GPU detected — will run on CPU";
#elif defined(WITH_COREML)
    // CoreML is always available on macOS ARM64.
    m_gpuBackend = QStringLiteral("CoreML");
#endif
    // No GPU path → m_gpuBackend stays empty; LlmController will hot-swap to Gemini.
#endif
}

LocalLlmEngine::~LocalLlmEngine() = default;

bool LocalLlmEngine::loadModel(const QString &modelDir)
{
#ifndef HAVE_ORTGENAI
    emit errorOccurred(QStringLiteral("Built without ONNX Runtime GenAI support"));
    return false;
#else
    const std::string path = modelDir.toStdString();
    try {
        // ORT-GenAI auto-selects the best available EP (CUDA → DirectML → CoreML → CPU)
        // based on the downloaded package variant and available hardware.
        // TODO: if ORT-GenAI 0.13.x exposes OgaConfig::Append("model.device", "cuda"),
        //       use that here to explicitly request CUDA and catch the exception for
        //       a more precise CPU-vs-GPU determination rather than the driver probe above.
        m_oga->model     = OgaModel::Create(path.c_str());
        m_oga->tokenizer = OgaTokenizer::Create(*m_oga->model);
        m_oga->tokStream = OgaTokenizerStream::Create(*m_oga->tokenizer);
        m_ready = true;
        ppInfo() << "[LocalLLM] Model loaded from" << modelDir
                 << "backend:" << (m_gpuBackend.isEmpty() ? "CPU" : m_gpuBackend);
        emit modelLoaded();
        return true;
    } catch (const std::exception &e) {
        const QString msg = QString::fromUtf8(e.what());
        ppWarn() << "[LocalLLM] loadModel failed:" << msg;
        emit errorOccurred(msg);
        return false;
    }
#endif
}

void LocalLlmEngine::chat(const QVariantList &history, const QString &systemPrompt)
{
#ifndef HAVE_ORTGENAI
    emit errorOccurred(QStringLiteral("Built without ONNX Runtime GenAI support"));
    return;
#else
    if (!m_ready) {
        emit errorOccurred(QStringLiteral("Model not loaded"));
        return;
    }

    m_stopFlag.store(false, std::memory_order_relaxed);

    const QString prompt = buildPrompt(history, systemPrompt);
    const std::string promptStd = prompt.toStdString();

    try {
        auto sequences = OgaSequences::Create();
        m_oga->tokenizer->Encode(promptStd.c_str(), *sequences);

        // Capture prompt length before the generator consumes the sequences object.
        const size_t promptLen = sequences->SequenceCount(0);

        auto params = OgaGeneratorParams::Create(*m_oga->model);
        params->SetSearchOption("max_length", 2048);

        auto generator = OgaGenerator::Create(*m_oga->model, *params);
        generator->AppendTokenSequences(*sequences);

        // Start past the prompt tokens so we only emit newly generated pieces.
        size_t lastLen = promptLen;

        QString accumulated;
        while (!generator->IsDone()) {
            if (m_stopFlag.load(std::memory_order_relaxed))
                break;

            generator->GenerateNextToken();

            const size_t newLen = generator->GetSequenceCount(0);
            for (size_t i = lastLen; i < newLen; ++i) {
                const int32_t tok = generator->GetSequenceData(0)[i];
                const char *piece = m_oga->tokStream->Decode(tok);
                const QString text = QString::fromUtf8(piece);
                accumulated += text;
                emit tokenReady(text);
            }
            lastLen = newLen;
        }

        if (!accumulated.isEmpty())
            emit responseReady(accumulated);

    } catch (const std::exception &e) {
        const QString msg = QString::fromUtf8(e.what());
        ppWarn() << "[LocalLLM] chat failed:" << msg;
        emit errorOccurred(msg);
    }
#endif
}

void LocalLlmEngine::stop()
{
    m_stopFlag.store(true, std::memory_order_relaxed);
}

// Formats conversation history into the Phi-4-mini chat template.
// Special tokens: <|system|>, <|user|>, <|assistant|>, <|end|>.
// The final <|assistant|> without <|end|> prompts the model to generate.
//
// If this engine is later swapped for a different model, update this function
// or switch to OgaTokenizer::ApplyChatTemplate() once the ORT-GenAI 0.13.x
// API for chat templates is confirmed.
QString LocalLlmEngine::buildPrompt(const QVariantList &history,
                                    const QString      &systemPrompt)
{
    QString prompt;

    if (!systemPrompt.isEmpty()) {
        prompt += QStringLiteral("<|system|>\n");
        prompt += systemPrompt;
        prompt += QStringLiteral("\n<|end|>\n");
    }

    for (const QVariant &v : history) {
        const QVariantMap msg = v.toMap();
        const QString role = msg.value(QStringLiteral("role")).toString();
        const QString text = msg.value(QStringLiteral("text")).toString();

        if (role == QStringLiteral("user")) {
            prompt += QStringLiteral("<|user|>\n");
            prompt += text;
            prompt += QStringLiteral("\n<|end|>\n");
        } else {
            prompt += QStringLiteral("<|assistant|>\n");
            prompt += text;
            prompt += QStringLiteral("\n<|end|>\n");
        }
    }

    prompt += QStringLiteral("<|assistant|>\n");
    return prompt;
}
