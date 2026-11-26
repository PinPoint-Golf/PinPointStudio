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

// RuleDataStore's reads that the norm stack needs (refactor step 7.1; P11: the rules open no file). The
// norm leaves in src/Diagnostics/content take RuleFile values; this is where their bytes are read, through
// the store's one reader and its PINPOINT_CORE_* seam. A translation unit of its own, apart from
// rule_data_store.cpp, so it links wherever the norm stack does (the offline tools and the light tests)
// without RuleData and the settings file coming with it. The store is still the one door to the files
// (FILE-APP-02, 04, 05): this is part of it.

#include "rule_data_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pinpoint {

using namespace pinpoint::analysis;

RuleFile RuleDataStore::readFile(const QString &path)
{
    if (path.isEmpty()) return RuleFile::absent(path);
    QFile f(path);
    if (!f.exists()) return RuleFile::absent(path);
    if (!f.open(QIODevice::ReadOnly)) return { path, RuleFile::State::Unreadable, {} };
    return RuleFile::read(path, f.readAll());
}

QString RuleDataStore::shippedPath(const char *env, const QString &resource)
{
    const QByteArray ov = qgetenv(env);
    return ov.isEmpty() ? resource : QString::fromLocal8Bit(ov);
}

} // namespace pinpoint

// ── The norm side, by path ──────────────────────────────────────────────────────────────────────
namespace pinpoint::analysis {

std::unique_ptr<INormProvider> makeResourceNormProvider(const QString &normsPath, const QString &contextsPath)
{
    // The tree's file is read first, as the leaf reports it first (resource_norm_provider.cpp).
    const RuleFile contexts = RuleDataStore::readFile(RuleDataStore::shippedPath("PINPOINT_CORE_CONTEXTS", contextsPath));
    const RuleFile norms    = RuleDataStore::readFile(RuleDataStore::shippedPath("PINPOINT_CORE_NORMS", normsPath));
    return makeResourceNormProvider(norms, contexts);
}

QStringList normSetFilesIn(const QString &directory)
{
    const QString dirPath = directory.isEmpty() ? diagnosticsDataDir() : directory;
    if (dirPath.isEmpty()) return {};

    const QDir dir(dirPath);
    if (!dir.exists()) return {};   // no user norms is the normal case, not a problem

    QStringList out;
    for (const QString &name : normSetFileOrder(
             dir.entryList({ QStringLiteral("*") + QLatin1String(kNormSetSuffix) }, QDir::Files, QDir::Name)))
        out << dir.filePath(name);
    return out;
}

std::unique_ptr<INormProvider> makeFileNormProvider(const QString &normsFile, PackOrigin origin,
                                                    const QString &contextsFile)
{
    // A tree that is absent is passed over in silence, as before; one that will not open too (the leaf).
    return makeFileNormProvider(RuleDataStore::readFile(normsFile.isEmpty() ? userNormPath() : normsFile), origin,
                                RuleDataStore::readFile(contextsFile));
}

std::vector<std::unique_ptr<INormProvider>> makeFileNormProviders(const QString &directory)
{
    const QString dirPath = directory.isEmpty() ? diagnosticsDataDir() : directory;
    // The directory's shared tree, under its fixed name: absent when there is no directory to look in.
    const RuleFile contexts = dirPath.isEmpty()
        ? RuleFile::absent(QString())
        : RuleDataStore::readFile(QDir(dirPath).filePath(QLatin1String(kContextTreeFileName)));

    std::vector<RuleFile> files;
    for (const QString &path : normSetFilesIn(dirPath)) files.push_back(RuleDataStore::readFile(path));
    return makeFileNormProviders(files, contexts);
}

std::unique_ptr<INormProvider> makeNormProvider()
{
    // ONE PROVIDER PER NORM-SET FILE, the same correction as makeCharacteristicPackProvider(). This
    // used to construct a single file provider over the whole directory, which could only hold one
    // set — so a second installed set was opened, parsed, had its issues added to the health list,
    // and then had every corridor in it dropped. See makeFileNormProviders() for the order, the
    // origin rule, and where the shared context tree goes.
    return makeMergedNormProvider(makeResourceNormProvider(), makeFileNormProviders());
}

void installNormFileFactory()
{
    // Called by a composition root, never from a static: nothing runs before main() (norm_provider.h).
    installNormProviderFactory([] { return makeNormProvider(); });
}

} // namespace pinpoint::analysis
