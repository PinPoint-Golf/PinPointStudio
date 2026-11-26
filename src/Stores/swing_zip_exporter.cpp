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

#include "swing_zip_exporter.h"

#include "../Core/pp_debug.h"
#include "../Video/display_tone.h"   // displayToneLut — the display tone curve (#183)
#include "export_tone.h"             // the curved copies a golfer takes away (#183)
#include "swing_store.h"
#include "video_encoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>

#include <private/qzipwriter_p.h>

#include <memory>
#include <utility>

#include "../Core/pp_profiler.h"
#include "../Core/pp_os_metrics.h"

namespace pinpoint {

namespace {

// imu_<alias>.csv / imu_<alias>.bin — legacy: written only by swings saved while the
// csv/binary IMU format existed (retired 23 Sept 2026). New swings inline IMU samples.
bool isImuSidecar(const QString &name)
{
    return name.startsWith(QStringLiteral("imu_"))
        && (name.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
         || name.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive));
}

// The confirmed per-file export policy, applied to one entry in a shot folder.
//   thumb.jpg          → always
//   *.raw              → never (GB-scale; would defeat the streaming budget)
//   swing.json         → iff includeJson — for a swing.ppsw swing, generated from it (below): the
//                        zip is an interchange format, and JSON is what anyone else can read
//   imu_*.csv|.bin     → iff includeJson (travels with swing.json as "data")
//   selected video     → iff its filename is checked in the options sheet
//   anything else      → excluded (unselected video, unknown artifact)
bool includeMember(const QString &name, const QSet<QString> &selectedVideos, bool includeJson)
{
    if (name.compare(QStringLiteral("thumb.jpg"), Qt::CaseInsensitive) == 0)
        return true;
    if (name.endsWith(QStringLiteral(".raw"), Qt::CaseInsensitive))
        return false;
    if (name.compare(QStringLiteral("swing.json"), Qt::CaseInsensitive) == 0)
        return includeJson;
    if (isImuSidecar(name))
        return includeJson;
    return selectedVideos.contains(name);
}

// A curved copy of one stored raw-Bayer clip (#183, export_tone.h), written into tmp. Returns its path, or
// empty when it could not be made (no encoder in this build, or the clip would not decode or encode): the
// caller then ships the stored clip as it is, and says so in the log.
QString curvedClip(const QString &storedPath, const ExportToneMembers::Clip &clip, const QTemporaryDir &tmp)
{
    if (!tmp.isValid()) {
        ppWarn() << "[SwingZipExporter] no temporary folder for the curved copy of" << storedPath;
        return {};
    }
    const std::unique_ptr<IVideoEncoder> enc = makeVideoEncoder(kExportToneCodec);
    if (!enc) {
        ppWarn() << "[SwingZipExporter] no video encoder in this build: exporting" << storedPath
                 << "as stored, without the display tone curve";
        return {};
    }
    const QString out = tmp.filePath(QFileInfo(storedPath).fileName());
    VideoEncoderConfig cfg;
    cfg.out_fps = clip.playbackFps;
    cfg.crf     = clip.impact ? kExportToneImpactCrf : kExportToneCrf;
    cfg.preset  = kExportTonePreset;
    cfg.path    = out.toStdString();
    const RetoneResult r = retoneClip(storedPath.toStdString(), *enc, cfg, displayToneLut());
    if (!r.ok) {
        ppWarn() << "[SwingZipExporter] could not curve" << storedPath << "(" << QString::fromStdString(r.error)
                 << "): exporting it as stored, without the display tone curve";
        QFile::remove(out);
        return {};
    }
    ppInfo() << "[SwingZipExporter] curved" << r.frames << "frames of" << storedPath;
    return out;
}

} // namespace

SwingZipExporter::SwingZipExporter(QObject *parent)
    : QObject(parent)
{
    // Result delivered on the UI thread; the lambda captures only `this`, and the
    // connection is auto-severed if `this` dies before the worker finishes.
    connect(&m_watcher, &QFutureWatcher<ZipResult>::finished, this, [this] {
        const ZipResult r = m_watcher.result();
        m_busy = false;
        emit busyChanged();
        if (r.ok)
            ppInfo() << "[SwingZipExporter] export complete:" << r.zipPath;
        else
            ppWarn() << "[SwingZipExporter] export failed:" << r.error;
        emit exportFinished(r.ok, r.zipPath, r.error);
    });
}

