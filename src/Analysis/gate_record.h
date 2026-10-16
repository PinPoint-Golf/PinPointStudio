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

// GATE RECORDS — what a gate leaves on a swing when it fires (docs/design/trust_and_gates_design.md
// §4.2, §6). One record per firing, persisted as `analysis.gates` beside `analysis.gatesVersion`.
//
// A RECORD IS EVIDENCE, NOT A SENTENCE. `evidence` holds the numbers the gate compared, keyed with
// their unit in the name (`sightLostMs`, `corr`); the words come from the gate catalogue
// (Metrics/gate_catalogue.h) filled from it, so a record written today reads correctly with words
// revised later. No producer formats words.
//
// TIMES IN EVIDENCE ARE RELATIVE TO IMPACT, in ms, + after the ball (the Charts' shot clock), never
// absolute instants: a record then needs no re-timing on any of the three paths the analysis takes
// (swing.json, the live analysisDetail map, the reload).
//
// THE ONE SERIALISATION. swing_doc.cpp, shot_processor.cpp and disk_replay_source.cpp all go
// through gateRecordsToJson, as the kinematic sequence goes through kinematic_sequence_json.h.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <vector>

// A GATE ID AT A FIRING SITE. Every id a producer fires is spelled PP_GATE("area.name"), so
// gate_catalogue_test can read the producer's source and tie its ids to the catalogue both ways.
#define PP_GATE(id) QStringLiteral(id)

