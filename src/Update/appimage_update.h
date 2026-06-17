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

class QProcess;

// Thin driver over the bundled `appimageupdatetool` CLI (the AppImageUpdate
// project), invoked via QProcess. This is the v1 transport for Linux in-app
// updates — see docs/design/linux_update.md §3 (as-built: the CLI rather than
// embedding libappimageupdate, so the controller stays pure Qt with no
// gpgme/libcurl/zsync2 build deps).
//
// Hard invariant (design §9): the **live** $APPIMAGE is NEVER the zsync target.
// start() copies $APPIMAGE to a sibling working file (same dir → cheap atomic swap)
// and runs the tool on the COPY, so an aborted/failed update can never leave
// unverified bytes as the live binary.
// The verified copy is renamed over $APPIMAGE by UpdateController only AFTER the
// signature gate passes (design §6). The doubled local disk I/O is the accepted
// cost of the CLI path; zsync still transfers only the delta.
//
// Lifecycle: a fresh AppImageUpdater is used per download attempt.
class AppImageUpdater : public QObject
{
    Q_OBJECT
public:
    explicit AppImageUpdater(QObject *parent = nullptr);
    ~AppImageUpdater() override;

    // Path of the running AppImage (the $APPIMAGE env var the runtime sets), or
    // empty when not launched from an AppImage (dev build / extracted tree).
    static QString runningAppImagePath();

    // Resolve `appimageupdatetool`: next to the app binary first (bundled in the
    // AppImage's usr/bin), then on PATH. Empty if unavailable.
    static QString toolPath();

    // True when both a tool and a running-AppImage path are present, i.e. an
    // in-app update can actually be performed on this process.
    static bool available();

    // Copy $APPIMAGE → cache working file, then run the tool on the copy. Progress
    // is parsed from the tool's stdout. Emits finished() exactly once.
    // Does NOT touch the live $APPIMAGE.
    void start();

    // Abort an in-flight download (kills the process, removes the partial copy).
    void cancel();

private slots:
    void onCopyFinished();
    void onProcReadyRead();
    void onProcFinished(int exitCode, int exitStatus);

private:
    void cleanupWorkingCopy();

    QProcess *m_proc = nullptr;
    QString   m_source;        // live $APPIMAGE
    QString   m_workingCopy;   // cache copy we actually update
    double    m_lastProgress = 0.0;
    bool      m_cancelled = false;

signals:
    // 0..1, or -1.0 when the tool has not yet reported a parseable percentage.
    void progress(double fraction);
    // A human-readable status line from the tool (surfaced to the UI log).
    void status(const QString &line);
    // ok=true → workingCopyPath holds the assembled, unverified new AppImage.
    // ok=false → error describes the failure; no file is left for the caller.
    void finished(bool ok, const QString &workingCopyPath, const QString &error);
};
