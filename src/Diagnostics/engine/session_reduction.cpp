/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "session_reduction.h"

#include "characteristic_engine.h"
#include "../measures/session_measure_source.h"
#include "../derived/club_distances.h"
#include "../derived/sequence_checks.h"
#include "../derived/strike_from_flight.h"
#include "../derived/tendencies.h"
#include "../derived/work_ons_pack.h"
#include "../../Vocabulary/Club/club_vocabulary.h"
#include "../../LaunchMonitor/inferred/lm_session_reductions.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::analysis {

namespace {

// An outcome seen on this many shots seeds the pattern graph as a declared miss would (SessionDiagnosticsModel's
// kDiagObservedMissMin, Contract F).
constexpr int kObservedMissMin = 3;

const ClubBaselineEntry *usualOf(const SessionReductionInput &in, const QString &club)
{
    if (!in.baseline) return nullptr;
    const auto it = in.baseline->constFind(canonicalClubId(club));
    return it == in.baseline->constEnd() ? nullptr : &it.value();
}

// THE SESSION-SCOPE CONDITIONS (SessionDiagnosticsModel::appendSessionScope's rule, values only): the session's
// order-check share and timing spread (`c`: the reading over `shots`, worked out once by the caller), through the same
// detect() every swing goes through.
void appendSessionScope(const CharacteristicPack &pack, const std::vector<ShotRecord> &shots,
                        const SessionSequenceConsistency &c, const LedgerOptions &opt, bool withSigma,
                        std::vector<ConditionLedger> &ledgers)
{
    SessionMeasureSource src(pack);
    if (c.share.available) {
        MeasureReading r;
        r.value = c.share.value;
        src.set(QStringLiteral("m_ksOrderShare"), r);
    }
    if (c.spread.available) {
        MeasureReading r;
        r.value = c.spread.value;
        if (withSigma && std::isfinite(c.spread.sigma) && c.spread.sigma > 0.0) {
            r.measSigma = c.spread.sigma;
            r.sigmaSrc  = SigmaSource::Reading;
        }
        src.set(QStringLiteral("m_ksTimingSpread"), r);
    }
    const DetectionResult det = detect(pack, src);
    const int n = int(shots.size());
    for (const Finding &f : det.findings) {
        if (!isSessionScopeCondition(pack, f.conditionId)) continue;
        ConditionLedger L;
        L.id           = f.conditionId;
        L.sessionScope = true;
        L.run.assign(size_t(n), ShotState::NotAssessable);
        L.zRun.assign(size_t(n), std::numeric_limits<double>::quiet_NaN());
        L.drivingMeasureId = f.evidence.hasEvidence ? f.evidence.drivingMeasureId : QStringLiteral("m_ksOrderShare");
        const bool assessed = f.state != FindingState::Unavailable;
        if (assessed) {
            L.assessable = c.share.available
                               ? c.share.resolved
                               : (c.spread.available ? c.spread.segments[size_t(c.spread.best)].n : 0);
            L.fired         = c.share.available ? c.share.resolved - c.share.inOrder : 0;
            L.effAssessable = c.share.effN;
            L.effFired      = c.share.effOut;
            L.wilsonLower   = c.share.lowerOut;
            L.tier          = f.state == FindingState::Fired ? Tier::Pattern : Tier::Clean;
            const double hard = L.tier == Tier::Pattern ? 1.0 : 0.0;
            L.pPattern = (opt.softTier && f.pFire >= 0.f) ? double(f.pFire) : hard;
        }
        bool orderFired = false, spreadFired = false;
        if (f.state == FindingState::Fired) {
            for (const QString &sid : f.firedSignals) {
                const Signal *s = pack.signal(sid);
                if (!s) continue;
                orderFired  |= s->measures.contains(QStringLiteral("m_ksOrderShare"));
                spreadFired |= s->measures.contains(QStringLiteral("m_ksTimingSpread"));
            }
        }
        L.recurrence = sequenceConsistencyCountLine(c, sequenceCountClauses(c, orderFired, spreadFired));
        ledgers.push_back(std::move(L));
    }
}

// The graph's nodes and edges over a node set (SessionDiagnosticsModel::marshalGraph's rule).
void marshalGraph(const CharacteristicPack &pack, const std::vector<ConditionLedger> &ledgers,
                  const QSet<QString> &keep, const QString &declaredMiss, const QHash<QString, bool> &screens,
                  std::vector<NodeSpec> &nodes, std::vector<EdgeSpec> &edges)
{
    nodes.clear();
    edges.clear();
    for (const Condition &c : pack.conditions) {
        if (!keep.contains(c.id)) continue;
        const ConditionLedger *l = ledgerFor(ledgers, c.id);
        NodeSpec n;
        n.id            = c.id;
        n.measurable    = l && l->assessable >= 1;
        n.screened      = c.confirmedBy == ConfirmedBy::Screened;
        n.screenEntered = screens.contains(c.id);
        n.asserted      = c.confirmedBy == ConfirmedBy::Asserted;
        if (!declaredMiss.isEmpty() && c.id == declaredMiss) n.outcomeId = declaredMiss;
        nodes.push_back(std::move(n));
    }
    for (const Edge &e : pack.edges) {
        if (e.type != EdgeType::Causes) continue;
        if (!keep.contains(e.from) || !keep.contains(e.to)) continue;
        EdgeSpec s;
        s.from           = e.from;
        s.to             = e.to;
        s.type           = 0;
        s.strength       = int(e.strength);
        s.sharedEvidence = e.sharedEvidence;
        edges.push_back(std::move(s));
    }
}

FocusSplit focusOf(const SessionReductionInput &in)
{
    FocusSplit focus;
    focus.declared    = !in.focusConditionId.isEmpty() && in.focusFromShot >= 0;
    focus.baselineEnd = in.focusFromShot - 1;
    return focus;
}

// The ledgers and the pattern graph's links over `shots` (SessionDiagnosticsModel::rebuild's first half and
// buildGraph's node set: the patterns, the declared miss, the outcomes seen on enough shots, all they descend from,
// and what lies between them and the declared miss), and the sequence-consistency reading over the same rows, which the
// session-scope ledgers are read from (worked out with or without a pack: it is the rows', not the pack's).
void reduceLedgers(const SessionReductionInput &in, const CharacteristicPack *pack, const std::vector<ShotRecord> &shots,
                   std::vector<ConditionLedger> &ledgers, std::vector<LinkEvidence> &links,
                   SessionSequenceConsistency &sequence)
{
    ledgers = conditionLedgers(shots, in.options);
    links.clear();
    sequence = sessionSequenceConsistency(shots, in.options);
    if (!pack) return;
    appendSessionScope(*pack, shots, sequence, in.options, in.withSigma, ledgers);

    QSet<QString> seeds;
    for (const ConditionLedger &l : ledgers)
        if (l.tier == Tier::Pattern) seeds.insert(l.id);
    const bool miss = !in.declaredMiss.isEmpty() && pack->condition(in.declaredMiss);
    if (miss) seeds.insert(in.declaredMiss);
    for (const ConditionLedger &l : ledgers) {
        const Condition *c = pack->condition(l.id);
        if (c && c->kind == ConditionKind::Outcome && l.fired >= kObservedMissMin) seeds.insert(l.id);
    }
    QSet<QString> keep = seeds;
    for (const QString &id : std::as_const(seeds)) keep.unite(causalClosure(*pack, id, /*downstream*/ false));
    if (miss) {
        const QSet<QString> missAncestors = causalClosure(*pack, in.declaredMiss, false);
        for (const QString &id : std::as_const(seeds))
            for (const QString &d : causalClosure(*pack, id, /*downstream*/ true))
                if (missAncestors.contains(d) || d == in.declaredMiss) keep.insert(d);
    }
    std::vector<NodeSpec> nodes;
    std::vector<EdgeSpec> edges;
    marshalGraph(*pack, ledgers, keep, in.declaredMiss, in.screens, nodes, edges);
    links = gradeLinks(nodes, edges, ledgers, focusOf(in), in.options);
}

// One ratchet step (rebuild's): the stage from the previous, and the latch the ratchet cannot carry across a close.
void stepStage(const SessionReductionInput &in, const std::vector<ConditionLedger> &ledgers,
               const std::vector<LinkEvidence> &links, Stage &stage, bool &reached)
{
    stage = sessionStage(stage, ledgers, links, in.closed, in.options);
    if (stage == Stage::Established || sessionStage(Stage::Cold, ledgers, links, false, in.options) == Stage::Established)
        reached = true;
}

} // namespace

