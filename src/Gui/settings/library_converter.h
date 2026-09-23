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

// LibraryConverter — the Storage panel's "Convert library" action.
//
// Finds every JSON-era swing (swing.json) under the athlete library and converts it to swing.ppsw
// through SwingDocWriter::convertToPpsw: the same verified conversion pps_convert_library runs, so
// the in-app path and the tool cannot disagree. Each swing is proven against its original before
// its JSON is deleted; a failure leaves that swing exactly as it was and is named in the app log.
//
// Worker-thread, one swing at a time, cancellable between swings. Not part of AppSettings on
// purpose: AppSettings compiles into test targets that have no business linking the writer.

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class AppSettings;

class LibraryConverter : public QObject {
    Q_OBJECT
    // JSON-era swings in the library at the last count; -1 until one has run.
    Q_PROPERTY(int     pending READ pending NOTIFY changed)
    Q_PROPERTY(bool    running READ running NOTIFY changed)
    Q_PROPERTY(int     done    READ done    NOTIFY changed)   // this run: swings finished
    Q_PROPERTY(int     total   READ total   NOTIFY changed)   // this run: swings to do
    Q_PROPERTY(int     failed  READ failed  NOTIFY changed)   // this run: swings left as JSON
    Q_PROPERTY(QString status  READ status  NOTIFY changed)   // one line for the panel

public:
    explicit LibraryConverter(AppSettings *settings, QObject *parent = nullptr);
    ~LibraryConverter() override;

    int     pending() const { return m_pending; }
    bool    running() const { return m_running; }
    int     done()    const { return m_done; }
    int     total()   const { return m_total; }
    int     failed()  const { return m_failed; }
    QString status()  const { return m_status; }

    Q_INVOKABLE void count();     // re-count JSON-era swings (worker thread)
    Q_INVOKABLE void convert();   // convert them all (worker thread)
    Q_INVOKABLE void cancel();    // stop after the swing in hand

signals:
    void changed();

private:
    void progress(int done, int failed, const QString &lastError);

    AppSettings *m_settings = nullptr;
    int     m_pending = -1;
    bool    m_running = false;
    int     m_done = 0, m_total = 0, m_failed = 0;
    QString m_status;
    std::shared_ptr<std::atomic_bool> m_abort;
    QFutureWatcher<int> m_watcher;
};
