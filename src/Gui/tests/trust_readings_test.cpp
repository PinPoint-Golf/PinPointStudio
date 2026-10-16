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

// The readings a gate touched (src/Gui/trust/trust_readings.h) — what the Trust panel, the in-place
// block and the Charts marks all read (trust_and_gates_design.md §4.3, §7.0). The fixture is 18 August
// Wrist_02 shot 4's own records, as analysis.gates stores them: a face-on pelvis bounded by the sighted
// band AND over the σ ceiling, a chest bounded by the band, a camera club drawn but not judged.

#include "../trust/trust_readings.h"
#include "../charts/session_series_types.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <memory>

using namespace pinpoint;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static QVariantList json(const char *s) { return QJsonDocument::fromJson(QByteArray(s)).array().toVariantList(); }

static charts::ShotSeries shot4()
{
    charts::ShotSeries sh;
    sh.ordinal = 4;
    sh.gatesVersion = 1;
    sh.kinematicSequence = QVariantMap{
        { QStringLiteral("impactUs"), 2523267 },
        { QStringLiteral("order"), QVariantList{ QStringLiteral("leadArm"), QStringLiteral("club") } },
        { QStringLiteral("nodes"), json(R"([
            {"segment":"pelvis","placed":false,"tPeakUs":2568000,"beforeImpactMs":-45,"tSigmaMs":46.6,"peakDps":782,"quality":"estimated","peakNoEarlierThanMs":129.109},
            {"segment":"thorax","placed":false,"tPeakUs":2635337,"beforeImpactMs":-112,"tSigmaMs":22.1,"peakDps":524,"quality":"estimated","peakNoEarlierThanMs":55.374},
            {"segment":"leadArm","placed":true,"tPeakUs":2420000,"beforeImpactMs":103,"tSigmaMs":3.0,"peakDps":780,"quality":"direct"},
            {"segment":"club","placed":true,"tPeakUs":2504000,"beforeImpactMs":19,"tSigmaMs":9.3,"peakDps":1968,"quality":"estimated"}])") } };
    sh.gates = json(R"([
        {"gate":"sequence.sightedBand","subject":"ks.pelvis","effect":"bounded","bound":{"noEarlierThanMs":129.109},
         "evidence":{"edge":"entry","route":"faceOn","sightBackMs":-35.238,"sightLostMs":-129.109,"sightedTurnDeg":20}},
        {"gate":"sequence.maxPlaceSigma","subject":"ks.pelvis","effect":"refused","evidence":{"route":"faceOn","maxPlaceSigmaMs":40,"tSigmaMs":46.58}},
        {"gate":"sequence.sightedBand","subject":"ks.thorax","effect":"bounded","bound":{"noEarlierThanMs":55.374},
         "evidence":{"edge":"entry","route":"faceOn","sightBackMs":45.034,"sightLostMs":-55.374,"sightedTurnDeg":20}},
        {"gate":"sequence.clubWithheld","subject":"ks.club","effect":"withheld","evidence":{"route":"faceOnClub","peakMs":-19.153}}])");
    return sh;
}

static QVariantMap readingOf(const QVariantList &rs, const char *subject)
{
    for (const QVariant &r : rs) if (r.toMap().value(QStringLiteral("subject")).toString() == QLatin1String(subject)) return r.toMap();
    return {};
}