namespace pinpoint::analysis {

// The record format's version. A document with `analysis.gatesVersion` absent was analysed before
// records existed: a reader says "reason not recorded" and falls back, rather than reading the empty
// array as "no gate fired". 1 = the kinematic sequence's gates (stage 2, 16 Oct 2026).
// 2 = every per-swing gate (stage 6, 16 Oct 2026), stamped on every analysis.
inline constexpr int kGatesVersion = 2;

enum class GateEffect : unsigned char {
    Refused,     // no value
    Bounded,     // a range instead of a value (`bound`)
    Withheld,    // value shown, not judged
    Downgraded,  // value kept at a lower trust level (a route fallback)
    Masked,      // samples out of sight / invalidated; the reading itself survives
};

inline const char *gateEffectKey(GateEffect e)
{
    switch (e) {
    case GateEffect::Refused:    return "refused";
    case GateEffect::Bounded:    return "bounded";
    case GateEffect::Withheld:   return "withheld";
    case GateEffect::Downgraded: return "downgraded";
    case GateEffect::Masked:     return "masked";
    }
    return "refused";
}

inline bool gateEffectFromKey(const QString &k, GateEffect &out)
{
    for (GateEffect e : { GateEffect::Refused, GateEffect::Bounded, GateEffect::Withheld,
                          GateEffect::Downgraded, GateEffect::Masked })
        if (k == QLatin1String(gateEffectKey(e))) { out = e; return true; }
    return false;
}

struct GateRecord {
    QString     gate;       // catalogue id, e.g. "sequence.sightedBand"
    QString     subject;    // what it acted on: "ks.pelvis", a metric key, "route.faceOn+dtl"
    GateEffect  effect = GateEffect::Refused;
    QVariantMap evidence;   // numbers (and short codes) the gate compared; never prose
    QVariantMap bound;      // effect = bounded: { noEarlierThanMs } / { noLaterThanMs } (ms BEFORE impact)
    QStringList blocks;     // filled downstream (§4.4): what this record prevented
    // A CASCADE IS ONE RECORD (stage 6): a gate that refuses a whole track or a whole reference
    // names the metric keys it took down here, once, rather than writing a record per metric. A
    // reader asking about a metric looks for a record whose subject IS the key or whose `covers`
    // holds it (recordsFor below).
    QStringList covers;
};

// The records that speak for `subject` (a metric key, "ks.pelvis", "track.club" …): those naming it,
// and those whose cascade covers it.
inline std::vector<const GateRecord *> recordsFor(const std::vector<GateRecord> &recs, const QString &subject)
{
    std::vector<const GateRecord *> out;
    for (const GateRecord &r : recs)
        if (r.subject == subject || r.covers.contains(subject)) out.push_back(&r);
    return out;
}

// THE SUMMARY OF A MASK (stage 6, Mark 16 Oct: one record per reading). A gate that drops a stretch
// of a reading's samples, frame by frame, leaves one record: how many of the reading's frames it took,
// of how many, and the first and last of them relative to impact. `gatedUs` are the instants it
// refused; `ofFrames` the reading's frames it could have refused (the denominator the reader is told).
// Empty when nothing in [fromUs, toUs] was refused, and then no record is written.
inline QVariantMap maskEvidence(const std::vector<int64_t> &gatedUs, int ofFrames, int64_t impactUs,
                                int64_t fromUs = INT64_MIN, int64_t toUs = INT64_MAX)
{
    int n = 0;
    int64_t first = INT64_MAX, last = INT64_MIN;
    for (int64_t t : gatedUs) {
        if (t < fromUs || t > toUs) continue;
        ++n;
        first = std::min(first, t);
        last = std::max(last, t);
    }
    if (n == 0 || ofFrames <= 0) return {};
    QVariantMap ev{ { QStringLiteral("frames"), n }, { QStringLiteral("ofFrames"), ofFrames },
                    { QStringLiteral("frac"), double(n) / double(ofFrames) } };
    if (impactUs >= 0) {
        ev.insert(QStringLiteral("fromMs"), double(first - impactUs) / 1000.0);
        ev.insert(QStringLiteral("toMs"), double(last - impactUs) / 1000.0);
    }
    return ev;
}

// The helper every producer fires through (§6).
class GateLog {
public:
    void fire(const QString &gate, const QString &subject, GateEffect effect,
              const QVariantMap &evidence = {}, const QVariantMap &bound = {})
    {
        m_records.push_back(GateRecord{ gate, subject, effect, evidence, bound, {}, {} });
    }
    // A cascade (see GateRecord::covers): one record, the readings it took down named.
    void fireCovering(const QString &gate, const QString &subject, GateEffect effect, const QStringList &covers,
                      const QVariantMap &evidence = {})
    {
        m_records.push_back(GateRecord{ gate, subject, effect, evidence, {}, {}, covers });
    }
    void append(const std::vector<GateRecord> &more) { m_records.insert(m_records.end(), more.begin(), more.end()); }
    const std::vector<GateRecord> &records() const { return m_records; }
    std::vector<GateRecord> take() { return std::move(m_records); }
    bool empty() const { return m_records.empty(); }
    size_t size() const { return m_records.size(); }

private:
    std::vector<GateRecord> m_records;
};

inline QJsonArray gateRecordsToJson(const std::vector<GateRecord> &recs)
{
    QJsonArray out;
    for (const GateRecord &r : recs) {
        QJsonObject o{ { QStringLiteral("gate"),    r.gate },
                       { QStringLiteral("subject"), r.subject },
                       { QStringLiteral("effect"),  QString::fromLatin1(gateEffectKey(r.effect)) } };
        if (!r.evidence.isEmpty()) o.insert(QStringLiteral("evidence"), QJsonObject::fromVariantMap(r.evidence));
        if (!r.bound.isEmpty())    o.insert(QStringLiteral("bound"),    QJsonObject::fromVariantMap(r.bound));
        if (!r.blocks.isEmpty())   o.insert(QStringLiteral("blocks"),   QJsonArray::fromStringList(r.blocks));
        if (!r.covers.isEmpty())   o.insert(QStringLiteral("covers"),   QJsonArray::fromStringList(r.covers));
        out.append(o);
    }
    return out;
}

inline std::vector<GateRecord> gateRecordsFromJson(const QJsonArray &a)
{
    std::vector<GateRecord> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        GateRecord r;
        r.gate    = o.value(QStringLiteral("gate")).toString();
        r.subject = o.value(QStringLiteral("subject")).toString();
        if (r.gate.isEmpty() || !gateEffectFromKey(o.value(QStringLiteral("effect")).toString(), r.effect))
            continue;
        r.evidence = o.value(QStringLiteral("evidence")).toObject().toVariantMap();
        r.bound    = o.value(QStringLiteral("bound")).toObject().toVariantMap();
        for (const QJsonValue &b : o.value(QStringLiteral("blocks")).toArray()) r.blocks << b.toString();
        for (const QJsonValue &c : o.value(QStringLiteral("covers")).toArray()) r.covers << c.toString();
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace pinpoint::analysis
