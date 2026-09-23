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

// raw_reencode — re-encode a swing's face-on camera from its raw frames at a chosen CRF.
//
//   raw_reencode <swing_dir> --crf N[,N...] --out <dir> [--codec h264|h265] [--face-on Face]
//
// Writes <dir>/<alias>.mp4 (the recorded stream's file name) and nothing else; with several CRFs,
// <dir>/crf<N>/<alias>.mp4 each, from ONE load of the raw (~900 MB a swing, often over SMB). For the
// storage study in docs/implementation/swing_storage_impl.md (Phase 2, stage 4): what does a
// higher-quality mp4 cost, and how far does re-analysis from it move the numbers?
//
// ⚠ THE ENCODE MUST BE THE LIVE ONE, or the study measures an encoder nobody ships. So nothing
// here is an encoder: the window is rebuilt from the .raw by SwingDiskLoader (the same payloads
// the live window held in RAM) and handed to the production SwingExporter with the job fields
// ShotProcessor sets (h264, native resolution, only the CRF varied). The study's own gate checks
// it: a CRF 23 re-encode must reproduce the recorded mp4's re-analysis deltas.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <cstdio>

#include "../../../src/Core/PpMessageLog.h"
#include "../../../src/Analysis/swing_reanalyzer.h"
#include "../../../src/Export/swing_exporter.h"
#include "../../../src/Export/swing_store.h"

using namespace pinpoint;
using namespace pinpoint::analysis;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    PpMessageLog::instance()->setEchoToStderr(true);

    QCommandLineParser cli;
    cli.addPositionalArgument("swing_dir", "Recorded swing directory (must hold the face-on .raw)");
    QCommandLineOption optCrf("crf", "x264 CRF, or a comma list (0 = lossless)", "n[,n...]");
    QCommandLineOption optOut({ "o", "out" }, "Output directory", "dir");
    QCommandLineOption optFace("face-on", "Substring identifying the face-on camera alias", "s", "Face");
    QCommandLineOption optCodec("codec", "Encoder factory key, as AppSettings videoCodec", "key", "h264");
    cli.addOptions({ optCrf, optOut, optFace, optCodec });
    cli.addHelpOption();
    cli.process(app);

    const auto fail = [](const QString &why) {
        std::fprintf(stderr, "[raw_reencode] %s\n", why.toUtf8().constData());
        return 1;
    };
    if (cli.positionalArguments().size() != 1 || !cli.isSet(optCrf) || !cli.isSet(optOut))
        return fail(QStringLiteral("usage: raw_reencode <swing_dir> --crf N --out <dir>"));

    const QString swingDir = QDir::cleanPath(cli.positionalArguments().first());
    QList<int> crfs;
    for (const QString &c : cli.value(optCrf).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool okCrf = false;
        const int crf = c.trimmed().toInt(&okCrf);
        if (!okCrf || crf < 0 || crf > 51)
            return fail(QStringLiteral("--crf must be 0..51"));
        crfs << crf;
    }
    if (crfs.isEmpty())
        return fail(QStringLiteral("--crf needs a value"));

    SwingLoadOptions lopts;
    lopts.faceOnSubstring = cli.value(optFace);
    lopts.faceOnExplicit  = cli.isSet(optFace);
    LoadedSwing ls = SwingDiskLoader::load(swingDir, lopts);
    if (!ls.ok || !ls.window)
        return fail(QStringLiteral("load failed: ") + ls.error);
    if (!ls.usedRaw)
        return fail(QStringLiteral("no .raw beside %1 — re-encoding the mp4 would measure nothing").arg(swingDir));
    if (ls.job.cameraSources.empty())
        return fail(QStringLiteral("no camera stream"));
    const SourceId faceOn = ls.job.cameraSources.front();   // face-on first (SwingDiskLoader)

    // The stream's alias and file name, as recorded, so the output drops in beside the document.
    QString alias, fileName;
    for (const QJsonValue &v : SwingStore::load(swingDir).value(QStringLiteral("streams")).toArray()) {
        const QJsonObject el = v.toObject();
        if (el.value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
        const QString a = el.value(QStringLiteral("alias")).toString();
        if (!a.contains(lopts.faceOnSubstring, Qt::CaseInsensitive)) continue;
        alias    = a;
        fileName = el.value(QStringLiteral("file")).toString();
        break;
    }
    if (fileName.isEmpty())
        return fail(QStringLiteral("no face-on video stream named in the document"));

    const QString outRoot = QDir::cleanPath(cli.value(optOut));
    for (const int crf : crfs) {
        const QString outDir = crfs.size() == 1 ? outRoot
                                                : outRoot + QStringLiteral("/crf%1").arg(crf);
        QDir().mkpath(outDir);

        // ShotProcessor::buildExportJob's video fields, and nothing that writes anything else.
        SwingExportJob job;
        job.swingDir = outDir;
        job.swingId  = QFileInfo(swingDir).fileName();
        SwingExportCamera cam;
        cam.sourceId    = faceOn;
        cam.alias       = alias;
        cam.fileName    = fileName;
        cam.perspective = 2;   // FaceOn
        job.cameras.push_back(cam);
        job.codec          = cli.value(optCodec);
        job.crf            = crf;
        job.resolutionMode = QStringLiteral("native");
        job.saveImu        = false;
        job.saveRaw        = false;
        job.savePose       = false;
        job.thumbnailSourceId    = kInvalidSourceId;
        job.thumbnailTimestampUs = -1;

        const SwingExportResult res = SwingExporter::run(*ls.window, job);
        if (!res.ok)
            return fail(QStringLiteral("encode failed at crf %1: %2").arg(crf).arg(res.error));
        const QFileInfo out(outDir + QLatin1Char('/') + fileName);
        std::fprintf(stderr, "[raw_reencode] %s crf %d -> %s (%lld bytes)\n",
                     swingDir.toUtf8().constData(), crf, out.absoluteFilePath().toUtf8().constData(),
                     (long long)out.size());
        if (!out.exists())
            return 1;
    }
    return 0;
}
