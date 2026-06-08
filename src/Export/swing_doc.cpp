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

#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

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

} // namespace pinpoint
