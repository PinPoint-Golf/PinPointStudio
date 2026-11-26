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

// isolated_stores_test (#187): with pinpoint::test::IsolatedStores on, the settings file, the app's own data folders
// and the standard locations are not the machine's, on every platform. It reads the app's own path helpers
// (ppSettings(), diagnosticsDataDir(), StudioProfileStore::studioDir(), RigStore::defaultFolder()), so a renamed seam
// fails here, not on a studio PC.
// The real locations are computed with test mode off and NO store opened: nothing here reads or writes a real store.

#include "isolated_stores.h"

#include "../pp_settings.h"
#include "../../App/studio_profile.h"
#include "../../Stores/studio_profile_store.h"
#include "../../Stores/rig_store.h"
#include "../../Diagnostics/engine/pack_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdio>
#include <iterator>

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

struct Loc
{
    QStandardPaths::StandardLocation type;
    const char                      *name;
};

const Loc kMoved[] = {
    { QStandardPaths::AppDataLocation, "AppDataLocation" },
    { QStandardPaths::AppLocalDataLocation, "AppLocalDataLocation" },
    { QStandardPaths::GenericDataLocation, "GenericDataLocation" },
    { QStandardPaths::CacheLocation, "CacheLocation" },
    { QStandardPaths::GenericCacheLocation, "GenericCacheLocation" },
    { QStandardPaths::AppConfigLocation, "AppConfigLocation" },
    { QStandardPaths::GenericConfigLocation, "GenericConfigLocation" },
};

const Loc kReported[] = {
    { QStandardPaths::HomeLocation, "HomeLocation" },
    { QStandardPaths::DocumentsLocation, "DocumentsLocation" },
    { QStandardPaths::DesktopLocation, "DesktopLocation" },
};

} // namespace

