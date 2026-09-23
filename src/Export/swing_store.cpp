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

#include "swing_store.h"

#include "ppsw_qt.h"

#include <ppswing/json.h>
#include <ppswing/ppswing.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

namespace pinpoint::SwingStore {

namespace {

constexpr auto kSummaryKey = "summary";

// libppswing takes UTF-8 paths. On Windows it opens them through the wide API (file.cpp), so
// a library under a non-ASCII user name still works.
std::string nativePath(const QString &p) { return p.toStdString(); }

DocInfo infoOf(const QString &path, Format f)
{
    const QFileInfo fi(path);
    if (!fi.exists()) return {};
    DocInfo d;
    d.format  = f;
    d.path    = path;
    d.size    = fi.size();
    d.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    return d;
}

QJsonObject loadJsonFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("cannot parse %1: %2").arg(path, pe.errorString());
        return {};
    }
    return doc.object();
}

QJsonObject loadPpswFile(const QString &path, QString *error)
{
    try {
        const ppsw::Reader r = ppsw::Reader::open(nativePath(path));
        const QJsonObject root = ppswqt::toQtObject(r.loadAll());
        if (root.isEmpty() && error)
            *error = QStringLiteral("%1 holds no document object").arg(path);
        return root;
    } catch (const std::exception &e) {
        if (error) *error = QStringLiteral("cannot read %1: %2").arg(path, QString::fromUtf8(e.what()));
        return {};
    }
}

} // namespace

QString ppswPath(const QString &swingDir)          { return swingDir + QStringLiteral("/swing.ppsw"); }
QString jsonPath(const QString &swingDir)          { return swingDir + QStringLiteral("/swing.json"); }
QString legacySummaryPath(const QString &swingDir) { return swingDir + QStringLiteral("/swing_summary.json"); }

DocInfo info(const QString &swingDir)
{
    if (swingDir.isEmpty()) return {};
    const DocInfo p = infoOf(ppswPath(swingDir), Format::Ppsw);
    if (p.exists()) return p;
    return infoOf(jsonPath(swingDir), Format::Json);
}

bool hasDocument(const QString &swingDir) { return info(swingDir).exists(); }

QJsonObject load(const QString &swingDir, QString *error)
{
    const DocInfo d = info(swingDir);
    switch (d.format) {
        case Format::Ppsw: return loadPpswFile(d.path, error);
        case Format::Json: return loadJsonFile(d.path, error);
        case Format::None: break;
    }
    if (error) *error = QStringLiteral("no swing document in %1").arg(swingDir);
    return {};
}

QJsonObject loadSummaryBlock(const QString &swingDir)
{
    const QString path = ppswPath(swingDir);
    if (!QFileInfo::exists(path)) return {};
    try {
        const ppsw::Reader r = ppsw::Reader::open(nativePath(path));
        const ppsw::Value root = r.root();
        const ppsw::Value *s = root.get(kSummaryKey);
        if (!s) return {};
        // A summary is a few KB and always lands in the root chunk, but a chunk boundary is the
        // writer's decision (splitBytes), not a promise — follow a ref rather than assume.
        if (s->kind() == ppsw::Value::Kind::Ref)
            return ppswqt::toQtObject(r.chunk(s->asRef().name));
        return ppswqt::toQtObject(*s);
    } catch (const std::exception &) {
        return {};
    }
}

bool writeFile(const QString &path, const QJsonObject &root, QString *error)
{
    try {
        // Lossless (the Phase 2 decision): no precision hints, so every value reads back exactly.
        ppsw::writeFile(nativePath(path), ppswqt::fromQt(root));
        return true;
    } catch (const std::exception &e) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, QString::fromUtf8(e.what()));
        return false;
    }
}

bool save(const QString &swingDir, const QJsonObject &root, QString *error)
{
    if (!writeFile(ppswPath(swingDir), root, error))
        return false;
    // The rewrite supersedes a JSON-era document and its sidecar. Left behind, a stale swing.json
    // would be invisible (swing.ppsw wins) but would still be copied, backed up and grepped.
    QFile::remove(jsonPath(swingDir));
    QFile::remove(legacySummaryPath(swingDir));
    return true;
}

