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
#include <QString>
#include <QVariantList>

// Abstract base for LLM chat backends.
//
// Subclasses implement model loading and auto-regressive generation, then emit
// tokenReady() for each piece of text as it is decoded and responseReady() with
// the complete response when generation finishes.
//
// history is a QVariantList of QVariantMap entries, each with keys:
//   "role" : "user" | "model"
//   "text" : QString
//
// Typical wiring (via LlmWorker on a background QThread):
//   auto engine = new LocalLlmEngine;
//   engine->loadModel("/path/to/model/dir");
//   engine->chat(history, systemPrompt);
//   // → tokenReady() per token, responseReady() on completion

class LlmEngine : public QObject
{
    Q_OBJECT

public:
    explicit LlmEngine(QObject *parent = nullptr);
    ~LlmEngine() override = default;

    // Load model weights from a directory containing genai_config.json.
    // Must succeed before chat() can be called.
    virtual bool loadModel(const QString &modelDir) = 0;

    // Begin generation. history contains the full conversation so far
    // (the engine generates the next model turn). Runs on the calling thread;
    // move the engine to a QThread to keep the caller free.
    virtual void chat(const QVariantList &history, const QString &systemPrompt) = 0;

    // Abort any generation in progress. Safe to call from any thread.
    virtual void stop() = 0;

    virtual bool isReady() const = 0;

    // Returns the active backend label: "CUDA", "CoreML", "Cloud", or empty (CPU).
    virtual QString gpuBackend() const { return {}; }

signals:
    void tokenReady(const QString &token);      // streaming partial output
    void responseReady(const QString &full);    // complete response on finish
    void modelLoaded();
    void errorOccurred(const QString &message);
};
