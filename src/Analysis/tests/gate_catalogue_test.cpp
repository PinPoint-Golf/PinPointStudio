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

// The gate catalogue (Metrics/gate_catalogue.h) and the record format (Analysis/gate_record.h) —
// docs/design/trust_and_gates_design.md §5, §6, §10 item 6.
//
// BOTH WAYS. Every id a producer fires — spelled PP_GATE("…") at the firing site — is in the
// catalogue, and every catalogue entry is fired by the producer it names. The producer's source is
// read from PP_SRC_ROOT, so a gate added without its entry (or an entry left behind when its gate is
// removed) fails here rather than reaching the reader as a bare id.

#include "../../Metrics/gate_catalogue.h"
#include "../gate_record.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static QString readSource(const QString &rel)
{
    QFile f(QStringLiteral(PP_SRC_ROOT) + QLatin1Char('/') + rel);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

int main()
{
    std::printf("gate_catalogue_test\n");
    const auto &cat = gates::gateCatalogue();

    // ── the catalogue is well formed ──────────────────────────────────────────────────────────
    {
        QSet<QString> ids;
        bool unique = true, complete = true, dotted = true;
        for (const gates::GateEntry &e : cat) {
            if (ids.contains(e.id)) unique = false;
            ids.insert(e.id);
            if (e.area.isEmpty() || e.title.isEmpty() || e.shortText.isEmpty() || e.explain.isEmpty()
                || e.evidenceText.isEmpty() || e.effects.empty() || e.design.isEmpty() || e.producer.isEmpty())
                complete = false;
            if (!e.id.contains(QLatin1Char('.'))) dotted = false;
        }
        check(!cat.empty(), "the catalogue has entries");
        check(unique, "ids are unique");
        check(complete, "every entry has area, title, short text, explanation, evidence text, effects, design, producer");
        check(dotted, "ids are area.name");
        check(gates::gateEntry(QStringLiteral("sequence.sightedBand")) != nullptr, "lookup by id");
        check(gates::gateEntry(QStringLiteral("no.such")) == nullptr, "an unknown id is null");
    }

    // ── both ways against the producers' sources ──────────────────────────────────────────────
    {
        static const QRegularExpression re(QStringLiteral("PP_GATE\\(\"([^\"]+)\"\\)"));
        QSet<QString> producers;
        for (const gates::GateEntry &e : cat) producers.insert(e.producer);
        QSet<QString> fired;
        bool readAll = true;
        for (const QString &p : producers) {
            const QString src = readSource(p);
            if (src.isEmpty()) { readAll = false; std::printf("    cannot read %s\n", qPrintable(p)); continue; }
            auto it = re.globalMatch(src);
            while (it.hasNext()) fired.insert(it.next().captured(1));
            for (const gates::GateEntry &e : cat)
                if (e.producer == p && !src.contains(QStringLiteral("PP_GATE(\"%1\")").arg(e.id))) {
                    std::printf("    entry %s is never fired by %s\n", qPrintable(e.id), qPrintable(p));
                    readAll = false;
                }
        }
        bool allCatalogued = true;
        for (const QString &id : fired)
            if (!gates::gateEntry(id)) { allCatalogued = false; std::printf("    fired id %s is not catalogued\n", qPrintable(id)); }
        check(readAll, "every catalogue entry is fired by the producer it names (no dead entries)");
        check(allCatalogued, "every id fired in a producer source is in the catalogue");
        check(fired.size() >= 11, "the kinematic sequence fires its eleven gates");
    }

    // ── evidence sentences ────────────────────────────────────────────────────────────────────
    {
        const gates::GateEntry *band = gates::gateEntry(QStringLiteral("sequence.sightedBand"));
        const QString minus = QString(QChar(0x2212)), nb = QString(QChar(0x00A0));
        const QString a = gates::evidenceText(*band, { { QStringLiteral("sightLostMs"), -129.0 },
                                                       { QStringLiteral("sightBackMs"), 44.6 },
                                                       { QStringLiteral("rateAtEdge"), QStringLiteral("rising") } });
        check(a == QStringLiteral("Out of sight from ") + minus + QStringLiteral("129") + nb + QStringLiteral("ms to +45") + nb
                       + QStringLiteral("ms; the rate was still rising as sight was lost."),
              "the sighted band's sentence, every optional part present");
        const QString b = gates::evidenceText(*band, { { QStringLiteral("sightLostMs"), -55.0 } });
        check(b == QStringLiteral("Out of sight from ") + minus + QStringLiteral("55") + nb + QStringLiteral("ms."),
              "optional parts drop when their key is missing");
        const gates::GateEntry *pair = gates::gateEntry(QStringLiteral("sequence.pairRoute"));
        const QString c = gates::evidenceText(*pair, { { QStringLiteral("code"), QStringLiteral("pairedInDomain") },
                                                       { QStringLiteral("paired"), 3 }, { QStringLiteral("minPaired"), 5 } });
        check(c == QStringLiteral("Too few frames pair across the two views in the downswing (3; 5 needed)."),
              "a coded refusal reads its phrase, filled from the same evidence");
        const gates::GateEntry *sw = gates::gateEntry(QStringLiteral("sequence.routePlacement"));
        check(gates::evidenceText(*sw, { { QStringLiteral("route"), QStringLiteral("faceOn") } })
                  == QStringLiteral("Timing switched off for the face-on camera route."),
              "a route id reads as words");
        check(gates::liftText(*band, QStringLiteral("ks.pelvis"))
                  == QStringLiteral("Timed on swings with the down-the-line camera or a pelvis sensor."),
              "lift text for the pelvis names the second camera and the pelvis sensor, not the chest sensor");
        check(gates::liftText(*gates::gateEntry(QStringLiteral("sequence.risingAtEnd"))).isEmpty(),
              "a gate no route lifts says nothing");
        bool tuned = false;
        for (const gates::GateTuning &t : band->tuning)
            if (t.key == QLatin1String("sequence.sightedTurnDeg") && t.value == tuned::sequence::kSightedTurnDeg) tuned = true;
        check(tuned, "tuning carries the compiled threshold (20° today)");
    }

    // ── what a finding a gate stopped says (stage 4, §4.4) ──
    {
        check(gates::subjectForMetricKey(QStringLiteral("pelvisPeakTime")) == QLatin1String("ks.pelvis")
                  && gates::subjectForMetricKey(QStringLiteral("clubPeakSpeed")) == QLatin1String("ks.club")
                  && gates::subjectForMetricKey(QStringLiteral("thoraxReversalTime")) == QLatin1String("ks.thorax")
                  && gates::subjectForMetricKey(QStringLiteral("pelvisSway")).isEmpty(),
              "a sequence node's metrics map to its subject, and nothing else does");
        std::vector<GateRecord> recs{
            { PP_GATE("sequence.maxPlaceSigma"), QStringLiteral("ks.pelvis"), GateEffect::Refused, {}, {}, {} },
            { PP_GATE("sequence.sightedBand"), QStringLiteral("ks.pelvis"), GateEffect::Bounded, {}, { { QStringLiteral("noEarlierThanMs"), 129.0 } }, {} },
            { PP_GATE("sequence.sightedBand"), QStringLiteral("ks.thorax"), GateEffect::Masked, {}, {}, {} },
            { PP_GATE("sequence.clubWithheld"), QStringLiteral("ks.club"), GateEffect::Withheld, {}, {}, {} } };
        const GateRecord *p = gates::primaryRecord(recs, QStringLiteral("ks.pelvis"));
        check(p && p->gate == QLatin1String("sequence.sightedBand"), "the bound outranks the σ ceiling as the reason shown");
        check(gates::primaryRecord(recs, QStringLiteral("ks.thorax")) == nullptr, "a band that only masked refuses nothing");
        check(gates::notJudgedClause(QStringLiteral("ks.pelvis"), QStringLiteral("sequence.sightedBand")) == QStringLiteral("pelvis peak out of sight"),
              "the not-judged clause: pelvis peak out of sight");
        check(gates::notJudgedClause(QStringLiteral("ks.club"), QStringLiteral("sequence.clubWithheld")) == QStringLiteral("club peak drawn, not judged"),
              "…and a withheld reading: club peak drawn, not judged");
    }

    // ── the record format round-trips ─────────────────────────────────────────────────────────
    {
        GateLog log;
        log.fire(PP_GATE("sequence.sightedBand"), QStringLiteral("ks.pelvis"), GateEffect::Bounded,
                 { { QStringLiteral("sightLostMs"), -129.0 }, { QStringLiteral("route"), QStringLiteral("faceOn") } },
                 { { QStringLiteral("noEarlierThanMs"), 129.0 } });
        log.fire(PP_GATE("sequence.maxPlaceSigma"), QStringLiteral("ks.pelvis"), GateEffect::Refused,
                 { { QStringLiteral("tSigmaMs"), 47.0 } });
        const QJsonArray j = gateRecordsToJson(log.records());
        check(j.size() == 2, "two records serialise");
        check(!j.at(1).toObject().contains(QStringLiteral("bound")), "an empty bound is not written");
        const std::vector<GateRecord> back = gateRecordsFromJson(j);
        check(back.size() == 2 && back[0].gate == QLatin1String("sequence.sightedBand")
                  && back[0].effect == GateEffect::Bounded
                  && back[0].bound.value(QStringLiteral("noEarlierThanMs")).toDouble() == 129.0
                  && back[0].evidence.value(QStringLiteral("sightLostMs")).toDouble() == -129.0
                  && back[1].effect == GateEffect::Refused,
              "records round-trip through JSON");
        QJsonArray bad = j;
        bad.append(QJsonObject{ { QStringLiteral("gate"), QStringLiteral("x.y") }, { QStringLiteral("effect"), QStringLiteral("nonsense") } });
        check(gateRecordsFromJson(bad).size() == 2, "a record with an unknown effect is dropped on read");
        for (const gates::GateEntry &e : cat)
            for (GateEffect ef : e.effects) {
                GateEffect r;
                if (!gateEffectFromKey(QString::fromLatin1(gateEffectKey(ef)), r) || r != ef)
                    check(false, "effect keys round-trip");
            }
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
