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

// ChartsPosturePresenter (posture_chart_design.md stage 2b) against the SHIPPED pack and norms, on the
// real Nick swing of the 2a core test (a trimmed copy of 2026-10-20_03 swing_0002) joined to its own
// ledger row (a trimmed copy of that session's diagnostics.json, shot 2). Synchronous.
//
//   · a five-shot session of that swing, each copy's posture rows scaled a little (×0.9 … ×1.1) so the
//     shots differ, every copy graded with the same ledger row;
//   · the payload carries every documented key; This shot and Session both deliver, scope only picks;
//   · the pattern override (Auto → Level → Squat and push) moves the bands and the pattern line;
//   · Against: the ★ reference, the previous swing and the session median draw a ghost value per cell;
//   · seekUsFor P1…P8 are the shot's own phase times; the playhead picks the column;
//   · a shot outside the snapshot reads from its own detail; an empty model is not available;
//   · Nick alone: no exit at P3 or the top (the one before P5 is this swing's P2 head, +4.7 cm, pinned,
//     in the W-D6 words: loss_of_posture_backswing was clean on it, so D3 is never named), and
//     trail_knee_straighten (fired on this swing) is a finding.

#include "charts/charts_posture_presenter.h"
#include "charts/posture_picture.h"
#include "charts/session_posture.h"
#include "charts/session_series_model.h"
#include "charts/session_series_types.h"
#include "charts/session_wrist.h"
#include "report/report_posture.h"
#include "Diagnostics/measures/measure_sample.h"
#include "Diagnostics/norms/norm_provider.h"
#include "Export/swing_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace pinpoint::charts;

namespace {

const QString kSession = QStringLiteral("/fixture/2026-10-20_Nick-Henn_Wrist_03");
QString dirOf(int i) { return kSession + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')); }

QJsonObject readJson(const char *env)
{
    QFile f(qEnvironmentVariable(env));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// The ledger row → the shot's ShotDiagnostics, as the model's join fills the fields the presenter reads.
ShotDiagnostics diagnosticsOf(const QJsonObject &row)
{
    ShotDiagnostics d;
    d.club = row.value(QStringLiteral("club")).toString();
    for (const QJsonValue &rv : row.value(QStringLiteral("rows")).toArray()) {
        const QJsonObject r = rv.toObject();
        const QString id = r.value(QStringLiteral("conditionId")).toString();
        const QString st = r.value(QStringLiteral("state")).toString();
        if (st == QLatin1String("fired")) d.fired << id;
        else if (st == QLatin1String("clean")) d.clean << id;
        else continue;
        ShotDiagnostics::Value v;
        v.value = r.value(QStringLiteral("value")).toDouble();
        v.z = r.value(QStringLiteral("z")).toDouble();
        v.lo = r.value(QStringLiteral("corridorLo")).toDouble();
        v.hi = r.value(QStringLiteral("corridorHi")).toDouble();
        v.confidence = r.value(QStringLiteral("confidence")).toDouble();
        d.values.insert(id, v);
        ShotDiagnostics::Reading g;
        g.measureId = r.value(QStringLiteral("drivingMeasureId")).toString();
        g.contextId = r.value(QStringLiteral("contextId")).toString();
        d.gradedBy.insert(id, g);
        if (st == QLatin1String("fired")) d.drivenBy.insert(id, g);
    }
    d.fired.sort();
    d.clean.sort();
    return d;
}

// A copy of the swing as shot `ordinal`, its posture rows' changes from address scaled by `k`.
std::shared_ptr<ShotSeries> copyOf(const ShotSeries &base, int ordinal, double k)
{
    auto s = std::make_shared<ShotSeries>(base);
    s->ordinal = ordinal;
    s->shotId = ordinal;
    s->swingDir = dirOf(ordinal);
    for (const PostureRowSpec &spec : postureRowSpecs()) {
        auto it = s->metrics.find(spec.metricKey);
        if (it == s->metrics.end() || it->value.empty()) continue;
        const double a = it->value.front();
        for (double &v : it->value) if (std::isfinite(v)) v = a + (v - a) * k;
        for (PhaseSample &ps : it->phaseSamples) if (std::isfinite(ps.value)) ps.value = a + (ps.value - a) * k;
    }
    return s;
}

// shotReplay.analysisDetail's shape, from a shot: phases, impact, and every series.
QVariantMap detailOf(const ShotSeries &s)
{
    QVariantList phases, series;
    for (const PhaseMark &p : s.phases)
        phases.append(QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) } });
    for (const QString &k : s.metricOrder) {
        const MetricSeries *m = s.metric(k);
        if (!m) continue;
        QVariantList t, v, valid, ps;
        for (size_t i = 0; i < m->tUs.size(); ++i) {
            t.append(qlonglong(m->tUs[i]));
            v.append(std::isfinite(m->value[i]) ? QVariant(m->value[i]) : QVariant::fromValue(nullptr));
            valid.append(m->isValid(i) ? 1 : 0);
        }
        for (const PhaseSample &p : m->phaseSamples)
            ps.append(QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                                   { QStringLiteral("value"), p.value } });
        series.append(QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("unit"), m->unit }, { QStringLiteral("t_us"), t },
                                   { QStringLiteral("value"), v }, { QStringLiteral("valid"), valid }, { QStringLiteral("phaseSamples"), ps } });
    }
    return { { QStringLiteral("phases"), phases }, { QStringLiteral("series"), series }, { QStringLiteral("impactUs"), qlonglong(s.impactUs) } };
}

void requireKeys(const QVariantMap &m, const QStringList &keys, const char *what)
{
    for (const QString &k : keys)
        QVERIFY2(m.contains(k), qPrintable(QStringLiteral("%1 lacks \"%2\"").arg(QLatin1String(what), k)));
}

QVariantMap rowOf(const QVariantMap &section, const QString &key)
{
    for (const QVariant &r : section.value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList())
        if (r.toMap().value(QStringLiteral("key")).toString() == key) return r.toMap();
    return {};
}
QVariantMap cellOf(const QVariantMap &section, const QString &key, int pos)
{
    return rowOf(section, key).value(QStringLiteral("cells")).toList().value(pos).toMap();
}

// ── the picture strip's fixture (data/posture/picture_doc.json: Nick 20 Oct _03 swing 2, re-analysed for stage 4a) ──
QJsonObject pictureRoot() { return readJson("PP_POSTURE_PICTURE"); }
QVariantMap groupOf(const QVariantMap &pic, const QString &camera)
{
    for (const QVariant &g : pic.value(QStringLiteral("groups")).toList())
        if (g.toMap().value(QStringLiteral("camera")).toString() == camera) return g.toMap();
    return {};
}
QVariantMap frameOf(const QVariantMap &pic, const QString &camera, const QString &tag)
{
    for (const QVariant &f : groupOf(pic, camera).value(QStringLiteral("frames")).toList())
        if (f.toMap().value(QStringLiteral("tag")).toString() == tag) return f.toMap();
    return {};
}
QVariantList marksOf(const QVariantMap &frame, const QString &role)
{
    QVariantList out;
    for (const QVariant &m : frame.value(QStringLiteral("marks")).toList())
        if (m.toMap().value(QStringLiteral("role")).toString() == role) out.append(m);
    return out;
}
QStringList labelsOf(const QVariantMap &frame, const QString &role)
{
    QStringList out;
    for (const QVariant &m : marksOf(frame, role)) out << m.toMap().value(QStringLiteral("label")).toString();
    return out;
}
pinpoint::charts::PictureBands levelBands()
{
    pinpoint::charts::PictureBands b;
    b.patternName = QStringLiteral("Level");
    b.hipHas = true;  b.hipLo = -1.5;  b.hipHi = 3.5;    // +1 ± 2.5 cm, + toward the ball
    b.headHas = true; b.headLo = -6.5; b.headHi = -0.5;  // −3.5 ± 3 cm, + up
    return b;
}

// ── the "At address" block's history fixture: an athlete folder of session folders ──
bool writeFile(const QString &path, const QJsonObject &o)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return true;
}
// A swing document holding only its streams: the cameras (a serial "" records none).
bool writeSwingDoc(const QString &sessionDir, const QString &dtlSerial, const QString &faceSerial)
{
    const auto stream = [](const char *alias, const char *persp, const QString &serial) {
        QJsonObject src{ { QStringLiteral("width"), 640 } };
        if (!serial.isEmpty()) src.insert(QStringLiteral("serial"), serial);
        return QJsonObject{ { QStringLiteral("kind"), QStringLiteral("video") }, { QStringLiteral("alias"), QLatin1String(alias) },
                            { QStringLiteral("setup"), QJsonObject{ { QStringLiteral("perspectiveName"), QLatin1String(persp) } } },
                            { QStringLiteral("source"), src } };
    };
    const QString dir = sessionDir + QStringLiteral("/swing_0001");
    QDir().mkpath(dir);
    const QJsonObject root{ { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/2") },
                            { QStringLiteral("streams"), QJsonArray{ stream("DTL", "DownTheLine", dtlSerial), stream("Face-On", "FaceOn", faceSerial) } } };
    return pinpoint::SwingStore::save(dir, root);
}
// A ledger shot: rows driven by the address measures, `armHang` only as a composite's part (as poor_setup_posture carries it).
QJsonObject ledgerShot(int id, double bend, double arm, double tilt)
{
    QJsonArray rows;
    rows.append(QJsonObject{ { QStringLiteral("conditionId"), QStringLiteral("posture_too_bent") }, { QStringLiteral("state"), QStringLiteral("clean") },
                             { QStringLiteral("drivingMeasureId"), QStringLiteral("m_spineBendAtAddress") }, { QStringLiteral("value"), bend } });
    rows.append(QJsonObject{ { QStringLiteral("conditionId"), QStringLiteral("reverse_tilt_at_address") }, { QStringLiteral("state"), QStringLiteral("clean") },
                             { QStringLiteral("drivingMeasureId"), QStringLiteral("m_axisTiltAddress") }, { QStringLiteral("value"), tilt } });
    rows.append(QJsonObject{ { QStringLiteral("conditionId"), QStringLiteral("poor_setup_posture") }, { QStringLiteral("state"), QStringLiteral("clean") },
                             { QStringLiteral("drivingMeasureId"), QStringLiteral("m_setupPosture") }, { QStringLiteral("value"), 0.4 },
                             { QStringLiteral("composite"), QJsonObject{ { QStringLiteral("parts"), QJsonArray{
                                 QJsonObject{ { QStringLiteral("name"), QStringLiteral("armHang") }, { QStringLiteral("measureId"), QStringLiteral("m_armHangAddress") },
                                              { QStringLiteral("value"), arm } } } } } } });
    // a row the engine could not assess carries a 0 nobody measured: never a reading
    rows.append(QJsonObject{ { QStringLiteral("conditionId"), QStringLiteral("excessive_knee_flex") }, { QStringLiteral("state"), QStringLiteral("notAssessable") },
                             { QStringLiteral("drivingMeasureId"), QStringLiteral("m_kneeFlexAddress") }, { QStringLiteral("value"), 0 },
                             { QStringLiteral("notAssessableReason"), QStringLiteral("metric not produced on this capture") } });
    return { { QStringLiteral("shotId"), id }, { QStringLiteral("club"), QStringLiteral("7 IRON") }, { QStringLiteral("contextId"), QStringLiteral("iron_7") },
             { QStringLiteral("rows"), rows } };
}
bool writeLedger(const QString &sessionDir, const QJsonArray &shots)
{
    return writeFile(sessionDir + QStringLiteral("/diagnostics.json"),
                     QJsonObject{ { QStringLiteral("schemaVersion"), 1 },
                                  { QStringLiteral("ledger"), QJsonObject{ { QStringLiteral("schemaVersion"), 9 }, { QStringLiteral("shots"), shots } } } });
}
QVariantMap addressRowOf(const QVariantMap &section, const QString &measureId)
{
    for (const QVariant &r : section.value(QStringLiteral("address")).toMap().value(QStringLiteral("rows")).toList())
        if (r.toMap().value(QStringLiteral("measureId")).toString() == measureId) return r.toMap();
    return {};
}

} // namespace

class ChartsPosturePresenterTest : public QObject
{
    Q_OBJECT

    std::shared_ptr<const ShotSeries> m_nick;          // the real swing, graded
    std::shared_ptr<SessionSnapshot> m_five, m_alone;

