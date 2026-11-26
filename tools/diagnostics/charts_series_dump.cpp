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

// charts_series_dump — THE CHARTS SERIES, OLD AGAINST NEW (refactor 5.5a; DUTIES-5.5 §D, deliverable 7). For each session
// folder named (A SCRATCH COPY: holding a session lets its owner write), the snapshot the Charts tabs read, two ways, in
// one canonical form:
//   NEW  the session's own series (SessionSeries), attached through the owners' hook, after its SessionDiagnostics has
//        judged every shot and written what it keeps;
//   OLD  the view-owned loader the app used until 5.5a (FolderSeries), reading the file that owner has just written.
// Every field of every shot is compared; each difference is printed and classed against the list written before the
// run (DUTIES-5.5 §D, D1–D5). Exit 0 when nothing differs outside that list, 1 when something does, 2 on a usage error.
//
//   charts_series_dump [--out <json>] <session dir>...
//
// The athlete is read from the athlete folder's athlete.json (FILE-ATH-01), so the Athlete holds its own sessions.

#include "../../src/Diagnostics/tests/owner_rig.h"

#include "../../src/Gui/charts/session_series.h"
#include "../../src/Gui/tests/folder_series.h"
#include "../../src/Stores/rule_data_install.h"
#include "../../src/Stores/series_store.h"
#include "../../src/Wrist/wrist_profile.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QStandardPaths>

#include <cmath>
#include <cstdio>

using namespace pinpoint;
using charts::SessionSnapshot;

namespace {

QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QStringLiteral("NaN")); }

template <class T> QString hashOf(const std::vector<T> &v)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray(reinterpret_cast<const char *>(v.data()), qsizetype(v.size() * sizeof(T))),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}

QJsonObject readingOf(const charts::ChartShotFindings::Reading &r)
{
    return { { "measureId", r.measureId }, { "metricKey", r.metricKey }, { "phase", r.phase }, { "reducer", r.reducer },
             { "contextId", r.contextId }, { "shape", r.shape }, { "sameQuantity", r.sameQuantity } };
}

template <class V, class F> QJsonObject sortedMap(const QHash<QString, V> &h, F f)
{
    QJsonObject o;
    QStringList keys = h.keys();
    keys.sort();
    for (const QString &k : keys) o.insert(k, f(h.value(k)));
    return o;
}

QJsonObject shotOf(const charts::ShotSeries &s)
{
    QJsonObject o{ { "swing", s.swingDir.section(QLatin1Char('/'), -1) }, { "ordinal", s.ordinal }, { "shotId", s.shotId },
                   { "club", s.club }, { "impactUs", double(s.impactUs) }, { "gatesVersion", s.gatesVersion },
                   { "metricOrder", QJsonArray::fromStringList(s.metricOrder) } };
    QJsonArray phases;
    for (const charts::PhaseMark &p : s.phases) phases.append(QJsonObject{ { "phase", p.phase }, { "tUs", double(p.tUs) }, { "conf", num(p.conf) } });
    o.insert("phases", phases);
    o.insert("metrics", sortedMap(s.metrics, [](const charts::MetricSeries &m) {
        QJsonArray ps;
        for (const charts::PhaseSample &p : m.phaseSamples)
            ps.append(QJsonObject{ { "phase", p.phase }, { "tUs", double(p.tUs) }, { "value", num(p.value) }, { "sigma", num(p.sigma) }, { "band", p.band } });
        return QJsonValue(QJsonObject{ { "label", m.label }, { "unit", m.unit }, { "n", int(m.tUs.size()) }, { "tUs", hashOf(m.tUs) },
                                       { "value", hashOf(m.value) }, { "valid", hashOf(m.valid) }, { "sigma", num(m.sigma) },
                                       { "sigmaKind", m.sigmaKind }, { "lineAngle", m.lineAngle }, { "routeId", m.routeId },
                                       { "phaseSamples", ps } });
    }));
    o.insert("kinematicSequence", QJsonObject::fromVariantMap(s.kinematicSequence));
    o.insert("gates", QJsonArray::fromVariantList(s.gates));
    o.insert("wristResemblance", QJsonObject::fromVariantMap(s.wristResemblance));
    const charts::ChartShotFindings &d = s.diagnostics;
    o.insert("findings", QJsonObject{
        { "club", d.club }, { "dataWarning", d.dataWarning }, { "fired", QJsonArray::fromStringList(d.fired) },
        { "clean", QJsonArray::fromStringList(d.clean) },
        { "values", sortedMap(d.values, [](const charts::ChartShotFindings::Value &v) {
              return QJsonValue(QJsonObject{ { "value", num(v.value) }, { "z", num(v.z) }, { "lo", num(v.lo) }, { "hi", num(v.hi) },
                                             { "dir", v.dir }, { "confidence", num(v.confidence) } }); }) },
        { "notJudgedBy", sortedMap(d.notJudgedBy, [](const QStringList &l) { return QJsonValue(QJsonArray::fromStringList(l)); }) },
        { "drivenBy", sortedMap(d.drivenBy, [](const charts::ChartShotFindings::Reading &r) { return QJsonValue(readingOf(r)); }) },
        { "gradedBy", sortedMap(d.gradedBy, [](const charts::ChartShotFindings::Reading &r) { return QJsonValue(readingOf(r)); }) },
        { "sequenceChecks", QJsonObject::fromVariantMap(d.sequenceChecks) },
        { "wrist", analysis::wristProfileToJson(d.wrist) } });
    return o;
}

