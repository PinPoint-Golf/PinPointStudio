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

// consistency_dump <fixture.json> <out.json>: both Consistency layers over one fixture, on the production
// env, written for tools/diagnostics/consistency_tally.py, which recomputes `result` from `shots` and
// `readings` alone and diffs the two. NaN is written null. A QJsonObject writes its keys sorted, so the
// file is deterministic.

#include "charts/consistency_env.h"
#include "charts/session_consistency.h"
#include "charts_maths_fixture.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <cstdio>

using namespace pinpoint::charts;

namespace {

QJsonValue number(double x) { return std::isfinite(x) ? QJsonValue(x) : QJsonValue(QJsonValue::Null); }

QJsonArray shotsOf(const SessionSnapshot &session, const ConsistencyInput &in)
{
    QJsonArray out;
    for (size_t i = 0; i < in.shots.size(); ++i) {
        const ConsistencyShot &s = in.shots[i];
        out.append(QJsonObject{ { "ordinal", s.ordinal }, { "swing", session.shots[i]->swingDir }, { "club", s.club },
                                { "guarded", s.guarded }, { "carry", number(s.carry) }, { "offline", number(s.offline) } });
    }
    return out;
}

QJsonArray readingsOf(const ConsistencyInput &in)
{
    QJsonArray out;
    for (const ConsistencyReading &r : in.readings) {
        QJsonArray values;
        for (size_t i = 0; i < in.shots.size(); ++i) {
            QJsonArray row;
            for (int j = 0; j < r.samples; ++j) row.append(number(r.at(int(i), j)));
            values.append(row);
        }
        out.append(QJsonObject{ { "key", r.key }, { "label", r.label }, { "unit", r.unit }, { "column", r.column },
                                { "kind", r.kind == ConsistencyReadingKind::Span ? "span" : "position" },
                                { "area", r.area }, { "ownSigma", number(r.ownSigma) },
                                { "normSigma", number(r.normSigma) }, { "values", values } });
    }
    return out;
}

QJsonObject resultOf(const ConsistencyResult &res)
{
    QJsonArray cells, readings, standouts, flags;
    for (const ConsistencyCell &c : res.cells)
        cells.append(QJsonObject{ { "area", c.area }, { "column", c.column }, { "word", consistencyWordToken(c.word) },
                                  { "judged", c.judged }, { "loose", c.loose }, { "nearError", c.nearError },
                                  { "unjudged", c.unjudged } });
    for (const ConsistencyJudged &r : res.readings)
        readings.append(QJsonObject{ { "key", r.key }, { "column", r.column }, { "spread", number(r.spread) },
                                     { "verdict", consistencyVerdictToken(r.verdict) } });
    for (const ConsistencyStandOut &s : res.standouts)
        standouts.append(QJsonObject{ { "ordinal", s.ordinal }, { "key", s.key }, { "column", s.column },
                                      { "z", number(s.z) }, { "dv", number(s.dv) }, { "k", s.sample } });
    for (const ConsistencyBallFlag &f : res.flags)
        flags.append(QJsonObject{ { "ordinal", f.ordinal }, { "flags", QJsonArray::fromStringList(f.flags) } });
    return QJsonObject{ { "cells", cells }, { "readings", readings }, { "standouts", standouts }, { "flags", flags },
                        { "shuffledMean", number(res.shuffledMean) }, { "p95", number(res.p95) } };
}

void defaultEnv(const char *name, const char *file)
{
    if (!qEnvironmentVariableIsSet(name)) qputenv(name, QByteArray(PP_DIAGNOSTICS_DIR "/") + file);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "usage: consistency_dump <fixture.json> <out.json>\n");
        return 2;
    }
    defaultEnv("PINPOINT_CORE_PACK", "core.json");
    defaultEnv("PINPOINT_CORE_NORMS", "norms.json");
    defaultEnv("PINPOINT_CORE_CONTEXTS", "contexts.json");

    const QString path = QString::fromLocal8Bit(argv[1]);
    const charts_test::Fixture fx = charts_test::loadFixture(path);
    if (!fx.ok) {
        std::fprintf(stderr, "consistency_dump: cannot read %s\n", argv[1]);
        return 1;
    }
    const ConsistencyEnv env = consistencyEnvFromPacks();

    QElapsedTimer timer;
    timer.start();
    const SessionAlignment alignment = alignSession(fx.session);
    const qint64 alignMs = timer.restart();
    const ConsistencyInput input = consistencyInput(fx.session, alignment, env);
    const qint64 sampleMs = timer.restart();
    const ConsistencyResult result = consistencyResult(input);
    const qint64 rulesMs = timer.elapsed();

    int placed = 0;
    for (const ConsistencyReading &r : input.readings) placed += r.area.isEmpty() ? 0 : 1;
    if (!input.readings.empty() && !placed) {
        std::fprintf(stderr, "consistency_dump: no reading has an area: the packs did not load\n");
        return 1;
    }

    const QJsonObject out{ { "session", fx.name.isEmpty() ? QFileInfo(path).completeBaseName() : fx.name },
                           { "afterImpactMs", number(input.afterImpactMs) }, { "shots", shotsOf(fx.session, input) },
                           { "readings", readingsOf(input) }, { "result", resultOf(result) } };
    QFile f(QString::fromLocal8Bit(argv[2]));
    if (!f.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "consistency_dump: cannot write %s\n", argv[2]);
        return 1;
    }
    f.write(QJsonDocument(out).toJson(QJsonDocument::Compact));
    std::printf("%s: %zu shots, %zu readings, align %lld ms, layer 1 %lld ms, layer 2 + shuffle %lld ms\n",
                qPrintable(fx.name), input.shots.size(), input.readings.size(), qint64(alignMs), qint64(sampleMs),
                qint64(rulesMs));
    return 0;
}
