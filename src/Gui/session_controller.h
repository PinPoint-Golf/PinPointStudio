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

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

// Owns the live session clock shown in the session toolbar and the session-global
// "capturing" flag. Timing only — capture orchestration (buffer gate, device start)
// is performed by callers (toolbar) against the existing managers.
class SessionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    running      READ running      NOTIFY runningChanged)
    Q_PROPERTY(bool    capturing    READ capturing    WRITE setCapturing NOTIFY capturingChanged)
    Q_PROPERTY(QString elapsedLabel READ elapsedLabel NOTIFY elapsedLabelChanged)

public:
    explicit SessionController(QObject *parent = nullptr);

    bool    running()      const { return m_running; }
    bool    capturing()    const { return m_capturing; }
    QString elapsedLabel() const { return m_label; }

    void setCapturing(bool c);

    Q_INVOKABLE void start();   // begin/refresh the session clock
    Q_INVOKABLE void stop();    // freeze the clock
    Q_INVOKABLE void reset();   // back to 00:00:00, stopped

signals:
    void runningChanged();
    void capturingChanged();
    void elapsedLabelChanged();

private:
    void tick();

    QElapsedTimer m_clock;
    QTimer        m_ticker;
    bool          m_running   = false;
    bool          m_capturing = false;
    QString       m_label     = QStringLiteral("00:00:00");
};
