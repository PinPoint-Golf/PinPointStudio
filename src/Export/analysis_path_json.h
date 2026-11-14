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

// THE ONE SERIALISER of `analysis.path` (AnalysisPath, swing_analysis.h) — the writer and its typed
// reader side by side (refactor step 1.3a). Telemetry for people: nothing reads it back to choose a
// path. It lives here rather than beside its producer (bindAnalysisPath, analysis_pipeline.cpp)
// because the block spells the `origin` key, and no analysis code may (swing_doc_test's provenance
// guard, the sibling of CT-I37): the document's adapter writes and reads it.

#include "../Analysis/swing_analysis.h"

#include <QJsonArray>
#include <QJsonObject>

namespace pinpoint::analysis {

// ── analysis.path ─────────────────────────────────────────────────────────────────────────────

// Which way each decision went (AnalysisPath): the versions say what produced a track, this says
// whether this run produced it or loaded it, and why. Written only once a pose or ladder decision
// was made (analysisPathIsWritten).
inline bool analysisPathIsWritten(const AnalysisPath &p) { return !p.pose.isEmpty() || !p.ladder.isEmpty(); }

inline QJsonObject analysisPathToJson(const AnalysisPath &path)
{
    const auto put = [](QJsonObject &j, const char *k, const QString &v) {
        if (!v.isEmpty()) j.insert(QString::fromLatin1(k), v);
    };
    QJsonObject frames;
    put(frames, "faceOn", path.framesFaceOn);
    put(frames, "dtl", path.framesDtl);
    QJsonObject reuse;
    for (auto it = path.reuse.cbegin(); it != path.reuse.cend(); ++it)
        reuse.insert(it.key(), it.value());
    QJsonObject p{ { QStringLiteral("origin"), path.origin },
                   { QStringLiteral("forced"), path.forced },
                   { QStringLiteral("fullWindow"), path.fullWindow },
                   { QStringLiteral("overrides"), QJsonArray::fromStringList(path.overrides) },
                   { QStringLiteral("frames"), frames } };
    if (!reuse.isEmpty()) p.insert(QStringLiteral("reuse"), reuse);
    put(p, "pose", path.pose);
    put(p, "ladder", path.ladder);
    put(p, "witness", path.witness);
    put(p, "trackerSpan", path.trackerSpan);
    return p;
}

inline AnalysisPath analysisPathFromJson(const QJsonObject &p)
{
    AnalysisPath path;
    path.origin     = p.value(QStringLiteral("origin")).toString();
    path.forced     = p.value(QStringLiteral("forced")).toBool();
    path.fullWindow = p.value(QStringLiteral("fullWindow")).toBool();
    for (const QJsonValue &v : p.value(QStringLiteral("overrides")).toArray()) path.overrides << v.toString();
    const QJsonObject frames = p.value(QStringLiteral("frames")).toObject();
    path.framesFaceOn = frames.value(QStringLiteral("faceOn")).toString();
    path.framesDtl    = frames.value(QStringLiteral("dtl")).toString();
    const QJsonObject reuse = p.value(QStringLiteral("reuse")).toObject();
    for (auto it = reuse.constBegin(); it != reuse.constEnd(); ++it) path.reuse.insert(it.key(), it.value().toString());
    path.pose        = p.value(QStringLiteral("pose")).toString();
    path.ladder      = p.value(QStringLiteral("ladder")).toString();
    path.witness     = p.value(QStringLiteral("witness")).toString();
    path.trackerSpan = p.value(QStringLiteral("trackerSpan")).toString();
    return path;
}

} // namespace pinpoint::analysis
