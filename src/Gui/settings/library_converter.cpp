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

#include "library_converter.h"

#include "../app/app_settings.h"
#include "../../Core/pp_debug.h"
#include "../../Export/swing_doc.h"
#include "../../Export/swing_store.h"

#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// The JSON-era swings: a swing.json and no swing.ppsw beside it (a pair is a conversion that did
// not finish, and convertToPpsw starts it again from the JSON — so it counts as pending too).
QStringList jsonSwings(const QString &root)
{
    QStringList out;
    for (const QString &d : pinpoint::SwingStore::swingDirsUnder(root))
        if (QFileInfo::exists(pinpoint::SwingStore::jsonPath(d)))
            out << d;
    return out;
}

} // namespace

LibraryConverter::LibraryConverter(AppSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
}

LibraryConverter::~LibraryConverter()
{
    if (m_abort) m_abort->store(true);
    // Bounded by one swing: the worker checks the flag between swings.
    m_watcher.disconnect();
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
}

void LibraryConverter::count()
{
    if (m_running || !m_settings) return;
    const QString root = m_settings->athleteLibraryPath();
    if (root.isEmpty()) return;

    m_watcher.disconnect();
    connect(&m_watcher, &QFutureWatcher<int>::finished, this, [this] {
        m_pending = m_watcher.result();
        emit changed();
    });
    m_watcher.setFuture(QtConcurrent::run([root]() -> int { return int(jsonSwings(root).size()); }));
}

void LibraryConverter::progress(int done, int failed, const QString &lastError)
{
    m_done   = done;
    m_failed = failed;
    m_status = lastError.isEmpty()
                   ? tr("Converting… %1 of %2").arg(done).arg(m_total)
                   : tr("Converting… %1 of %2 — %3 left as JSON").arg(done).arg(m_total).arg(failed);
    emit changed();
}

void LibraryConverter::convert()
{
    if (m_running || !m_settings) return;
    const QString root = m_settings->athleteLibraryPath();
    if (root.isEmpty()) return;

    m_running = true;
    m_done = m_failed = m_total = 0;
    m_status = tr("Finding swings…");
    emit changed();

    auto abort = std::make_shared<std::atomic_bool>(false);
    m_abort = abort;
    QPointer<LibraryConverter> self(this);

    m_watcher.disconnect();
    connect(&m_watcher, &QFutureWatcher<int>::finished, this, [this] {
        const int converted = m_watcher.result();
        m_running = false;
        m_pending = m_failed;   // what is still JSON: exactly the swings that failed (or were not reached)
        if (m_abort && m_abort->load())
            m_pending = -1;     // stopped part-way — the next count says how many remain
        m_status = m_failed == 0
                       ? tr("%1 swings converted to the new format.").arg(converted)
                       : tr("%1 converted; %2 could not be and were left as they were — see the log.")
                             .arg(converted).arg(m_failed);
        ppInfo() << "[LibraryConverter]" << converted << "converted," << m_failed << "failed";
        emit changed();
        if (m_pending < 0) count();
    });

    m_watcher.setFuture(QtConcurrent::run([root, abort, self]() -> int {
        const QStringList dirs = jsonSwings(root);
        QMetaObject::invokeMethod(self, [self, n = int(dirs.size())] {
            if (self) { self->m_total = n; self->progress(0, 0, {}); }
        }, Qt::QueuedConnection);

        int converted = 0, failed = 0, done = 0;
        for (const QString &d : dirs) {
            if (abort->load()) break;
            const pinpoint::SwingStore::ConvertResult r =
                pinpoint::SwingDocWriter::convertToPpsw(d, /*deleteJson=*/true);
            ++done;
            QString err;
            if (r.outcome == pinpoint::SwingStore::ConvertResult::Outcome::Converted) {
                ++converted;
            } else if (r.outcome == pinpoint::SwingStore::ConvertResult::Outcome::Failed) {
                ++failed;
                err = r.error;
                ppWarn() << "[LibraryConverter] left as JSON:" << d << "—" << r.error;
            }
            QMetaObject::invokeMethod(self, [self, done, failed, err] {
                if (self) self->progress(done, failed, err);
            }, Qt::QueuedConnection);
        }
        return converted;
    }));
}

void LibraryConverter::cancel()
{
    if (m_abort) m_abort->store(true);
}