    void setUp(ChartsPosturePresenter &p, std::shared_ptr<const SessionSnapshot> snap, int picked)
    {
        p.setSynchronous(true);
        p.setSnapshot(std::move(snap));
        if (picked > 0) p.setPickedSwingDir(dirOf(picked));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        warmWristBands();
        QVERIFY(postureSharedEnv().pack);
        const QJsonObject root = readJson("PP_POSTURE_FIXTURE");
        QVERIFY2(!root.isEmpty(), "PP_POSTURE_FIXTURE");
        const QJsonObject ledger = readJson("PP_POSTURE_DIAGNOSTICS");
        QVERIFY2(!ledger.isEmpty(), "PP_POSTURE_DIAGNOSTICS");
        QString why;
        auto shot = SessionSeriesModel::extractShot(root, dirOf(2), &why);
        QVERIFY2(shot, qPrintable(why));
        shot->diagnostics = diagnosticsOf(ledger);
        QVERIFY(shot->diagnostics.fired.contains(QStringLiteral("trail_knee_straighten")));
        shot->ordinal = 1;
        shot->shotId = 2;
        m_nick = shot;

        m_alone = std::make_shared<SessionSnapshot>();
        m_alone->sessionDir = kSession;
        m_alone->revision = 1;
        m_alone->shots = { m_nick };

        m_five = std::make_shared<SessionSnapshot>();
        m_five->sessionDir = kSession;
        m_five->revision = 1;
        const double ks[] = { 0.9, 0.95, 1.0, 1.05, 1.1 };
        for (int i = 0; i < 5; ++i) m_five->shots.push_back(copyOf(*m_nick, i + 1, ks[i]));
    }