QVariantList SwingZipExporter::camerasForShots(const QVariantList &swingDirs) const
{
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &v : swingDirs) {
        const QString dir = v.toString();
        if (dir.isEmpty())
            continue;
        if (!SwingStore::hasDocument(dir))
            continue;
        QString err;
        const QJsonObject doc = SwingStore::load(dir, &err);
        if (doc.isEmpty()) {
            ppWarn() << "[SwingZipExporter] bad swing document in" << dir << err;
            continue;
        }
        const QJsonArray streams = doc.value(QStringLiteral("streams")).toArray();
        for (const QJsonValue &sv : streams) {
            const QJsonObject so = sv.toObject();
            if (so.value(QStringLiteral("kind")).toString() != QStringLiteral("video"))
                continue;
            const QString file = so.value(QStringLiteral("file")).toString();
            if (file.isEmpty() || seen.contains(file))
                continue;
            seen.insert(file);
            QString alias = so.value(QStringLiteral("alias")).toString();
            if (alias.isEmpty())
                alias = QFileInfo(file).completeBaseName();
            out.append(QVariantMap{ { QStringLiteral("file"),  file },
                                    { QStringLiteral("alias"), alias } });
        }
    }
    return out;
}

void SwingZipExporter::exportShots(const QVariantList &swingDirs,
                                   const QStringList &selectedVideoFiles,
                                   bool includeJson)
{
    if (m_busy) {
        ppWarn() << "[SwingZipExporter] export already in progress — ignoring";
        return;
    }

    // Keep only existing on-disk shot dirs (analysis-only shots have none).
    QStringList dirs;
    for (const QVariant &v : swingDirs) {
        const QString d = v.toString();
        if (!d.isEmpty() && QFileInfo(d).isDir())
            dirs.append(d);
    }
    if (dirs.isEmpty()) {
        emit exportFinished(false, QString(), tr("No saved shots to export"));
        return;
    }

    // Session name = leaf of the shots' common parent folder. The carousel is
    // per-session, so they normally agree; warn (don't fail) if they don't.
    const QString parent0 = QFileInfo(dirs.first()).absolutePath();
    for (const QString &d : dirs) {
        if (QFileInfo(d).absolutePath() != parent0) {
            ppWarn() << "[SwingZipExporter] shots span multiple session folders — "
                        "using the first:" << parent0;
            break;
        }
    }
    QString sessionName = QFileInfo(parent0).fileName();
    if (sessionName.isEmpty())
        sessionName = QStringLiteral("session");

    // A single-shot export names the zip after the shot too (e.g.
    // "<session>_swing_0003.zip") so the file is self-identifying; a multi-shot
    // export keeps the bare session name. The folder inside is always swing_NNNN.
    QString baseName = sessionName;
    if (dirs.size() == 1)
        baseName += QLatin1Char('_') + QFileInfo(dirs.first()).fileName();

    // ~/<base>.zip, uniquified to "<base> (N).zip" on collision so we never
    // clobber a prior export and the toast reports the real filename.
    const QString home = QDir::homePath();
    QString outZipPath = home + QLatin1Char('/') + baseName + QStringLiteral(".zip");
    for (int n = 2; QFileInfo::exists(outZipPath); ++n)
        outZipPath = QStringLiteral("%1/%2 (%3).zip").arg(home, baseName).arg(n);

    ZipJob job;
    job.swingDirs          = dirs;
    job.sessionName        = sessionName;
    job.outZipPath         = outZipPath;
    job.selectedVideoFiles = selectedVideoFiles;
    job.includeJson        = includeJson;

    m_busy = true;
    emit busyChanged();
    emit exportStarted();
    ppInfo() << "[SwingZipExporter] exporting" << dirs.size() << "shots to" << outZipPath;

    m_watcher.setFuture(QtConcurrent::run([job = std::move(job)] {
        // Exception barrier: a throw escaping the worker would be rethrown on the
        // UI thread at result() with no handler (std::terminate). Degrade to a
        // failed result instead.
        try {
            return runZip(job);
        } catch (const std::exception &e) {
            return ZipResult{ false, QString(), QString::fromUtf8(e.what()) };
        } catch (...) {
            return ZipResult{ false, QString(),
                              QStringLiteral("unknown exception during zip export") };
        }
    }));
}