QJsonObject dumpOf(const SessionSnapshot &snap, int skipped)
{
    QJsonArray shots;
    for (const auto &s : snap.shots) shots.append(shotOf(*s));
    return { { "shots", shots }, { "skipped", skipped },
             { "conditions", sortedMap(snap.conditions, [](const charts::ConditionInfo &c) {
                   return QJsonValue(QJsonObject{ { "name", c.name }, { "group", c.group }, { "metricKey", c.metricKey },
                                                  { "phase", c.phase }, { "reducer", c.reducer } }); }) },
             { "tiers", sortedMap(snap.tiers, [](const QString &t) { return QJsonValue(t); }) },
             { "workOnOrder", QJsonArray::fromStringList(snap.workOnOrder) }, { "wristPendingWhy", snap.wristPendingWhy } };
}

// Every leaf that differs, by path.
void differences(const QJsonValue &a, const QJsonValue &b, const QString &path, QStringList &out)
{
    if (a.isObject() && b.isObject()) {
        const QJsonObject x = a.toObject(), y = b.toObject();
        QStringList keys = x.keys();
        for (const QString &k : y.keys()) if (!keys.contains(k)) keys << k;
        keys.sort();
        for (const QString &k : keys) differences(x.value(k), y.value(k), path + QLatin1Char('.') + k, out);
        return;
    }
    if (a.isArray() && b.isArray() && a.toArray().size() == b.toArray().size()) {
        const QJsonArray x = a.toArray(), y = b.toArray();
        for (qsizetype i = 0; i < x.size(); ++i) differences(x.at(i), y.at(i), path + QStringLiteral("[%1]").arg(i), out);
        return;
    }
    if (a != b)
        out << QStringLiteral("%1: old %2 / new %3").arg(path,
                    QString::fromUtf8(QJsonDocument(QJsonArray{ a }).toJson(QJsonDocument::Compact)).left(160),
                    QString::fromUtf8(QJsonDocument(QJsonArray{ b }).toJson(QJsonDocument::Compact)).left(160));
}

