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

#include "LlmWorker.h"
#include "LlmEngine.h"

LlmWorker::LlmWorker(LlmEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
    // Reparenting causes the engine to follow this object onto its QThread.
    m_engine->setParent(this);

    connect(m_engine, &LlmEngine::tokenReady,    this, &LlmWorker::tokenReady);
    connect(m_engine, &LlmEngine::responseReady, this, &LlmWorker::responseReady);
    connect(m_engine, &LlmEngine::errorOccurred, this, &LlmWorker::errorOccurred);
    // modelLoaded is NOT forwarded — loadModel() emits backendChanged then
    // modelReady explicitly so the controller always sees backend before ready.
}

void LlmWorker::loadModel(const QString &modelDir)
{
    if (!m_engine->loadModel(modelDir)) {
        emit modelFailed(QStringLiteral("loadModel failed — see errorOccurred for details"));
        return;
    }
    emit backendChanged(m_engine->gpuBackend());
    emit modelReady();
}

void LlmWorker::chat(const QVariantList &history, const QString &systemPrompt)
{
    m_engine->chat(history, systemPrompt);
}

void LlmWorker::stop()
{
    m_engine->stop();
}
