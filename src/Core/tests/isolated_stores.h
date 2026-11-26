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

// pinpoint::test::IsolatedStores: the ONE way a test suite keeps off the machine's per-user stores (#187).
//
// Make one before the suite touches a store, and keep it alive for the whole run:
//   - a main() suite: the first line of main(), before QCoreApplication;
//   - a QTest suite: the FIRST member of the test class (so it is made before every other member, and before
//     initTestCase);
//   - a gtest suite with its own main(): the first line of that main(); with gtest_main, a file-scope static.
// store_isolation_lint_test fails a suite that makes it anywhere else (#258).
// It is test-only code (header-only, no library). Include it by relative path, e.g. from src/<Dir>/tests/:
//   #include "../../Core/tests/isolated_stores.h"
//
// What it does, in its constructor, against one fresh QTemporaryDir per process (so two runs at once, or two
// build trees, never share a file):
//   - QSettings::setPath for IniFormat and NativeFormat, UserScope and SystemScope, to <tmp>/settings. ppSettings()
//     (src/Core/pp_settings.h, IniFormat + UserScope, the settings and the phone pairings) is then
//     <tmp>/settings/PinPointStudio/PinPointStudio.ini on every platform. NativeFormat's setPath is honoured only on
//     Linux; on Windows (the registry) and macOS (CFPreferences) Qt ignores it. No product code uses NativeFormat today
//     (every module goes through ppSettings()).
//   - QStandardPaths::setTestModeEnabled(true). Qt cannot point the standard locations into a temporary directory on
//     macOS or Windows (only Linux honours XDG_*), so they move to Qt's test locations instead: ~/.qttest/... on
//     macOS and Linux, %APPDATA%\qttest\..., %LOCALAPPDATA%\qttest\... on Windows. Those are off the real profile,
//     but shared by every run of the same suite. On macOS test mode moves Home, Documents and the Desktop too
//     (~/.qttest, isolated_stores_test prints them); on Windows it leaves them where they are, so a suite that
//     writes there needs its own guard.
//   - The app's own path helpers, pointed into the temporary directory through their seams:
//       PINPOINT_DIAGNOSTICS_DIR = <tmp>/diagnostics   (pinpoint::analysis::diagnosticsDataDir(): every user pack,
//                                                       norm set, screen, drill, gate-word and reference layer)
//       PINPOINT_STUDIO_DIR      = <tmp>/studio        (StudioProfileStore::studioDir(): the report's logo and photo)
//       PINPOINT_RIG_DIR         = <tmp>/rig           (RigStore::defaultFolder(): the machine's rig, rig.json)
//     A suite that sets one again afterwards (report_studio_test does) wins.
//   - libraryRoot() is <tmp>/library, made, for a suite to hand to a store as its library root. The stores' own
//     fallback when no root is set (AppDataLocation + "/swings", athlete_store.cpp, session_store.cpp) has no seam:
//     under this helper it is the test location above, never the real one.
//
// It never restores anything in its destructor (a store written after it would land in the real place); the
// directory is removed, and the process ends soon after. It refuses to run (qFatal) when no temporary directory
// can be made, rather than fall back to a real store.
//
// contains(path) says whether a path lies inside the temporary directory, canonicalised on both sides (macOS's
// /var -> /private/var, Windows' case and short names), for a path that does not exist yet too.
//
// Guards: isolated_stores_test (src/Core/tests) proves the redirections on every platform;
// store_isolation_lint_test (the tests umbrella) fails when an executable compiles or links code that calls
// ppSettings() and does not make an IsolatedStores first. docs/developer/testing_developer_guide.md §9 has the how-to.

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

namespace pinpoint::test {

class IsolatedStores
{
public:
    IsolatedStores()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (!m_dir.isValid())
            qFatal("IsolatedStores: no temporary directory (%s), so the suite would reach a real store",
                   qPrintable(m_dir.errorString()));
        m_root = QFileInfo(m_dir.path()).canonicalFilePath();
        if (m_root.isEmpty()) m_root = QDir::cleanPath(m_dir.path());
        for (const QString &sub : { settingsDir(), diagnosticsDir(), studioDir(), rigDir(), libraryRoot() })
            if (!QDir().mkpath(sub)) qFatal("IsolatedStores: cannot make %s", qPrintable(sub));

        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir() + QStringLiteral("/system"));
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir());
        QSettings::setPath(QSettings::NativeFormat, QSettings::SystemScope, settingsDir() + QStringLiteral("/system"));

        qputenv("PINPOINT_DIAGNOSTICS_DIR", QDir::toNativeSeparators(diagnosticsDir()).toLocal8Bit());
        qputenv("PINPOINT_STUDIO_DIR", QDir::toNativeSeparators(studioDir()).toLocal8Bit());
        qputenv("PINPOINT_RIG_DIR", QDir::toNativeSeparators(rigDir()).toLocal8Bit());
    }

    IsolatedStores(const IsolatedStores &)            = delete;
    IsolatedStores &operator=(const IsolatedStores &) = delete;

    QString path() const { return m_root; }
    QString settingsDir() const { return m_root + QStringLiteral("/settings"); }
    QString diagnosticsDir() const { return m_root + QStringLiteral("/diagnostics"); }
    QString studioDir() const { return m_root + QStringLiteral("/studio"); }
    QString rigDir() const { return m_root + QStringLiteral("/rig"); }
    QString libraryRoot() const { return m_root + QStringLiteral("/library"); }

    // Whether `path` lies inside the temporary directory (the directory itself counts). The nearest existing
    // ancestor is canonicalised and the rest appended, so a file not yet written is judged the same way.
    bool contains(const QString &path) const
    {
        if (path.isEmpty()) return false;
        const QString p = canonical(path);
#ifdef Q_OS_WIN
        const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
        const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
        return p.compare(m_root, cs) == 0 || p.startsWith(m_root + QLatin1Char('/'), cs);
    }

    static QString canonical(const QString &path)
    {
        QString rest;
        QString at   = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
        while (!at.isEmpty()) {
            const QFileInfo fi(at);
            if (fi.exists()) {
                const QString c = fi.canonicalFilePath();
                return QDir::cleanPath((c.isEmpty() ? at : c) + rest);
            }
            const int slash = at.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0) break;
            rest.prepend(at.mid(slash));
            at.truncate(slash);
        }
        return QDir::cleanPath(at + rest);
    }

private:
    QTemporaryDir m_dir;
    QString       m_root;
};

} // namespace pinpoint::test
