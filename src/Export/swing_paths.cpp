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

#include "swing_paths.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "session_files.h"

#include "../Core/atomic_write.h"
#include "../Core/pp_debug.h"

namespace pinpoint {

// Highest "_NN" index among "<base>_(\d+)" session folders under athleteDir
// (0 when none exist).
static int maxSessionIndex(const QString& athleteDir, const QString& base)
{
    int maxIndex = 0;
    const QRegularExpression sessionRe(
        QStringLiteral("^%1_(\\d+)$").arg(QRegularExpression::escape(base)));
    const QStringList existing =
        QDir(athleteDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : existing) {
        const auto m = sessionRe.match(name);
        if (m.hasMatch())
            maxIndex = std::max(maxIndex, m.captured(1).toInt());
    }
    return maxIndex;
}

QString SwingPaths::sanitise(const QString& raw)
{
    QString s = raw.trimmed();
    // Whitespace and path-hostile characters become '-'.
    static const QRegularExpression hostile(QStringLiteral("[^A-Za-z0-9._-]+"));
    s.replace(hostile, QStringLiteral("-"));
    // Collapse runs of separators and strip them from the ends.
    static const QRegularExpression runs(QStringLiteral("-{2,}"));
    s.replace(runs, QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-')) || s.startsWith(QLatin1Char('.')))
        s.removeFirst();
    // ⚠ TRUNCATE FIRST, THEN STRIP THE TAIL — in that order, and it used to be
    // the other way round.  Cutting a long name at 64 can land the cut on a
    // separator, and Windows silently drops a trailing '.' from a directory
    // name: the folder the app then computes and the folder that exists are
    // different strings, and every later lookup misses one it created itself.
    s.truncate(64);
    while (s.endsWith(QLatin1Char('-')) || s.endsWith(QLatin1Char('.')))
        s.removeLast();
    if (s.isEmpty())
        return QStringLiteral("unknown");

    // ⚠ AND A NAME CAN BE LEGAL EVERYWHERE AND IMPOSSIBLE ON WINDOWS.  "Con" is
    // an ordinary surname and a reserved device; so are Prn, Aux, Nul, Com1-9
    // and Lpt1-9, with or without an extension ("CON.txt" is reserved too), so
    // the test is on the stem.  mkpath fails for such a folder on that platform
    // and nowhere else, which is the worst shape a bug can have.  A trailing
    // '_' is enough to clear it, is inside the allowed character class, and
    // survives a second pass through here unchanged (sanitise is idempotent).
    static const QStringList reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
        QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"),
        QStringLiteral("COM4"), QStringLiteral("COM5"), QStringLiteral("COM6"),
        QStringLiteral("COM7"), QStringLiteral("COM8"), QStringLiteral("COM9"),
        QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"),
        QStringLiteral("LPT7"), QStringLiteral("LPT8"), QStringLiteral("LPT9"),
    };
    const QString stem = s.left(s.indexOf(QLatin1Char('.')) < 0 ? s.size()
                                                                : s.indexOf(QLatin1Char('.')));
    for (const QString& r : reserved) {
        if (stem.compare(r, Qt::CaseInsensitive) == 0) {
            s.append(QLatin1Char('_'));
            break;
        }
    }
    return s;
}

QString SwingPaths::sessionFolderBase(const QString& namingPattern, const QDate& day,
                                      const QString& athleteToken, const QString& sessionTypeLabel)
{
    // Tokens are filesystem-sanitised; the athlete token doubles as the name.
    const QString date    = day.toString(Qt::ISODate);
    const QString nameTok = athleteToken;
    const QString typeTok = sanitise(sessionTypeLabel.trimmed().isEmpty()
                                         ? QStringLiteral("session") : sessionTypeLabel);
    if (namingPattern == QLatin1String("date-type-name"))
        return date + QLatin1Char('_') + typeTok + QLatin1Char('_') + nameTok;
    if (namingPattern == QLatin1String("name-date-type"))
        return nameTok + QLatin1Char('_') + date + QLatin1Char('_') + typeTok;
    if (namingPattern == QLatin1String("date-only"))
        return date;
    return date + QLatin1Char('_') + nameTok + QLatin1Char('_') + typeTok;   // "date-name-type" (default)
}

QString SwingPaths::sessionFolderName(const QString& base, int counter)
{
    return QStringLiteral("%1_%2").arg(base).arg(counter, 2, 10, QLatin1Char('0'));
}

int SwingPaths::nextSessionCounter(const QString& athleteDir, const QString& base)
{
    return maxSessionIndex(athleteDir, base) + 1;
}

QString SwingPaths::swingFolderName(int swingNumber)
{
    if (swingNumber < 1)
        return {};
    return QStringLiteral("swing_%1").arg(swingNumber, 4, 10, QLatin1Char('0'));
}

// The number in a "swing_NNNN" folder name, or 0 when it carries none.
int SwingPaths::swingNumberOf(const QString& name)
{
    if (!name.startsWith(QStringLiteral("swing_")))
        return 0;
    bool ok = false;
    const int n = name.mid(6).toInt(&ok);
    return ok && n > 0 ? n : 0;
}

QStringList SwingPaths::swingFolders(const QString& sessionDir)
{
    return QDir(sessionDir).entryList({QStringLiteral("swing_*")}, QDir::Dirs | QDir::NoDotAndDotDot);
}

void SwingPaths::visitLibrarySwings(const QString& libraryRoot,
                                    const std::function<bool(const QString&, const QString&)>& visit)
{
    const QDir root(libraryRoot);
    for (const QString& athlete : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir aDir(root.filePath(athlete));
        for (const QString& session : aDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QDir sDir(aDir.filePath(session));
            for (const QString& swing : swingFolders(sDir.path()))
                if (!visit(session, sDir.filePath(swing))) return;
        }
    }
}

bool SwingPaths::trashPath(const QString& path, QString* where, bool useOsTrash)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;
    const QFileInfo fi(path);
    const QString   name    = fi.fileName();
    const bool      isSwing = name.startsWith(QStringLiteral("swing_"));
    const QString   parent  = QDir::cleanPath(fi.absolutePath());
    // A swing leaving its session takes its number with it, never back into
    // circulation (SessionRepository::allocateSwing). A session recorded before the high-water
    // existed has none, so the swing that leaves is what records it — whichever
    // trash it goes to.
    if (isSwing) {
        if (const AtomicWriteResult w = SessionFiles::raiseSwingHighWater(parent, swingNumberOf(name)); !w.ok)
            ppWarn() << "[SwingExport] could not record the last swing number in" << parent << "-" << w.detail;
    }
    if (useOsTrash && QFile::moveToTrash(path)) {
        if (where) *where = QStringLiteral("the system trash");
        return true;
    }
    // The volume has no trash (a network share).  Same volume, so this is a
    // rename: `<root>/.pinpoint-trash/<athlete>/<stamp>_[<session>_]<name>`.
    // A swing folder sits in a session which sits in an athlete folder; a
    // session sits in an athlete folder.
    const QString   athleteDir = isSwing ? QDir::cleanPath(parent + QStringLiteral("/.."))
                                         : parent;
    const QString   root       = QDir::cleanPath(athleteDir + QStringLiteral("/.."));
    const QString   athlete    = QFileInfo(athleteDir).fileName();
    const QString   trashDir   = root + QStringLiteral("/.pinpoint-trash/") + athlete;
    if (!QDir().mkpath(trashDir)) {
        ppWarn() << "[SwingPaths] no trash on this volume and could not create" << trashDir;
        return false;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString tag   = isSwing ? QFileInfo(parent).fileName() + QStringLiteral("_") : QString();
    QString dest = trashDir + QStringLiteral("/") + stamp + QStringLiteral("_") + tag + name;
    for (int n = 2; QFileInfo::exists(dest) && n < 100; ++n)
        dest = trashDir + QStringLiteral("/") + stamp + QStringLiteral("_") + tag + name
               + QStringLiteral("-") + QString::number(n);
    if (!QDir().rename(path, dest)) {
        ppWarn() << "[SwingPaths] no trash on this volume and could not move" << path
                 << "to" << dest;
        return false;
    }
    ppInfo() << "[SwingPaths] no trash on this volume — moved to" << dest;
    if (where) *where = dest;
    return true;
}

// The library's own trash (refactor step 3.8): moved here from the Archiving tab's controller, unchanged — the
// entries of every athlete folder of `.pinpoint-trash`, the stamp trashPath writes, and the one purge.
QStringList SwingPaths::trashEntries(const QString& libraryRoot)
{
    QStringList out;
    const QDir t(libraryRoot + QStringLiteral("/.pinpoint-trash"));
    for (const QString& athlete : t.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir a(t.filePath(athlete));
        for (const QString& e : a.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
            out << a.filePath(e);
    }
    return out;
}

QDateTime SwingPaths::trashedAt(const QString& entry)
{
    const QDateTime t = QDateTime::fromString(QFileInfo(entry).fileName().left(15),
                                              QStringLiteral("yyyyMMdd-HHmmss"));
    return t.isValid() ? t : QFileInfo(entry).lastModified();
}

qint64 SwingPaths::libraryBytes(const QString& libraryRoot, const std::atomic_bool* abort)
{
    QDirIterator it(libraryRoot, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    qint64 total = 0;
    int    n     = 0;
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        // Polled rather than checked every entry: on a network share the abort flag is far cheaper than the stat
        // beside it, but the loop runs tens of thousands of times and there is no reason to make quitting more
        // responsive than one batch of files.
        if ((++n & 0xFF) == 0 && abort && abort->load())
            return -1;
    }
    return total;
}

SwingPaths::Purged SwingPaths::purgeTrash(const QString& libraryRoot, const QDateTime& cutoff)
{
    Purged p;
    for (const QString& e : trashEntries(libraryRoot)) {
        if (cutoff.isValid() && !(trashedAt(e) < cutoff)) continue;
        const QFileInfo fi(e);
        const bool removed = fi.isDir() ? QDir(e).removeRecursively() : QFile::remove(e);
        (removed ? p.removed : p.failed)++;
    }
    return p;
}

} // namespace pinpoint