// DUTIES-5.5 §D's list, written before the run.
QString classOf(const QString &d)
{
    if (d.startsWith(QLatin1String(".tiers."))) return QStringLiteral("D2");              // the owner's session-scope conditions
    if (d.startsWith(QLatin1String(".wristPendingWhy"))) return QStringLiteral("D3");
    if (d.startsWith(QLatin1String(".workOnOrder"))) return QStringLiteral("D4");
    if (d.startsWith(QLatin1String(".skipped")) || d.startsWith(QLatin1String(".shots:"))) return QStringLiteral("D5");
    return QString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);   // the user's packs stay out
    QStringList args = app.arguments().mid(1);
    QString outPath;
    if (args.size() >= 2 && args.first() == QLatin1String("--out")) { outPath = args.at(1); args = args.mid(2); }
    if (args.isEmpty()) { std::fprintf(stderr, "usage: charts_series_dump [--out <json>] <session dir>...\n"); return 2; }
    RuleData &rules = installProcessRuleData();

    QJsonObject all;
    int unexpected = 0, expected = 0;
    for (const QString &arg : args) {
        const QString dir = QDir::cleanPath(QFileInfo(arg).absoluteFilePath());
        const QString athleteDir = QFileInfo(dir).absolutePath();
        QFile af(QDir(athleteDir).filePath(QStringLiteral("athlete.json")));
        QString athleteId;
        if (af.open(QIODevice::ReadOnly)) athleteId = QJsonDocument::fromJson(af.readAll()).object().value(QStringLiteral("athleteId")).toString();
        owner_rig::ViewRig rig(athleteDir, athleteId);
        charts::SeriesDoors doors;
        doors.reader      = std::make_shared<SwingStoreSeriesReader>(rig.store);
        doors.ruleData    = &rules;
        doors.synchronous = true;
        DiagnosticsDoors d = rig.athlete->diagnosticsDoors();
        d.attach = [doors](Session *s, const DiagnosticsDoors &dd) {
            SessionDiagnostics *o = SessionDiagnostics::attach(s, dd);
            charts::SessionSeries::attach(s, doors);
            return o;
        };
        rig.athlete->setDiagnosticsDoors(d);
        Session *s = rig.athlete->sessionAt(dir);
        if (!s) { std::printf("%s: no Session holds it (athlete %s)\n", qPrintable(dir), qPrintable(athleteId)); ++unexpected; continue; }
        if (s->diagnostics()) { owner_rig::ViewRig::settle(s->diagnostics()); s->diagnostics()->flush(true); }
        charts::SessionSeries *series = charts::SessionSeries::of(s);
        const auto fresh = series->snapshot();
        charts::FolderSeries old;
        old.setSynchronous(true);
        old.activateSession(dir);
        const QJsonObject a = dumpOf(*old.snapshot(), old.skippedCount());
        const QJsonObject b = dumpOf(*fresh, series->skippedCount());
        QStringList diffs;
        if (a.value("shots").toArray().size() != b.value("shots").toArray().size())
            diffs << QStringLiteral(".shots: old %1 / new %2 shots").arg(a.value("shots").toArray().size()).arg(b.value("shots").toArray().size());
        else
            differences(a, b, QString(), diffs);
        const QString name = dir.section(QLatin1Char('/'), -1);
        std::printf("%s: %lld shots, %d skipped; %lld differences\n", qPrintable(name), (long long)fresh->shots.size(),
                    series->skippedCount(), (long long)diffs.size());
        QJsonArray dj;
        for (const QString &x : diffs) {
            const QString c = classOf(x);
            if (c.isEmpty()) ++unexpected; else ++expected;
            std::printf("  %s %s\n", c.isEmpty() ? "UNEXPECTED" : qPrintable(c), qPrintable(x));
            dj.append(QStringLiteral("%1 %2").arg(c.isEmpty() ? QStringLiteral("UNEXPECTED") : c, x));
        }
        all.insert(name, QJsonObject{ { "old", a }, { "new", b }, { "differences", dj } });
        rig.release(dir);
    }
    std::printf("\nTOTAL: %d unexpected, %d listed before the run (D1-D5)\n", unexpected, expected);
    if (!outPath.isEmpty()) {
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
    }
    return unexpected == 0 ? 0 : 1;
}