SwingZipExporter::ZipResult SwingZipExporter::runZip(const ZipJob &job)
{
    // Runs on a transient QtConcurrent pool thread — register for the duration so
    // it shows a labelled per-thread CPU row, and drop it again on every exit path.
    pinpoint::osmetrics::registerThread("Export.Zip");
    struct ThreadUnreg { ~ThreadUnreg() { pinpoint::osmetrics::unregisterThread(); } } threadUnreg_;

    // Filename ctor → opens an internal QFile; each member's compressed bytes are
    // written to disk as it is added, so the whole archive is never buffered.
    QZipWriter writer(job.outZipPath);
    if (!writer.isWritable())
        return { false, QString(), tr("Cannot write to %1").arg(job.outZipPath) };
    // Auto: store already-compressed members (MP4/JPEG), deflate the rest (JSON/CSV).
    writer.setCompressionPolicy(QZipWriter::AutoCompress);

    const QString root = job.sessionName + QLatin1Char('/');
    writer.addDirectory(root);

    const QSet<QString> selected(job.selectedVideoFiles.cbegin(),
                                 job.selectedVideoFiles.cend());
    int filesAdded = 0;
    // The curved copies are made one at a time in here, each removed once it is in the zip (#183).
    const QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/pps-export-XXXXXX"));

    for (const QString &dir : job.swingDirs) {
        const QString swingId = QFileInfo(dir).fileName();      // e.g. swing_0001
        const QString base    = root + swingId + QLatin1Char('/');
        writer.addDirectory(base);
        // What a golfer takes away is shown as on screen (#183): which of this swing's files get the display
        // tone curve, from its streams' source formats. The stored files are only read.
        const ExportToneMembers tone =
            exportToneMembers(SwingStore::loadParts(dir, { QStringLiteral("/streams") }));

        const QFileInfoList entries =
            QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            const QString name = fi.fileName();
            if (!includeMember(name, selected, job.includeJson))
                continue;
            if (tone.thumbnail && name.compare(QStringLiteral("thumb.jpg"), Qt::CaseInsensitive) == 0) {
                const QByteArray curved = retoneJpeg(fi.absoluteFilePath(), displayToneLut());
                if (!curved.isEmpty()) {
                    writer.addFile(base + name, curved);
                    ++filesAdded;
                    continue;
                }
                ppWarn() << "[SwingZipExporter] could not curve" << fi.absoluteFilePath()
                         << ": exporting it as stored, without the display tone curve";
            }
            if (const auto clip = tone.clips.constFind(name); clip != tone.clips.constEnd()) {
                const QString curvedPath = curvedClip(fi.absoluteFilePath(), clip.value(), tmp);
                if (!curvedPath.isEmpty()) {
                    QFile curvedIn(curvedPath);
                    if (curvedIn.open(QIODevice::ReadOnly)) {
                        PP_PROFILE_MEM_SCOPE("Export.Staging", int64_t(curvedIn.size()));
                        writer.addFile(base + name, &curvedIn);
                        curvedIn.close();
                        QFile::remove(curvedPath);
                        ++filesAdded;
                        continue;
                    }
                    ppWarn() << "[SwingZipExporter] could not read back the curved copy of" << fi.absoluteFilePath()
                             << ": exporting it as stored, without the display tone curve";
                    QFile::remove(curvedPath);
                }
            }
            QFile in(fi.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly)) {
                ppWarn() << "[SwingZipExporter] skipping unreadable" << fi.absoluteFilePath();
                continue;   // one missing artifact must not fail the whole zip
            }
            // QZipWriter buffers one member at a time → peak ≈ largest file.
            PP_PROFILE_MEM_SCOPE("Export.Staging", int64_t(in.size()));
            writer.addFile(base + name, &in);   // streams; QZipWriter buffers one file
            in.close();
            ++filesAdded;
        }
        // A swing.ppsw travels as swing.json. The binary file is this app's storage format; a zip
        // goes to other people and other tools, and the JSON is the form they can all read.
        if (job.includeJson && SwingStore::info(dir).format == SwingStore::Format::Ppsw) {
            const QJsonObject doc = SwingStore::load(dir);
            if (doc.isEmpty()) {
                ppWarn() << "[SwingZipExporter] skipping unreadable document in" << dir;
            } else {
                const QByteArray text = SwingStore::toJsonText(doc);
                PP_PROFILE_MEM_SCOPE("Export.Staging", int64_t(text.size()));
                writer.addFile(base + QStringLiteral("swing.json"), text);
                ++filesAdded;
            }
        }
    }

    writer.close();
    if (writer.status() != QZipWriter::NoError) {
        QFile::remove(job.outZipPath);          // don't leave a half-written zip
        return { false, QString(),
                 tr("Zip write failed (status %1)").arg(int(writer.status())) };
    }

    ppInfo() << "[SwingZipExporter] wrote" << filesAdded << "files to" << job.outZipPath;
    return { true, job.outZipPath, QString() };
}

} // namespace pinpoint
