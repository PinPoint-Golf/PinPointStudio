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
#include "STTBackend.h"
#include <memory>

class STTBackendFactory {
public:
  enum class Backend {
    WhisperCpp, // whisper.cpp (all platforms)
    Apple,      // SFSpeechRecognizer — macOS only; falls back to WhisperCpp elsewhere
  };

  static std::unique_ptr<STTBackend> create(
      Backend backend,
      QObject* parent = nullptr);

  // Returns the best available backend for the current platform.
  static std::unique_ptr<STTBackend> createDefault(QObject* parent = nullptr);
};
