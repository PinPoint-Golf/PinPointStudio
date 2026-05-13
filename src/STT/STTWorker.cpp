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

#include "STTWorker.h"
#include "STTBackend.h"
#include "pp_debug.h"

STTWorker::STTWorker(STTBackend* backend, QObject* parent)
  : QObject(parent), m_backend(backend)
{
    // Backend becomes a child so it moves to the worker thread automatically
    // when this object is moveToThread()'d.
    m_backend->setParent(this);

    connect(m_backend, &STTBackend::transcriptionReady,
            this,      &STTWorker::transcriptionReady);
    connect(m_backend, &STTBackend::transcriptionFailed,
            this,      &STTWorker::transcriptionFailed);
}

void STTWorker::loadModel(const QString& modelPath)
{
    if (m_backend->loadModel(modelPath)) {
        emit modelReady();
        emit backendLabelReady(m_backend->backendLabel());
    } else {
        emit modelFailed(QStringLiteral("Failed to load model from: ") + modelPath);
    }
}

void STTWorker::transcribe(const std::vector<float>& pcmF32)
{
    m_backend->transcribe(pcmF32);
}

void STTWorker::stopStreaming()
{
    m_backend->stopStreaming();
}
