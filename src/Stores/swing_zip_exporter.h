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
// What the golfer takes away looks as it does on screen (#183): a clip recorded
// from a raw-Bayer camera, which is stored linear and plays dark elsewhere, goes
// in as a curved copy (export_tone.h: decoded frame by frame, mapped through the
// display tone table, encoded again as H.264), and thumb.jpg likewise when it came
// from such a stream (ruled: the Product Owner, 26 Nov 2026, #183 batch 3 (4)).
// Every other file is copied byte for byte, and the stored
// files are never written. If a copy can't be made (no encoder in the build, a
// clip that won't decode) the stored file goes in as it is, with a warning in the
// log. ⚠ The zip's swing.json still names the stream's Bayer source format, so a
// tool that read the zip as a recording would curve its clips twice; none does.
//
// Memory: the archive is streamed straight to the on-disk file via QZipWriter's
// filename constructor — the whole zip is never held in memory (a session may be
// multiple GB).  Peak RAM is one member at a time (QZipWriter buffers a single
// file while compressing it), bounded here by one swing's MP4 since `*.raw` is
// excluded.  A curved copy is made one decoded frame at a time into a file in a
// temporary folder (one clip on disk at once, removed once it is in the zip), so
// it adds one frame and the encoder's own buffers to that peak.  The build runs on a worker thread (QtConcurrent), mirroring the
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
