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

// macOS-native STT backend using SFSpeechRecognizer (Speech framework).
// No model file is required; recognition is performed on-device (macOS 13+)
// or via Apple's servers on earlier releases.
class STTBackendApple : public STTBackend {
    Q_OBJECT
public:
    explicit STTBackendApple(QObject* parent = nullptr);
    ~STTBackendApple() override;

    // modelPath is ignored — the Speech framework uses its own built-in models.
    // Requests speech recognition authorization from the user on first call.
    bool loadModel(const QString& modelPath) override;
    void transcribe(const std::vector<float>& pcmF32) override;
    bool    isReady() const override;
    bool    requiresModelFile() const override { return false; }
    QString backendLabel() const override { return QStringLiteral("Apple"); }

private:
    struct Private;
    Private* d = nullptr;
};
