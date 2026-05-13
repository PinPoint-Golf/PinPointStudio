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
#include <QObject>
#include <QString>
#include <vector>

class STTBackend : public QObject {
  Q_OBJECT
public:
  explicit STTBackend(QObject* parent = nullptr) : QObject(parent) {}
  virtual ~STTBackend() = default;

  // Load model from an absolute file path. Returns true on success.
  virtual bool loadModel(const QString& modelPath) = 0;

  // Transcribe audio. Input MUST be 16kHz, mono, 32-bit float PCM.
  // Result delivered via transcriptionReady signal.
  virtual void transcribe(const std::vector<float>& pcmF32) = 0;

  virtual bool isReady() const = 0;

  // Returns true if this backend needs a local model file passed to loadModel().
  // Backends that use OS-provided models (e.g. Apple Speech) return false.
  virtual bool requiresModelFile() const { return true; }

  // Returns true if silent chunks should be dropped before calling transcribe().
  // Local backends (whisper.cpp) and REST cloud backends (Azure) benefit from this.
  // WebSocket streaming backends that rely on silence for server-side end-of-turn
  // detection (AssemblyAI) return false so all audio is delivered.
  virtual bool requiresSilenceGating() const { return true; }

  // Short label describing the compute backend, e.g. "CPU", "Vulkan", "CUDA", "Apple".
  virtual QString backendLabel() const { return QStringLiteral("CPU"); }

  // Called when the user stops listening. Cloud backends should close their
  // connection here; local backends can ignore it.
  virtual void stopStreaming() {}

signals:
  void transcriptionReady(const QString& text);
  void transcriptionFailed(const QString& error);
};