int main(int argc, char **argv)
{
    // What the locations are WITHOUT the helper: test mode off, before anything else, and no QSettings made.
    QStandardPaths::setTestModeEnabled(false);
    qunsetenv("PINPOINT_DIAGNOSTICS_DIR");
    qunsetenv("PINPOINT_STUDIO_DIR");
    qunsetenv("PINPOINT_RIG_DIR");
    QCoreApplication app(argc, argv);
    QString real[std::size(kMoved)];
    for (size_t i = 0; i < std::size(kMoved); ++i) real[i] = QStandardPaths::writableLocation(kMoved[i].type);
    QString realReported[std::size(kReported)];
    for (size_t i = 0; i < std::size(kReported); ++i)
        realReported[i] = QStandardPaths::writableLocation(kReported[i].type);
    const QString realDiag   = pinpoint::analysis::diagnosticsDataDir();

    pinpoint::test::IsolatedStores stores;
    std::printf("isolated_stores_test — the temporary directory: %s\n", qPrintable(stores.path()));

    std::printf("\nthe settings file (ppSettings: the settings and the phone pairings)\n");
    {
        QSettings s = ppSettings();
        std::printf("  ppSettings().fileName() = %s\n", qPrintable(s.fileName()));
        check(stores.contains(s.fileName()), "S1 ppSettings() is a file inside the temporary directory");
        s.setValue(QStringLiteral("isolated_stores_test/probe"), 1);
        s.sync();
        check(s.status() == QSettings::NoError && QFileInfo::exists(s.fileName()) && stores.contains(s.fileName()),
              "S2 a value written through ppSettings() lands in that file");
        check(ppSettings().value(QStringLiteral("isolated_stores_test/probe")).toInt() == 1,
              "S3 and the next ppSettings() reads it back from there");
        QSettings sys(QSettings::IniFormat, QSettings::SystemScope, QStringLiteral("PinPointStudio"),
                      QStringLiteral("PinPointStudio"));
        check(stores.contains(sys.fileName()), "S4 the IniFormat system-scope file is inside it too");
    }

    std::printf("\nthe app's own folders\n");
    {
        const QString diag = pinpoint::analysis::diagnosticsDataDir();
        std::printf("  diagnosticsDataDir() = %s (without the helper: %s)\n", qPrintable(diag), qPrintable(realDiag));
        check(stores.contains(diag) && QDir::cleanPath(diag) == stores.diagnosticsDir(),
              "D1 the user diagnostics folder (packs, norms, screens, drills, gate words, references) is inside it");
        check(stores.contains(pinpoint::analysis::diagnosticsFilePath(QStringLiteral("user_pack.json"))),
              "D2 and so is a file in it");
        // The studio folder is the studio profile's store's: an imported logo is copied into it, and its path kept.
        const QString svg = stores.path() + QStringLiteral("/logo-source.svg");
        {
            QFile f(svg);
            f.open(QIODevice::WriteOnly);
            f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"/>");
        }
        StudioProfileStore studioStore;
        StudioProfile profile(studioStore);
        const bool imported = profile.importLogo(svg);
        std::printf("  StudioProfile logo copy = %s (the studio folder without the helper: %s/studio)\n",
                    qPrintable(profile.logoPath()), qPrintable(real[0]));
        check(imported && stores.contains(profile.logoPath()) && QFileInfo::exists(profile.logoPath())
                  && QDir::cleanPath(QFileInfo(profile.logoPath()).absolutePath()) == stores.studioDir(),
              "D3 the studio profile's folder (an imported logo's copy) is inside it");
        check(stores.contains(stores.libraryRoot()) && QFileInfo(stores.libraryRoot()).isDir(),
              "D4 libraryRoot() is a folder inside it");
        // The Rig's folder (FILE-APP-07): the store the composition root makes reads RigStore::defaultFolder().
        const QString rig = RigStore::defaultFolder();
        std::printf("  RigStore::defaultFolder() = %s (without the helper: %s/rig)\n", qPrintable(rig), qPrintable(real[0]));
        check(stores.contains(rig) && rig == QDir::cleanPath(stores.rigDir()) && stores.contains(RigStore(rig).filePath()),
              "D5 the Rig's folder (rig.json) is inside it");
    }

    std::printf("\nthe standard locations (test mode: Qt's own test folders, never the real ones)\n");
    for (size_t i = 0; i < std::size(kMoved); ++i) {
        const QString now = QStandardPaths::writableLocation(kMoved[i].type);
        std::printf("  %-22s %s  (real: %s)\n", kMoved[i].name, qPrintable(now), qPrintable(real[i]));
        check(!now.isEmpty() && QDir::cleanPath(now) != QDir::cleanPath(real[i]),
              QStringLiteral("L%1 %2 is not the real one").arg(i + 1).arg(QLatin1String(kMoved[i].name)));
    }
    {
        const QString fallback     = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/swings");
        const QString realFallback = real[0] + QStringLiteral("/swings");
        check(QDir::cleanPath(fallback) != QDir::cleanPath(realFallback),
              "L8 the library's no-root fallback (AppDataLocation/swings) is not the real one");
    }
    for (size_t i = 0; i < std::size(kReported); ++i) {
        const QString now = QStandardPaths::writableLocation(kReported[i].type);
        std::printf("  %-22s %s  (real: %s)%s\n", kReported[i].name, qPrintable(now), qPrintable(realReported[i]),
                    QDir::cleanPath(now) == QDir::cleanPath(realReported[i]) ? "  NOT MOVED by test mode" : "");
    }

    std::printf("\ncontains()\n");
    {
        const QString root = stores.path();
        check(stores.contains(root), "C1 the directory itself");
        check(stores.contains(root + QStringLiteral("/not/made/yet.json")), "C2 a file not written yet");
        check(!stores.contains(root + QStringLiteral("x/settings")), "C3 a sibling whose name starts the same is not");
        check(!stores.contains(root + QStringLiteral("/../outside")), "C4 a path that climbs out is not");
        check(!stores.contains(QString()), "C5 an empty path is not");
        check(!stores.contains(QDir::homePath()), "C6 the home folder is not");
        check(stores.contains(QDir::toNativeSeparators(root + QStringLiteral("/settings"))),
              "C7 native separators are understood");
#ifdef Q_OS_MACOS
        // QTemporaryDir is under /var/folders, which is /private/var/folders: both spellings are inside.
        QString other = root;
        if (other.startsWith(QStringLiteral("/private/var/"))) other.remove(0, 8);
        check(stores.contains(other + QStringLiteral("/settings")), "C8 the /var spelling of /private/var is inside");
#endif
#ifdef Q_OS_WIN
        check(stores.contains(root.toUpper() + QStringLiteral("/settings")), "C8 case does not matter on Windows");
#endif
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
