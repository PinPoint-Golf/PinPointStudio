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
#include <QAudioFormat>
#include <QObject>
#include <QString>

class TTSEngine;

// Thin QObject wrapper that binds a TTSEngine to a dedicated QThread.
// All public slots are safe to invoke via queued signal-slot connections
// or QMetaObject::invokeMethod from any other thread.
//
// The engine is reparented to this object so it moves with it when
// moveToThread() is called.

class TtsWorker : public QObject
{
    Q_OBJECT

public:
    explicit TtsWorker(TTSEngine *engine, QObject *parent = nullptr);

public slots:
    void loadModel(const QString &modelPath,
                   const QString &voicePath,
                   const QString &tokensPath);
    void synthesise(const QString &text);
    void stop();

signals:
    void modelReady();
    void modelFailed(const QString &error);
    void backendChanged(const QString &backend);  // emitted after model load; empty = CPU
    void synthesisStarted();
    void synthesisFinished();
    void audioReady(const QByteArray &pcmData, const QAudioFormat &format);
    void errorOccurred(const QString &error);

private:
    TTSEngine *m_engine;
};