int main()
{
    std::printf("trust_readings_test\n");
    const charts::ShotSeries sh = shot4();
    const QVariantList rs = trust::trustReadings(sh);

    // ── one reading per subject a record names, in segment order; the lead arm (no record) is absent ──
    check(rs.size() == 3, "three readings: pelvis, chest, club (the lead arm has no record)");
    check(rs.size() == 3 && rs[0].toMap().value(QStringLiteral("subject")) == QStringLiteral("ks.pelvis")
              && rs[2].toMap().value(QStringLiteral("subject")) == QStringLiteral("ks.club"), "in segment order");

    // ── levels (§4.3) and the figure the claim shows ──
    const QVariantMap pel = readingOf(rs, "ks.pelvis"), che = readingOf(rs, "ks.thorax"), clu = readingOf(rs, "ks.club");
    check(pel.value(QStringLiteral("level")) == QStringLiteral("bounded") && pel.value(QStringLiteral("levelWord")) == QStringLiteral("Bounded"),
          "pelvis: Bounded");
    check(pel.value(QStringLiteral("figure")).toString() == QStringLiteral("after ") + QChar(0x2212) + QStringLiteral("129") + QChar(0x00A0) + QStringLiteral("ms"),
          "pelvis figure: after −129 ms");
    check(pel.value(QStringLiteral("shortText")) == QStringLiteral("out of sight"), "pelvis short words: out of sight");
    check(che.value(QStringLiteral("level")) == QStringLiteral("bounded"), "chest: Bounded");
    check(clu.value(QStringLiteral("level")) == QStringLiteral("notJudged") && clu.value(QStringLiteral("levelWord")) == QStringLiteral("Drawn, not judged"),
          "club: Drawn, not judged (placed, withheld)");

    // ── the gates behind a reading: the one the claim shows first, both kept (§3.3) ──
    const QVariantList pg = pel.value(QStringLiteral("gates")).toList();
    check(pg.size() == 2, "pelvis carries both gates that fired");
    check(pg.size() == 2 && pg[0].toMap().value(QStringLiteral("gate")) == QStringLiteral("sequence.sightedBand")
              && pg[0].toMap().value(QStringLiteral("primary")).toBool()
              && pg[1].toMap().value(QStringLiteral("gate")) == QStringLiteral("sequence.maxPlaceSigma"),
          "the band (it carries the bound) comes first and is the primary; the σ ceiling second");
    check(!pg.isEmpty() && pg[0].toMap().value(QStringLiteral("evidence")).toString().startsWith(QStringLiteral("Out of sight from ")),
          "evidence is the catalogue's sentence, filled");
    check(!pg.isEmpty() && pg[0].toMap().value(QStringLiteral("lift")) == QStringLiteral("Timed on swings with the down-the-line camera or a pelvis sensor."),
          "lift for a face-on-only pelvis names the second camera and the pelvis sensor");
    check(!pg.isEmpty() && pg[0].toMap().value(QStringLiteral("tuning")) == QStringLiteral("Threshold 20°"), "threshold with its value");

    // ── a route the swing already ran is not offered as the lift ──
    {
        charts::ShotSeries two = sh;
        two.gates << json(R"([{"gate":"sequence.routeWithdrawn","subject":"ks.pelvis","effect":"downgraded",
                                "evidence":{"route":"faceOn+dtl","switch":"sequence.pairTrunk.thoraxPlacement"}}])")[0];
        const QVariantList g = readingOf(trust::trustReadings(two), "ks.pelvis").value(QStringLiteral("gates")).toList();
        check(!g.isEmpty() && g[0].toMap().value(QStringLiteral("lift")) == QStringLiteral("Timed on swings with a pelvis sensor."),
              "a two-camera swing is not told to add the down-the-line camera");
        bool sw = false;
        for (const QVariant &x : g)
            if (x.toMap().value(QStringLiteral("gate")) == QStringLiteral("sequence.routeWithdrawn")
                && x.toMap().value(QStringLiteral("evidence")).toString().contains(QStringLiteral("sequence.pairTrunk.thoraxPlacement")))
                sw = true;
        check(sw, "a withdrawal says which switch stopped the route");
    }

    // ── a band that only masked samples: the reading keeps its route level, the gate says "shaded" ──
    {
        charts::ShotSeries m = sh;
        m.gates = json(R"([{"gate":"sequence.sightedBand","subject":"ks.pelvis","effect":"masked",
                            "evidence":{"route":"faceOn","sightLostMs":-60,"sightBackMs":-20,"sightedTurnDeg":20}}])");
        QVariantMap ks = m.kinematicSequence;
        QVariantList nodes = ks.value(QStringLiteral("nodes")).toList();
        QVariantMap p0 = nodes[0].toMap(); p0.insert(QStringLiteral("placed"), true); p0.remove(QStringLiteral("peakNoEarlierThanMs"));
        nodes[0] = p0; ks.insert(QStringLiteral("nodes"), nodes); m.kinematicSequence = ks;
        const QVariantMap r = readingOf(trust::trustReadings(m), "ks.pelvis");
        check(r.value(QStringLiteral("level")) == QStringLiteral("estimated"), "masked only: the reading stays Estimated (face-on route)");
        const QVariantList g = r.value(QStringLiteral("gates")).toList();
        check(g.size() == 1 && g[0].toMap().value(QStringLiteral("effectWord")) == QStringLiteral("marked on the chart"), "…and the gate says it is marked on the chart");
    }

    // ── the shot's head sentence, and a shot analysed before records existed ──
    {
        const QVariantMap t = trust::trustShot(sh);
        check(t.value(QStringLiteral("head")).toString()
                  == QStringLiteral("Not timed: pelvis peak (out of sight) and chest peak (out of sight). Drawn, not judged: club peak."),
              "head: what this shot could not measure");
        charts::ShotSeries old = sh; old.gatesVersion = 0;
        const QVariantMap o = trust::trustShot(old);
        check(!o.value(QStringLiteral("recorded")).toBool() && o.value(QStringLiteral("readings")).toList().isEmpty()
                  && o.value(QStringLiteral("head")).toString().contains(QStringLiteral("before the reasons were recorded")),
              "an old shot says its reasons were not recorded, and lists nothing");
    }

    // ── the session: each gate's count, per reading, over the shots that carry records ──
    {
        charts::SessionSnapshot s;
        for (int i = 0; i < 3; ++i) s.shots.push_back(std::make_shared<charts::ShotSeries>(sh));
        charts::ShotSeries old = sh; old.gatesVersion = 0;
        s.shots.push_back(std::make_shared<charts::ShotSeries>(old));
        const QVariantMap t = trust::trustSession(s);
        check(t.value(QStringLiteral("n")).toInt() == 4 && t.value(QStringLiteral("recorded")).toInt() == 3,
              "four shots, three with records (the old one is not counted against)");
        bool band = false;
        for (const QVariant &gv : t.value(QStringLiteral("gates")).toList()) {
            const QVariantMap g = gv.toMap();
            if (g.value(QStringLiteral("gate")) != QStringLiteral("sequence.sightedBand")) continue;
            const QVariantList subs = g.value(QStringLiteral("subjects")).toList();
            band = g.value(QStringLiteral("countText")) == QStringLiteral("3 of 3") && subs.size() == 2
                && subs[0].toMap().value(QStringLiteral("label")) == QStringLiteral("Pelvis peak")
                && subs[0].toMap().value(QStringLiteral("shots")).toInt() == 3;
        }
        check(band, "the sighted band: 3 of 3, pelvis and chest each on 3");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
