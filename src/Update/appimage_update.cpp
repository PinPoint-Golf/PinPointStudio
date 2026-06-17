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

#include "appimage_update.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

// appimageupdatetool flags. Pinned against the tool version bundled in the
// AppImage (impl plan P0); centralised here so a version bump is a one-line change.
static const QString kFlagOverwrite = QStringLiteral("--overwrite");

AppImageUpdater::AppImageUpdater(QObject *parent) : QObject(parent) {}

AppImageUpdater::~AppImageUpdater()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QString AppImageUpdater::runningAppImagePath()
{
    // The AppImage runtime exports $APPIMAGE = absolute path of the .AppImage file.
    return qEnvironmentVariable("APPIMAGE");
}

QString AppImageUpdater::toolPath()
{
    // Prefer the copy bundled next to the app binary (AppImage usr/bin), then PATH.
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("appimageupdatetool"));
    if (QFileInfo(bundled).isExecutable())
        return bundled;
    return QStandardPaths::findExecutable(QStringLiteral("appimageupdatetool"));
}

bool AppImageUpdater::available()
{
    return !runningAppImagePath().isEmpty() && !toolPath().isEmpty();
}

void AppImageUpdater::start()
{
    m_cancelled = false;
    m_lastProgress = 0.0;
    m_source = runningAppImagePath();

    if (m_source.isEmpty()) {
        emit finished(false, QString(), tr("Not running as an AppImage."));
        return;
    }
    const QString tool = toolPath();
    if (tool.isEmpty()) {
        emit finished(false, QString(), tr("appimageupdatetool not found."));
        return;
    }

    // Working copy is a sibling of $APPIMAGE (same filesystem) so the controller's
    // post-verify swap is an atomic same-dir rename, not a second multi-GB copy.
    // If the directory is not writable, the copy below fails → we report an error,
    // which is exactly the read-only-location handling the design calls for (§8).
    m_workingCopy = QFileInfo(m_source).absolutePath()
                    + QStringLiteral("/.PinPointStudio-update.AppImage");
    cleanupWorkingCopy();

    emit status(tr("Preparing update…"));
    emit progress(-1.0);

    // Copy off the GUI thread — the AppImage can be ~1–2 GB.
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, &AppImageUpdater::onCopyFinished);
    connect(watcher, &QFutureWatcher<bool>::finished, watcher, &QObject::deleteLater);
    const QString src = m_source, dst = m_workingCopy;
    watcher->setFuture(QtConcurrent::run([src, dst]() {
        return QFile::copy(src, dst);
    }));
}

void AppImageUpdater::onCopyFinished()
{
    if (m_cancelled)
        return;
    // sender() is the copy watcher (this slot is only connected to it); qobject_cast
    // doesn't work on the QFutureWatcher<T> template instantiation, so static_cast.
    auto *watcher = static_cast<QFutureWatcher<bool> *>(sender());
    const bool copied = watcher && watcher->result();
    if (!copied || !QFileInfo::exists(m_workingCopy)) {
        emit finished(false, QString(), tr("Could not stage the current application for update."));
        return;
    }

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);  // tool prints progress to stderr
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &AppImageUpdater::onProcReadyRead);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AppImageUpdater::onProcFinished);

    emit status(tr("Downloading update…"));
    m_proc->start(toolPath(), {kFlagOverwrite, m_workingCopy});
}

void AppImageUpdater::onProcReadyRead()
{
    if (!m_proc)
        return;
    static const QRegularExpression pctRe(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*%"));
    const QString chunk = QString::fromUtf8(m_proc->readAll());
    const auto lines = chunk.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const auto m = pctRe.match(line);
        if (m.hasMatch()) {
            bool ok = false;
            const double pct = m.captured(1).toDouble(&ok);
            if (ok) {
                m_lastProgress = qBound(0.0, pct / 100.0, 1.0);
                emit progress(m_lastProgress);
            }
        } else {
            emit status(line);
        }
    }
}

void AppImageUpdater::onProcFinished(int exitCode, int exitStatus)
{
    if (m_cancelled)
        return;
    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0)
                    && QFileInfo::exists(m_workingCopy);
    if (ok) {
        emit progress(1.0);
        emit finished(true, m_workingCopy, QString());
    } else {
        cleanupWorkingCopy();
        emit finished(false, QString(),
                      tr("Update download failed (exit %1).").arg(exitCode));
    }
}

void AppImageUpdater::cancel()
{
    m_cancelled = true;
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
    cleanupWorkingCopy();
}

void AppImageUpdater::cleanupWorkingCopy()
{
    if (m_workingCopy.isEmpty())
        return;
    QFile::remove(m_workingCopy);
    QFile::remove(m_workingCopy + QStringLiteral(".zs-old"));  // appimageupdatetool backup
}