    // ── every documented key ─────────────────────────────────────────────────────────────────
    void payloadShape()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 3);
        QVERIFY(p.available());
        requireKeys(p.choices(), { "scope", "scopes", "against", "againstOptions", "pattern", "patterns", "patternResolved",
                                   "patternAfter", "reference", "summary" }, "choices");
        const QVariantMap t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        requireKeys(t, { "available", "empty", "ordinal", "shotId", "swingDir", "inSession", "kicker", "graded", "head", "headText",
                         "pattern", "patternLine", "profile", "exits", "departures", "whatMoved", "findings", "workingWell",
                         "findingsEmpty", "workingWellEmpty", "address", "absolutes", "trust", "standfirsts" }, "thisShot");
        QCOMPARE(t.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 3 of 5  ·  7 iron  ·  Posture"));
        requireKeys(t.value(QStringLiteral("pattern")).toMap(),
                    { "mode", "pattern", "name", "source", "sourceWords", "line", "ok", "nearest", "nearestName", "fit", "fitWords",
                      "basis", "basisWords", "dLevel", "dSquatPush", "dLevelText", "dSquatPushText", "gap", "cells", "rows", "own", "trail" },
                    "PATTERN");
        requireKeys(t.value(QStringLiteral("pattern")).toMap().value(QStringLiteral("trail")).toMap(),
                    { "known", "kept", "word", "words", "changeAtTop", "changeText" }, "trail");
        const QVariantMap prof = t.value(QStringLiteral("profile")).toMap();
        requireKeys(prof, { "positions", "rows", "ghostWord", "ghostRefusal", "sub", "caption" }, "profile");
        QCOMPARE(prof.value(QStringLiteral("positions")).toList().size(), 8);
        requireKeys(prof.value(QStringLiteral("positions")).toList().first().toMap(),
                    { "index", "tag", "phase", "name", "available", "tUs", "seekUs" }, "POS");
        const QVariantList rows = prof.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), kPostureRows);
        for (const QVariant &rv : rows) {
            const QVariantMap r = rv.toMap();
            requireKeys(r, { "key", "label", "metricKey", "hueKey", "camera", "unit", "decimals", "poleWord", "poleText", "plusWord",
                             "minusWord", "present", "style", "styleWord", "styleText", "outCount", "hasAlt", "yLo", "yHi", "cells", "boxes" }, "ROW");
            QVERIFY(r.value(QStringLiteral("boxes")).isNull());   // This shot: no boxes
            QCOMPARE(r.value(QStringLiteral("cells")).toList().size(), 8);
            QVERIFY(r.value(QStringLiteral("yLo")).toDouble() < r.value(QStringLiteral("yHi")).toDouble());
            for (const QVariant &cv : r.value(QStringLiteral("cells")).toList())
                requireKeys(cv.toMap(), { "index", "tag", "available", "value", "text", "valueText", "warned", "warn", "warnReason",
                                          "gateWords", "counted", "banded", "bandLo", "bandHi", "bandMu", "watchLo", "watchHi",
                                          "bandContext", "bandInherited", "bandSource", "wash", "z", "outside", "ring", "ringWord",
                                          "exit", "beyondFault", "faultHi", "faultLo", "firedShots", "note", "mark", "ghost",
                                          "ghostText", "seekUs", "picked", "pickedText", "alt", "altAvailable", "altValue",
                                          "altLabel", "altFigure", "altText", "altWarn", "altWarnShort" }, "CELL");
        }
        // the trail knee is a style, said so
        QVERIFY(rowOf(t, QStringLiteral("trailKnee")).value(QStringLiteral("style")).toBool());
        QVERIFY(rowOf(t, QStringLiteral("trailKnee")).value(QStringLiteral("styleText")).toString().startsWith(QStringLiteral("style: ")));
        // a ⚠ cell keeps its number and says why (trunk bend at the top; hip depth there is the butt line since
        // stage 4c, posture_chart_design.md §2.3, so it carries no ⚠ and is matched)
        const QVariantMap bendTop = cellOf(t, QStringLiteral("bend"), 3);
        QVERIFY(bendTop.value(QStringLiteral("warned")).toBool());
        QCOMPARE(bendTop.value(QStringLiteral("warn")).toString(), QStringLiteral("⚠"));
        QVERIFY(!bendTop.value(QStringLiteral("warnReason")).toString().isEmpty());
        QVERIFY(!bendTop.value(QStringLiteral("value")).isNull());
        QVERIFY(!bendTop.value(QStringLiteral("counted")).toBool());
        const QVariantMap hipsTop = cellOf(t, QStringLiteral("hips"), 3);
        QVERIFY(!hipsTop.value(QStringLiteral("warned")).toBool());
        QCOMPARE(hipsTop.value(QStringLiteral("warn")).toString(), QString());
        QVERIFY(hipsTop.value(QStringLiteral("counted")).toBool());
        // "-" for a missing value, never 0; the fault hairline where a condition grades the cell
        const QVariantMap hipsP8 = cellOf(t, QStringLiteral("hips"), 7);
        QCOMPARE(hipsP8.value(QStringLiteral("text")).toString(), QStringLiteral("-"));
        QVERIFY(hipsP8.value(QStringLiteral("value")).isNull());
        QCOMPARE(hipsP8.value(QStringLiteral("mark")).toString(), QStringLiteral("none"));
        const QVariantMap hips7 = cellOf(t, QStringLiteral("hips"), 6);
        requireKeys(hips7.value(QStringLiteral("faultHi")).toMap(), { "edge", "edgeText", "conditionId", "label" }, "FAULT");
        QCOMPARE(hips7.value(QStringLiteral("faultHi")).toMap().value(QStringLiteral("conditionId")).toString(), QStringLiteral("early_extension"));

        for (const QVariant &e : t.value(QStringLiteral("exits")).toList())
            requireKeys(e.toMap(), { "row", "rowLabel", "pos", "tag", "value", "valueText", "z", "zText", "departure", "name",
                                     "conditionIds", "named", "words", "seekUs" }, "EXIT");
        for (const QVariant &d : t.value(QStringLiteral("departures")).toList())
            requireKeys(d.toMap(), { "id", "name", "conditionIds", "positions", "positionsText", "firstTag", "seekUs" }, "DEPARTURE");
        for (const char *k : { "top", "impact" })
            requireKeys(t.value(QStringLiteral("whatMoved")).toMap().value(QLatin1String(k)).toMap(),
                        { "pos", "tag", "when", "line", "head", "bend", "attribution", "hipsIn", "hipsWarned", "bendWarned", "seekUs" }, "MOVED");
        const QVariantList findings = t.value(QStringLiteral("findings")).toList();
        QVERIFY(!findings.isEmpty());
        for (const QVariant &f : findings + t.value(QStringLiteral("workingWell")).toList())
            requireKeys(f.toMap(), { "id", "name", "kind", "departure", "departureName", "severity", "severityWord", "tier", "positions",
                                     "positionsText", "tag", "seekUs", "seekText", "value", "valueText", "z", "explanation", "protect" },
                        "FINDING");
        requireKeys(t.value(QStringLiteral("address")).toMap(), { "available", "title", "sub", "rows", "legend", "lines" }, "ADDRESSBLOCK");
        for (const QVariant &a : t.value(QStringLiteral("absolutes")).toList())
            requireKeys(a.toMap(), { "measureId", "label", "measureLabel", "unit", "camera", "cameraKey", "metricKey", "hueKey", "value",
                                     "text", "valueWord", "banded", "bandLo", "bandHi", "bandMu", "bandText", "bandSource", "bandContext",
                                     "bandInherited", "box", "boxN", "boxText", "values", "dot", "history", "historyN", "historySessions",
                                     "hasHistory", "historyText", "historyWhy", "warned", "warn", "warnShort", "xLo", "xHi", "trust" }, "ADDRESS");
        requireKeys(t.value(QStringLiteral("trust")).toMap(), { "lines", "cameras", "warnings", "bandSources", "trunkDatum", "rigRule" }, "TRUST");
        QVERIFY(!t.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList().isEmpty());
        QCOMPARE(t.value(QStringLiteral("standfirsts")).toList().size(), 8);
        QVERIFY(!p.standfirst().value(QStringLiteral("text")).toString().isEmpty());

        const QVariantMap s = p.session();
        requireKeys(s, { "available", "n", "shots", "small", "kicker", "fromSession", "head", "headText", "pattern", "patternLine",
                         "profile", "strip", "departures", "departuresEmpty", "repeat", "whatMoved", "address", "absolutes", "trust" }, "session");
        QVERIFY(s.value(QStringLiteral("available")).toBool());
        QVERIFY(s.value(QStringLiteral("fromSession")).toBool());
        QCOMPARE(s.value(QStringLiteral("shots")).toInt(), 5);
        const QVariantMap strip = s.value(QStringLiteral("strip")).toMap();
        requireKeys(strip, { "marks", "onPattern", "line", "legend" }, "strip");
        const QVariantList marks = strip.value(QStringLiteral("marks")).toList();
        QCOMPARE(marks.size(), 5);
        for (const QVariant &m : marks)
            requireKeys(m.toMap(), { "ordinal", "shotId", "swingDir", "ok", "nearest", "nearestName", "between", "differs", "picked",
                                     "dLevel", "dSquatPush", "word" }, "MARK");
        QCOMPARE(marks.at(2).toMap().value(QStringLiteral("swingDir")).toString(), dirOf(3));   // click a mark → that swing
        QVERIFY(marks.at(2).toMap().value(QStringLiteral("picked")).toBool());
        for (const QVariant &d : s.value(QStringLiteral("departures")).toList()) {
            requireKeys(d.toMap(), { "id", "name", "conditionIds", "shots", "countText", "ordinals", "marks", "firstTag", "line" }, "COUNT");
            QVERIFY(!d.toMap().value(QStringLiteral("conditionIds")).toStringList().isEmpty());
        }
        requireKeys(s.value(QStringLiteral("repeat")).toMap(), { "tightest", "loosest", "words" }, "repeat");
        requireKeys(s.value(QStringLiteral("repeat")).toMap().value(QStringLiteral("tightest")).toMap(),
                    { "has", "row", "rowLabel", "tag", "iqr", "iqrText", "words" }, "REPEAT");
        const QVariantMap hipsS = rowOf(s, QStringLiteral("hips"));
        const QVariantList boxes = hipsS.value(QStringLiteral("boxes")).toList();
        QCOMPARE(boxes.size(), 8);
        const QVariantMap b7 = boxes.at(6).toMap();
        requireKeys(b7, { "box", "n", "median", "medianText", "wide", "wideText", "picked" }, "BOXCELL");
        QCOMPARE(b7.value(QStringLiteral("n")).toInt(), 5);
        QVERIFY(!b7.value(QStringLiteral("picked")).isNull());   // this shot ringed
        QVERIFY(!b7.value(QStringLiteral("wideText")).toString().isEmpty());
        QCOMPARE(boxes.at(0).toMap().value(QStringLiteral("wideText")).toString(), QString());   // P1: its figure alone
        requireKeys(b7.value(QStringLiteral("box")).toMap(), { "n", "min", "max", "q1", "med", "q3", "lo", "hi", "outliers", "texts" }, "BOX");

        // the report's page is the session map, with nothing picked
        const QVariantMap rep = postureSessionReportPayload(*m_five);
        QStringList repKeys = rep.keys();
        QVERIFY(repKeys.removeOne(QStringLiteral("picture")));   // + the typical swing's picture strip (stage 5)
        QCOMPARE(repKeys, QStringList(s.keys()));
        QCOMPARE(rep.value(QStringLiteral("headText")).toString(), s.value(QStringLiteral("headText")).toString());
        for (const QVariant &m : rep.value(QStringLiteral("strip")).toMap().value(QStringLiteral("marks")).toList())
            QVERIFY(!m.toMap().value(QStringLiteral("picked")).toBool());
        QVERIFY(!postureSessionReportPayload(SessionSnapshot{}).value(QStringLiteral("available")).toBool());
        // the report counts swings: one sentence code, the noun passed in
        const QVariantMap sw = postureSessionReportPayload(*m_five, -1, QStringLiteral("swings"));
        QVERIFY2(sw.value(QStringLiteral("kicker")).toString().startsWith(QStringLiteral("5 swings")), qPrintable(sw.value(QStringLiteral("kicker")).toString()));
        QVERIFY2(sw.value(QStringLiteral("headText")).toString().contains(QStringLiteral(" swings")), qPrintable(sw.value(QStringLiteral("headText")).toString()));
        for (const char *k : { "headText", "patternLine", "kicker", "departuresEmpty" })
            QVERIFY2(!sw.value(QString::fromLatin1(k)).toString().contains(QStringLiteral("shot")), k);
        QVERIFY(!sw.value(QStringLiteral("strip")).toMap().value(QStringLiteral("line")).toString().contains(QStringLiteral("shot")));
        for (const QVariant &d : sw.value(QStringLiteral("departures")).toList())
            QVERIFY(!d.toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("shot")));

        // …and the report's Posture page block cut from it (report/report_posture.h): the real rows and boxes,
        // the strip numbered by swing folder, absent for an empty session
        QHash<QString, int> swingNo;
        for (int i = 1; i <= 5; ++i) swingNo.insert(dirOf(i), 10 + i);
        const QVariantMap blk = pinpoint::report::postureReportBlock(rep, swingNo).toMap();
        QVERIFY(!blk.isEmpty());
        const QVariantList bRows = blk.value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList();
        QVERIFY(!bRows.isEmpty() && bRows.size() <= 6);
        QCOMPARE(bRows.at(0).toMap().value(QStringLiteral("boxes")).toList().size(), 8);
        QCOMPARE(blk.value(QStringLiteral("profile")).toMap().value(QStringLiteral("positions")).toList().size(), 8);
        QCOMPARE(blk.value(QStringLiteral("head")).toString(), rep.value(QStringLiteral("headText")).toString());
        const QVariantList bMarks = blk.value(QStringLiteral("strip")).toMap().value(QStringLiteral("marks")).toList();
        QCOMPARE(bMarks.size(), 5);
        QCOMPARE(bMarks.at(0).toMap().value(QStringLiteral("swing")).toInt(), 11);
        QVERIFY(!blk.value(QStringLiteral("trust")).toStringList().isEmpty());
        QVERIFY(pinpoint::report::postureReportBlock(postureSessionReportPayload(SessionSnapshot{}), swingNo).isNull());
    }

    // ── Mark's honesty rule (22 Oct; decision 16): every reading shown, under a ⚠ where it may be unreliable ──
    void honestReadings()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 3);
        const QVariantMap t = p.thisShot(), s = p.session();
        for (const QVariantMap &view : { t, s }) {
            // (1) pelvis height at P3–P5: the authored seed band drawn (Level: −3, −5, −8 ± 6 % stance), the ⚠ kept,
            // out of the match
            const double mus[] = { -3.0, -5.0, -8.0 };
            for (int pos = 2; pos <= 4; ++pos) {
                const QVariantMap c = cellOf(view, QStringLiteral("pelvisHeight"), pos);
                QVERIFY2(c.value(QStringLiteral("banded")).toBool(), qPrintable(QStringLiteral("pelvis P%1 banded").arg(pos + 1)));
                QCOMPARE(c.value(QStringLiteral("wash")).toString(), QStringLiteral("band"));
                QCOMPARE(c.value(QStringLiteral("bandMu")).toDouble(), mus[pos - 2]);
                QCOMPARE(c.value(QStringLiteral("bandSource")).toString(), QStringLiteral("heuristic"));
                QVERIFY(c.value(QStringLiteral("bandContext")).toString() != QLatin1String("any") || pos != 3);   // P4: at the pattern node
                QVERIFY(c.value(QStringLiteral("warned")).toBool());
                QVERIFY(!c.value(QStringLiteral("counted")).toBool());
                QVERIFY(!c.value(QStringLiteral("ring")).toBool() && !c.value(QStringLiteral("exit")).toBool());
                if (!c.value(QStringLiteral("value")).isNull()) QCOMPARE(c.value(QStringLiteral("warn")).toString(), QStringLiteral("⚠"));
            }
            // (2) trunk bend's second reading: the back line at P3–P5 only, with its words; never counted
            for (int pos = 0; pos < 8; ++pos) {
                const QVariantMap c = cellOf(view, QStringLiteral("bend"), pos);
                const bool mid = pos >= 2 && pos <= 4;
                QCOMPARE(c.value(QStringLiteral("alt")).toBool(), mid);
                if (!mid) { QCOMPARE(c.value(QStringLiteral("altText")).toString(), QString()); continue; }
                QVERIFY(!c.value(QStringLiteral("counted")).toBool());
                QVERIFY(c.value(QStringLiteral("altText")).toString().startsWith(QStringLiteral("back line ")));
                QCOMPARE(c.value(QStringLiteral("altLabel")).toString(), QStringLiteral("back line"));
                if (c.value(QStringLiteral("altAvailable")).toBool()) {
                    QCOMPARE(c.value(QStringLiteral("altWarn")).toString(), QStringLiteral("⚠"));
                    QCOMPARE(c.value(QStringLiteral("altWarnShort")).toString(), QStringLiteral("can catch the arms at the top"));
                }
            }
            QVERIFY(rowOf(view, QStringLiteral("bend")).value(QStringLiteral("hasAlt")).toBool());
            for (const char *k : { "head", "hips", "leadKnee", "trailKnee", "pelvisHeight" }) {
                QVERIFY(!rowOf(view, QLatin1String(k)).value(QStringLiteral("hasAlt")).toBool());
                for (const QVariant &cv : rowOf(view, QLatin1String(k)).value(QStringLiteral("cells")).toList())
                    QVERIFY(!cv.toMap().value(QStringLiteral("alt")).toBool());
            }
            // Nick's top: the joints read −9, the back line −3 (51.2° → 48.7°: his back held, posture_chart_design.md §2.3)
            const QVariantMap top = cellOf(view, QStringLiteral("bend"), 3);
            QCOMPARE(top.value(QStringLiteral("altText")).toString(), QStringLiteral("back line −3"));
            QVERIFY(std::fabs(top.value(QStringLiteral("altValue")).toDouble() - (48.6688276358798 - 51.234310969705014)) < 1e-6);
            // d: exactly the counted cells — no ⚠ cell, no second reading
            int counted = 0;
            for (const QVariant &rv : view.value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList())
                for (const QVariant &cv : rv.toMap().value(QStringLiteral("cells")).toList())
                    if (cv.toMap().value(QStringLiteral("counted")).toBool()) ++counted;
            QCOMPARE(view.value(QStringLiteral("pattern")).toMap().value(QStringLiteral("cells")).toInt(), counted);
            // said once in the trust fold: the two readings, and the pelvis band an authored seed
            const QStringList lines = view.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
            QCOMPARE(lines.count(postureBackLineTrust()), 1);
            QVERIFY(std::none_of(lines.begin(), lines.end(), [](const QString &l) { return l.startsWith(QStringLiteral("⚠ Trunk bend at P3–P5")); }));
            QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString &l) {
                return l.startsWith(QStringLiteral("⚠ Pelvis height at P3–P5")) && l.contains(QStringLiteral("authored seed")); }));
        }
        // the match itself: the pelvis band at P3–P5 changes no distance — d with and without those cells' values agree
        PostureShotInput in = postureInputFromShot(*m_nick, postureSharedEnv());
        const PostureMatch with = postureMatch(in.value, in.present, postureShotWarn(in), postureSharedEnv());
        for (int pos = 2; pos <= 4; ++pos) in.value[size_t(PostureRow::PelvisHeight)][size_t(pos)] = 40.0;
        in.backLine.fill(40.0);
        const PostureMatch moved = postureMatch(in.value, in.present, postureShotWarn(in), postureSharedEnv());
        QCOMPARE(moved.cells, with.cells);
        QCOMPARE(moved.dLevel, with.dLevel);
        QCOMPARE(moved.dSquatPush, with.dSquatPush);
        // a capture with no back line: the cells still say "back line -", no ⚠ on a missing figure, and the trust fold
        // keeps the joint reading's own line
        auto bare = std::make_shared<ShotSeries>(*m_nick);
        bare->metrics.remove(QStringLiteral("spineBackLine"));
        bare->metricOrder.removeAll(QStringLiteral("spineBackLine"));
        auto one = std::make_shared<SessionSnapshot>(*m_alone);
        one->shots = { bare };
        ChartsPosturePresenter q;
        setUp(q, one, 2);   // the swing is swing 2 (dirOf(2)), ordinal 1
        const QVariantMap bt = cellOf(q.thisShot(), QStringLiteral("bend"), 3);
        QCOMPARE(bt.value(QStringLiteral("altText")).toString(), QStringLiteral("back line -"));
        QCOMPARE(bt.value(QStringLiteral("altWarn")).toString(), QString());
        const QStringList bl = q.thisShot().value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
        QCOMPARE(bl.count(postureBackLineTrust()), 0);
        QVERIFY(std::any_of(bl.begin(), bl.end(), [](const QString &l) { return l.startsWith(QStringLiteral("⚠ Trunk bend at P3–P5")); }));
    }

    // ── scope picks; both views deliver ───────────────────────────────────────────────────────
    void thisShotAgainstSession()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 2);
        QCOMPARE(p.choices().value(QStringLiteral("scope")).toString(), QStringLiteral("shot"));
        const QString shotHips7 = cellOf(p.thisShot(), QStringLiteral("hips"), 6).value(QStringLiteral("text")).toString();
        p.setScope(QStringLiteral("session"));
        const QVariantMap ch = p.choices();
        QCOMPARE(ch.value(QStringLiteral("scope")).toString(), QStringLiteral("session"));
        QVERIFY(ch.value(QStringLiteral("scopes")).toList().at(1).toMap().value(QStringLiteral("current")).toBool());
        QVERIFY(p.thisShot().value(QStringLiteral("available")).toBool());
        QVERIFY(p.session().value(QStringLiteral("available")).toBool());
        // This shot draws the shot; Session draws the median (shot 3 of the scaled copies), this shot ringed on it
        const QVariantMap med = cellOf(p.session(), QStringLiteral("hips"), 6);
        const double shot2 = cellOf(p.thisShot(), QStringLiteral("hips"), 6).value(QStringLiteral("value")).toDouble();
        QVERIFY(std::fabs(med.value(QStringLiteral("value")).toDouble() - shot2 / 0.95) < 1e-6);   // shot 2 is ×0.95 of the median shot
        QCOMPARE(med.value(QStringLiteral("pickedText")).toString(), shotHips7);
        QVERIFY(rowOf(p.thisShot(), QStringLiteral("hips")).value(QStringLiteral("boxes")).isNull());
        QVERIFY(!rowOf(p.session(), QStringLiteral("hips")).value(QStringLiteral("boxes")).isNull());
    }

    // ── the pattern: Auto → Level → Squat and push ────────────────────────────────────────────
    void patternOverride()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 1);
        const QVariantMap autoP = p.thisShot().value(QStringLiteral("pattern")).toMap();
        QCOMPARE(autoP.value(QStringLiteral("mode")).toInt(), -1);
        QCOMPARE(autoP.value(QStringLiteral("source")).toString(), QStringLiteral("session"));
        const QString resolved = autoP.value(QStringLiteral("name")).toString();
        QCOMPARE(p.choices().value(QStringLiteral("patternAfter")).toString(), QStringLiteral("→ ") + resolved);
        QVERIFY(p.choices().value(QStringLiteral("summary")).toString().startsWith(QStringLiteral("Pattern Auto → ") + resolved));

        p.setPattern(0);
        const QVariantMap level = p.thisShot();
        QCOMPARE(level.value(QStringLiteral("patternLine")).toString().left(22), QStringLiteral("Level (your choice) · "));
        QCOMPARE(p.choices().value(QStringLiteral("patternAfter")).toString(), QString());
        const QVariantMap lHead5 = cellOf(level, QStringLiteral("head"), 4);

        p.setPattern(1);
        const QVariantMap sp = p.thisShot();
        QVERIFY(sp.value(QStringLiteral("patternLine")).toString().startsWith(QStringLiteral("Squat and push (your choice) · ")));
        QCOMPARE(sp.value(QStringLiteral("pattern")).toMap().value(QStringLiteral("pattern")).toInt(), 1);
        const QVariantMap sHead5 = cellOf(sp, QStringLiteral("head"), 4);
        // §7.1: the head at P5 is 0 ± 3 on Level, −4 ± 3 on Squat and push
        QVERIFY(lHead5.value(QStringLiteral("banded")).toBool() && sHead5.value(QStringLiteral("banded")).toBool());
        QVERIFY(sHead5.value(QStringLiteral("bandMu")).toDouble() < lHead5.value(QStringLiteral("bandMu")).toDouble() - 3.0);
        QVERIFY(sHead5.value(QStringLiteral("bandLo")).toDouble() < lHead5.value(QStringLiteral("bandLo")).toDouble());
        QCOMPARE(p.session().value(QStringLiteral("pattern")).toMap().value(QStringLiteral("pattern")).toInt(), 1);
        p.setPattern(7);   // out of range: Auto
        QCOMPARE(p.pattern(), -1);
    }

    // ── Against: a ghost value per cell ───────────────────────────────────────────────────────
    void againstGhost()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 1);
        p.setAgainst(QStringLiteral("reference"));
        QVERIFY(!p.thisShot().value(QStringLiteral("profile")).toMap().value(QStringLiteral("ghostRefusal")).toString().isEmpty());
        p.setReferenceSwingDir(dirOf(5));
        QVariantMap t = p.thisShot();
        QCOMPARE(t.value(QStringLiteral("profile")).toMap().value(QStringLiteral("ghostWord")).toString(), QStringLiteral("the reference ★"));
        QVERIFY(t.value(QStringLiteral("profile")).toMap().value(QStringLiteral("sub")).toString().contains(QStringLiteral("dashed: the reference ★")));
        const QVariantMap c7 = cellOf(t, QStringLiteral("hips"), 6);
        QVERIFY(!c7.value(QStringLiteral("ghost")).isNull());
        // the reference is ×1.1, this shot ×0.9 of the same swing
        QCOMPARE(qRound(c7.value(QStringLiteral("ghost")).toDouble() / c7.value(QStringLiteral("value")).toDouble() * 900), 1100);
        QVERIFY(cellOf(t, QStringLiteral("hips"), 7).value(QStringLiteral("ghost")).isNull());   // no value, no ghost
        const QVariantMap ref = p.choices().value(QStringLiteral("reference")).toMap();
        QVERIFY(ref.value(QStringLiteral("set")).toBool());
        QVERIFY(!ref.value(QStringLiteral("isThisShot")).toBool());

        p.setAgainst(QStringLiteral("median"));
        t = p.thisShot();
        QCOMPARE(t.value(QStringLiteral("profile")).toMap().value(QStringLiteral("ghostWord")).toString(), QStringLiteral("session median, n 5"));
        QVERIFY(!cellOf(t, QStringLiteral("head"), 6).value(QStringLiteral("ghost")).isNull());

        p.setAgainst(QStringLiteral("previous"));   // shot 1 has no previous
        t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("profile")).toMap().value(QStringLiteral("ghostWord")).toString().isEmpty());
        QVERIFY(cellOf(t, QStringLiteral("head"), 6).value(QStringLiteral("ghost")).isNull());
        p.setPickedSwingDir(dirOf(2));
        QCOMPARE(p.thisShot().value(QStringLiteral("profile")).toMap().value(QStringLiteral("ghostWord")).toString(), QStringLiteral("previous swing"));

        p.setAgainst(QStringLiteral("address"));
        QVERIFY(cellOf(p.thisShot(), QStringLiteral("hips"), 6).value(QStringLiteral("ghost")).isNull());
    }

    // ── seekUsFor and the playhead ────────────────────────────────────────────────────────────
    void seekAndPlayhead()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 2);
        QVERIFY(p.available());
        const ShotSeries &s = *m_five->shots[1];
        const auto &ids = posturePhaseIds();
        int found = 0;
        for (int i = 0; i < kPosturePositions; ++i) {
            qint64 want = -1;
            for (const PhaseMark &pm : s.phases) if (pm.phase == ids[size_t(i)]) { want = pm.tUs; break; }
            QCOMPARE(p.seekUsFor(QStringLiteral("P%1").arg(i + 1)), want);
            if (want >= 0) ++found;
        }
        QVERIFY(found >= 6);
        QVERIFY(p.seekUsFor(QStringLiteral("P1")) >= 0);
        QVERIFY(p.seekUsFor(QStringLiteral("P7")) >= 0);
        QCOMPARE(p.seekUsFor(QStringLiteral("P9")), qint64(-1));
        QCOMPARE(p.seekUsFor(QStringLiteral("top")), qint64(-1));
        p.setPlayheadUs(p.seekUsFor(QStringLiteral("P4")));
        QCOMPARE(p.selected(), 3);
        QVERIFY(p.standfirst().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("At P4, the top: ")));
        p.setPlayheadUs(p.seekUsFor(QStringLiteral("P7")) + 1000);
        QCOMPARE(p.selected(), 6);
        // the column seek is on every cell too
        QCOMPARE(cellOf(p.thisShot(), QStringLiteral("head"), 6).value(QStringLiteral("seekUs")).toLongLong(), p.seekUsFor(QStringLiteral("P7")));
    }

    // ── a shot outside the snapshot reads from its own detail ─────────────────────────────────
    void shotFromDetail()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 0);
        const QString dir = QStringLiteral("/fixture/elsewhere/swing_0009");
        p.setPickedSwingDir(dir);
        QVERIFY(!p.thisShot().value(QStringLiteral("available")).toBool());   // no detail yet
        QVERIFY(p.thisShot().value(QStringLiteral("empty")).toString().startsWith(QStringLiteral("No posture readings for this swing.")));
        p.setPickedDetail(detailOf(*m_nick));
        const QVariantMap t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QVERIFY(!t.value(QStringLiteral("inSession")).toBool());
        QCOMPARE(t.value(QStringLiteral("swingDir")).toString(), dir);
        QVERIFY(!t.value(QStringLiteral("graded")).toBool());
        QCOMPARE(t.value(QStringLiteral("findingsEmpty")).toString(), QStringLiteral("Diagnostics have not graded this swing yet."));
        // the same reading as the snapshot's copy of this swing (shot 3, ×1.0)
        ChartsPosturePresenter q;
        setUp(q, m_five, 3);
        for (const char *k : { "head", "hips", "bend", "leadKnee", "trailKnee", "pelvisHeight" })
            for (int pos = 0; pos < kPosturePositions; ++pos)
                QCOMPARE(cellOf(t, QLatin1String(k), pos).value(QStringLiteral("text")).toString(),
                         cellOf(q.thisShot(), QLatin1String(k), pos).value(QStringLiteral("text")).toString());
        QVERIFY(p.seekUsFor(QStringLiteral("P4")) >= 0);
        // nothing on the strip is this swing
        for (const QVariant &m : p.session().value(QStringLiteral("strip")).toMap().value(QStringLiteral("marks")).toList())
            QVERIFY(!m.toMap().value(QStringLiteral("picked")).toBool());
    }

    // ── nothing to read ───────────────────────────────────────────────────────────────────────
    void emptyModel()
    {
        ChartsPosturePresenter p;
        p.setSynchronous(true);
        QVERIFY(!p.available());
        QVERIFY(!p.thisShot().value(QStringLiteral("available")).toBool());
        QVERIFY(p.thisShot().value(QStringLiteral("empty")).toString().startsWith(QStringLiteral("Select a swing to review.")));
        QVERIFY(!p.session().value(QStringLiteral("available")).toBool());
        QCOMPARE(p.seekUsFor(QStringLiteral("P4")), qint64(-1));
        p.setSnapshot(std::make_shared<SessionSnapshot>());
        QVERIFY(!p.available());
    }

    // ── "At address" (§7.4.2): every rig-sensitive reading shown, its band the norm row's, its ⚠; no history here ─────
    void addressBlock()
    {
        ChartsPosturePresenter p;
        setUp(p, m_five, 3);
        const PostureEnv &env = postureSharedEnv();
        QVERIFY2(env.absolutes.size() >= 6, "the pack's rigSensitive measures");
        const QVariantMap t = p.thisShot();
        const QVariantMap ad = t.value(QStringLiteral("address")).toMap();
        QVERIFY(ad.value(QStringLiteral("available")).toBool());
        QCOMPARE(ad.value(QStringLiteral("title")).toString(), QStringLiteral("At address"));
        const QVariantList rows = ad.value(QStringLiteral("rows")).toList();
        // Nick's swing carries both cameras' series: a row for every rigSensitive measure, read off the flag, in pack order
        QCOMPARE(rows.size(), int(env.absolutes.size()));
        for (size_t i = 0; i < env.absolutes.size(); ++i)
            QCOMPARE(rows.at(int(i)).toMap().value(QStringLiteral("measureId")).toString(), env.absolutes[i].measureId);
        for (const char *id : { "m_spineBendAtAddress", "m_kneeFlexAddress", "m_armHangAddress", "m_headPitchAddress",
                                "m_balanceHeelToeAddress", "m_axisTiltAddress" })
            QVERIFY2(!addressRowOf(t, QLatin1String(id)).isEmpty(), id);
        const QString ctx = m_nick->diagnostics.gradedBy.isEmpty() ? QStringLiteral("iron_7") : m_nick->diagnostics.gradedBy.cbegin()->contextId;
        for (const QVariant &rv : rows) {
            const QVariantMap r = rv.toMap();
            const QString id = r.value(QStringLiteral("measureId")).toString();
            QVERIFY2(!r.value(QStringLiteral("label")).toString().isEmpty() && !r.value(QStringLiteral("unit")).toString().isEmpty(), qPrintable(id));
            // the ⚠ and its few words on every row (no capture can be declared trusted yet)
            QVERIFY(r.value(QStringLiteral("warned")).toBool());
            QCOMPARE(r.value(QStringLiteral("warn")).toString(), QStringLiteral("⚠"));
            QCOMPARE(r.value(QStringLiteral("warnShort")).toString(), QStringLiteral("depends on where the camera stands"));
            // this shot's number, on this session's box (five shots), the dot at it
            QVERIFY2(!r.value(QStringLiteral("value")).isNull(), qPrintable(id));
            QVERIFY(r.value(QStringLiteral("text")).toString() != QLatin1String("-"));
            QCOMPARE(r.value(QStringLiteral("dot")).toDouble(), r.value(QStringLiteral("value")).toDouble());
            QCOMPARE(r.value(QStringLiteral("boxN")).toInt(), 5);
            QCOMPARE(r.value(QStringLiteral("values")).toList().size(), 5);
            // the band: the norm row the pack resolves at the shot's grading context, never invented
            const pinpoint::analysis::NormResolution res = env.norms->resolve(id, ctx);
            QVERIFY2(res.found(), qPrintable(id));
            QVERIFY(r.value(QStringLiteral("banded")).toBool());
            QCOMPARE(r.value(QStringLiteral("bandMu")).toDouble(), res.norm->mu);
            QVERIFY(r.value(QStringLiteral("bandLo")).toDouble() < res.norm->mu && r.value(QStringLiteral("bandHi")).toDouble() > res.norm->mu);
            QVERIFY(r.value(QStringLiteral("bandText")).toString().startsWith(QStringLiteral("good players ")));
            QCOMPARE(r.value(QStringLiteral("bandSource")).toString(), pinpoint::analysis::normSourceName(res.norm->source));
            QVERIFY(r.value(QStringLiteral("xLo")).toDouble() <= r.value(QStringLiteral("bandLo")).toDouble());
            QVERIFY(r.value(QStringLiteral("xHi")).toDouble() >= r.value(QStringLiteral("bandHi")).toDouble());
            // no earlier session (the fixture's folder is not on disk): no history box, said in words
            QVERIFY(!r.value(QStringLiteral("hasHistory")).toBool());
            QVERIFY(r.value(QStringLiteral("history")).isNull());
            QCOMPARE(r.value(QStringLiteral("historyN")).toInt(), 0);
            QVERIFY(!r.value(QStringLiteral("historyWhy")).toString().isEmpty());
        }
        // forward bend at 40 ± 8° (the any row): "good players 32 to 48°" — words, never a dash after a minus
        QCOMPARE(addressRowOf(t, QStringLiteral("m_spineBendAtAddress")).value(QStringLiteral("bandText")).toString(), QStringLiteral("good players 32 to 48°"));
        for (const QVariant &rv : t.value(QStringLiteral("address")).toMap().value(QStringLiteral("rows")).toList())
            QVERIFY2(!rv.toMap().value(QStringLiteral("bandText")).toString().contains(QChar(0x2013)), qPrintable(rv.toMap().value(QStringLiteral("bandText")).toString()));
        // one judge: this shot's number is the ledger's graded value for the same measure (shot 3 is the unscaled copy)
        const QJsonObject ledger = readJson("PP_POSTURE_DIAGNOSTICS");
        for (const QJsonValue &rv : ledger.value(QStringLiteral("rows")).toArray())
            if (rv.toObject().value(QStringLiteral("drivingMeasureId")).toString() == QLatin1String("m_spineBendAtAddress")) {
                QVERIFY(std::fabs(addressRowOf(t, QStringLiteral("m_spineBendAtAddress")).value(QStringLiteral("value")).toDouble()
                                  - rv.toObject().value(QStringLiteral("value")).toDouble()) < 0.5);
                break;
            }
        // the trust fold says the rule once, verbatim, and what the set-up is keyed on
        const QStringList lines = t.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
        QCOMPARE(lines.count(postureRigRule()), 1);
        QVERIFY(postureRigRule().startsWith(QStringLiteral("How bent you are at address depends on where the camera stands, so read the "
                                                           "good-player band as a guide and your own sessions from this set-up as the real comparison.")));
        QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString &l) { return l.startsWith(QStringLiteral("Set-up not recorded")); }));
        QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString &l) { return l.contains(QStringLiteral("heuristic")); }));
        for (const QString &l : ad.value(QStringLiteral("lines")).toStringList()) QVERIFY2(lines.contains(l), qPrintable(l));

        // Session: the session's box, its median as the number, no dot
        const QVariantMap sr = addressRowOf(p.session(), QStringLiteral("m_spineBendAtAddress"));
        QVERIFY(!sr.isEmpty());
        QCOMPARE(sr.value(QStringLiteral("valueWord")).toString(), QStringLiteral("median"));
        QVERIFY(sr.value(QStringLiteral("dot")).isNull());
        QCOMPARE(sr.value(QStringLiteral("value")).toDouble(), sr.value(QStringLiteral("box")).toMap().value(QStringLiteral("med")).toDouble());

        // a reading with no value on the shot is "-", the row kept, its band still drawn
        auto bare = std::make_shared<ShotSeries>(*m_nick);
        bare->metrics.remove(QStringLiteral("armHang"));
        auto snap = std::make_shared<SessionSnapshot>(*m_five);
        snap->revision = 7;
        snap->shots[2] = [&] { auto c = copyOf(*bare, 3, 1.0); return std::shared_ptr<const ShotSeries>(c); }();
        ChartsPosturePresenter q;
        setUp(q, snap, 3);
        const QVariantMap arm = addressRowOf(q.thisShot(), QStringLiteral("m_armHangAddress"));
        QVERIFY(!arm.isEmpty());
        QCOMPARE(arm.value(QStringLiteral("text")).toString(), QStringLiteral("-"));
        QVERIFY(arm.value(QStringLiteral("value")).isNull());
        QVERIFY(arm.value(QStringLiteral("banded")).toBool());
        QCOMPARE(arm.value(QStringLiteral("boxN")).toInt(), 4);

        // a capture with no down-the-line camera: those readings are no row, said once in the trust fold
        auto faceOnly = std::make_shared<SessionSnapshot>();
        faceOnly->sessionDir = kSession;
        faceOnly->revision = 1;
        for (int i = 0; i < 5; ++i) {
            auto c = copyOf(*m_nick, i + 1, 1.0);
            for (const char *k : { "pelvisThrust", "hipDepthButt", "spineForwardBend", "leadKneeFlexion", "trailKneeFlexion",
                                   "balanceHeelToe", "armHang", "headPitch" })
                c->metrics.remove(QLatin1String(k));
            faceOnly->shots.push_back(c);
        }
        ChartsPosturePresenter f;
        setUp(f, faceOnly, 3);
        const QVariantMap ft = f.thisShot();
        const QVariantList frows = ft.value(QStringLiteral("address")).toMap().value(QStringLiteral("rows")).toList();
        QCOMPARE(frows.size(), 1);
        QCOMPARE(frows.at(0).toMap().value(QStringLiteral("measureId")).toString(), QStringLiteral("m_axisTiltAddress"));
        const QStringList flines = ft.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
        QVERIFY(std::any_of(flines.begin(), flines.end(), [](const QString &l) {
            return l.startsWith(QStringLiteral("Not read at address on this capture:")) && l.contains(QStringLiteral("down the line camera")); }));
    }

    // ── "At address": the golfer's earlier sessions from the same set-up, off the athlete folder's ledgers ─────────
    void addressHistory()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString athlete = tmp.path() + QStringLiteral("/Nick-Henn");
        const QString cur = athlete + QStringLiteral("/2026-10-20_Nick-Henn_Wrist_03");
        const QString same = athlete + QStringLiteral("/2026-10-13_Nick-Henn_Wrist_01");
        const QString other = athlete + QStringLiteral("/2026-10-13_Nick-Henn_Wrist_02");
        const QString later = athlete + QStringLiteral("/2026-10-25_Nick-Henn_Wrist_01");
        QVERIFY(writeSwingDoc(cur, QStringLiteral("18277032"), QStringLiteral("17453937")));
        QVERIFY(writeSwingDoc(same, QStringLiteral("18277032"), QStringLiteral("17453937")));
        QVERIFY(writeSwingDoc(other, QStringLiteral("99999999"), QStringLiteral("17453937")));   // another DTL camera
        QVERIFY(writeSwingDoc(later, QStringLiteral("18277032"), QStringLiteral("17453937")));
        QVERIFY(writeLedger(same, QJsonArray{ ledgerShot(1, 38.0, 4.0, -1.0), ledgerShot(2, 39.0, 5.0, -2.0), ledgerShot(3, 41.0, 6.0, -3.0) }));
        QVERIFY(writeLedger(other, QJsonArray{ ledgerShot(1, 30.0, 9.0, -4.0), ledgerShot(2, 31.0, 9.5, -5.0) }));
        QVERIFY(writeLedger(later, QJsonArray{ ledgerShot(1, 50.0, 12.0, 4.0) }));
        QVERIFY(writeLedger(cur, QJsonArray{}));

        auto snap = std::make_shared<SessionSnapshot>(*m_five);
        snap->sessionDir = cur;
        snap->revision = 9;
        ChartsPosturePresenter p;
        setUp(p, snap, 3);
        const QVariantMap t = p.thisShot();
        // forward bend (DTL): only the session on the same DTL camera, before this one — n 3 over one session
        const QVariantMap bend = addressRowOf(t, QStringLiteral("m_spineBendAtAddress"));
        QVERIFY(bend.value(QStringLiteral("hasHistory")).toBool());
        QCOMPARE(bend.value(QStringLiteral("historyN")).toInt(), 3);
        QCOMPARE(bend.value(QStringLiteral("historySessions")).toInt(), 1);
        QCOMPARE(bend.value(QStringLiteral("history")).toMap().value(QStringLiteral("med")).toDouble(), 39.0);
        QCOMPARE(bend.value(QStringLiteral("historyText")).toString(), QStringLiteral("your earlier sessions: median 39°, n 3 shots over 1 session"));
        QVERIFY(bend.value(QStringLiteral("historyWhy")).toString().isEmpty());
        // arm hang: a composite's part is a reading too
        QCOMPARE(addressRowOf(t, QStringLiteral("m_armHangAddress")).value(QStringLiteral("historyN")).toInt(), 3);
        // side lean (face-on): both earlier sessions share the face-on camera — n 5 over two; the later one never counts
        const QVariantMap tilt = addressRowOf(t, QStringLiteral("m_axisTiltAddress"));
        QCOMPARE(tilt.value(QStringLiteral("historyN")).toInt(), 5);
        QCOMPARE(tilt.value(QStringLiteral("historySessions")).toInt(), 2);
        // trail-knee bend: the earlier ledgers could not assess it (a 0 nobody measured): no history, said
        const QVariantMap knee = addressRowOf(t, QStringLiteral("m_kneeFlexAddress"));
        QVERIFY(!knee.value(QStringLiteral("hasHistory")).toBool());
        QVERIFY(!knee.value(QStringLiteral("historyWhy")).toString().isEmpty());
        QVERIFY(!knee.value(QStringLiteral("text")).toString().isEmpty() && knee.value(QStringLiteral("banded")).toBool());
        // the trust fold: what the set-up is keyed on, per camera, and the session on another camera left out
        const QStringList lines = t.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
        const auto line = [&](const QString &start) {
            for (const QString &l : lines) if (l.startsWith(start)) return l;
            return QString();
        };
        const QString dtl = line(QStringLiteral("Set-up not recorded (where the camera stood): your earlier sessions at address are all your down the line"));
        QVERIFY2(dtl.contains(QStringLiteral("1 session with 3 shots")), qPrintable(dtl));
        QVERIFY2(dtl.contains(QStringLiteral("1 session on another down the line camera is left out")), qPrintable(dtl));
        const QString fo = line(QStringLiteral("Set-up not recorded (where the camera stood): your earlier sessions at address are all your face-on"));
        QVERIFY2(fo.contains(QStringLiteral("2 sessions with 5 shots")), qPrintable(fo));

        // Session: the earlier sessions' box beside the session's
        const QVariantMap sb = addressRowOf(p.session(), QStringLiteral("m_spineBendAtAddress"));
        QCOMPARE(sb.value(QStringLiteral("historyN")).toInt(), 3);
        QCOMPARE(sb.value(QStringLiteral("boxN")).toInt(), 5);

        // the report: the same block, counting swings, through the page's block
        const QVariantMap rep = postureSessionReportPayload(*snap, -1, QStringLiteral("swings"));
        const QVariantMap rb = addressRowOf(rep, QStringLiteral("m_spineBendAtAddress"));
        QCOMPARE(rb.value(QStringLiteral("historyText")).toString(), QStringLiteral("your earlier sessions: median 39°, n 3 swings over 1 session"));
        const QVariantMap block = pinpoint::report::postureReportBlock(rep, {}).toMap();
        const QVariantMap ra = block.value(QStringLiteral("address")).toMap();
        QCOMPARE(ra.value(QStringLiteral("title")).toString(), QStringLiteral("At address"));
        QCOMPARE(ra.value(QStringLiteral("rows")).toList().size(), int(postureSharedEnv().absolutes.size()));
        QVERIFY(block.value(QStringLiteral("trust")).toStringList().contains(postureRigRule()));
        QVERIFY(std::none_of(lines.begin(), lines.end(), [](const QString &l) { return l.contains(QStringLiteral("data warning")); }));

        // A swing with a DATA WARNING counts in the history as it does in the session box: its rows were turned
        // NotAssessable after they were read (the corridor applied, the value kept) — read; a row never assessed
        // (no corridor, a 0 nobody measured) stays out. Said once in the trust fold.
        QJsonObject warned = ledgerShot(3, 41.0, 6.0, -3.0);
        warned.insert(QStringLiteral("dataWarning"), true);
        QJsonArray wrows;
        for (const QJsonValue &rv : warned.value(QStringLiteral("rows")).toArray()) {
            QJsonObject r = rv.toObject();
            if (r.value(QStringLiteral("state")).toString() == QLatin1String("clean")) {
                r.insert(QStringLiteral("state"), QStringLiteral("notAssessable"));
                r.insert(QStringLiteral("corridorLo"), -10.0);
                r.insert(QStringLiteral("corridorHi"), 60.0);
                r.insert(QStringLiteral("notAssessableReason"), QStringLiteral("the recording has a data issue"));
            }
            wrows.append(r);
        }
        warned.insert(QStringLiteral("rows"), wrows);
        QVERIFY(writeLedger(same, QJsonArray{ ledgerShot(1, 38.0, 4.0, -1.0), ledgerShot(2, 39.0, 5.0, -2.0), warned }));
        ChartsPosturePresenter w;
        setUp(w, snap, 3);
        const QVariantMap wt = w.thisShot();
        const QVariantMap wb = addressRowOf(wt, QStringLiteral("m_spineBendAtAddress"));
        QCOMPARE(wb.value(QStringLiteral("historyN")).toInt(), 3);
        QCOMPARE(wb.value(QStringLiteral("history")).toMap().value(QStringLiteral("med")).toDouble(), 39.0);
        QCOMPARE(addressRowOf(wt, QStringLiteral("m_axisTiltAddress")).value(QStringLiteral("historyN")).toInt(), 5);
        QVERIFY(!addressRowOf(wt, QStringLiteral("m_kneeFlexAddress")).value(QStringLiteral("hasHistory")).toBool());
        const QStringList wl = wt.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
        QVERIFY2(wl.contains(QStringLiteral("Your earlier sessions include 1 shot with a data warning (a recording flagged as possibly broken); "
                                            "it is counted, as this session's own are.")), qPrintable(wl.join(QLatin1Char('|'))));
    }

    // ── Nick alone: no exit at the top; trail_knee_straighten is a finding ────────────────────
    void nickFixture()
    {
        ChartsPosturePresenter p;
        setUp(p, m_alone, 2);
        const QVariantMap t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QVERIFY(t.value(QStringLiteral("graded")).toBool());
        QCOMPARE(t.value(QStringLiteral("pattern")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("shot"));
        // Nick holds his posture going back: no exit at P3 or the top. THIS swing's face-on head reads +4.7 cm
        // at P2 (measure_dump reads 4.708 there too; +0.5 at P3), so the one exit before P5 is that P2 head.
        // Its candidate is D3 (the head rule), but loss_of_posture_backswing was CLEAN on this shot, so the
        // chart does not name it: the W-D6 words, and no D3 among the departures.
        const QVariantList exits = t.value(QStringLiteral("exits")).toList();
        QVERIFY(!exits.isEmpty());
        QStringList early;
        for (const QVariant &e : exits) {
            const int pos = e.toMap().value(QStringLiteral("pos")).toInt();
            QVERIFY2(pos != 2 && pos != 3, qPrintable(QStringLiteral("an exit at the top: ") + e.toMap().value(QStringLiteral("words")).toString()));
            if (pos < 4) early << e.toMap().value(QStringLiteral("words")).toString();
        }
        QCOMPARE(early, QStringList{ QStringLiteral("P2 head +4.7") + QChar(0x00A0)
                                     + QStringLiteral("cm: outside the good-player band at P2, not far enough to be a fault") });
        QVERIFY(m_nick->diagnostics.clean.contains(QStringLiteral("loss_of_posture_backswing")));
        for (const QVariant &e : exits) {
            const QVariantMap em = e.toMap();
            if (em.value(QStringLiteral("pos")).toInt() != 1) continue;
            QCOMPARE(em.value(QStringLiteral("departure")).toString(), QStringLiteral("D3"));   // the candidate, kept for the hover
            QVERIFY(!em.value(QStringLiteral("named")).toBool());
        }
        QVERIFY(!t.value(QStringLiteral("headText")).toString().contains(QStringLiteral("Standing up going back")));
        bool anyLate = false;
        for (const QVariant &e : exits) anyLate = anyLate || e.toMap().value(QStringLiteral("pos")).toInt() >= 4;
        QVERIFY(anyLate);   // he leaves from P5 on: hips in, bend lost
        QStringList depIds;
        for (const QVariant &d : t.value(QStringLiteral("departures")).toList()) depIds << d.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(!depIds.contains(QStringLiteral("D3")), qPrintable(depIds.join(QLatin1Char(','))));
        QVERIFY2(depIds.contains(QStringLiteral("D1")), qPrintable(depIds.join(QLatin1Char(','))));   // early_extension fired

        const QVariantList findings = t.value(QStringLiteral("findings")).toList();
        QStringList ids;
        for (const QVariant &f : findings) ids << f.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(ids.contains(QStringLiteral("trail_knee_straighten")), qPrintable(ids.join(QLatin1Char(','))));
        QVERIFY(ids.contains(QStringLiteral("early_extension")));
        QVERIFY(ids.contains(QStringLiteral("loss_of_posture")));
        // nothing fired outside the posture section's conditions is listed (over the top fired too)
        QVERIFY(!ids.contains(QStringLiteral("over_the_top")));
        QVERIFY(!ids.contains(QStringLiteral("pelvis_thrust_backswing")));
        for (const QVariant &fv : findings) {
            const QVariantMap f = fv.toMap();
            QVERIFY(!f.value(QStringLiteral("name")).toString().isEmpty());
            QVERIFY(!f.value(QStringLiteral("tag")).toString().isEmpty());
            QCOMPARE(f.value(QStringLiteral("seekUs")).toLongLong(), p.seekUsFor(f.value(QStringLiteral("tag")).toString()));
            QCOMPARE(f.value(QStringLiteral("severity")).toString(), QStringLiteral("watching"));   // no session tiers here
            QVERIFY(!f.value(QStringLiteral("explanation")).toString().contains(QLatin1Char('`')));
        }
        const QVariantMap ee = findings.at(int(ids.indexOf(QStringLiteral("early_extension")))).toMap();
        QCOMPARE(ee.value(QStringLiteral("departure")).toString(), QStringLiteral("D1"));
        QCOMPARE(ee.value(QStringLiteral("tag")).toString(), QStringLiteral("P7"));
        QCOMPARE(ee.value(QStringLiteral("seekText")).toString(), QStringLiteral("Go to P7"));
        const QVariantMap tk = findings.at(int(ids.indexOf(QStringLiteral("trail_knee_straighten")))).toMap();
        QCOMPARE(tk.value(QStringLiteral("kind")).toString(), QStringLiteral("trailKnee"));
        QCOMPARE(tk.value(QStringLiteral("tag")).toString(), QStringLiteral("P4"));
        QVERIFY(!tk.value(QStringLiteral("valueText")).toString().isEmpty());   // degrees: the row's own unit
        // loss_of_posture: a Finding naming no departure, and its composite reading (a z) shows no figure
        const QVariantMap lp = findings.at(int(ids.indexOf(QStringLiteral("loss_of_posture")))).toMap();
        QCOMPARE(lp.value(QStringLiteral("kind")).toString(), QStringLiteral("finding"));
        QCOMPARE(lp.value(QStringLiteral("departure")).toString(), QString());
        QCOMPARE(lp.value(QStringLiteral("valueText")).toString(), QString());
        QVERIFY(lp.value(QStringLiteral("value")).isNull());
        for (const QVariant &d : t.value(QStringLiteral("departures")).toList())
            QVERIFY(!d.toMap().value(QStringLiteral("conditionIds")).toStringList().contains(QStringLiteral("loss_of_posture")));

        // Working well: golfer words of clean posture conditions inside their band, never an axis that fired
        const QVariantList well = t.value(QStringLiteral("workingWell")).toList();
        QVERIFY(!well.isEmpty());
        for (const QVariant &wv : well) {
            const QVariantMap w = wv.toMap();
            QCOMPARE(w.value(QStringLiteral("severityWord")).toString(), QStringLiteral("working"));
            QVERIFY(!w.value(QStringLiteral("name")).toString().isEmpty());
            QVERIFY(!m_nick->diagnostics.fired.contains(w.value(QStringLiteral("id")).toString()));
            QVERIFY(m_nick->diagnostics.clean.contains(w.value(QStringLiteral("id")).toString()));
            QVERIFY(std::fabs(m_nick->diagnostics.values.value(w.value(QStringLiteral("id")).toString()).z) <= 1.0);
            // its golferWell line and the position only: golferWhy describes the fault
            QCOMPARE(w.value(QStringLiteral("protect")).toString(), QString());
            QCOMPARE(w.value(QStringLiteral("explanation")).toString(), QString());
        }

        // Roots before symptoms: the session's work-on order leads the Findings and the head line
        auto ranked = std::make_shared<SessionSnapshot>(*m_alone);
        ranked->revision = 2;
        ranked->workOnOrder = { QStringLiteral("early_extension"), QStringLiteral("trail_knee_straighten"), QStringLiteral("loss_of_posture") };
        ChartsPosturePresenter q;
        setUp(q, ranked, 2);
        QStringList rids;
        for (const QVariant &f : q.thisShot().value(QStringLiteral("findings")).toList()) rids << f.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(rids.size() >= 3 && rids.mid(0, 3) == ranked->workOnOrder, qPrintable(rids.join(QLatin1Char(','))));
        const QString head = q.thisShot().value(QStringLiteral("headText")).toString();
        QVERIFY2(head.startsWith(QStringLiteral("Your hips moved")), qPrintable(head));   // D1, early extension's exit
        ranked = std::make_shared<SessionSnapshot>(*m_alone);
        ranked->revision = 3;
        ranked->workOnOrder = { QStringLiteral("loss_of_posture"), QStringLiteral("trail_knee_straighten"), QStringLiteral("early_extension") };
        q.setSnapshot(ranked);
        rids.clear();
        for (const QVariant &f : q.thisShot().value(QStringLiteral("findings")).toList()) rids << f.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(rids.mid(0, 3) == ranked->workOnOrder, qPrintable(rids.join(QLatin1Char(','))));
        QVERIFY(!t.value(QStringLiteral("headText")).toString().isEmpty());
        QVERIFY(t.value(QStringLiteral("patternLine")).toString().contains(QStringLiteral("trail knee ")));
    }

    // ── the picture strip (posture_chart_design.md §5.4, stage 5) ────────────────────────────────────────
    // The document's reading: both cameras, the persisted DTL ruler, the face-on inter-ear ruler, the marks.
    void pictureDocument()
    {
        const QJsonObject root = pictureRoot();
        QVERIFY2(!root.isEmpty(), "PP_POSTURE_PICTURE");
        const PictureDoc d = postureDocFrom(root, QStringLiteral("/fixture/picture/swing_0002"));
        QVERIFY2(d.ok, qPrintable(d.why));
        QVERIFY(d.dtl.present && d.faceOn.present);
        QCOMPARE(d.dtl.W, 976);  QCOMPARE(d.dtl.H, 1024);
        QCOMPARE(d.faceOn.W, 1008);
        QVERIFY(qAbs(d.dtlCmPerPx - 0.27167502042182085) < 1e-12);
        QCOMPARE(d.dtlToward, 1);
        QVERIFY2(d.headPxPerCm > 1.0 && d.headPxPerCm < 20.0, qPrintable(QString::number(d.headPxPerCm)));
        QCOMPARE(d.marksVersion, 4);
        QVERIFY(d.dtl.marksFound && d.faceOn.marksFound);
        // the head outline off the face-on mask (stage version 4): a circle about a head wide on the inter-ear ruler
        QVERIFY(d.faceOn.headFound);
        const double headCm = 2.0 * d.faceOn.headR * d.faceOn.H / d.headPxPerCm;
        QVERIFY2(headCm > 13.0 && headCm < 20.0, qPrintable(QString::number(headCm)));
        QVERIFY(d.headTicks);
        QVERIFY(d.dtl.impactButtFound);
        QVERIFY(d.dtl.cropPx.isValid() && d.dtl.cropPx.width() > 100 && QRect(0, 0, d.dtl.W, d.dtl.H).contains(d.dtl.cropPx));
        for (int k = 0; k < kPicturePositions; ++k) { QVERIFY(d.dtl.posUs[size_t(k)] > 0); QVERIFY(d.dtl.frameIndex[size_t(k)] >= 0); }
        QVERIFY(!d.dtl.addressKp.isEmpty() && !d.faceOn.addressKp.isEmpty());
        QVERIFY(d.dtl.hasBall);
    }

    // The px offsets ARE cm × (1 / cmPerPx) on the DTL ruler, cm × pxPerCm on the inter-ear one; the lines are frozen.
    void pictureOffsetsScaleWithTheRuler()
    {
        const PictureDoc d = postureDocFrom(pictureRoot(), QStringLiteral("/fixture/picture/swing_0002"));
        const QVariantMap pic = posturePicture(d, levelBands(), PictureOptions{});
        QVERIFY(pic.value(QStringLiteral("available")).toBool());
        const QVariantList groups = pic.value(QStringLiteral("groups")).toList();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.first().toMap().value(QStringLiteral("camera")).toString(), QStringLiteral("dtl"));   // DTL first
        for (const QVariant &g : groups) QCOMPARE(g.toMap().value(QStringLiteral("frames")).toList().size(), 3);
        QVERIFY(pic.value(QStringLiteral("why")).toStringList().isEmpty());

        // DTL impact: the strip in front of the address butt line, −1.5…+3.5 cm toward the ball (image-right)
        const QVariantMap di = frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P7"));
        QCOMPARE(di.value(QStringLiteral("width")).toInt(), d.dtl.cropPx.width());
        QCOMPARE(di.value(QStringLiteral("height")).toInt(), d.dtl.cropPx.height());
        const QVariantList dBand = marksOf(di, QStringLiteral("band"));
        QCOMPARE(dBand.size(), 1);
        const QVariantMap strip = dBand.first().toMap();
        const double buttPx = d.dtl.buttX * d.dtl.W - d.dtl.cropPx.x();
        QVERIFY(qAbs(strip.value(QStringLiteral("x")).toDouble() - (buttPx + (-1.5) / d.dtlCmPerPx)) < 1e-6);
        QVERIFY(qAbs(strip.value(QStringLiteral("w")).toDouble() - 5.0 / d.dtlCmPerPx) < 1e-6);   // 18.4 px on this ruler
        QCOMPARE(strip.value(QStringLiteral("hueKey")).toString(), QStringLiteral("pelvisThrust"));
        QCOMPARE(di.value(QStringLiteral("caption")).toString(), QStringLiteral("impact: good players' hips sit in the shaded strip"));
        // the golfer's own butt line at impact, in ink, and its travel said
        const QVariantList ink = marksOf(di, QStringLiteral("ink"));
        QCOMPARE(ink.size(), 1);
        QVERIFY(qAbs(ink.first().toMap().value(QStringLiteral("x")).toDouble() - (d.dtl.impactButtX * d.dtl.W - d.dtl.cropPx.x())) < 1e-6);
        const double cm = (d.dtl.impactButtX - d.dtl.buttX) * d.dtl.W * d.dtlCmPerPx;
        QVERIFY2(di.value(QStringLiteral("note")).toString().contains(postureButtTravelWords(cm)), qPrintable(di.value(QStringLiteral("note")).toString()));

        // face-on impact: the band hung 0.5…6.5 cm below the tick at the tracked head centre at address — not off the
        // head circle (the circle is the mask's drawing of the skull, headLift measures the head's centre)
        const QVariantMap fi = frameOf(pic, QStringLiteral("faceOn"), QStringLiteral("P7"));
        const QVariantList fBand = marksOf(fi, QStringLiteral("band"));
        QCOMPARE(fBand.size(), 2);   // the band and its anchor, the tick at your head at address
        QVariantMap hb, addrTick, impTick;
        for (const QVariant &m : fBand) (m.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("rect") ? hb : addrTick) = m.toMap();
        QCOMPARE(addrTick.value(QStringLiteral("label")).toString(), QStringLiteral("your head at address"));
        const QVariantList fInk = marksOf(fi, QStringLiteral("ink"));
        QCOMPARE(fInk.size(), 1);
        impTick = fInk.first().toMap();
        QCOMPARE(impTick.value(QStringLiteral("label")).toString(), QStringLiteral("your head at impact"));
        const auto tickY = [](const QVariantMap &m) { return m.value(QStringLiteral("pts")).toList().value(0).toList().value(1).toDouble(); };
        const double ay = tickY(addrTick), iy = tickY(impTick);
        QVERIFY(qAbs(ay - (d.headAddrPx.y() - d.faceOn.cropPx.y())) < 1e-6);
        QVERIFY(qAbs(hb.value(QStringLiteral("y")).toDouble() - (ay + 0.5 * d.headPxPerCm)) < 1e-6);
        QVERIFY(qAbs(hb.value(QStringLiteral("h")).toDouble() - 6.0 * d.headPxPerCm) < 1e-6);
        QCOMPARE(fi.value(QStringLiteral("caption")).toString(), QStringLiteral("impact: good players' heads sit in the shaded band"));
        // the circle is the mask's: its top is the crown, not the band's anchor
        QVariantMap head;
        for (const QVariant &m : marksOf(fi, QStringLiteral("coach"))) if (m.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("circle")) head = m.toMap();
        QVERIFY(qAbs(head.value(QStringLiteral("r")).toDouble() - d.faceOn.headR * d.faceOn.H) < 1e-6);
        // SELF-CONSISTENCY, the head: (address tick − impact tick) / pxPerCm is headLift P7 − P1 as the document's own
        // series reads at those positions through the chart's sampler (buildPhaseGrid) — the head-height cell at P7
        const pinpoint::analysis::SwingPhaseGrid g = pinpoint::analysis::buildPhaseGrid(pictureRoot().value(QStringLiteral("analysis")).toObject());
        const pinpoint::analysis::MetricPhaseGrid *lift = g.metric(QStringLiteral("headLift"));
        QVERIFY(lift && lift->at(pinpoint::analysis::Phase::Address) && lift->at(pinpoint::analysis::Phase::Impact));
        const double cellCm = lift->at(pinpoint::analysis::Phase::Impact)->value - lift->at(pinpoint::analysis::Phase::Address)->value;
        const double drawnCm = (ay - iy) / d.headPxPerCm;
        QVERIFY2(qAbs(drawnCm - cellCm) < 0.05, qPrintable(QStringLiteral("drawn %1 cm, the chart's cell %2 cm").arg(drawnCm).arg(cellCm)));
        QVERIFY2(fi.value(QStringLiteral("note")).toString().contains(QStringLiteral("Your head at impact (the white tick): ") + postureHeadRiseWords(drawnCm)),
                 qPrintable(fi.value(QStringLiteral("note")).toString()));
        // SELF-CONSISTENCY, the hips: the white butt line at impact against the address butt line, through the DTL ruler,
        // is hipDepthButt at P7 — the same reading (addressMarks.dtlPhases, Impact) the series is built from
        double depthP7 = std::numeric_limits<double>::quiet_NaN();
        for (const QJsonValue &mv : pictureRoot().value(QStringLiteral("analysis")).toObject().value(QStringLiteral("metrics")).toArray())
            if (mv.toObject().value(QStringLiteral("key")).toString() == QLatin1String("hipDepthButt"))
                for (const QJsonValue &ps : mv.toObject().value(QStringLiteral("phaseSamples")).toArray())
                    if (ps.toObject().value(QStringLiteral("phase")).toInt() == int(pinpoint::analysis::Phase::Impact)) depthP7 = ps.toObject().value(QStringLiteral("value")).toDouble();
        QVERIFY(std::isfinite(depthP7));
        const double inkPx = ink.first().toMap().value(QStringLiteral("x")).toDouble(), addrPx = buttPx;
        const double drawnDepth = d.dtlToward * (inkPx - addrPx) / (1.0 / d.dtlCmPerPx);
        QVERIFY2(qAbs(drawnDepth - depthP7) < 1e-6, qPrintable(QStringLiteral("drawn %1 cm, hipDepthButt P7 %2 cm").arg(drawnDepth).arg(depthP7)));

        // frozen: the address coach lines are the same on the top and impact frames; no band, no ink at the top (and
        // no back line of the top's own: it was held at the gate)
        const QVariantMap da = frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P1")), dt = frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P4"));
        QCOMPARE(marksOf(dt, QStringLiteral("coach")), marksOf(da, QStringLiteral("coach")));
        QCOMPARE(marksOf(di, QStringLiteral("coach")), marksOf(da, QStringLiteral("coach")));
        QVERIFY(marksOf(dt, QStringLiteral("band")).isEmpty() && marksOf(dt, QStringLiteral("ink")).isEmpty());
        const QStringList dl = labelsOf(da, QStringLiteral("coach"));
        QVERIFY2(dl.contains(QStringLiteral("butt")) && dl.contains(QStringLiteral("spine")) && dl.contains(QStringLiteral("trail leg")), qPrintable(dl.join(',')));
        QCOMPARE(dl.count(QStringLiteral("spine")), 1);
        // the spine: the trail hip joint (12, a right-hander — the trail leg's first point, so the two join) → the joint
        // neck, halfway from the shoulder midpoint to the ears (AddressMarksStage's neck seed); no tail, no extension
        // (Mark, 23 Oct 2026). The shoulder plane runs parallel to it.
        {
            QVariantList sp, lp, pp;
            for (const QVariant &m : marksOf(da, QStringLiteral("coach"))) {
                const QString l = m.toMap().value(QStringLiteral("label")).toString();
                if (l == QLatin1String("spine")) sp = m.toMap().value(QStringLiteral("pts")).toList();
                if (l == QLatin1String("trail leg")) lp = m.toMap().value(QStringLiteral("pts")).toList();
                if (l == QLatin1String("shoulder plane")) pp = m.toMap().value(QStringLiteral("pts")).toList();
            }
            QCOMPARE(sp.size(), 2);
            QVERIFY(!lp.isEmpty() && pp.size() == 2);
            QCOMPARE(sp.first(), lp.first());
            const QVector<float> &kp = d.dtl.addressKp;
            for (int j : { 3, 4, 5, 6, 12 }) QVERIFY2(kp[j * 3 + 2] > 0.3f, qPrintable(QString::number(j)));
            const auto k = [&](int j, int c) { return double(kp[j * 3 + c]); };
            const double W = d.dtl.W, H = d.dtl.H, ox = d.dtl.cropPx.x(), oy = d.dtl.cropPx.y();
            const double sx = 0.5 * (k(5, 0) + k(6, 0)), sy = 0.5 * (k(5, 1) + k(6, 1));
            const double ex = 0.5 * (k(3, 0) + k(4, 0)), ey = 0.5 * (k(3, 1) + k(4, 1));
            const QVariantList h = sp.first().toList(), n = sp.last().toList();
            QVERIFY(qAbs(h[0].toDouble() - (k(12, 0) * W - ox)) < 1e-6 && qAbs(h[1].toDouble() - (k(12, 1) * H - oy)) < 1e-6);
            QVERIFY2(qAbs(n[0].toDouble() - ((sx + 0.5 * (ex - sx)) * W - ox)) < 1e-6 && qAbs(n[1].toDouble() - ((sy + 0.5 * (ey - sy)) * H - oy)) < 1e-6,
                     qPrintable(QStringLiteral("neck %1, %2").arg(n[0].toDouble()).arg(n[1].toDouble())));
            const double dsx = n[0].toDouble() - h[0].toDouble(), dsy = n[1].toDouble() - h[1].toDouble();
            const QVariantList p0 = pp.first().toList(), p1 = pp.last().toList();
            const double dpx = p1[0].toDouble() - p0[0].toDouble(), dpy = p1[1].toDouble() - p0[1].toDouble();
            QVERIFY2(qAbs(dsx * dpy - dsy * dpx) < 1e-6 * std::hypot(dsx, dsy) * std::hypot(dpx, dpy), "the shoulder plane is parallel to the spine");
        }
        const QStringList fl = labelsOf(frameOf(pic, QStringLiteral("faceOn"), QStringLiteral("P1")), QStringLiteral("coach"));
        QVERIFY2(fl.contains(QStringLiteral("head")) && fl.count(QStringLiteral("hip edge")) == 2, qPrintable(fl.join(',')));
        for (const QVariant &m : marksOf(da, QStringLiteral("coach")))
            QVERIFY(QStringList({ "posture", "plane", "headHips" }).contains(m.toMap().value(QStringLiteral("set")).toString()));
        // each frame: its position's instant to seek to, and what to make its still from
        QCOMPARE(da.value(QStringLiteral("seekUs")).toLongLong(), qlonglong(d.dtl.posUs[0]));
        const QVariantMap still = di.value(QStringLiteral("still")).toMap();
        QCOMPARE(still.value(QStringLiteral("phase")).toString(), QStringLiteral("P7"));
        QCOMPARE(still.value(QStringLiteral("w")).toInt(), d.dtl.cropPx.width());
        QVERIFY(still.value(QStringLiteral("outPath")).toString().isEmpty());   // no stillDir: the caller makes it
        QVERIFY(!pic.value(QStringLiteral("legend")).toString().isEmpty());

        // a ruler half the size halves the strip
        PictureDoc d2 = d;
        d2.dtlCmPerPx = 2 * d.dtlCmPerPx;
        const QVariantMap s2 = marksOf(frameOf(posturePicture(d2, levelBands(), PictureOptions{}), QStringLiteral("dtl"), QStringLiteral("P7")),
                                       QStringLiteral("band")).first().toMap();
        QVERIFY(qAbs(s2.value(QStringLiteral("w")).toDouble() - 0.5 * strip.value(QStringLiteral("w")).toDouble()) < 1e-6);
    }

    // No DTL ruler → no strip, and the frame says why; no head ruler → no head band, said.
    void pictureWithoutARuler()
    {
        QJsonObject root = pictureRoot();
        QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
        an.insert(QStringLiteral("dtlRuler"), QJsonObject{ { QStringLiteral("schema"), QStringLiteral("pinpoint.dtlRuler/1") },
                                                           { QStringLiteral("source"), QStringLiteral("none") },
                                                           { QStringLiteral("cmPerPx"), QJsonValue() } });
        root.insert(QStringLiteral("analysis"), an);
        PictureDoc d = postureDocFrom(root, QStringLiteral("/fixture/picture/swing_0002"));
        QVERIFY(!std::isfinite(d.dtlCmPerPx));
        d.headPxPerCm = std::numeric_limits<double>::quiet_NaN();
        d.headRulerWhy = QStringLiteral("the ears were not seen at address, so the head has no ruler");
        const QVariantMap pic = posturePicture(d, levelBands(), PictureOptions{});
        const QVariantMap di = frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P7"));
        QVERIFY(marksOf(di, QStringLiteral("band")).isEmpty());
        QCOMPARE(di.value(QStringLiteral("caption")).toString(), QStringLiteral("impact"));
        QVERIFY2(di.value(QStringLiteral("note")).toString().contains(QStringLiteral("No strip: no ruler \u2014 no ball to measure with in either view")),
                 qPrintable(di.value(QStringLiteral("note")).toString()));
        QCOMPARE(marksOf(di, QStringLiteral("ink")).size(), 1);   // the butt line at impact still drawn, its travel not said in cm
        QVERIFY(!di.value(QStringLiteral("note")).toString().contains(QStringLiteral(" cm ")));
        const QVariantMap fi = frameOf(pic, QStringLiteral("faceOn"), QStringLiteral("P7"));
        QVERIFY(marksOf(fi, QStringLiteral("band")).isEmpty());
        QVERIFY(fi.value(QStringLiteral("note")).toString().startsWith(QStringLiteral("No band: the ears")));
    }

    // A swing analysed before the address marks: the frames, with the lines the pose and club give (the spine among them:
    // it is the joints' since 23 Oct), and why the rest are absent.
    void pictureWithoutAddressMarks()
    {
        QJsonObject root = pictureRoot();
        QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
        an.remove(QStringLiteral("addressMarks"));
        root.insert(QStringLiteral("analysis"), an);
        const PictureDoc d = postureDocFrom(root, QStringLiteral("/fixture/picture/swing_0002"));
        QCOMPARE(d.marksVersion, 0);
        const QVariantMap pic = posturePicture(d, levelBands(), PictureOptions{});
        QVERIFY(pic.value(QStringLiteral("available")).toBool());
        QCOMPARE(pic.value(QStringLiteral("groups")).toList().size(), 2);
        const QStringList why = pic.value(QStringLiteral("why")).toStringList();
        QVERIFY2(!why.isEmpty() && why.first().startsWith(QStringLiteral("This swing was analysed before the address marks were read")), qPrintable(why.join('|')));
        const QStringList dl = labelsOf(frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P1")), QStringLiteral("coach"));
        QVERIFY2(!dl.contains(QStringLiteral("butt")) && dl.contains(QStringLiteral("spine")) && dl.contains(QStringLiteral("shoulder plane"))
                 && dl.contains(QStringLiteral("trail leg")), qPrintable(dl.join(',')));
        const QStringList fl = labelsOf(frameOf(pic, QStringLiteral("faceOn"), QStringLiteral("P1")), QStringLiteral("coach"));
        QVERIFY2(fl == QStringList{ QStringLiteral("head") }, qPrintable(fl.join(',')));
        const QVariantMap di = frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P7"));
        QVERIFY(marksOf(di, QStringLiteral("band")).isEmpty() && marksOf(di, QStringLiteral("ink")).isEmpty());   // no butt line to offset
        QCOMPARE(marksOf(frameOf(pic, QStringLiteral("faceOn"), QStringLiteral("P7")), QStringLiteral("band")).size(), 2);   // the head needs no marks: band + tick
    }

    // No face-on camera: the down-the-line frames only, said.
    void pictureDtlOnly()
    {
        QJsonObject root = pictureRoot();
        QJsonArray streams;
        for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray())
            if (!v.toObject().value(QStringLiteral("alias")).toString().contains(QStringLiteral("Face"))) streams.append(v);
        root.insert(QStringLiteral("streams"), streams);
        const PictureDoc d = postureDocFrom(root, QStringLiteral("/fixture/picture/swing_0002"));
        QVERIFY(d.dtl.present);
        QVERIFY(!d.faceOn.present);
        const QVariantMap pic = posturePicture(d, levelBands(), PictureOptions{});
        const QVariantList groups = pic.value(QStringLiteral("groups")).toList();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups.first().toMap().value(QStringLiteral("camera")).toString(), QStringLiteral("dtl"));
        QVERIFY(pic.value(QStringLiteral("why")).toStringList().contains(QStringLiteral("No face-on camera on this swing: the down-the-line frames only.")));
        // an unreadable document: no frames, said
        const QVariantMap none = posturePicture(postureDocFrom(QJsonObject{}, QStringLiteral("/x")), levelBands(), PictureOptions{});
        QVERIFY(!none.value(QStringLiteral("available")).toBool());
        QVERIFY(!none.value(QStringLiteral("why")).toStringList().isEmpty());
    }

    // The presenter: thisShot.picture with the pattern's bands, the still maker fills `src`, a frame seeks to its position.
    void presenterPicture()
    {
        // the stills cache starts empty (test mode: ~/.qttest), so "not made yet" and "made once" are this run's
        QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("posture-picture"))).removeRecursively();
        ChartsPosturePresenter p;
        setUp(p, m_five, 3);
        p.setPictureDocumentForTest(dirOf(3), qEnvironmentVariable("PP_POSTURE_PICTURE"));
        const QVariantMap t = p.thisShot();
        const QVariantMap pic = t.value(QStringLiteral("picture")).toMap();
        QVERIFY(pic.value(QStringLiteral("available")).toBool());
        const QVariantMap hips7 = cellOf(t, QStringLiteral("hips"), 6);
        const PictureDoc d = postureDocFrom(pictureRoot(), dirOf(3));
        const QVariantMap strip = marksOf(frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P7")), QStringLiteral("band")).value(0).toMap();
        QVERIFY(!strip.isEmpty());
        QVERIFY(qAbs(strip.value(QStringLiteral("w")).toDouble()
                     - (hips7.value(QStringLiteral("bandHi")).toDouble() - hips7.value(QStringLiteral("bandLo")).toDouble()) / d.dtlCmPerPx) < 1e-6);
        // the head ticks against the chart's own head-height cell at P7 (headLift P7 − P1, the measure engine's sampler)
        {
            const QVariantMap fi = frameOf(pic, QStringLiteral("faceOn"), QStringLiteral("P7"));
            double ay = std::numeric_limits<double>::quiet_NaN(), iy = ay;
            for (const QVariant &m : fi.value(QStringLiteral("marks")).toList()) {
                const QVariantMap mm = m.toMap();
                const double y = mm.value(QStringLiteral("pts")).toList().value(0).toList().value(1).toDouble();
                if (mm.value(QStringLiteral("label")).toString() == QLatin1String("your head at address")) ay = y;
                if (mm.value(QStringLiteral("label")).toString() == QLatin1String("your head at impact")) iy = y;
            }
            const QVariantMap head7 = cellOf(t, QStringLiteral("head"), 6);
            QVERIFY(head7.value(QStringLiteral("available")).toBool());
            const double drawn = (ay - iy) / d.headPxPerCm, cell = head7.value(QStringLiteral("value")).toDouble();
            QVERIFY2(qAbs(drawn - cell) < 0.05, qPrintable(QStringLiteral("drawn %1 cm, the chart's P7 cell %2 cm").arg(drawn).arg(cell)));
        }
        QVERIFY(pic.value(QStringLiteral("pending")).toBool());   // no maker: nothing made yet
        QVERIFY(frameOf(pic, QStringLiteral("dtl"), QStringLiteral("P1")).value(QStringLiteral("src")).toString().isEmpty());

        // a maker (the test's: a grey JPEG the crop's size) makes them once; the picture comes back with `src`
        int made = 0;
        ChartsPosturePresenter::setStillMaker([&made](const QVariantList &stills) {
            QHash<QString, QString> errs;
            for (const QVariant &v : stills) {
                const QVariantMap s = v.toMap();
                QImage img(qMax(8, s.value(QStringLiteral("w")).toInt() / 4), qMax(8, s.value(QStringLiteral("h")).toInt() / 4), QImage::Format_RGB32);
                img.fill(Qt::gray);
                QDir().mkpath(QFileInfo(s.value(QStringLiteral("outPath")).toString()).absolutePath());
                if (img.save(s.value(QStringLiteral("outPath")).toString(), "JPG")) ++made;
                else errs.insert(s.value(QStringLiteral("outPath")).toString(), QStringLiteral("could not write"));
            }
            return errs;
        });
        ChartsPosturePresenter q;
        setUp(q, m_five, 3);
        q.setPictureDocumentForTest(dirOf(3), qEnvironmentVariable("PP_POSTURE_PICTURE"));
        const QVariantMap pq = q.thisShot().value(QStringLiteral("picture")).toMap();
        ChartsPosturePresenter::setStillMaker({});
        QCOMPARE(made, 6);
        QVERIFY(!pq.value(QStringLiteral("pending")).toBool());
        for (const QVariant &g : pq.value(QStringLiteral("groups")).toList())
            for (const QVariant &fv : g.toMap().value(QStringLiteral("frames")).toList())
                QVERIFY(QFileInfo::exists(QUrl(fv.toMap().value(QStringLiteral("src")).toString()).toLocalFile()));
        const QVariantMap f = frameOf(pq, QStringLiteral("faceOn"), QStringLiteral("P4"));
        QVERIFY(QFileInfo::exists(QUrl(f.value(QStringLiteral("src")).toString()).toLocalFile()));
        QCOMPARE(f.value(QStringLiteral("seekUs")).toLongLong(), q.seekUsFor(QStringLiteral("P4")));

        // a shot whose document is not there: no frames, said
        p.setPickedSwingDir(dirOf(2));
        const QVariantMap p2 = p.thisShot().value(QStringLiteral("picture")).toMap();
        QVERIFY(!p2.value(QStringLiteral("available")).toBool());
        QVERIFY(p2.value(QStringLiteral("why")).toStringList().value(0).contains(QStringLiteral("not on disk")));

        // the report: the typical swing's picture (here not on disk) cut to the page, null with its reasons
        const QVariantMap rep = postureSessionReportPayload(*m_five, -1, QStringLiteral("swings"));
        const QVariantMap rp = rep.value(QStringLiteral("picture")).toMap();
        QVERIFY(rp.contains(QStringLiteral("why")));
        const QVariantMap block = pinpoint::report::postureReportBlock(rep, {}).toMap();
        QVERIFY(block.value(QStringLiteral("picture")).isNull());
        QVERIFY(!block.value(QStringLiteral("pictureWhy")).toStringList().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ChartsPosturePresenterTest)
#include "charts_posture_presenter_test.moc"
