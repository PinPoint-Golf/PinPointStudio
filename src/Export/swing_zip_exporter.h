/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace pinpoint {

// SwingZipExporter — the shot carousel's bulk "export selected shots to a zip"
// action.  Exposed in main.cpp as the QML context property `swingExporter`.
//
// The zip is named after the session folder and lands in the user's HOME
// directory; extracting it reproduces the session: a top-level `<session>/`
// folder with one `swing_NNNN/` subfolder per shot.  Per shot we include
// thumb.jpg (always), the selected cameras' video files, and — only when the
// JSON toggle is on — swing.json plus any IMU sidecars.  Raw frame sidecars are
// never included.
//
// Memory: the archive is streamed straight to the on-disk file via QZipWriter's
// filename constructor — the whole zip is never held in memory (a session may be
// multiple GB).  Peak RAM is one member at a time (QZipWriter buffers a single
// file while compressing it), bounded here by one swing's MP4 since `*.raw` is
// excluded.  The build runs on a worker thread (QtConcurrent), mirroring the
// SwingExporter discipline: a self-contained value job, a stateless static
// worker, the result delivered back on the UI thread via QFutureWatcher.
class SwingZipExporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit SwingZipExporter(QObject *parent = nullptr);

    // UI thread.  Read each shot's swing.json and union its kind=="video"
    // streams, returning [{ "file": "<name>.mp4", "alias": "<label>" }, …]
    // de-duped by file (first-seen order) — the model for the options sheet's
    // camera checkboxes.  Missing/unreadable swing.json is skipped.
    Q_INVOKABLE QVariantList camerasForShots(const QVariantList &swingDirs) const;

    // UI thread.  Build ~/<session>.zip from the given absolute swing_NNNN dirs
    // on a worker thread.  selectedVideoFiles holds the stream "file" names whose
    // checkboxes are on; includeJson gates swing.json + IMU sidecars.  Emits
    // exportFinished(ok, zipPath, error) when done.  Ignored while busy.
    Q_INVOKABLE void exportShots(const QVariantList &swingDirs,
                                 const QStringList &selectedVideoFiles,
                                 bool includeJson);

    bool busy() const { return m_busy; }

signals:
    void exportStarted();
    void exportFinished(bool ok, const QString &zipPath, const QString &error);
    void busyChanged();

private:
    struct ZipResult {
        bool    ok = false;
        QString zipPath;
        QString error;
    };
    // Self-contained — everything the worker needs, resolved on the UI thread so
    // the lambda touches no member state.
    struct ZipJob {
        QStringList swingDirs;          // absolute swing_NNNN dirs (existing)
        QString     sessionName;        // common-parent leaf, e.g. 2026-06-09_…
        QString     outZipPath;         // ~/<session>.zip, uniquified on collision
        QStringList selectedVideoFiles; // stream "file" names to include
        bool        includeJson = false;
    };
    static ZipResult runZip(const ZipJob &job);   // worker thread

    QFutureWatcher<ZipResult> m_watcher;
    bool m_busy = false;
};

} // namespace pinpoint