ConvertResult convertDir(const QString &swingDir, const SummaryFn &summaryFn, bool deleteJson)
{
    ConvertResult r;
    const QString json = jsonPath(swingDir);
    const QString ppswFile = ppswPath(swingDir);
    const bool haveJson = QFileInfo::exists(json);
    if (QFileInfo::exists(ppswFile) && !haveJson) {
        r.outcome   = ConvertResult::Outcome::AlreadyPpsw;
        r.ppswBytes = QFileInfo(ppswFile).size();
        return r;
    }
    if (!haveJson) {
        r.outcome = ConvertResult::Outcome::NoDocument;
        return r;
    }

    QFile f(json);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("cannot read %1: %2").arg(json, f.errorString());
        return r;
    }
    const QByteArray text = f.readAll();
    f.close();
    r.jsonBytes = text.size();

    // A swing.ppsw beside a swing.json is a previous attempt that did not finish (it would have
    // deleted the JSON). The JSON is the original; start again from it, and on any failure remove
    // whatever .ppsw is there — while the JSON exists, the .ppsw is only ever derived from it.
    try {
        ppsw::Value original = ppsw::parseJson(std::string_view(text.constData(), size_t(text.size())));
        if (original.kind() != ppsw::Value::Kind::Object) {
            r.error = QStringLiteral("%1 is not a JSON object").arg(json);
            return r;
        }
        if (original.get(kSummaryKey)) {
            r.error = QStringLiteral("%1 already carries a top-level \"summary\"").arg(json);
            return r;
        }

        // The summary is built from Qt's reading of the same text — it is the app's function, and
        // it only ever sees what the app would see.
        QJsonObject summary;
        if (summaryFn) {
            QJsonParseError pe{};
            const QJsonObject qroot = QJsonDocument::fromJson(text, &pe).object();
            if (pe.error == QJsonParseError::NoError)
                summary = summaryFn(qroot, swingDir);
        }

        ppsw::Value tree = original;
        if (!summary.isEmpty())
            tree.asObject().add(kSummaryKey, ppswqt::fromQt(summary));
        ppsw::writeFile(nativePath(ppswFile), tree);

        // Verify from the file, with a reader that shares nothing with the writer's state.
        ppsw::Value back = ppsw::Reader::open(nativePath(ppswFile)).loadAll();
        if (back.kind() != ppsw::Value::Kind::Object) {
            QFile::remove(ppswFile);
            r.error = QStringLiteral("%1 read back as a non-object").arg(ppswFile);
            return r;
        }
        auto &members = back.asObject().members;
        bool sawSummary = false;
        for (auto it = members.begin(); it != members.end(); ++it) {
            if (it->first == kSummaryKey) { members.erase(it); sawSummary = true; break; }
        }
        std::string diff;
        if (sawSummary == summary.isEmpty() || !ppsw::semanticEqual(back, original, &diff)) {
            QFile::remove(ppswFile);
            r.error = sawSummary == summary.isEmpty()
                          ? QStringLiteral("summary block lost in the round trip")
                          : QStringLiteral("round trip differs at %1")
                                .arg(QString::fromStdString(diff.empty() ? std::string("/") : diff));
            return r;
        }
    } catch (const std::exception &e) {
        QFile::remove(ppswFile);
        r.error = QStringLiteral("%1: %2").arg(json, QString::fromUtf8(e.what()));
        return r;
    }

    r.ppswBytes = QFileInfo(ppswFile).size();
    if (deleteJson) {
        if (!QFile::remove(json)) {
            r.error = QStringLiteral("converted and verified, but cannot delete %1").arg(json);
            return r;
        }
        QFile::remove(legacySummaryPath(swingDir));
    }
    r.outcome = ConvertResult::Outcome::Converted;
    return r;
}

namespace {
void collectSwingDirs(const QString &dir, QStringList &out, int depth)
{
    if (hasDocument(dir))
        out << dir;
    if (depth > 8) return;   // library/athlete/session/swing is four; never follow a loop forever
    // An explicit walk, not QDirIterator::Subdirectories: that RECURSES into hidden directories even
    // when QDir::Hidden keeps them out of the listing, and .pinpoint-trash must stay out of it.
    const QStringList subs = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &name : subs)
        if (!name.startsWith(QLatin1Char('.')))
            collectSwingDirs(dir + QLatin1Char('/') + name, out, depth + 1);
}
} // namespace

QStringList swingDirsUnder(const QString &root)
{
    QStringList dirs;
    collectSwingDirs(QDir::cleanPath(root), dirs, 0);
    dirs.sort();
    return dirs;
}

QByteArray toJsonText(const QJsonObject &root)
{
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace pinpoint::SwingStore
