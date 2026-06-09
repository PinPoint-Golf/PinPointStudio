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

#include "swing_doc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

#include "swing_paths.h"
#include "../Analysis/swing_analysis.h"

namespace pinpoint {
namespace {

// Serialise the analyzed swing into the additive "analysis" object (schema
// pinpoint.analysis/2 — versions the embedded block, distinct from the document's
// pinpoint.swing/2). Mirrors the QML analysisDetail shape, t_us as JSON numbers.
QJsonObject serializeAnalysis(const analysis::SwingAnalysis &a)
{
    using namespace analysis;
    QJsonObject o;
    o[QStringLiteral("schema")] = QStringLiteral("pinpoint.analysis/2");
    o[QStringLiteral("tier")]   = a.tier;
    o[QStringLiteral("score")]  = a.score.overall;

    QJsonArray metrics;
    for (const MetricSeries &m : a.series) {
        QJsonArray ts, vs, samples;
        for (const int64_t t : m.t_us) ts.append(static_cast<qint64>(t));
        for (const double v : m.value) vs.append(v);
        for (const PhaseSample &ps : m.phaseSamples)
            samples.append(QJsonObject{ { QStringLiteral("phase"), int(ps.phase) },
                                        { QStringLiteral("t_us"),  static_cast<qint64>(ps.t_us) },
                                        { QStringLiteral("value"), ps.value },
                                        { QStringLiteral("band"),  ps.band } });
        metrics.append(QJsonObject{ { QStringLiteral("key"),   m.key },
                                    { QStringLiteral("label"), m.label },
                                    { QStringLiteral("unit"),  m.unit },
                                    { QStringLiteral("t_us"),  ts },
                                    { QStringLiteral("value"), vs },
                                    { QStringLiteral("phaseSamples"), samples } });
    }
    o[QStringLiteral("metrics")] = metrics;

    QJsonArray phases;
    for (const PhaseEvent &e : a.phases)
        phases.append(QJsonObject{ { QStringLiteral("phase"), int(e.phase) },
                                   { QStringLiteral("t_us"),  static_cast<qint64>(e.t_us) },
                                   { QStringLiteral("conf"),  e.conf } });
    o[QStringLiteral("phases")] = phases;
    return o;
}

} // namespace

bool SwingDocWriter::writeSwingJson(const QString &swingDir, const QJsonObject &rawManifest,
                                    const analysis::SwingAnalysis *analysis, QString *error)
{
    QJsonObject root = rawManifest;
    root[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/2");
    if (analysis)
        root[QStringLiteral("analysis")] = serializeAnalysis(*analysis);

    const QString path = swingDir + QStringLiteral("/swing.json");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("failed to commit %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool SwingDocWriter::updateReview(const QString &swingDir, int rating, const QString &note,
                                  QString *error)
{
    const QString path = swingDir + QStringLiteral("/swing.json");

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1: %2").arg(path, in.errorString());
        return false;
    }
    QJsonParseError pe;
    QJsonObject root = QJsonDocument::fromJson(in.readAll(), &pe).object();
    in.close();
    if (pe.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("cannot parse %1: %2").arg(path, pe.errorString());
        return false;
    }

    // Additive "review" block — additive, readers ignore unknown keys.
    root[QStringLiteral("review")] = QJsonObject{
        { QStringLiteral("rating"), std::clamp(rating, 0, 5) },
        { QStringLiteral("note"),   note },
    };

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, out.errorString());
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        if (error) *error = QStringLiteral("failed to commit %1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

// ── reader ──────────────────────────────────────────────────────────────────

PersistedShot SwingDocReader::readSwingJson(const QString &swingDir)
{
    PersistedShot ps;
    ps.swingDir = swingDir;

    QFile f(swingDir + QStringLiteral("/swing.json"));
    if (!f.open(QIODevice::ReadOnly))
        return ps;
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || root.isEmpty())
        return ps;

    ps.ordinal = root[QStringLiteral("swing")].toObject()[QStringLiteral("index")].toInt();
    ps.club    = QStringLiteral("DRIVER");   // club isn't persisted yet

    const QString wc = root[QStringLiteral("clock")].toObject()[QStringLiteral("wallclock")].toString();
    const QDateTime dt = QDateTime::fromString(wc, Qt::ISODateWithMs);
    ps.timestampLabel = dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("hh:mm:ss")) : wc;
    ps.wallclockMs    = dt.isValid() ? dt.toMSecsSinceEpoch() : 0;

    // Only video presence is reconstructed here. imu / pose streams and the raw
    // sidecar are not parsed on reload yet — see the "Reload & replay consumer
    // contract" in docs/swing_export_developer_guide.md for the shapes a future
    // consumer must honor (IMU json/csv/binary, pose coco17, raw reconstruction).
    for (const QJsonValue &v : root[QStringLiteral("streams")].toArray())
        if (v.toObject()[QStringLiteral("kind")].toString() == QLatin1String("video")) { ps.hasVideo = true; break; }

    const QJsonObject thumb = root[QStringLiteral("thumbnail")].toObject();
    if (!thumb.isEmpty())
        ps.thumbnailPath = swingDir + QStringLiteral("/")
                         + thumb[QStringLiteral("file")].toString(QStringLiteral("thumb.jpg"));

    if (root.contains(QStringLiteral("analysis"))) {
        const QJsonObject an = root[QStringLiteral("analysis")].toObject();
        ps.score = an[QStringLiteral("score")].toInt();

        // analysisDetail in the live QML-role shape (metrics→series, score→overall).
        ps.analysisDetail = QVariantMap{
            { QStringLiteral("tier"),    an[QStringLiteral("tier")].toInt() },
            { QStringLiteral("overall"), an[QStringLiteral("score")].toInt() },
            { QStringLiteral("series"),  an[QStringLiteral("metrics")].toArray().toVariantList() },
            { QStringLiteral("phases"),  an[QStringLiteral("phases")].toArray().toVariantList() },
        };

        // Flat metrics: each metric's value at Impact (Phase::Impact == 5), signed degrees.
        QVariantMap metrics;
        for (const QJsonValue &mv : an[QStringLiteral("metrics")].toArray()) {
            const QJsonObject m = mv.toObject();
            bool found = false;
            double impact = 0.0;
            for (const QJsonValue &sv : m[QStringLiteral("phaseSamples")].toArray()) {
                const QJsonObject s = sv.toObject();
                if (s[QStringLiteral("phase")].toInt() == 5) { impact = s[QStringLiteral("value")].toDouble(); found = true; break; }
            }
            if (!found)
                continue;
            const long r = std::lround(impact);
            const QString val = (r > 0 ? QStringLiteral("+") : QString()) + QString::number(r) + QStringLiteral("°");
            metrics.insert(m[QStringLiteral("key")].toString(),
                           QVariantMap{ { QStringLiteral("label"), m[QStringLiteral("label")].toString() },
                                        { QStringLiteral("value"), val } });
        }
        ps.metrics = metrics;
    }

    // User review (rating/note) — written through by updateReview after edits.
    if (root.contains(QStringLiteral("review"))) {
        const QJsonObject rv = root[QStringLiteral("review")].toObject();
        ps.rating = std::clamp(rv[QStringLiteral("rating")].toInt(), 0, 5);
        ps.note   = rv[QStringLiteral("note")].toString();
    }

    ps.ok = true;
    return ps;
}

QStringList SwingDocReader::findSwingDirs(const QString &sessionDir)
{
    QDir d(sessionDir);
    QStringList out;
    for (const QString &name : d.entryList(QStringList{ QStringLiteral("swing_*") },
                                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        out.append(sessionDir + QStringLiteral("/") + name);
    return out;
}

QString SwingDocReader::latestSessionDir(const QString &libraryRoot, const QString &athleteName)
{
    if (libraryRoot.isEmpty() || athleteName.isEmpty())
        return {};
    const QString athleteDir = libraryRoot + QStringLiteral("/") + SwingPaths::sanitise(athleteName);
    const QFileInfoList sessions =
        QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (sessions.isEmpty())
        return {};
    // Pick the most recently modified session dir. Folder names now embed the
    // naming pattern's tokens (date / athlete / session-type), so a plain name
    // sort no longer tracks recency when several session types share one day.
    const QFileInfo *newest = &sessions.first();
    for (const QFileInfo &fi : sessions)
        if (fi.lastModified() > newest->lastModified())
            newest = &fi;
    return newest->absoluteFilePath();
}

QStringList SwingDocReader::sessionDirs(const QString &libraryRoot, const QString &athleteName)
{
    if (libraryRoot.isEmpty() || athleteName.isEmpty())
        return {};
    const QString athleteDir = libraryRoot + QStringLiteral("/") + SwingPaths::sanitise(athleteName);
    QFileInfoList sessions =
        QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    // Most-recently-modified first — same recency basis as latestSessionDir(),
    // since the naming pattern means a plain name sort doesn't track recency.
    std::sort(sessions.begin(), sessions.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    QStringList out;
    out.reserve(sessions.size());
    for (const QFileInfo &fi : sessions)
        out.append(fi.absoluteFilePath());
    return out;
}

} // namespace pinpoint