SessionDiagnosis reduceSession(const SessionReductionInput &in)
{
    SessionDiagnosis out;
    const CharacteristicPack *pack = in.pack ? &in.pack->pack() : nullptr;
    static const ShotOutcomePack kNoOutcomePack;
    const ShotOutcomePack &outcomePack = in.outcomePack ? *in.outcomePack : kNoOutcomePack;

    // ── The stage ratchet. With no stage held, replayed one shot at a time in strike order (the old model's headless
    //    back-fill: each prefix's rows checked against that prefix's own usual flight), then the whole session. ──
    Stage stage   = in.previousStage;
    bool  reached = in.reachedEstablished;
    if (in.replay) {
        std::vector<ShotRecord> prefix;
        prefix.reserve(in.shots.size());
        {   // the empty session first, as the back-fill's opening rebuild
            std::vector<ConditionLedger> l;
            std::vector<LinkEvidence> k;
            SessionSequenceConsistency q;   // the prefix's own, for its ledgers only
            reduceLedgers(in, pack, prefix, l, k, q);
            stepStage(in, l, k, stage, reached);
        }
        for (size_t i = 0; i + 1 < in.shots.size(); ++i) {
            prefix.push_back(in.shots[i]);
            for (ShotRecord &r : prefix) uncheckShotAgainstFlight(r);
            checkStrikesAgainstFlight(prefix, in.flightNorms);
            std::vector<ConditionLedger> l;
            std::vector<LinkEvidence> k;
            SessionSequenceConsistency q;   // the prefix's own, for its ledgers only
            reduceLedgers(in, pack, prefix, l, k, q);
            stepStage(in, l, k, stage, reached);
        }
    }
    reduceLedgers(in, pack, in.shots, out.ledgers, out.links, out.sequence);   // the reading, once (5.6d)
    stepStage(in, out.ledgers, out.links, stage, reached);
    out.stage              = stage;
    out.reachedEstablished = reached;
    for (const ConditionLedger &l : out.ledgers)
        if (l.tier == Tier::Pattern) out.patterns << l.id;
    out.patterns.sort();

    SessionResults &r = out.results;
    r.hasGrowth = true;
    r.usuals    = in.usuals;   // G-10: the session's usuals, held and stored, never recomputed by a reader
    // The golfer's word each shot was judged with (DEC-192 as amended, round 3).
    for (auto it = in.intents.constBegin(); it != in.intents.constEnd(); ++it)
        if (!it.value().isEmpty()) r.shotIntents.insert(it.key(), it.value());
    r.shotCorrections = in.corrections;

    // ── The work-ons and the evidence (sessionPrioritiesForPack, as SessionDiagnosticsModel::sessionWorkOns) ──
    r.workOns = sessionPrioritiesForPack(pack, in.shots, out.ledgers);
    {   // the order uncapped (5.5a): what the Charts' Wrist sections order by (SessionDiagnosis::workOnOrder)
        PriorityOptions uncapped;
        uncapped.maxPerSession = std::numeric_limits<int>::max();
        for (const PriorityEntry &e : sessionPrioritiesForPack(pack, in.shots, out.ledgers, uncapped).entries)
            out.workOnOrder << e.id;
    }
    r.workOns.sessionId = in.sessionId;
    for (PriorityEntry &e : r.workOns.entries) {
        const Condition *c = pack ? pack->condition(e.id) : nullptr;
        e.name = (c && !c->label.isEmpty()) ? c->label : e.id;   // today's key (FIELD-LIST D5), a pack label
    }

    if (!pack) return out;

    // ── The evidence on every path up from an outcome seen at least once (ensureOutcomes') ──
    QSet<QString> keep;
    for (const ConditionLedger &l : out.ledgers) {
        const Condition *cond = pack->condition(l.id);
        if (!cond || cond->kind != ConditionKind::Outcome || l.fired < 1) continue;
        keep.insert(l.id);
        keep.unite(causalClosure(*pack, l.id, /*downstream*/ false));
    }
    std::vector<NodeSpec> outcomeNodes;
    std::vector<EdgeSpec> outcomeEdges;
    marshalGraph(*pack, out.ledgers, keep, in.declaredMiss, in.screens, outcomeNodes, outcomeEdges);
    const std::vector<LinkEvidence> outcomeLinks = gradeLinks(outcomeNodes, outcomeEdges, out.ledgers, focusOf(in), in.options);
    for (const ConditionLedger &l : out.ledgers)
        if (l.tier != Tier::Clean) r.outcomeEvidence.tiers.insert(l.id, l.tier);
    for (const LinkEvidence &e : outcomeLinks)
        if (e.grade != LinkGrade::Unanchored) r.outcomeEvidence.links.push_back(e);
    r.outcomeEvidence.chainConfirmed = chainConfirmedConditions(outcomePack, outcomeLinks);

    // ── Each shot's outcome WITH the session's evidence: its "because" (J-14 (b)) ──
    const ShotOutcomeSessionFacts facts{ r.outcomeEvidence.links, r.outcomeEvidence.tiers, in.usuals, &in.flightNorms,
                                         in.athleteHandedness };
    const int nShots = int(in.shots.size());
    out.composed.reserve(size_t(nShots));
    for (const ShotRecord &s : in.shots) {
        out.composed.push_back(composeShotOutcome(s, in.corrections.value(s.shotId), in.intents.value(s.shotId),
                                                  usualOf(in, s.club), outcomePack, facts));
        ShotBecause b;
        b.shotId = s.shotId;
        b.driver = out.composed.back().driver;
        b.links  = out.composed.back().links;
        r.because.push_back(std::move(b));
    }

    // ── Per shot (DEC-192): the shot's OWN outcome and factors, as its derived data holds them (DEC-124: never
    //    recomposed by a reader); a shot whose derive has not landed is composed here without the "because". ──
    FiringInputs derivedFrom;
    derivedFrom.flightNorms = in.flightNorms;
    derivedFrom.usuals      = in.usuals;
    derivedFrom.athleteHandedness = in.athleteHandedness;
    std::vector<ShotFactors> factorsAt(static_cast<size_t>(nShots), ShotFactors{});
    for (int i = 0; i < nShots; ++i) {
        const ShotRecord &s = in.shots[size_t(i)];
        ShotResult sr;
        sr.shotId = s.shotId;
        sr.club   = canonicalClubId(s.club);
        if (const auto it = in.derived.constFind(s.shotId); it != in.derived.constEnd()) {
            sr.outcome = it->outcome;
            sr.factors = it->factors;
        } else {
            sr.outcome = composeShotOutcome(s, in.corrections.value(s.shotId), in.intents.value(s.shotId),
                                            usualOf(in, s.club), outcomePack, ShotOutcomeSessionFacts{ {}, {}, in.usuals, &in.flightNorms,
                                                                                     in.athleteHandedness });
            sr.factors = deriveShotFactors(s, sr.outcome, outcomePack, derivedFrom);
        }
        factorsAt[size_t(i)] = sr.factors;
        r.shots.push_back(std::move(sr));
    }

    // ── The reasons: one row per headline driver of a bad shot, the unexplained last (ensureOutcomes') ──
    struct Tally {
        int shots = 0;
        QStringList order;
        QHash<QString, int> phrases;
        double cost = 0.0;
        int costN = 0;
        int strengthRank = 0;
        QString strength;
        std::vector<int> firedAt;
    };
    const auto strengthRank = [](const QString &s) {
        return s == QLatin1String("clear") ? 3 : s == QLatin1String("likely") ? 2 : s == QLatin1String("possible") ? 1 : 0;
    };
    QHash<QString, Tally> tally;
    QStringList tallyOrder;
    std::vector<bool> assessedAt(size_t(nShots), false);
    for (int i = 0; i < nShots; ++i) {
        const ShotOutcome &o = out.composed[size_t(i)];
        const bool anyMiss = std::any_of(o.labels.begin(), o.labels.end(), [](const ShotOutcomeLabel &l) { return l.miss; });
        assessedAt[size_t(i)] = o.ready && !o.noReading && (o.clean || anyMiss);
        if (!anyMiss || o.clean) continue;
        const QString &id = o.driver.id;
        if (!tally.contains(id)) tallyOrder << id;
        Tally &t = tally[id];
        ++t.shots;
        t.firedAt.push_back(i);
        if (!o.mixKey.isEmpty()) {
            if (!t.phrases.contains(o.mixKey)) t.order << o.mixKey;
            ++t.phrases[o.mixKey];
        }
        // G-08 (refactor 3.5-ii, the new owner only): the carry a reason cost is counted against an EARNED usual
        // only — the rule the carry waterfall and carry today already use (B-22: ClubBaselineEntry::earned).
        const ClubBaselineEntry *usual = usualOf(in, in.shots[size_t(i)].club);
        if (usual && usual->earned()) {
            const double loss = shotCarryLoss(in.shots[size_t(i)], usual);
            if (std::isfinite(loss)) { t.cost += loss; ++t.costN; }
        }
        if (strengthRank(o.driver.strength) > t.strengthRank) {
            t.strengthRank = strengthRank(o.driver.strength);
            t.strength = o.driver.strength;
        }
    }
    std::sort(tallyOrder.begin(), tallyOrder.end(), [&tally](const QString &a, const QString &b) {
        if (a.isEmpty() != b.isEmpty()) return !a.isEmpty();
        const Tally &x = tally[a], &y = tally[b];
        if (x.shots != y.shots) return x.shots > y.shots;
        if (x.cost != y.cost)   return x.cost > y.cost;
        return a < b;
    });
    for (const QString &id : std::as_const(tallyOrder)) {
        const Tally &t = tally[id];
        SessionReason x;
        x.conditionId = id;
        x.shots       = t.shots;
        QStringList phrases = t.order;
        std::stable_sort(phrases.begin(), phrases.end(), [&t](const QString &a, const QString &b) {
            return t.phrases.value(a) > t.phrases.value(b);
        });
        for (const QString &p : std::as_const(phrases)) x.produced.push_back(ReasonProduced{ p, t.phrases.value(p) });
        x.cost = t.costN > 0 ? t.cost : std::numeric_limits<double>::quiet_NaN();
        const QSet<int> firedSet(t.firedAt.begin(), t.firedAt.end());
        for (int i = 0; i < nShots; ++i)
            x.ticks.push_back(ReasonTick{ firedSet.contains(i) ? ShotState::Fired
                                          : assessedAt[size_t(i)] ? ShotState::Clean : ShotState::NotAssessable,
                                          in.shots[size_t(i)].shotId });
        if (!id.isEmpty() && t.shots >= 2) {
            const int lastStart = nShots - std::max(1, nShots / 3);
            int late = 0, early = 0;
            for (int i : t.firedAt) (i >= lastStart ? late : early) += 1;
            const double lateShare  = double(late) / double(std::max(1, nShots - lastStart));
            const double earlyShare = double(early) / double(std::max(1, lastStart));
            if (lateShare - earlyShare <= -0.15)     x.trend = -1;
            else if (lateShare - earlyShare >= 0.15) x.trend = 1;
        }
        if (!id.isEmpty())
            if (const ConditionLedger *l = ledgerFor(out.ledgers, id)) x.tier = l->tier;
        x.strength       = t.strength;
        x.chainConfirmed = !id.isEmpty() && r.outcomeEvidence.chainConfirmed.contains(id);
        r.reasons.push_back(std::move(x));
    }

    // ── The shot mix ──
    {
        const ShotMix mix = composeSessionMix(out.composed);
        r.shotMix.corrections = mix.corrections;
        r.shotMix.fromFlight  = mix.fromFlight;
    }

    // ── Per club: the baseline window (D7) and today's carry against the frozen usual, one pooling ──
    ClubDistanceSession self;
    self.folder      = in.folderName;
    self.day         = in.day;
    self.shots       = in.shots;
    self.corrections = in.corrections;
    for (ClubSessionPool &pool : poolSessionByClub(self, in.flightNorms)) {
        if (!pool.shots.empty()) {
            ClubWindowSession window;
            window.shots = pool.shots;
            const std::vector<LmFieldStats> stats = lmSessionStats(windowLmValues({ window }), -1);
            ClubCarryToday c;
            c.clubId = pool.club;
            c.mean   = clubStatMean(stats, "lm.carryDistance");
            c.sd     = clubStatSd(stats, "lm.carryDistance");
            if (const LmFieldStats *carry = clubStat(stats, "lm.carryDistance")) c.n = carry->n;
            const ClubBaselineEntry *usual = usualOf(in, pool.club);
            if (usual && usual->earned() && std::isfinite(usual->carryMean)) {
                c.baselineMean = usual->carryMean;
                c.baselineSd   = usual->carrySd;
                c.baselineN    = usual->n;
            }
            r.carryToday.push_back(std::move(c));
        }
        pool.folder.clear();   // the session's own: not written (session_results.h)
        pool.day = QDate();
        r.clubWindows.push_back(std::move(pool));
    }

    // ── Each club's shots by factor, every value a shot shows counted (G-09's rule, named: ClubFactorMix) ──
    struct Mix { int judged = 0; ShotFactorTally strike, direction, flight; };
    QStringList mixClubs;
    QHash<QString, Mix> mixByClub;
    for (int i = 0; i < nShots; ++i) {
        const ShotOutcome &o = out.composed[size_t(i)];
        const QString club = canonicalClubId(in.shots[size_t(i)].club);
        if (!o.ready || club.isEmpty()) continue;
        if (!mixByClub.contains(club)) mixClubs << club;
        Mix &mix = mixByClub[club];
        const ShotFactors &f = factorsAt[size_t(i)];
        if (f.judged()) ++mix.judged;
        const auto add = [&outcomePack](ShotFactorTally &t, const ShotFactor &part) {
            if (!part.read) { ++t.notRead; return; }
            ++t.read;
            for (const QString &id : part.values) t.add(id, shotFactorValueLabel(outcomePack, id), part.faults.contains(id));
        };
        add(mix.strike, f.strike);
        add(mix.direction, f.direction);
        add(mix.flight, f.flight);
    }
    for (const QString &club : std::as_const(mixClubs)) {
        const Mix &mix = mixByClub[club];
        if (mix.judged == 0) continue;
        ClubFactorMix m;
        m.clubId    = club;
        m.judged    = mix.judged;
        m.strike    = composeFactorMixBar(mix.strike);
        m.direction = composeFactorMixBar(mix.direction);
        m.flight    = composeFactorMixBar(mix.flight);
        r.clubMix.push_back(std::move(m));
    }

    // ── DEC-192 as Mark amended it (16 Nov, "b"; refactor 3.6 round 3): the COMPACT per-shot facts the athlete level's
    //    tendencies and themes read — each shot judged by the Tendencies rule with this session's evidence, corrections,
    //    intents and frozen usual (tendencies.h tendencyShotFacts), and as the themes read it (themeShotOf) — never the
    //    judged rows, which stay in diagnostics.json. ──
    {
        TendencySessionInput self;
        self.session.shots       = in.shots;
        self.session.corrections = in.corrections;
        self.intents             = in.intents;
        self.evidence            = r.outcomeEvidence;
        self.baseline            = in.baseline.value_or(QHash<QString, ClubBaselineEntry>{});
        self.athleteHandedness   = in.athleteHandedness;
        r.tendencyShots = tendencySessionShots(self, outcomePack, in.flightNorms).shots;
        r.themeShots.reserve(in.shots.size());
        for (const ShotRecord &s : in.shots) r.themeShots.push_back(themes::themeShotOf(s));
        r.hasFacts = true;
    }
    return out;
}

} // namespace pinpoint::analysis
