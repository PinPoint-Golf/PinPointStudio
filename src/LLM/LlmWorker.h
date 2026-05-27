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

class LlmEngine;

// Thin QObject wrapper that binds an LlmEngine to a dedicated QThread.
// All public slots are safe to invoke via queued signal-slot connections
// or QMetaObject::invokeMethod from any other thread.
//
// The engine is reparented to this object so it moves with it when
// moveToThread() is called.

class LlmWorker : public QObject
{
    Q_OBJECT

public:
    explicit LlmWorker(LlmEngine *engine, QObject *parent = nullptr);

public slots:
    void loadModel(const QString &modelDir);
    void chat(const QVariantList &history, const QString &systemPrompt);
    void stop();

signals:
    void modelReady();
    void modelFailed(const QString &error);
    void backendChanged(const QString &backend);  // emitted before modelReady; empty = CPU
    void tokenReady(const QString &token);
    void responseReady(const QString &full);
    void errorOccurred(const QString &error);

private:
    LlmEngine *m_engine;
};
