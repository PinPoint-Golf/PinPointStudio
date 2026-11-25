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

#include "charts_wrist_presenter.h"
#include "../../DomainModel/rule_data.h"   // the packs and norms: RuleData's snapshot (refactor step 3.2)

#include "charts_relations_presenter.h"     // movesWith(): the Relations ledger, asked our own question
#include "charts_gadget.h"                  // chartsGadgetMap: the report's payload, from the typed session
#include "charts_words.h"
#include "session_curves.h"
#include "session_relations.h"              // boxStats, pearson
#include "session_series_extract.h"   // folderShotId (refactor 5.5a)
#include "../../Export/series_store.h"   // seriesSwingLabelOf: the ★ label
#include "session_warp.h"
#include "session_wrist.h"
#include "variability_clubs.h"                    // perClubInputs: one window per club (R2b)
#include "../variability/variability_facade.h"    // VariabilityResultView: the `variabilityResult` QML reads
#include "../../Metrics/variability/variability_grader.h"

#include "../../Core/pp_debug.h"
#include "../../Diagnostics/engine/condition_reading.h"
#include "../../Diagnostics/engine/wrist_domain_score.h"
#include "../../Diagnostics/norms/norm.h"            // NormSource: where the wrist bands come from (TRUST)
#include "../../Diagnostics/norms/norm_provider.h"   // sharedNormProvider: the bands' own rows
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Vocabulary/Analysis/wrist_assessment_types.h"   // dofForMetricKey
#include "../../Wrist/reference_bands.h"          // NormBandProvider::cellMeasureId, BandContext
#include "../review/timeline_labels.h"
#include "../../Metrics/gate_catalogue.h"   // the gate behind an empty wrist value (its words)

#include <QCoreApplication>
#include <QDate>
#include <QDeadlineTimer>
#include <QDir>
#include <QLocale>
#include <QMap>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>

// The value types the worker keeps. A NAMED namespace, not an anonymous one: Worker (a member type of
// a class with external linkage) holds them.
namespace pinpoint::charts::wristview {

constexpr int kP = 8;   // checkpoints, P1..P8 (wristCheckpoints())

struct Cell {
    bool    available = false;
    double  delta = kNaN;
    QString rag = QStringLiteral("none");
    bool    banded = false;
    double  lo = kNaN, hi = kNaN;
    bool out() const { return available && (rag == QLatin1String("red") || rag == QLatin1String("amber")); }
};

struct Dof {
    QString id, key, name, sub, axisName, source, poleTop, poleBottom;
    double  conf = 0.0;
    bool    hasStrip = false;
    std::array<Cell, kP> cells{};
};

struct Checkpoint {
    QString tag, note;
    int     phase = -1;
    bool    available = false;
    qint64  tUs = -1;
};

// ShotSeries::wrist, parsed once per job.
struct Profile {
    bool    has = false;
    int     archetype = 0, mode = -1;
    QString archetypeName;
    // Wrist readings and no profile in the findings yet (session_wrist.h): the DOFs and checkpoints, no cell measured, and
    // why. Not `has`: nothing of it is a graded reading, so no box, count or style is made from it.
    bool    pending = false;
    QString pendingWhy;
    bool shown() const { return has || pending; }
    QVariantMap resemblance;
    std::array<Checkpoint, kP> cps{};
    std::vector<Dof> dofs;
    const Dof *dof(const QString &id) const
    {
        for (const Dof &d : dofs) if (d.id == id) return &d;
        return nullptr;
    }
};

// One condition of the wrist domain, as the pack states it.
struct Cond {
    QString id, label, golfer, golferWell, golferWhy, axis;   // golfer-facing words only: the author's
                                                              // `consequence` never reaches the tab
    std::vector<pinpoint::analysis::ConditionReading> readings;
    QStringList linked;     // the conditions an edge joins it to, by label
};

} // namespace pinpoint::charts::wristview

namespace pinpoint::charts {

using namespace wristview;
namespace W = words;
namespace pa = pinpoint::analysis;
namespace WU = wristui;   // the typed results QML reads (charts_wrist_types.h)

// ── the result and the worker ───────────────────────────────────────────────────────────────

struct ChartsWristPresenter::Result {
    bool available = false;
    WU::Choices choices;
    WU::ThisShot thisShot;
    WU::Session session;
    RelationsMovesWith movesWith;             // the Relations presenter's ledger, its own type (5.5c)
    // the session standfirst's parts, for paper (wristSessionReport): scoreLine, styleLine, repeat,
    // sizeLine. Not in `session`: the screen's value is unchanged by them.
    QVariantMap sessionParts;
    QList<WU::Sentence> standfirsts;          // 8 Sentences (thisShot.standfirsts)
    std::array<qint64, kP> cpUs{};            // the picked shot's checkpoint µs, −1 none
    QStringList tags;
};

struct ChartsWristPresenter::Worker {
    QMutex mutex;                                            // held for a whole job
    std::unique_ptr<TimelineLabels> tl;
    std::shared_ptr<const pa::ICharacteristicPackProvider> pack;  // RuleData's pack, for the conditions' words (5.5a: follows EVT-13)
    bool condsBuilt = false;
    std::vector<Cond> conds;                                 // the wrist domain, in pack order

    // movesWith, per snapshot revision and reading
    const SessionSnapshot *mwSnap = nullptr;
    quint64 mwRev = 0;
    QString mwId;
    RelationsMovesWith mw;

    void ensure()
    {
        if (!tl) tl = std::make_unique<TimelineLabels>();
        // THE PACK FOLLOWS ITS SAVES (refactor 5.5a; J-74 Q9): RuleData's current pack (any thread), the conditions'
        // words made again when a saved pack edit (EVT-13) has published another.
        const std::shared_ptr<const pinpoint::RuleDataSnapshot> now = pinpoint::RuleData::current();
        if (now && now->pack != pack) { pack = now->pack; condsBuilt = false; conds.clear(); }
        if (condsBuilt || !pack) return;
        condsBuilt = true;
        const pa::CharacteristicPack &p = pack->pack();
        for (const pa::Condition &c : p.conditions) {
            if (c.domain != QLatin1String("wrist")) continue;
            Cond w;
            w.id = c.id;
            w.label = c.label.isEmpty() ? c.id : c.label;
            // a phrase carrying a backticked id is an author's note, never the golfer's words
            const auto golferSafe = [](const QString &t) { return t.contains(QLatin1Char('`')) ? QString() : t; };
            w.golfer = golferSafe(c.golfer);
            w.golferWell = golferSafe(c.golferWell);
            w.golferWhy = golferSafe(c.golferWhy);
            w.axis = c.axis;
            w.readings = pa::conditionReadings(p, c.id);
            for (const pa::Edge &e : p.edges) {
                const QString other = e.from == c.id ? e.to : e.to == c.id ? e.from : QString();
                if (other.isEmpty()) continue;
                const pa::Condition *o = p.condition(other);
                const QString name = o && !o->label.isEmpty() ? o->label : other;
                if (!w.linked.contains(name)) w.linked << name;
            }
            conds.push_back(std::move(w));
        }
        if (conds.empty()) ppWarn() << "[Charts] the condition pack has no wrist-domain condition: the Wrist tab will show no findings";
    }
};

namespace {

// ── small words and numbers ─────────────────────────────────────────────────────────────────

QVariant nv(double v) { return std::isfinite(v) ? QVariant(v) : QVariant::fromValue(nullptr); }
QVariant nullv() { return QVariant::fromValue(nullptr); }
QString degText(double v) { return W::figSigned(v, QStringLiteral("°")); }      // "+19°", "−19°", "-"
QString scoreText(double v) { return W::fig(v, 0); }                                 // the wrist score's box: whole points
QString degBare(double v) { return W::figUnit(v, QStringLiteral("°")); }        // "19°"
QString pips(double c)
{
    const int k = std::clamp(int(std::lround((std::isfinite(c) ? c : 0.0) * 4)), 0, 4);
    return QString(k, QChar(0x25CF)) + QString(4 - k, QChar(0x25CB));
}
QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

const QStringList &dofOrder()
{
    static const QStringList k{ QStringLiteral("leadWristRadUln"), QStringLiteral("leadWristFlexExt"), QStringLiteral("leadForearmRot"),
                                QStringLiteral("trailWristFlexExt"), QStringLiteral("leadElbowFlex") };
    return k;
}
// The standfirst's word for a DOF, and the phrase "Not measured on this swing" names it by.
QString shortName(const QString &id)
{
    if (id == QLatin1String("leadWristRadUln"))   return QStringLiteral("hinge");
    if (id == QLatin1String("leadWristFlexExt"))  return QStringLiteral("bow / cup");
    if (id == QLatin1String("leadForearmRot"))    return QStringLiteral("forearm roll");
    if (id == QLatin1String("trailWristFlexExt")) return QStringLiteral("trail wrist");
    if (id == QLatin1String("leadElbowFlex"))     return QStringLiteral("lead elbow");
    return id;
}
QString longName(const QString &id)
{
    if (id == QLatin1String("leadWristRadUln"))   return QStringLiteral("lead wrist hinge");
    if (id == QLatin1String("leadWristFlexExt"))  return QStringLiteral("lead wrist bow / cup");
    if (id == QLatin1String("leadForearmRot"))    return QStringLiteral("lead forearm roll");
    return shortName(id);
}


// A per-shot (or per-checkpoint) list of numbers, NaN where there is none: its readers take NaN as null did.
QList<double> numList(const std::vector<double> &v)
{
    return QList<double>(v.begin(), v.end());
}

// words::Sentence as QML takes it, with its first clause (the words module's rule, never cut in a view).
WU::Sentence sentenceOf(const W::Sentence &s)
{
    WU::Sentence out;
    out.text = s.text();
    for (const W::Sentence::Run &r : s.runs) out.runs.append(WU::Run{ r.text, r.fig });
    out.clause = W::firstClauseOf(out.text);
    return out;
}

// box_map.h's BOX, typed: NaN as null, the outliers as ordinals, the texts in the reading's rounding (n > 0).
WU::Box boxOf(const BoxStats &b, const std::function<QString(double)> &fmt)
{
    WU::Box out;
    out.n = b.n;
    out.min = nv(b.min); out.max = nv(b.max);
    out.q1 = nv(b.q1); out.med = nv(b.med); out.q3 = nv(b.q3);
    out.lo = nv(b.lo); out.hi = nv(b.hi);
    for (int i : b.outliers) out.outliers.append(i + 1);   // values are per shot: index + 1 = ordinal
    if (fmt && b.n > 0) out.texts = WU::BoxTexts{ fmt(b.lo), fmt(b.q1), fmt(b.med), fmt(b.q3), fmt(b.hi) };
    return out;
}

// ── the profile ─────────────────────────────────────────────────────────────────────────────

Profile parseProfile(const QVariantMap &p)
{
    Profile out;
    if (p.isEmpty()) return out;
    out.archetype = p.value(QStringLiteral("archetype")).toInt();
    out.archetypeName = p.value(QStringLiteral("archetypeName")).toString();
    if (out.archetypeName.isEmpty()) out.archetypeName = wristArchetypeName(out.archetype);
    out.mode = p.value(QStringLiteral("archetypeMode"), -1).toInt();
    out.pending = p.value(QStringLiteral("pending")).toBool();
    out.pendingWhy = p.value(QStringLiteral("pendingWhy")).toString();
    out.resemblance = p.value(QStringLiteral("resemblance")).toMap();
    const QVariantList cps = p.value(QStringLiteral("checkpoints")).toList();
    for (int i = 0; i < kP && i < cps.size(); ++i) {
        const QVariantMap m = cps.at(i).toMap();
        Checkpoint &c = out.cps[size_t(i)];
        c.tag = m.value(QStringLiteral("tag")).toString();
        c.note = m.value(QStringLiteral("note")).toString();
        c.phase = m.value(QStringLiteral("phase"), -1).toInt();
        c.available = m.value(QStringLiteral("available")).toBool() && m.contains(QStringLiteral("tUs"));
        c.tUs = c.available ? m.value(QStringLiteral("tUs")).toLongLong() : -1;
    }
    for (const QVariant &dv : p.value(QStringLiteral("dofs")).toList()) {
        const QVariantMap m = dv.toMap();
        Dof d;
        d.id = m.value(QStringLiteral("dof")).toString();
        d.key = m.value(QStringLiteral("metricKey")).toString();
        d.name = m.value(QStringLiteral("name")).toString();
        d.sub = m.value(QStringLiteral("sub")).toString();
        d.axisName = m.value(QStringLiteral("axisName")).toString();
        d.source = m.value(QStringLiteral("source")).toString();
        d.poleTop = m.value(QStringLiteral("poleTop")).toString();
        d.poleBottom = m.value(QStringLiteral("poleBottom")).toString();
        d.conf = m.value(QStringLiteral("confidence")).toDouble();
        d.hasStrip = m.value(QStringLiteral("hasStrip")).toBool();
        const QVariantList cells = m.value(QStringLiteral("cells")).toList();
        for (int i = 0; i < kP && i < cells.size(); ++i) {
            const QVariantMap cm = cells.at(i).toMap();
            Cell &c = d.cells[size_t(i)];
            c.available = cm.value(QStringLiteral("available")).toBool() && cm.contains(QStringLiteral("deltaDeg"));
            c.delta = c.available ? cm.value(QStringLiteral("deltaDeg")).toDouble() : kNaN;
            c.rag = cm.value(QStringLiteral("rag")).toString();
            c.banded = cm.value(QStringLiteral("banded")).toBool();
            c.lo = c.banded ? cm.value(QStringLiteral("bandLo")).toDouble() : kNaN;
            c.hi = c.banded ? cm.value(QStringLiteral("bandHi")).toDouble() : kNaN;
        }
        out.dofs.push_back(std::move(d));
    }
    out.has = !out.dofs.empty() && !out.pending;
    if (out.dofs.empty()) out.pending = false;
    return out;
}

QVariantMap resemblanceOfDetail(const QVariantMap &d)
{
    const QVariantMap r = d.value(QStringLiteral("resemblance")).toMap();
    QVariantMap out;
    for (const char *k : { "neutral", "bowed", "cupped" }) {
        bool ok = false;
        const double v = r.value(QLatin1String(k)).toDouble(&ok);
        if (!ok || !r.contains(QLatin1String(k))) return {};
        out.insert(QLatin1String(k), int(std::lround(v)));
    }
    return out;
}

// shotReplay.analysisDetail → the part of a ShotSeries the Wrist tab reads (phases, impact, the wrist
// series), on the same window-relative clock. For the picked shot when it is not in the snapshot yet.
std::shared_ptr<ShotSeries> wristShotFromDetail(const QVariantMap &d, const QString &dir)
{
    auto sh = std::make_shared<ShotSeries>();
    sh->swingDir = dir;
    sh->shotId = folderShotId(dir);
    // the records, so an empty value can name the gate behind it (trust stage 6 session 2)
    sh->gatesVersion = d.value(QStringLiteral("gatesVersion")).toInt();
    sh->gates = d.value(QStringLiteral("gates")).toList();
    for (const QVariant &pv : d.value(QStringLiteral("phases")).toList()) {
        const QVariantMap p = pv.toMap();
        PhaseMark pm;
        pm.phase = p.value(QStringLiteral("phase"), -1).toInt();
        pm.tUs = p.value(QStringLiteral("t_us")).toLongLong();
        pm.conf = p.contains(QStringLiteral("conf")) ? p.value(QStringLiteral("conf")).toDouble() : kNaN;
        if (pm.phase == phase::Impact && sh->impactUs < 0) sh->impactUs = pm.tUs;
        sh->phases.push_back(pm);
    }
    for (const QVariant &sv : d.value(QStringLiteral("series")).toList()) {
        const QVariantMap s = sv.toMap();
        MetricSeries m;
        m.key = s.value(QStringLiteral("key")).toString();
        if (m.key.isEmpty() || !pa::dofForMetricKey(m.key) || sh->metrics.contains(m.key)) continue;
        // the face-on elbow (routeId faceOn) is a camera reading, not the sensors' grid (wrist_assessment_types.h)
        if (pa::isCameraWristDofSeries(s.value(QStringLiteral("routeId")).toString())) continue;
        m.label = s.value(QStringLiteral("label")).toString();
        m.unit = s.value(QStringLiteral("unit")).toString();
        for (const QVariant &t : s.value(QStringLiteral("t_us")).toList()) m.tUs.push_back(t.toLongLong());
        for (const QVariant &v : s.value(QStringLiteral("value")).toList()) {
            bool ok = false;
            const double x = v.toDouble(&ok);
            m.value.push_back(ok && !v.isNull() ? x : kNaN);
        }
        const size_t n = std::min(m.tUs.size(), m.value.size());
        m.tUs.resize(n);
        m.value.resize(n);
        sh->metricOrder << m.key;
        sh->metrics.insert(m.key, std::move(m));
    }
    return sh;
}

// "shot 3, 18 Aug 2026": which swing the ★ is — its swing number and its session's day (SessionIdentity).
QString referenceLabelOf(const QString &dir) { return pinpoint::seriesSwingLabelOf(dir); }

// ── the job's context ───────────────────────────────────────────────────────────────────────

struct Ctx {
    ChartsWristPresenter::Worker &w;
    const ChartsWristPresenter::Inputs &in;
    std::shared_ptr<const SessionSnapshot> snap;
    Ctx(ChartsWristPresenter::Worker &wk, const ChartsWristPresenter::Inputs &i, std::shared_ptr<const SessionSnapshot> s)
        : w(wk), in(i), snap(std::move(s)) {}
    int n = 0;                                   // shots in the snapshot
    int index = -1;                              // the picked shot's index in it, −1 none
    std::shared_ptr<const ShotSeries> shot;      // the picked shot (the snapshot's, or its own detail)
    Profile pick;
    std::vector<Profile> profs;                  // parallel to snap->shots, re-graded to the archetype
    int ready = 0;                               // shots with a wrist profile in their findings
    int waiting = 0;                             // shots with wrist readings and no profile yet (pending)
    std::shared_ptr<const ShotSeries> ref;
    Profile refProf;
    QString refWhy, refLabel;
    bool refIsPicked = false;
    // THE NOUN the session's sentences count in: "shots" on Charts, "swings" in the report (its own word).
    QString many = QStringLiteral("shots"), one = QStringLiteral("shot");
    // the report's band wash (wristSessionReport): the dominant style, −1 = the screen (the picked shot's)
    int washStyle = -1;
    // paper (the report payload): its sentences never name the Auto control, which only the screen has
    bool paper = false;

    TimelineLabels &tl() const { return *w.tl; }
    QString phaseName(int ph) const { return tl().phaseFullName(ph); }
    // A phase as the profile's own column tag when it is one of the checkpoints, else its P tag.
    QString tagOf(int ph, const Profile &p) const
    {
        for (const Checkpoint &c : p.cps) if (c.phase == ph && !c.tag.isEmpty()) return c.tag;
        const QString t = tl().phaseShortTag(ph);
        return t.isEmpty() ? phaseName(ph) : t;
    }
};

qint64 phaseUsOf(const ShotSeries &s, int ph)
{
    for (const PhaseMark &p : s.phases) if (p.phase == ph) return p.tUs;
    return -1;
}

// ── the verdicts of one shot (the diagnostics' wrist conditions) ───────────────────────────

struct Verdicts {
    bool graded = false;                         // some wrist condition was assessed on the shot
    bool refused = false;                        // a gate refused (or bounded) one of the shot's wrist readings
    std::vector<const Cond *> fired, clean;
    pa::WristDomainScoreResult score;
};

QString sessionTier(const Ctx &c, const QString &id, bool fired)
{
    const QString t = c.snap ? c.snap->tiers.value(id) : QString();
    if (!t.isEmpty()) return t;
    return fired ? QStringLiteral("watching") : QStringLiteral("clean");
}

double confidenceOf(const ShotSeries &s, const QString &id)
{
    const auto it = s.diagnostics.values.constFind(id);
    return it != s.diagnostics.values.constEnd() && std::isfinite(it->confidence) ? it->confidence : 1.0;
}

struct WristGate;
WristGate wristGate(const ShotSeries *s, const QString &dofId, const QString &key, double atMs);
bool wristRefused(const ShotSeries &s);

Verdicts verdictsOf(const Ctx &c, const ShotSeries &s)
{
    Verdicts v;
    v.refused = wristRefused(s);
    std::vector<pa::WristDomainVerdict> vs;
    for (const Cond &k : c.w.conds) {
        const bool f = s.diagnostics.fired.contains(k.id);
        const bool cl = !f && s.diagnostics.clean.contains(k.id);
        if (!f && !cl) continue;                 // not assessed on this shot: not a verdict
        (f ? v.fired : v.clean).push_back(&k);
        pa::WristDomainVerdict d;
        d.conditionId = k.id;
        d.label = k.label;
        // the SHOT's verdict on the session's scale: fired → pattern when the session has made a
        // pattern of it, else watching; clean costs nothing
        d.tier = f ? (sessionTier(c, k.id, true) == QLatin1String("pattern") ? QStringLiteral("pattern") : QStringLiteral("watching"))
                   : QStringLiteral("clean");
        d.confidence = confidenceOf(s, k.id);
        d.fired = f;
        vs.push_back(d);
    }
    v.graded = !vs.empty();
    v.score = pa::scoreWristDomain(vs);
    return v;
}

// The phase a condition is read at on this shot: the reading that drove it, else its first.
int condPhase(const ShotSeries &s, const Cond &k)
{
    const auto it = s.diagnostics.drivenBy.constFind(k.id);
    if (it != s.diagnostics.drivenBy.constEnd() && it->phase >= 0) return it->phase;
    for (const pa::ConditionReading &r : k.readings) if (r.found && r.phase >= 0) return r.phase;
    return -1;
}
QString condKey(const ShotSeries &s, const Cond &k)
{
    const auto it = s.diagnostics.drivenBy.constFind(k.id);
    if (it != s.diagnostics.drivenBy.constEnd() && !it->metricKey.isEmpty()) return it->metricKey;
    for (const pa::ConditionReading &r : k.readings) if (r.hasKey()) return r.metricKey;
    return QString();
}

WU::Finding findingOf(const Ctx &c, const ShotSeries &s, const Profile &p, const Cond &k, const Verdicts &v, bool strength,
                      const QStringList &corroborated)
{
    const int ph = condPhase(s, k);
    const QString tag = ph >= 0 ? c.tagOf(ph, p) : QString();
    const qint64 seek = ph >= 0 ? phaseUsOf(s, ph) : -1;
    const ChartShotFindings::Value val = s.diagnostics.values.value(k.id);
    const QString key = condKey(s, k);
    const MetricSeries *ms = key.isEmpty() ? nullptr : s.metric(key);
    const QString unit = ms ? ms->unit : QString();
    double outBy = kNaN;
    if (std::isfinite(val.value) && std::isfinite(val.lo) && std::isfinite(val.hi))
        outBy = val.value > val.hi ? val.value - val.hi : val.value < val.lo ? val.lo - val.value : 0.0;
    const QString tier = sessionTier(c, k.id, !strength);
    // a finding speaks in the SESSION's tier words, as Session diagnostics does ("pattern" / "watching");
    // the cells under it keep the band's own grade (fault / watch) — the caption says which is which
    const QString severity = strength ? QStringLiteral("good") : tier == QLatin1String("pattern") ? QStringLiteral("pattern") : QStringLiteral("watching");
    int lost = 0;
    for (const pa::WristDomainContribution &ct : v.score.contributions) if (ct.id == k.id) { lost = int(std::lround(ct.penalty)); break; }
    const double conf = confidenceOf(s, k.id);
    // Golfer-facing only: what it costs (golferWhy), else the fault said plainly (golfer). The pack has no
    // golfer-facing coaching field (`drills` are ids, `consequence` is the author's mechanics note), so
    // coaching stays empty rather than showing either.
    const QString explanation = strength ? QString() : (k.golferWhy.isEmpty() ? W::cap1(k.golfer) : k.golferWhy);
    const QString coaching;
    const QString protect = strength ? k.golferWhy : QString();
    QList<WU::More> more;
    const auto addMore = [&](const QString &kk, const QString &vv) {
        if (!vv.isEmpty()) more.append(WU::More{ kk, vv });
    };
    addMore(strength ? QStringLiteral("Protect") : QStringLiteral("Coaching"), strength ? protect : coaching);
    addMore(QStringLiteral("Linked to"), k.linked.join(QStringLiteral(", ")));
    addMore(QStringLiteral("Corroborated by"), corroborated.join(QStringLiteral(", ")));
    const int mag = std::isfinite(outBy) && unit == QStringLiteral("°") ? int(std::lround(outBy)) : 0;
    WU::Finding f;
    f.id = k.id;
    f.name = strength ? W::cap1(k.golferWell) : k.label;
    f.severity = severity;
    f.severityWord = strength ? QStringLiteral("working") : severity;
    f.tier = tier;
    f.pointsLost = lost;
    f.pointsText = lost > 0 ? QString(QChar(0x2212)) + QString::number(lost) + QStringLiteral(" points") : QString();
    f.positions = tag.isEmpty() ? QStringList{} : QStringList{ tag };
    f.positionsText = tag.isEmpty() ? QString() : QStringLiteral("at ") + tag;
    f.phase = ph;
    f.seekUs = seek;
    f.value = strength ? nullv() : nv(val.value);
    f.valueText = strength ? QString() : W::figUnit(val.value, unit);
    f.outBy = strength ? nullv() : nv(outBy);
    f.magnitude = strength ? 0 : mag;
    f.magnitudeText = !strength && mag > 0 ? degBare(mag) : QString();
    f.confidence = conf;
    f.pips = pips(conf);
    f.lowConfidence = conf < 0.5;
    f.explanation = explanation;
    f.coaching = coaching;
    f.protect = protect;
    f.linkedTo = k.linked;
    f.corroboratedBy = corroborated;
    f.more = more;
    return f;
}

// Findings (fired), severity then confidence then how far out; strengths (an axis clean on every tail
// the shot assessed, said in its golfer words), in pack order.
void ledgersOf(const Ctx &c, const ShotSeries &s, const Profile &p, const Verdicts &v, QList<WU::Finding> *findings,
               QList<WU::Finding> *strengths)
{
    struct F { const Cond *k; bool fault; double conf, z; };
    std::vector<F> fs;
    for (const Cond *k : v.fired)
        fs.push_back({ k, sessionTier(c, k->id, true) == QLatin1String("pattern"), confidenceOf(s, k->id),
                       std::abs(s.diagnostics.values.value(k->id).z) });
    std::stable_sort(fs.begin(), fs.end(), [](const F &a, const F &b) {
        if (a.fault != b.fault) return a.fault;
        if (a.conf != b.conf) return a.conf > b.conf;
        return (std::isfinite(a.z) ? a.z : 0.0) > (std::isfinite(b.z) ? b.z : 0.0);
    });
    for (const F &f : fs) {
        const int ph = condPhase(s, *f.k);
        QStringList corr;
        for (const Cond *o : v.fired)
            if (o != f.k && ph >= 0 && condPhase(s, *o) == ph) corr << o->label;
        findings->append(findingOf(c, s, p, *f.k, v, false, corr));
    }
    QStringList seenAxis;
    for (const Cond &k : c.w.conds) {
        const QString axis = k.axis.isEmpty() ? k.id : k.axis;
        if (seenAxis.contains(axis)) continue;
        seenAxis << axis;
        bool anyFired = false, anyClean = false;
        const Cond *speak = nullptr;
        for (const Cond &t : c.w.conds) {
            if ((t.axis.isEmpty() ? t.id : t.axis) != axis) continue;
            if (std::find(v.fired.begin(), v.fired.end(), &t) != v.fired.end()) anyFired = true;
            if (std::find(v.clean.begin(), v.clean.end(), &t) != v.clean.end()) {
                anyClean = true;
                if (!speak && !t.golferWell.isEmpty()) speak = &t;
            }
        }
        if (anyFired || !anyClean || !speak) continue;
        strengths->append(findingOf(c, s, p, *speak, v, true, {}));
    }
}

// ── the profile rows ────────────────────────────────────────────────────────────────────────

QList<WU::Position> positionsOf(const Ctx &c, const Profile &p)
{
    QList<WU::Position> out;
    for (int i = 0; i < kP; ++i) {
        const Checkpoint &k = p.cps[size_t(i)];
        WU::Position pos;
        pos.index = i;
        pos.tag = k.tag.isEmpty() ? QStringLiteral("P%1").arg(i + 1) : k.tag;
        pos.phase = k.phase;
        pos.name = k.phase >= 0 ? c.phaseName(k.phase) : QString();
        pos.note = k.note;
        pos.available = k.available;
        pos.tUs = k.tUs;
        pos.seekUs = k.available ? k.tUs : -1;
        out.append(pos);
    }
    return out;
}

// THE GATE BEHIND AN EMPTY WRIST VALUE (trust stage 6 session 2): a refusing gate whose record names the
// DOF's reading (its subject or a cascade's covers: the metric key, or any key that is this DOF), else the
// mask whose stretch holds the checkpoint (wrist.cellRefused: the positions it could not read). Only ever
// asked of a value that is missing — masks do not label values. Empty on an old swing (gatesVersion 0).
struct WristGate { QString words, subject, why; };   // why: the refusing gate's evidence, said in full in TRUST
bool namesDof(const QString &k, const QString &dofId, const QString &key)
{
    if (k.isEmpty()) return false;
    if (!key.isEmpty() && k == key) return true;
    const auto d = pa::dofForMetricKey(k);
    return d && QString::fromLatin1(pa::dofName(*d)) == dofId;
}
WristGate wristGate(const ShotSeries *s, const QString &dofId, const QString &key, double atMs)
{
    WristGate mask;
    if (!s || s->gatesVersion <= 0 || s->gates.isEmpty()) return mask;
    for (const QVariant &rv : s->gates) {
        const QVariantMap r = rv.toMap();
        const QString subject = r.value(QStringLiteral("subject")).toString();
        QStringList ks = r.value(QStringLiteral("covers")).toStringList();
        ks.prepend(subject);
        if (std::none_of(ks.cbegin(), ks.cend(), [&](const QString &k) { return namesDof(k, dofId, key); })) continue;
        const gates::GateEntry *e = gates::gateEntry(r.value(QStringLiteral("gate")).toString());
        if (!e) continue;
        const QString eff = r.value(QStringLiteral("effect")).toString();
        if (eff == QLatin1String("refused") || eff == QLatin1String("bounded"))
            return WristGate{ gates::claimWords(*e), subject, gates::evidenceText(*e, r.value(QStringLiteral("evidence")).toMap()) };
        if (eff == QLatin1String("masked") && mask.words.isEmpty() && std::isfinite(atMs)) {
            const QVariantMap ev = r.value(QStringLiteral("evidence")).toMap();
            if (!ev.contains(QStringLiteral("fromMs")) || !ev.contains(QStringLiteral("toMs"))) continue;
            const double a = ev.value(QStringLiteral("fromMs")).toDouble(), b = ev.value(QStringLiteral("toMs")).toDouble();
            if (atMs >= std::min(a, b) - 2.0 && atMs <= std::max(a, b) + 2.0) mask = WristGate{ e->shortText, subject, QString() };
        }
    }
    return mask;
}
// A GATE REFUSED THE WRIST (Mark, 17 Oct 2026): any wrist reading a gate refused or bounded leaves the shot with no
// wrist score — what is left (a camera fallback, the trail wrist) would score the swing on the readings it kept
// and call a refused lead wrist "good". The reason is said where the reading is missing, not in the score.
bool wristRefused(const ShotSeries &s)
{
    for (const QString &id : dofOrder())
        if (!wristGate(&s, id, QString(), kNaN).words.isEmpty()) return true;
    return false;
}
// A checkpoint's instant about impact (ms), NaN when the swing has no impact or no such checkpoint.
double checkpointMs(const ShotSeries &s, const Profile &p, int i)
{
    const Checkpoint &k = p.cps[size_t(i)];
    return k.available && k.tUs >= 0 && s.impactUs >= 0 ? double(k.tUs - s.impactUs) / 1000.0 : kNaN;
}
WU::Point pointOf(const Dof &d, const Cell &c, bool shotScope)
{
    const bool out = shotScope && c.out() && c.banded;
    const bool high = out && c.delta > c.hi;
    const double by = out ? (high ? c.delta - c.hi : c.lo - c.delta) : kNaN;
    const QString pole = out ? (high ? d.poleTop : d.poleBottom) : QString();
    const QString sev = c.rag == QLatin1String("red") ? QStringLiteral("fault") : QStringLiteral("watch");
    WU::Point pt;
    pt.available = c.available;
    pt.value = nv(c.delta);
    pt.valueText = c.available ? degText(c.delta) : QStringLiteral("-");
    pt.rag = c.rag;
    pt.banded = c.banded;
    pt.bandLo = nv(c.lo);
    pt.bandHi = nv(c.hi);
    pt.out = out;
    pt.fault = out && c.rag == QLatin1String("red");
    pt.watch = out && c.rag == QLatin1String("amber");
    pt.outBy = nv(by);
    pt.outPole = pole;
    pt.word = out ? sev + QStringLiteral(" · ") + degBare(by) + QLatin1Char(' ') + pole : QString();
    pt.wordShort = out ? sev + QLatin1Char(' ') + pole : QString();
    return pt;
}

// The 40 ms windowed curve behind a row: the shot's own curve, its time stretched piecewise-linearly
// between the checkpoint ticks (x = the checkpoint index) and offset so it passes the first measured
// checkpoint's Δ (the profile's numbers are Δ from address; the curve is the raw angle).
QList<WU::TracePoint> traceFor(const ShotSeries &s, const Profile &p, const Dof &d)
{
    if (s.impactUs < 0 || d.key.isEmpty()) return {};
    const TraceCurve curve = shotCurve(s, d.key);
    if (curve.empty()) return {};
    std::vector<WarpAnchor> an;
    double last = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < kP; ++i) {
        const Checkpoint &k = p.cps[size_t(i)];
        if (!k.available) continue;
        const double ms = double(k.tUs - s.impactUs) / 1000.0;
        if (!(ms > last)) continue;              // a ladder that runs backwards is not a time map
        an.push_back(WarpAnchor{ k.phase, Landmark::None, ms, double(i) });
        last = ms;
    }
    if (an.size() < 2) return {};
    double off = kNaN;
    for (const WarpAnchor &a : an) {
        const Cell &cell = d.cells[size_t(std::lround(a.canonMs))];
        if (!cell.available) continue;
        const double v = valueAt(curve, a.shotMs);
        if (std::isfinite(v)) { off = cell.delta - v; break; }
    }
    if (!std::isfinite(off)) return {};
    const ShotWarp warp = makeWarp(an, WarpInterp::Linear);
    std::vector<size_t> idx;
    for (size_t i = 0; i < curve.t.size(); ++i)
        if (curve.t[i] >= an.front().shotMs && curve.t[i] <= an.back().shotMs) idx.push_back(i);
    const size_t stride = std::max<size_t>(1, idx.size() / 400);
    QList<WU::TracePoint> out;
    for (size_t j = 0; j < idx.size(); j += stride) {
        const size_t i = idx[j];
        const bool ok = curve.ok[i] && std::isfinite(curve.v[i]);
        out.append(WU::TracePoint{ warp.warpMs(curve.t[i]), ok ? QVariant(curve.v[i] + off) : nullv() });
    }
    return out;
}

struct Scale {
    double lo = 0.0, hi = 0.0;
    void add(double v) { if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); } }
    void finish()
    {
        if (hi - lo < 10) { const double m = (hi + lo) / 2; lo = m - 5; hi = m + 5; }
        const double pad = 0.08 * (hi - lo);
        lo -= pad; hi += pad;
    }
};

// ── the session's aggregates ────────────────────────────────────────────────────────────────

struct DofAgg {
    QString id;
    std::array<std::vector<double>, kP> values;   // per checkpoint, one per shot (NaN = none)
    std::array<BoxStats, kP> box;
};

std::vector<DofAgg> aggregates(const Ctx &c)
{
    std::vector<DofAgg> out;
    for (const QString &id : dofOrder()) {
        DofAgg a;
        a.id = id;
        bool any = false;
        for (int p = 0; p < kP; ++p) a.values[size_t(p)].assign(size_t(c.n), kNaN);
        for (int i = 0; i < c.n; ++i) {
            const Dof *d = c.profs[size_t(i)].dof(id);
            if (!d || !d->hasStrip) continue;
            any = true;
            for (int p = 0; p < kP; ++p) a.values[size_t(p)][size_t(i)] = d->cells[size_t(p)].delta;
        }
        if (!any) continue;
        for (int p = 0; p < kP; ++p) a.box[size_t(p)] = boxStats(a.values[size_t(p)]);
        out.push_back(std::move(a));
    }
    return out;
}

const DofAgg *aggOf(const std::vector<DofAgg> &&, const QString &) = delete;   // a temporary would leave the result dangling (#247, #248)
const DofAgg *aggOf(const std::vector<DofAgg> &aggs, const QString &id)
{
    for (const DofAgg &a : aggs) if (a.id == id) return &a;
    return nullptr;
}

double medianAt(const DofAgg *a, int p) { return a && a->box[size_t(p)].n >= 3 ? a->box[size_t(p)].med : kNaN; }

// ── This shot ───────────────────────────────────────────────────────────────────────────────

QString emptyTail()
{
    return QStringLiteral(" The Wrist view reads lead and trail wrist angles at the eight checkpoints against a band; "
                          "it needs a wrist sensor or a face-on camera's hands.");
}

WU::Style styleOf(const Profile &p)
{
    WU::Style m;
    const bool has = p.resemblance.contains(QStringLiteral("neutral"));
    const bool byHand = p.mode >= 0;
    QList<WU::StyleItem> items;
    W::Sentence line;
    static const char *const keys[] = { "neutral", "bowed", "cupped" };
    if (has) line.append(QStringLiteral("Resembles "));
    else line.append(QStringLiteral("Style resemblance: - (this swing’s analysis carries no resemblance index); "));
    for (int i = 0; i < 3; ++i) {
        const QString k = QLatin1String(keys[i]);
        const QVariant pv = p.resemblance.value(k);
        const QString pct = has ? W::withUnit(QString::number(pv.toInt()), QStringLiteral("%")) : QStringLiteral("-");
        items.append(WU::StyleItem{ k, wristArchetypeName(i), has ? pv : nullv(), pct, p.archetype == i });
        if (has) {
            if (i > 0) line.append(QStringLiteral(" · "));
            line.append(wristArchetypeName(i) + QLatin1Char(' ')).figure(pct);
        }
    }
    if (p.pending) line.append(has ? QStringLiteral("; the style is read with the checkpoints, not graded yet.")
                                   : QStringLiteral("the style is read with the checkpoints, not graded yet."));
    else if (has) line.append(byHand ? QStringLiteral("; bands set to %1 by hand.").arg(p.archetypeName)
                                     : QStringLiteral("; the bands are %1’s.").arg(p.archetypeName));
    else line.append(byHand ? QStringLiteral("bands set to %1 by hand.").arg(p.archetypeName)
                            : QStringLiteral("Auto resolved the bands to %1.").arg(p.archetypeName));
    m.hasIndex = has;
    m.archetype = p.archetype;
    m.archetypeName = p.archetypeName;
    m.mode = p.mode;
    m.byHand = byHand;
    m.items = items;
    m.line = sentenceOf(line);
    return m;
}

WU::Score scoreOf(const Verdicts &v, const Profile &p)
{
    WU::Score m;
    const bool ok = v.graded && !v.refused && v.score.score >= 0;
    double pen = 0.0;
    QList<WU::Contribution> cs;
    for (const pa::WristDomainContribution &c : v.score.contributions) {
        pen += c.penalty;
        const int pr = int(std::lround(c.penalty));
        cs.append(WU::Contribution{ c.id, c.label, pr, QString(QChar(0x2212)) + QString::number(pr) });
    }
    const QString sum = ok ? QStringLiteral("%1 %2 %3 = %4").arg(v.score.base).arg(QChar(0x2212)).arg(int(std::lround(pen))).arg(v.score.score) : QString();
    m.score = ok ? QVariant(v.score.score) : nullv();
    m.scoreText = ok ? QString::number(v.score.score) : QStringLiteral("-");
    m.band = ok ? v.score.band : QString();
    m.base = v.score.base;
    m.sumText = sum;
    m.contributions = ok ? cs : QList<WU::Contribution>();
    m.line = ok ? QStringLiteral("Wrist score %1 of 100, %2  ·  %3").arg(v.score.score).arg(v.score.band, sum)
                : v.refused ? QStringLiteral("Wrist score -")
                : v.graded ? QStringLiteral("Wrist score -: no wrist condition could be read on this swing")
                           : QStringLiteral("Wrist score -: diagnostics have not graded this swing yet");
    // the folded section's state (PpChWristShotSection's fold head): the score as the head would say it
    m.foldText = m.scoreText.isEmpty() ? QString() : QStringLiteral("score ") + m.scoreText;
    (void)p;
    return m;
}

// ── How far can I trust this? (TRUST, said once; the shape of the Posture presenter's) ──────────

// Where a row's reading comes from, in the golfer's words: the instrument, never a product's name.
QString sourceWords(const QString &src)
{
    if (src == QLatin1String("IMU"))        return QStringLiteral("the wrist sensor");
    if (src == QLatin1String("IMU + pose")) return QStringLiteral("the wrist sensor and the face-on camera");
    if (src.startsWith(QLatin1String("pose"))) return QStringLiteral("the face-on camera");
    return src.isEmpty() ? QString() : src.toLower();
}
// The same without the article, for a row's label on paper ("wrist sensor, °").
QString sourceLabel(const QString &src)
{
    QString w = sourceWords(src);
    w.replace(QStringLiteral("the "), QString());
    return w;
}

// "out of sight: the hands left the frame" — a gate's words, then its evidence lower-cased, no stop.
QString gateReason(const WristGate &g)
{
    QString w = g.why.trimmed();
    while (w.endsWith(QLatin1Char('.'))) w.chop(1);
    if (!w.isEmpty() && !(w.size() > 1 && w.at(1).isUpper())) w = w.left(1).toLower() + w.mid(1);
    if (g.words.isEmpty()) return w;
    return w.isEmpty() || w.compare(g.words, Qt::CaseInsensitive) == 0 ? g.words : g.words + QStringLiteral(": ") + w;
}

// WHERE THE BANDS COME FROM: the norm rows the cells resolve to (m_<dof>_p<N>, P2–P8, in the style's context),
// by their provenance. Said plainly: an authored row is a coaching starting point, not a measurement.
QString bandSourceLine(const QStringList &dofIds, const QList<int> &styles)
{
    const std::shared_ptr<const pa::INormProvider> norms = pa::sharedNormProvider();
    if (!norms) return QString();
    bool heuristic = false, literature = false, seated = false, measured = false, imported = false, any = false;
    for (const QString &id : dofIds) {
        const auto dof = pa::dofForMetricKey(id);
        if (!dof) continue;
        for (int style : styles) {
            pa::BandContext ctx;
            ctx.archetype = style;
            for (int p = 1; p < kP; ++p) {
                const pa::NormResolution res = norms->resolve(pa::NormBandProvider::cellMeasureId(*dof, pa::PpSwingPosition(p)), ctx.resolvedContextId());
                if (!res.found()) continue;
                any = true;
                switch (res.norm->source) {
                case pa::NormSource::Heuristic:  heuristic = true; break;
                case pa::NormSource::Literature: literature = true; break;
                case pa::NormSource::Seated:     seated = true; break;
                case pa::NormSource::Measured:   measured = true; break;
                case pa::NormSource::Imported:   imported = true; break;
                }
            }
        }
    }
    if (!any) return QStringLiteral("No band resolves for these readings yet, so no checkpoint is graded against one.");
    QStringList from;
    if (literature) from << QStringLiteral("published measurement");
    if (seated) from << QStringLiteral("swings marked as well placed");
    if (measured) from << QStringLiteral("readings measured across the library");
    if (imported) from << QStringLiteral("another set of bands");
    if (from.isEmpty()) return QStringLiteral("The bands are coaching starting points; they have not yet been measured on good players.");
    return QStringLiteral("The bands come from ") + W::list(from)
           + (heuristic ? QStringLiteral("; the rest are coaching starting points, not yet measured on good players.") : QStringLiteral("."));
}

// "on 6 of 6 swings" | "on every swing"
QString onCount(const Ctx &c, int k, int of)
{
    if (k == of && of > 1) return QStringLiteral("on every ") + c.one;
    return QStringLiteral("on %1 of %2 %3").arg(k).arg(of).arg(of == 1 ? c.one : c.many);
}

// ONE HELPER for both scopes (`shot` given: This shot; else the session, in the noun). Only lines that are true.
//   This shot: each DOF's source and confidence; each DOF not measured, with the gate's FULL reason (the
//   in-place words stay short); not graded yet; a gate refused the wrist (no score); the bands' style and
//   source; no resemblance index.
//   Session: each row's source and confidence on how many shots; no reading; a gate refused; not graded; how
//   the shots are graded (by style), the report's wash (washStyle) when it shades one; the bands' source; no
//   index; a small session (the standfirst's size words).
WU::Trust trustOf(const Ctx &c, const ShotSeries *shot, const Profile *p, const Verdicts *v,
                  const std::vector<Verdicts> *all, const QString &sizeWords)
{
    QStringList lines;
    const bool byHand = c.in.archetype >= 0;
    if (shot && p && v) {
        QStringList present;
        for (const Dof &d : p->dofs) {
            present << d.id;
            const QString src = sourceWords(d.source);
            lines << W::cap1(longName(d.id)) + QStringLiteral(": ") + (src.isEmpty() ? QStringLiteral("source not recorded") : QStringLiteral("from ") + src)
                         + QStringLiteral(", confidence ") + pips(d.conf) + (d.hasStrip ? QString() : QStringLiteral(" (no band: the angle only)")) + QLatin1Char('.');
        }
        const bool forearmRotation = shot->metric(QStringLiteral("hm.forearmRotation")) || shot->metric(QStringLiteral("forearmRotation"));
        for (const QString &id : dofOrder()) {
            if (present.contains(id) || (id == QLatin1String("leadForearmRot") && forearmRotation)) continue;
            const WristGate g = wristGate(shot, id, QString(), kNaN);
            const QString why = gateReason(g);
            lines << W::cap1(longName(id)) + QStringLiteral(": not measured on this swing") + (why.isEmpty() ? QString() : QStringLiteral(" — ") + why) + QLatin1Char('.');
        }
        if (v->refused) lines << QStringLiteral("No wrist score on this swing: a check refused one of its wrist readings, and the score is not made from the rest.");
        else if (!v->graded) lines << QStringLiteral("The diagnostics have not graded this swing yet, so it has no wrist score and no findings.");
        if (p->pending) {
            // no profile in the findings yet: the reason, and no band words (nothing was graded against one)
            lines << p->pendingWhy;
            return WU::Trust{ lines };
        }
        // Auto resolves the style off the lead wrist's bow / cup at the top: without it there is nothing to resolve
        const bool fe = p->dof(QStringLiteral("leadWristFlexExt")) != nullptr;
        lines << (byHand ? QStringLiteral("The bands are the %1 style's, set by hand.").arg(p->archetypeName)
                  : fe ? QStringLiteral("The bands are the %1 style's, the style Auto resolved this swing to.").arg(p->archetypeName)
                       : QStringLiteral("This swing has no lead wrist bow / cup reading, so Auto has no style to resolve and the bands are the %1 style's.").arg(p->archetypeName));
        QStringList ids;
        for (const Dof &d : p->dofs) if (d.hasStrip) ids << d.id;
        const QString src = bandSourceLine(ids, { p->archetype });
        if (!src.isEmpty()) lines << src;
        if (!p->resemblance.contains(QStringLiteral("neutral")))
            lines << (byHand ? QStringLiteral("This swing's analysis carries no resemblance index, so there is no style to set beside the bands chosen by hand.")
                      : fe ? QStringLiteral("This swing's analysis carries no resemblance index, so its style is the one Auto resolved from its own readings.")
                           : QStringLiteral("This swing's analysis carries no resemblance index."));
        return WU::Trust{ lines };
    }

    // ── the session ──
    QStringList ids;
    QList<int> styles;
    for (const QString &id : dofOrder()) {
        // the source(s) of each banded row, on how many shots, and the median confidence
        QMap<QString, int> bySource;
        std::vector<double> confs;
        int k = 0;
        for (const Profile &pf : c.profs) {
            const Dof *d = pf.has ? pf.dof(id) : nullptr;
            if (!d || !d->hasStrip) continue;
            ++k;
            ++bySource[sourceWords(d->source)];
            confs.push_back(d->conf);
        }
        if (k == 0) continue;
        ids << id;
        std::vector<std::pair<QString, int>> sl;
        for (auto it = bySource.cbegin(); it != bySource.cend(); ++it) sl.emplace_back(it.key(), it.value());
        std::stable_sort(sl.begin(), sl.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        QString from;
        if (sl.size() == 1) from = (sl.front().first.isEmpty() ? QStringLiteral("source not recorded") : QStringLiteral("from ") + sl.front().first) + QLatin1Char(' ') + onCount(c, k, c.n);
        else {
            QStringList parts;
            for (const auto &x : sl) parts << (x.first.isEmpty() ? QStringLiteral("unrecorded") : x.first) + QStringLiteral(" on %1").arg(x.second);
            from = QStringLiteral("from ") + W::list(parts) + QStringLiteral(" of %1 %2").arg(c.n).arg(c.n == 1 ? c.one : c.many);
        }
        std::sort(confs.begin(), confs.end());
        const double med = confs.size() % 2 ? confs[confs.size() / 2] : 0.5 * (confs[confs.size() / 2 - 1] + confs[confs.size() / 2]);
        lines << W::cap1(longName(id)) + QStringLiteral(": ") + from + QStringLiteral(", confidence ") + pips(med)
                     + (confs.size() > 1 ? QStringLiteral(" (the median)") : QString()) + QLatin1Char('.');
    }
    // a shot with wrist readings whose findings hold no profile yet is NOT a shot with no reading (3.8-ii, REVIEW-ii B2)
    if (c.ready + c.waiting < c.n)
        lines << QStringLiteral("No wrist reading %1.").arg(onCount(c, c.n - c.ready - c.waiting, c.n));
    if (c.waiting > 0) {
        QString why = c.snap ? c.snap->wristPendingWhy : QString();
        if (!why.isEmpty()) why[0] = why[0].toLower();
        lines << QStringLiteral("Not graded yet %1").arg(onCount(c, c.waiting, c.n))
                     + (why.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + why);
    }
    int refused = 0, ungraded = 0;
    QStringList refusedWhat;
    for (int i = 0; all && i < c.n && i < int(all->size()); ++i) {
        const Verdicts &vv = (*all)[size_t(i)];
        if (!c.profs[size_t(i)].has) continue;
        if (vv.refused) {
            ++refused;
            for (const QString &id : dofOrder()) {
                const WristGate g = wristGate(c.snap->shots[size_t(i)].get(), id, QString(), kNaN);
                const QString w = g.words.isEmpty() ? QString() : longName(id) + QStringLiteral(": ") + g.words;
                if (!w.isEmpty() && !refusedWhat.contains(w)) refusedWhat << w;
            }
        } else if (!vv.graded) ++ungraded;
    }
    if (refused)
        lines << QStringLiteral("No wrist score %1: a check refused a wrist reading%2.").arg(onCount(c, refused, c.ready))
                     .arg(refusedWhat.isEmpty() ? QString() : QStringLiteral(" (") + refusedWhat.join(QStringLiteral("; ")) + QLatin1Char(')'));
    if (ungraded)
        lines << QStringLiteral("No wrist score yet %1: the diagnostics have not graded %2.").arg(onCount(c, ungraded, c.ready),
                                                                                             ungraded == 1 ? QStringLiteral("it") : QStringLiteral("them"));
    // how the shots are graded, by style
    QMap<int, int> byStyle;
    for (const Profile &pf : c.profs) if (pf.has) ++byStyle[pf.archetype];
    for (auto it = byStyle.cbegin(); it != byStyle.cend(); ++it) styles << it.key();
    // Auto resolves each style off the lead wrist's bow / cup at the top: a session without it has nothing to resolve
    bool anyFe = false;
    for (const Profile &pf : c.profs) anyFe = anyFe || (pf.has && pf.dof(QStringLiteral("leadWristFlexExt")));
    if (!byHand && !anyFe) {
        lines << (c.paper ? QStringLiteral("No %1 reads the lead wrist's bow / cup, so every %1 is graded against the %2 band.")
                          : QStringLiteral("No %1 reads the lead wrist's bow / cup, so Auto has no style to resolve: every %1 is graded against the %2 band."))
                     .arg(c.one, wristArchetypeName(byStyle.isEmpty() ? 0 : byStyle.firstKey()));
    } else if (byHand) {
        lines << QStringLiteral("Every %1 is graded against the %2 band, set by hand.").arg(c.one, wristArchetypeName(c.in.archetype));
    } else if (byStyle.size() == 1) {
        lines << (c.paper ? QStringLiteral("Every %1 with a wrist reading is graded as a %2 wrist, against the %2 band.")
                          : QStringLiteral("Auto resolved every %1 with a wrist reading to %2, so each is graded against the %2 band."))
                     .arg(c.one, wristArchetypeName(byStyle.firstKey()));
    } else {
        QStringList parts;
        for (int s = 0; s < 3; ++s) if (byStyle.contains(s)) parts << QStringLiteral("%1 on %2").arg(wristArchetypeName(s)).arg(byStyle.value(s));
        lines << (c.paper ? QStringLiteral("Each %1 is graded against the band of its own style: %2.")
                          : QStringLiteral("Each %1 is graded against the band of the style Auto resolved it to: %2.")).arg(c.one, W::list(parts));
    }
    // the report's wash (the dominant style): said only when swings were graded against more than one style (one
    // style, by hand or with no bow / cup reading, is said by the line above and the caption); a tie is said plainly
    if (c.washStyle >= 0 && anyFe && !byHand && byStyle.size() > 1) {
        QStringList tied;
        for (int s = 0; s < 3; ++s) if (byStyle.value(s) == byStyle.value(c.washStyle)) tied << wristArchetypeName(s);
        lines << (tied.size() > 1 ? QStringLiteral("%1 were tied; the %2 band is shaded.").arg(W::list(tied), wristArchetypeName(c.washStyle))
                                  : QStringLiteral("The shaded band is the %1 style's, the style most %2 were graded as.").arg(wristArchetypeName(c.washStyle), c.many));
    }
    const QString src = bandSourceLine(ids, styles);
    if (!src.isEmpty()) lines << src;
    bool anyIndex = false;
    for (const Profile &pf : c.profs) anyIndex = anyIndex || (pf.has && pf.resemblance.contains(QStringLiteral("neutral")));
    if (!anyIndex)
        lines << (byHand ? QStringLiteral("No %1 in this session carries a resemblance index, so there is no style to set beside the bands chosen by hand.").arg(c.one)
                  : anyFe ? (c.paper ? QStringLiteral("No %1 in this session carries a resemblance index, so each %1's style is read from its own wrist readings.")
                                     : QStringLiteral("No %1 in this session carries a resemblance index, so each %1's style is the one Auto resolved from its own readings.")).arg(c.one)
                          : QStringLiteral("No %1 in this session carries a resemblance index, so there is no style to compare.").arg(c.one));
    if (c.n < 10 && !sizeWords.isEmpty()) lines << sizeWords;
    return WU::Trust{ lines };
}

void buildThisShot(Ctx &c, const std::vector<DofAgg> &aggs, ChartsWristPresenter::Result &R)
{
    WU::ThisShot out;
    R.cpUs.fill(-1);
    for (int i = 0; i < kP; ++i) R.tags << QStringLiteral("P%1").arg(i + 1);
    const bool has = c.shot && c.pick.shown();
    out.available = has;
    if (!has) {
        out.empty = (c.shot || !c.in.pickedSwingDir.trimmed().isEmpty()
                         ? QStringLiteral("No wrist metrics for this swing.") : QStringLiteral("Select a swing to review."))
                    + emptyTail();
        R.thisShot = out;
        return;
    }
    const ShotSeries &s = *c.shot;
    const Profile &p = c.pick;
    for (int i = 0; i < kP; ++i) { R.cpUs[size_t(i)] = p.cps[size_t(i)].available ? p.cps[size_t(i)].tUs : -1; R.tags[i] = p.cps[size_t(i)].tag; }
    const bool inSession = c.index >= 0;
    out.empty = QString();
    out.ordinal = s.ordinal;
    out.shotId = s.shotId;
    out.swingDir = s.swingDir;
    out.inSession = inSession;
    const QString club = s.club.toLower();
    out.kicker = (inSession ? W::shotOfText(s.shotId, s.ordinal, c.n) : QStringLiteral("This swing"))
                 + (club.isEmpty() ? QString() : QStringLiteral("  ·  ") + club) + QStringLiteral("  ·  Wrist");

    const Verdicts v = verdictsOf(c, s);
    QList<WU::Finding> findings, strengths;
    ledgersOf(c, s, p, v, &findings, &strengths);

    int outCells = 0;
    for (const Dof &d : p.dofs) if (d.hasStrip) for (const Cell &cell : d.cells) if (cell.out()) ++outCells;
    out.outCells = outCells;
    out.graded = v.graded;

    // THE HEAD: the first finding; or, with none, the cells said honestly (W-D6)
    W::Sentence head;
    const QString cells = QStringLiteral("%1 checkpoint%2").arg(outCells).arg(outCells == 1 ? QString() : QStringLiteral("s"));
    // a gate refused the wrist: no all-clear without the readings to back it (Mark, 17 Oct 2026); a finding still heads
    if (p.pending) {
        // NO PROFILE IN THE FINDINGS YET (J-58: show, never hide): the reason heads the section; the cells say it too
        QString why = p.pendingWhy;
        if (why.endsWith(QLatin1Char('.'))) why.chop(1);
        head.append(why);
    } else if (v.refused && findings.isEmpty()) {
    } else if (!v.graded) {
        if (outCells == 0) head.append(QStringLiteral("The wrist stayed inside its bands at every checkpoint; diagnostics have not graded this swing yet"));
        else head.figure(W::cap1(cells)).append(QStringLiteral(" sat outside a band; diagnostics have not graded this swing yet"));
    } else if (findings.isEmpty()) {
        if (outCells == 0) head.append(QStringLiteral("The wrist stayed inside its bands at every checkpoint"));
        else head.append(QStringLiteral("No finding fired; ")).figure(cells).append(outCells == 1 ? QStringLiteral(" sat outside a band") : QStringLiteral(" sat outside a band"));
    } else {
        const WU::Finding &f0 = findings.first();
        head.append(f0.name);
        const QStringList &pos = f0.positions;
        if (!pos.isEmpty()) head.append(QStringLiteral(" at ")).figure(pos.join(QStringLiteral(", ")));
        if (findings.size() > 1) head.append(QStringLiteral(", and ") + W::word(int(findings.size()) - 1) + QStringLiteral(" more"));
    }
    out.head = sentenceOf(head);

    // THE STANDFIRSTS: one per checkpoint, the live one by the playhead (GUI thread)
    for (int i = 0; i < kP; ++i) {
        const Checkpoint &k = p.cps[size_t(i)];
        W::Sentence st;
        const QString nm = k.phase >= 0 ? c.phaseName(k.phase).toLower() : QString();
        if (!k.available) {
            st.append(QStringLiteral("At %1, this swing has no %2 to read.").arg(k.tag, nm.isEmpty() ? QStringLiteral("checkpoint") : nm));
        } else {
            st.append(QStringLiteral("At %1, %2").arg(k.tag, nm) + (k.note.isEmpty() ? QString() : QStringLiteral(" (") + k.note + QStringLiteral(")")) + QStringLiteral(": "));
            bool first = true;
            for (const Dof &d : p.dofs) {
                if (!d.hasStrip) continue;
                const Cell &cell = d.cells[size_t(i)];
                if (!first) st.append(QStringLiteral(", "));
                first = false;
                // an empty cell a gate left: its words, quiet (a "-" else)
                const WristGate g = cell.available ? WristGate{} : wristGate(&s, d.id, d.key, checkpointMs(s, p, i));
                if (p.pending) st.append(shortName(d.id) + QStringLiteral(" not graded yet"));
                else if (!g.words.isEmpty()) st.append(shortName(d.id) + QLatin1Char(' ') + g.words);
                else st.append(shortName(d.id) + QLatin1Char(' ')).figure(cell.available ? degText(cell.delta) : QStringLiteral("-"));
                if (cell.out() && cell.banded)
                    st.append(QStringLiteral(" outside its band, ") + (cell.delta > cell.hi ? d.poleTop : d.poleBottom));
            }
            st.append(QStringLiteral(", from address."));
        }
        R.standfirsts.append(sentenceOf(st));
    }
    out.standfirsts = R.standfirsts;
    {
        // NO SCORE BECAUSE A GATE REFUSED THE WRIST: a plain "-"; the standfirst says what was out of sight and the
        // section's tail line names it (said once; Mark, 17 Oct 2026)
        out.score = scoreOf(v, p);
    }
    out.style = styleOf(p);

    // THE PROFILE
    const QString ag = c.in.against;
    const Profile *ghostProf = nullptr;
    QString ghostWord, ghostRefusal;
    bool ghostMedian = false;
    if (ag == QLatin1String("previous")) {
        const int pi = c.index >= 0 ? c.index - 1 : c.n - 1;
        if (pi >= 0 && pi < c.n && c.profs[size_t(pi)].has) { ghostProf = &c.profs[size_t(pi)]; ghostWord = QStringLiteral("previous swing"); }
        else ghostRefusal = QStringLiteral("The swing before this one has no wrist reading.");
    } else if (ag == QLatin1String("reference")) {
        if (!c.ref) ghostRefusal = c.refWhy;
        else if (c.refIsPicked) ghostRefusal = QStringLiteral("This swing is your reference ★.");
        else if (!c.refProf.has) ghostRefusal = QStringLiteral("The reference swing carries no wrist reading.");
        else { ghostProf = &c.refProf; ghostWord = QStringLiteral("the reference ★"); }
    } else if (ag == QLatin1String("median")) {
        if (c.ready >= 3) { ghostMedian = true; ghostWord = QStringLiteral("session median, n %1").arg(c.ready); }
        else ghostRefusal = QStringLiteral("Three shots with a wrist reading are needed for a session median; this session has %1.").arg(W::countWord(c.ready));
    }
    QList<WU::Row> rows;
    QStringList present;
    for (const Dof &d : p.dofs) {
        present << d.id;
        if (!d.hasStrip) {
            WU::Row row;
            row.numbersOnly = true;
            row.dof = d.id;
            row.key = d.key;
            row.name = d.name;
            row.sub = QStringLiteral("no corridor: the angle only");
            for (int i = 0; i < kP; ++i) {
                const Cell &cell = d.cells[size_t(i)];
                WU::NumberCell cm;
                cm.available = cell.available;
                cm.value = nv(cell.delta);
                cm.valueText = cell.available ? degText(cell.delta) : QStringLiteral("-");
                if (p.pending) { cm.valueText = QStringLiteral("not graded yet"); cm.trust = p.pendingWhy; }
                row.cells.append(cm);
            }
            rows.append(row);
            continue;
        }
        Scale sc;
        QList<WU::Point> points;
        int outCount = 0;
        for (int i = 0; i < kP; ++i) {
            const Cell &cell = d.cells[size_t(i)];
            WU::Point pm = pointOf(d, cell, true);
            // a cell the findings have not graded yet says so, with why (the QML's in-place words: valueText + trust)
            if (p.pending) { pm.valueText = QStringLiteral("not graded yet"); pm.trust = p.pendingWhy; }
            points.append(pm);
            sc.add(cell.delta);
            if (cell.banded) { sc.add(cell.lo); sc.add(cell.hi); }
            if (cell.out()) ++outCount;
        }
        QList<double> ghost;                     // empty: no ghost drawn
        std::vector<double> g(size_t(kP), kNaN);
        bool anyG = false;
        if (ghostProf) {
            if (const Dof *gd = ghostProf->dof(d.id))
                for (int i = 0; i < kP; ++i) { g[size_t(i)] = gd->cells[size_t(i)].delta; anyG = anyG || std::isfinite(g[size_t(i)]); }
        } else if (ghostMedian) {
            const DofAgg *a = aggOf(aggs, d.id);
            for (int i = 0; i < kP; ++i) { g[size_t(i)] = medianAt(a, i); anyG = anyG || std::isfinite(g[size_t(i)]); }
        }
        if (anyG) { ghost = numList(g); for (double x : g) sc.add(x); }
        const QList<WU::TracePoint> trace = c.in.curvesBehind ? traceFor(s, p, d) : QList<WU::TracePoint>{};
        for (const WU::TracePoint &t : trace) if (!t.v.isNull()) sc.add(t.v.toDouble());
        sc.finish();
        WU::Row row;
        row.numbersOnly = false;
        row.dof = d.id;
        row.key = d.key;
        row.name = d.name;
        row.sub = d.sub;
        row.source = d.source;
        row.confidence = d.conf;
        row.pips = pips(d.conf);
        row.sourceText = d.source.isEmpty() ? QString() : QString(d.source).replace(QLatin1String("IMU"), QLatin1String("motion sensor")) + QLatin1Char(' ') + pips(d.conf);
        row.shortText = shortName(d.id);
        row.poleTop = d.poleTop;
        row.poleBottom = d.poleBottom;
        row.poleTopText = d.poleTop.isEmpty() ? QString() : QStringLiteral("↑ ") + d.poleTop;
        row.poleBottomText = d.poleBottom.isEmpty() ? QString() : QStringLiteral("↓ ") + d.poleBottom;
        row.points = points;
        row.ghost = ghost;
        row.trace = trace;
        row.yLo = sc.lo;
        row.yHi = sc.hi;
        row.outCount = outCount;
        rows.append(row);
    }
    QStringList missing;
    // The lead forearm's ROLL (the radioulnar pronation strip) is a different quantity from the forearm's
    // rotation from address, which the shot may still carry and Beside your session lists: say which is
    // absent, and that the rotation is monitored, not graded (rule 2), rather than "not measured".
    const bool forearmRotation = s.metric(QStringLiteral("hm.forearmRotation")) || s.metric(QStringLiteral("forearmRotation"));
    bool rollNote = false;
    // each missing DOF a gate refused names the gate's words, and opens it (missingParts: the QML links them)
    QStringList missingNamed;
    QList<WU::MissingPart> missingParts;
    for (const QString &id : dofOrder()) {
        if (present.contains(id)) continue;
        if (id == QLatin1String("leadForearmRot") && forearmRotation) { rollNote = true; continue; }
        missing << longName(id);
        const WristGate g = wristGate(&s, id, QString(), kNaN);
        missingNamed << (g.words.isEmpty() ? longName(id) : longName(id) + QStringLiteral(" (") + g.words + QLatin1Char(')'));
        missingParts << WU::MissingPart{ longName(id), g.words, g.subject };
    }
    // the words before the DOFs not measured: said here once, for the plain line and for the QML's linked one
    const QString missingLead = missing.isEmpty() ? QString() : QStringLiteral("Not measured on this swing: ");
    QString missingText = missing.isEmpty() ? QString() : missingLead + missingNamed.join(QStringLiteral(", ")) + QStringLiteral(".");
    const QString rollText = rollNote ? QStringLiteral("The lead forearm's roll (pronation) has no strip here; its rotation from address is shown beside your session, monitored, not graded.")
                                      : QString();
    if (rollNote)
        missingText += (missingText.isEmpty() ? QString() : QStringLiteral(" ")) + rollText;
    QString sub = QStringLiteral("Δ from address, degrees");
    if (!ghostWord.isEmpty()) sub += QStringLiteral("  ·  dashed: ") + ghostWord;
    if (c.in.curvesBehind) sub += QStringLiteral("  ·  faint: the 40 ms curve, stretched between checkpoints");
    WU::Profile &pr = out.profile;
    pr.positions = positionsOf(c, p);
    pr.rows = rows;
    pr.ghostWord = ghostWord;
    pr.ghostRefusal = ghostRefusal;
    pr.sub = sub;
    // a profile not graded yet has no band to shade: the caption says why instead (J-58)
    pr.caption = p.pending ? QStringLiteral("No band is shaded: %1").arg(p.pendingWhy.isEmpty() ? QString() : p.pendingWhy.left(1).toLower() + p.pendingWhy.mid(1))
                           : QStringLiteral("Shaded: the %1 band at each checkpoint. A ring and a word under the value: outside it, "
                                            "graded by how far (fault, watch). A finding's word is the session's: pattern, it keeps "
                                            "firing across the session; watching, not yet.").arg(p.archetypeName);
    pr.missing = missing;
    pr.missingParts = missingParts;
    pr.missingLead = missingLead;
    pr.missingTail = rollText;
    pr.missingText = missingText;
    out.findings = findings;
    out.strengths = strengths;
    out.findingsEmpty = v.graded ? QStringLiteral("No faults and nothing to watch.") : QStringLiteral("Diagnostics have not graded this swing yet.");
    out.strengthsEmpty = QStringLiteral("Nothing stood out as a strength on this swing.");
    out.trust = trustOf(c, &s, &p, &v, nullptr, QString());
    R.thisShot = out;
}

// ── Session ─────────────────────────────────────────────────────────────────────────────────

// THE DOMINANT STYLE: the style most shots were graded as; a tie goes to the earlier of Neutral, Bowed, Cupped.
// With the style set by hand every shot is graded as it, so it is that style.
int dominantStyle(const Ctx &c)
{
    std::array<int, 3> graded{};
    for (const Profile &p : c.profs) if (p.has) ++graded[size_t(std::clamp(p.archetype, 0, 2))];
    int dom = 0;
    for (int s = 1; s < 3; ++s) if (graded[size_t(s)] > graded[size_t(dom)]) dom = s;
    return dom;
}

// The dominant style's band at a checkpoint: off a shot graded as it, else a shot carrying the row re-graded to it
// (`regraded` caches that re-grade per DOF). False where the style has no band there.
bool dominantBandAt(const Ctx &c, int dom, std::map<QString, Profile> &regraded, const QString &id, int p, double *lo, double *hi)
{
    for (const Profile &pf : c.profs) {
        if (!pf.has || pf.archetype != dom) continue;
        const Dof *d = pf.dof(id);
        if (!d || !d->hasStrip) continue;
        const Cell &cl = d->cells[size_t(p)];
        if (cl.banded && std::isfinite(cl.lo) && std::isfinite(cl.hi)) { *lo = cl.lo; *hi = cl.hi; return true; }
    }
    auto it = regraded.find(id);
    if (it == regraded.end()) {
        Profile rp;
        for (int i = 0; i < c.n; ++i)
            if (c.profs[size_t(i)].has && c.profs[size_t(i)].dof(id)) {
                const ShotSeries &s = *c.snap->shots[size_t(i)];
                rp = parseProfile(shotWristPayload(s, dom, c.snap ? c.snap->wristPendingWhy : QString()));
                break;
            }
        it = regraded.emplace(id, rp).first;
    }
    const Dof *d = it->second.dof(id);
    if (!d) return false;
    const Cell &cl = d->cells[size_t(p)];
    if (!(cl.banded && std::isfinite(cl.lo) && std::isfinite(cl.hi))) return false;
    *lo = cl.lo; *hi = cl.hi;
    return true;
}

// ── consistency (Mark's rulings R2b, R7, R11) ───────────────────────────────────────────────────
//
// Each DOF at each checkpoint from P2 on is graded per club: the club's shots' Δ from address there, against the
// band the panel draws for the session — the DOMINANT style's (the report shades it; the screen shades the picked
// shot's, which is that style whenever the picked shot was graded as it). The band is the Ideal band, μ − σLo … μ + σHi,
// so its σ, (σLo + σHi)/2, is its half-width; it goes to the grader as `variabilityScale`, so the basis reads
// "of scale". A checkpoint with no band is ungraded: no badge, out of the ranking. No propagated σ is known for a
// wrist cell, so none is passed. The grade words are the vocabularies' (QML); this file spells none.

namespace pv = pinpoint::variability;

struct WristClubGrades {
    QString club;
    int     shots = 0;    // the club's shots with a wrist reading: the order the clubs are named in
    std::map<QString, std::array<pv::VariabilityResult, kP>> result;   // by DOF id
    std::map<QString, std::array<bool, kP>> has;
};

std::vector<WristClubGrades> wristGrades(const Ctx &c, const std::vector<DofAgg> &aggs)
{
    std::vector<WristClubGrades> out;
    std::map<QString, int> count;
    for (int i = 0; i < c.n; ++i) if (c.profs[size_t(i)].has) ++count[c.snap->shots[size_t(i)]->club];
    for (const auto &[club, n] : count) { WristClubGrades g; g.club = club; g.shots = n; out.push_back(g); }
    std::stable_sort(out.begin(), out.end(), [](const WristClubGrades &a, const WristClubGrades &b) { return a.shots > b.shots; });
    if (out.empty()) return out;
    const int dom = dominantStyle(c);
    std::map<QString, Profile> regraded;
    const auto grader = pv::makeVariabilityGrader(pv::Basis::Scalar);
    for (const DofAgg &a : aggs) {
        for (WristClubGrades &g : out) { g.result[a.id]; g.has[a.id].fill(false); }
        for (int p = 1; p < kP; ++p) {   // P1 is address, the reference every Δ is taken from
            double lo = kNaN, hi = kNaN;
            if (!dominantBandAt(c, dom, regraded, a.id, p, &lo, &hi)) continue;
            const double sigma = (hi - lo) / 2.0;
            if (!(std::isfinite(sigma) && sigma > 0.0)) continue;
            std::vector<VariabilityClubValue> swings;
            for (int i = 0; i < c.n; ++i)
                if (c.profs[size_t(i)].has) swings.push_back({ c.snap->shots[size_t(i)]->club, a.values[size_t(p)][size_t(i)] });
            pv::VariabilityInput base;
            base.scaleType = pv::ScaleType::Interval;
            base.variabilityScale = sigma;
            for (const VariabilityClubWindow &w : perClubInputs(swings, base))
                for (WristClubGrades &g : out)
                    if (g.club == w.club) {
                        g.result[a.id][size_t(p)] = grader->grade(w.input);
                        g.has[a.id][size_t(p)] = g.result[a.id][size_t(p)].graded;
                    }
        }
    }
    return out;
}

bool wristHas(const WristClubGrades &g, const QString &id, int p)
{
    const auto it = g.has.find(id);
    return it != g.has.end() && it->second[size_t(p)];
}

pv::VariabilityResultView wristViewOf(const pv::VariabilityResult &res)
{
    return pv::VariabilityResultView::fromResult(res, QStringLiteral("°"), 0);
}
// A grade that may be null (a club's one grade, Most / Least): the view, in a QVariant
QVariant wristResultOf(const pv::VariabilityResult &res)
{
    return QVariant::fromValue(wristViewOf(res));
}

QString wristTagOf(const Profile *wash, int p)
{
    return wash && !wash->cps[size_t(p)].tag.isEmpty() ? wash->cps[size_t(p)].tag : QStringLiteral("P%1").arg(p + 1);
}

// ROW.grades: per club, the graded checkpoints (or the one grade-0 result for a club too few to grade).
QList<WU::Grade> wristRowGrades(const std::vector<WristClubGrades> &grades, const QString &id, const Profile *wash)
{
    QList<WU::Grade> out;
    const bool named = grades.size() > 1;
    for (const WristClubGrades &g : grades) {
        QList<WU::GradeCell> cells;
        QVariant first;
        bool tooFew = true;
        for (int p = 1; p < kP; ++p) {
            if (!wristHas(g, id, p)) continue;
            const pv::VariabilityResult &res = g.result.at(id)[size_t(p)];
            const pv::VariabilityResultView rv = wristViewOf(res);
            if (!first.isValid()) first = QVariant::fromValue(rv);
            if (res.grade != 0) tooFew = false;
            cells.append(WU::GradeCell{ p, wristTagOf(wash, p), rv });
        }
        if (cells.isEmpty()) continue;
        WU::Grade gr;
        gr.club = g.club;
        gr.named = named;
        gr.tooFew = tooFew;
        gr.result = tooFew ? first : nullv();
        gr.cells = tooFew ? QList<WU::GradeCell>{} : cells;
        out.append(gr);
    }
    return out;
}

// ROW.rowGrades (Mark's ruling R20): per club, ONE grade for the row — the cell rule (R1, cellGrade: the loosest third)
// over the DOF's graded checkpoints for that club (P2 on: R21 keeps P2's grade; only the Most / Least ranking starts at
// P3), the view itself (the report's payload is the typed session as a map: chartsGadgetMap makes it one). A club with no graded checkpoint has no entry. The report's Wrist
// page draws it beside the row's name; the Charts screen keeps ROW.grades, the badge per checkpoint.
QList<WU::RowGrade> wristRowCellGrades(const std::vector<WristClubGrades> &grades, const QString &id)
{
    QList<WU::RowGrade> out;
    const bool named = grades.size() > 1;
    for (const WristClubGrades &g : grades) {
        std::vector<pv::VariabilityResult> readings;
        for (int p = 1; p < kP; ++p)
            if (wristHas(g, id, p)) readings.push_back(g.result.at(id)[size_t(p)]);
        if (readings.empty()) continue;
        const pv::CellGrade cell = pv::cellGrade(readings);
        if (!cell.graded) continue;
        out.append(WU::RowGrade{ g.club, named, pv::VariabilityResultView::fromCellGrade(cell) });
    }
    return out;
}

// consistency.lines: per club, its most and least consistent checkpoint FROM P3 ON, ranked by ρ (graded 1–5 only).
WU::Consistency wristConsistency(const std::vector<WristClubGrades> &grades, const std::vector<DofAgg> &aggs, const Profile *wash)
{
    QList<WU::ConsistencyLine> lines;
    const bool named = grades.size() > 1;
    for (const WristClubGrades &g : grades) {
        struct Ranked { QString id; int p; double rho; };
        std::vector<Ranked> ranked;
        QVariant zero;
        for (const DofAgg &a : aggs)
            for (int p = 2; p < kP; ++p) {
                if (!wristHas(g, a.id, p)) continue;
                const pv::VariabilityResult &res = g.result.at(a.id)[size_t(p)];
                if (res.grade >= 1 && std::isfinite(res.rho)) ranked.push_back({ a.id, p, res.rho });
                else if (res.grade == 0 && !zero.isValid()) zero = wristResultOf(res);
            }
        WU::ConsistencyLine line;
        line.club = g.club;
        line.named = named;
        if (ranked.empty()) {
            if (!zero.isValid()) continue;   // nothing graded for this club: no line
            line.tooFew = true;
            line.result = zero;
            line.mostText = QString();
            line.most = nullv();
            line.leastText = QString();
            line.least = nullv();
            lines.append(line);
            continue;
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &x, const Ranked &y) { return x.rho < y.rho; });
        const Ranked &lo = ranked.front(), &hi = ranked.back();
        const auto where = [&](const Ranked &x) { return QStringLiteral("%1 at %2").arg(longName(x.id), wristTagOf(wash, x.p)); };
        line.tooFew = false;
        line.result = nullv();
        line.mostText = QStringLiteral("Most: ") + where(lo);
        line.most = wristResultOf(g.result.at(lo.id)[size_t(lo.p)]);
        const bool two = ranked.size() >= 2;
        line.leastText = two ? QStringLiteral("Least: ") + where(hi) : QString();
        line.least = two ? wristResultOf(g.result.at(hi.id)[size_t(hi.p)]) : nullv();
        lines.append(line);
    }
    return WU::Consistency{ QStringLiteral("ranked from P3 on"), lines };
}

WU::Drift wristDriftOf(const Correlation &d, int n)
{
    WU::Drift out;
    out.defined = d.defined;
    out.r = nv(d.r);
    out.lo = nv(d.lo);
    out.hi = nv(d.hi);
    out.clear = d.clear;
    out.rText = W::figR(d.r);
    out.ciText = d.defined ? QStringLiteral("(%1 to %2)").arg(W::figR(d.lo), W::figR(d.hi)) : QString();
    out.slope = nv(d.slope);
    out.intercept = nv(d.intercept);
    out.line = d.defined && d.clear
        ? QVariant::fromValue(WU::DriftLine{ 1.0, d.intercept + d.slope, double(n), d.intercept + d.slope * n })
        : nullv();
    return out;
}

void buildSession(Ctx &c, const std::vector<DofAgg> &aggs, ChartsWristPresenter::Result &R)
{
    WU::Session out;
    out.n = c.n;
    out.ready = c.ready;
    out.isSmall = c.n < 10;
    out.available = c.ready > 0;
    // NO SHOT GRADED YET, BUT SHOTS WITH WRIST READINGS (3.8-ii, REVIEW-ii B1; show, never hide): the section is shown with
    // its head and the reason, and nothing drawn from grades (`pending`; PpChartsBody shows it, PpChWristSession draws the
    // head alone, the report's page its head and the reason).
    const bool pendingOnly = c.ready == 0 && c.waiting > 0;
    out.pending = pendingOnly;
    QStringList clubs;
    for (int i = 0; i < c.n; ++i) { const QString k = c.snap->shots[size_t(i)]->club.toLower(); if (!k.isEmpty() && !clubs.contains(k)) clubs << k; }
    out.kicker = QStringLiteral("%1 %2").arg(c.n).arg(c.many)
                 + (clubs.size() == 1 ? QStringLiteral("  ·  ") + clubs.first() : clubs.size() > 1 ? QStringLiteral("  ·  mixed clubs") : QString())
                 + QStringLiteral("  ·  Wrist");
    if (pendingOnly) {
        out.head = sentenceOf(W::Sentence().append(QStringLiteral("Wrist grades aren't ready yet: "))
                                  .figure(QString::number(c.waiting)).append(QStringLiteral(" of "))
                                  .figure(QString::number(c.n)).append(QStringLiteral(" %1 %2 waiting for %3 findings")
                                  .arg(c.n == 1 ? c.one : c.many, c.waiting == 1 ? QStringLiteral("is") : QStringLiteral("are"),
                                       c.waiting == 1 ? QStringLiteral("its") : QStringLiteral("their"))));
        const QString why = c.snap ? c.snap->wristPendingWhy : QString();
        out.standfirst = sentenceOf(W::Sentence().append(why));
        out.pendingWhy = why;
        // the wrist curves' keys, so the body knows a wrist curve is on show with no graded row to name it
        QStringList keys;
        for (const Profile &p : c.profs)
            if (p.pending) for (const Dof &d : p.dofs) if (d.hasStrip && !d.key.isEmpty() && !keys.contains(d.key)) keys << d.key;
        out.keys = keys;
        out.recurringEmpty = QString();
        R.session = out;
        return;
    }
    if (c.ready == 0) {
        out.head = sentenceOf(W::Sentence().append(QStringLiteral("No %1 in this session carries a wrist reading").arg(c.one)));
        out.recurringEmpty = QStringLiteral("No wrist finding fired on any %1.").arg(c.one);
        R.session = out;
        return;
    }
    // the picked shot's profile for the wash and the positions, else the first shot with one
    const Profile *wash = c.pick.has ? &c.pick : nullptr;
    if (!wash) for (const Profile &p : c.profs) if (p.has) { wash = &p; break; }
    out.positions = positionsOf(c, *wash);

    // per shot: the verdicts, the score, the style
    std::vector<Verdicts> vs(size_t(c.n));
    std::vector<double> scores(size_t(c.n), kNaN);
    QMap<QString, int> styles;
    for (int i = 0; i < c.n; ++i) {
        const ShotSeries &s = *c.snap->shots[size_t(i)];
        vs[size_t(i)] = verdictsOf(c, s);
        if (vs[size_t(i)].graded && !vs[size_t(i)].refused && vs[size_t(i)].score.score >= 0) scores[size_t(i)] = vs[size_t(i)].score.score;
        if (c.profs[size_t(i)].has) ++styles[c.profs[size_t(i)].archetypeName];
    }
    std::vector<std::pair<QString, int>> styleList;
    for (auto it = styles.cbegin(); it != styles.cend(); ++it) styleList.emplace_back(it.key(), it.value());
    std::stable_sort(styleList.begin(), styleList.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

    // THE HEAD: the style the shots were graded as
    W::Sentence head;
    const auto &top = styleList.front();
    if (styleList.size() == 1)
        head.append(QStringLiteral("Graded as a %1 wrist on ").arg(top.first))
            .append(top.second == c.n ? QStringLiteral("every ") + c.one : QStringLiteral("%1 of %2 %3").arg(top.second).arg(c.n).arg(c.many));
    else {
        QStringList rest;
        for (size_t i = 1; i < styleList.size(); ++i) rest << QStringLiteral("%1 on %2").arg(styleList[i].first).arg(styleList[i].second);
        head.append(QStringLiteral("Graded %1 on %2 of %3 %4, ").arg(top.first).arg(top.second).arg(c.ready).arg(c.many) + rest.join(QStringLiteral(", ")));
    }
    out.head = sentenceOf(head);

    // THE STANDFIRST: the score, the resemblance index, the n (consistency is its own block: `consistency`)
    const BoxStats sb = boxStats(scores);
    // built in its four parts (score, repeat, resemblance, size) and joined, run for run, into the one standfirst
    // the screen sets; paper prints the parts as lines of their own (sessionParts)
    W::Sentence stScore, stRepeat, stIndex, stIndexPaper, stSize;
    if (sb.n > 0)
    {
        stScore.append(QStringLiteral("Wrist score median ")).figure(W::fig(sb.med));
        // a middle half of one value is said as one ("all at 100"), never a range of it to itself ("100–100")
        if (W::fig(sb.q1) == W::fig(sb.q3)) stScore.append(QStringLiteral(", middle half all at ")).figure(W::fig(sb.q1));
        else stScore.append(QStringLiteral(", middle half ")).figure(W::fig(sb.q1) + QStringLiteral("–") + W::fig(sb.q3));
        stScore.append(QStringLiteral(". "));
    }
    else
        stScore.append(QStringLiteral("No %1's wrist conditions have been graded yet. ").arg(c.one));
    // the old "most repeatable … least" clause is gone (R7, R11): the checkpoints are graded and ranked by ρ in
    // `consistency`, below; stRepeat stays empty, so the joined standfirst and paper's `repeat` part say nothing
    QMap<QString, int> most;
    int withRes = 0, disagree = 0;
    for (const Profile &p : c.profs) {
        if (!p.has || !p.resemblance.contains(QStringLiteral("neutral"))) continue;
        ++withRes;
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (p.resemblance.value(QLatin1String(i == 1 ? "bowed" : "cupped")).toInt()
                > p.resemblance.value(QLatin1String(best == 0 ? "neutral" : best == 1 ? "bowed" : "cupped")).toInt()) best = i;
        ++most[wristArchetypeName(best)];
        if (best != p.archetype) ++disagree;
    }
    if (withRes) {
        std::vector<std::pair<QString, int>> ml;
        for (auto it = most.cbegin(); it != most.cend(); ++it) ml.emplace_back(it.key(), it.value());
        std::stable_sort(ml.begin(), ml.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        stIndex.append(QStringLiteral("By the resemblance index, most resemble "));
        for (size_t i = 0; i < ml.size(); ++i) {
            if (i) stIndex.append(QStringLiteral(", "));
            stIndex.append(ml[i].first + QStringLiteral(" (")).figure(QString::number(ml[i].second)).append(QStringLiteral(")"));
        }
        // the disagreement: on the screen it names the Auto control; paper (styleLine) says it without the control
        stIndexPaper = stIndex;
        if (disagree) {
            const QString screenTail = c.in.archetype < 0 ? QStringLiteral(" that is not the style Auto graded against. ")
                                                         : QStringLiteral(" that is not the style the bands are set to. ");
            const QString paperTail = c.in.archetype < 0 ? QStringLiteral(" that is not the style it was graded against. ") : screenTail;
            for (W::Sentence *x : { &stIndex, &stIndexPaper }) {
                const QString &tail = x == &stIndex ? screenTail : paperTail;
                // every swing (each carries an index and none agrees): "on every swing", not "on 5 of 5 swings"
                if (disagree == withRes && withRes == c.n) x->append(QStringLiteral("; on every ") + c.one + tail);
                else x->append(QStringLiteral("; on ")).figure(QString::number(disagree)).append(QStringLiteral(" of ")).figure(QString::number(withRes))
                       .append(QLatin1Char(' ') + c.many + tail);
            }
        } else {
            stIndex.append(QStringLiteral(". "));
            stIndexPaper.append(QStringLiteral(". "));
        }
    }
    stSize.append(c.n < 10 ? W::cap1(W::word(c.n)) + QStringLiteral(" %1 is a small session: each").arg(c.many) : QStringLiteral("Each"))
      .append(QStringLiteral(" box is drawn from ")).figure(QString::number(c.ready)).append(QStringLiteral(" values."));
    W::Sentence st;
    for (const W::Sentence *part : { &stScore, &stRepeat, &stIndex, &stSize })
        for (const W::Sentence::Run &r : part->runs) st.runs.push_back(r);
    {
        // a part as a line of its own: its text without the joining space (and, for the score and the style,
        // without the full stop: they are a labelled line, not a sentence)
        const auto line = [](const W::Sentence &x, bool stop) {
            QString t = x.text().trimmed();
            if (!stop) while (t.endsWith(QLatin1Char('.'))) t.chop(1);
            return t;
        };
        // the style line carries only the resemblance index's clause: the head already says the style counts
        // ("Graded Cupped on 5 of 6 swings, Neutral on 1"), and paper does not print one fact twice ("" with no index)
        const QString styleLine = stIndexPaper.isEmpty() ? QString() : line(stIndexPaper, false);
        R.sessionParts = QVariantMap{ { QStringLiteral("scoreLine"), line(stScore, false) }, { QStringLiteral("styleLine"), styleLine },
                                      { QStringLiteral("repeat"), line(stRepeat, true) }, { QStringLiteral("sizeLine"), line(stSize, true) } };
    }
    out.standfirst = sentenceOf(st);

    // THE CHECKPOINT BOXES, with each DOF's consistency per club against the dominant style's band
    const std::vector<WristClubGrades> grades = wristGrades(c, aggs);
    out.consistency = wristConsistency(grades, aggs, wash);
    QList<WU::Row> rows;
    for (const DofAgg &a : aggs) {
        const Dof *meta = c.pick.dof(a.id);
        if (!meta) for (const Profile &p : c.profs) if ((meta = p.dof(a.id))) break;
        const Dof *mine = c.pick.dof(a.id);
        Scale sc;
        QList<WU::Point> points;
        QList<WU::BoxCell> boxes;
        for (int p = 0; p < kP; ++p) {
            const BoxStats &b = a.box[size_t(p)];
            if (mine) {
                const Cell &cell = mine->cells[size_t(p)];
                points.append(pointOf(*mine, cell, false));
                if (cell.banded) { sc.add(cell.lo); sc.add(cell.hi); }
            } else {
                // the picked shot has no reading here: an unavailable point, no band
                WU::Point pm;
                pm.valueText = QStringLiteral("-");
                points.append(pm);
            }
            sc.add(b.min); sc.add(b.max);
            const double med = b.n >= 3 ? b.med : kNaN, wide = b.n >= 3 ? b.q3 - b.q1 : kNaN;
            WU::BoxCell bc;
            bc.box = boxOf(b, degText);
            bc.values = numList(a.values[size_t(p)]);
            bc.n = b.n;
            bc.median = nv(med);
            bc.medianText = b.n >= 3 ? degText(med) : QStringLiteral("-");
            bc.wide = nv(wide);
            bc.wideText = b.n >= 3 ? W::fig(wide) + QStringLiteral("° wide") : QString();
            bc.graded = std::any_of(grades.begin(), grades.end(), [&](const WristClubGrades &g) { return wristHas(g, a.id, p); });
            bc.picked = mine ? nv(mine->cells[size_t(p)].delta) : nullv();
            boxes.append(bc);
        }
        sc.finish();
        WU::Row row;
        row.numbersOnly = false;
        row.dof = a.id;
        row.key = meta->key;
        row.name = meta->name;
        row.sub = meta->sub;
        row.source = mine ? mine->source : QString();
        row.confidence = mine ? mine->conf : 0.0;
        row.pips = mine ? pips(mine->conf) : QString();
        row.sourceText = mine && !mine->source.isEmpty() ? mine->source + QLatin1Char(' ') + pips(mine->conf) : QString();
        row.shortText = shortName(a.id);
        row.poleTop = meta->poleTop;
        row.poleBottom = meta->poleBottom;
        row.poleTopText = meta->poleTop.isEmpty() ? QString() : QStringLiteral("↑ ") + meta->poleTop;
        row.poleBottomText = meta->poleBottom.isEmpty() ? QString() : QStringLiteral("↓ ") + meta->poleBottom;
        row.points = points;
        row.boxes = boxes;
        row.yLo = sc.lo;
        row.yHi = sc.hi;
        row.outCount = 0;
        row.grades = wristRowGrades(grades, a.id, wash);
        row.rowGrades = wristRowCellGrades(grades, a.id);
        rows.append(row);
    }
    out.rows = rows;
    const QString pickedWord = c.index >= 0 ? QStringLiteral("%1 %2’s").arg(c.one).arg(W::shotNumber(c.shot->shotId, c.shot->ordinal)) : QStringLiteral("this swing’s");
    out.caption = wash == &c.pick
        ? QStringLiteral("Shaded: %1 %2 band. ").arg(pickedWord, c.pick.archetypeName)
              + (c.in.archetype < 0 ? QStringLiteral("Each %1 is graded against its own style’s band on This shot.").arg(c.one)
                                    : QStringLiteral("Every %1 is graded against the %2 band.").arg(c.one, wristArchetypeName(c.in.archetype)))
        : QStringLiteral("No band is shaded: the picked swing has no wrist reading.");
    out.sub = QStringLiteral("Δ from address, degrees  ·  a box per checkpoint, n %1, this %2 ringed  ·  under each: median, and how wide the middle half is").arg(c.ready).arg(c.one);

    // THE SCORE RUN (PpChRunBox's RUN)
    QList<WU::RunPoint> pts;
    std::vector<double> xs, ys;
    for (int i = 0; i < c.n; ++i) {
        const ShotSeries &s = *c.snap->shots[size_t(i)];
        const double v = scores[size_t(i)];
        WU::RunPoint rp;
        rp.ordinal = s.ordinal;
        rp.shotId = s.shotId;
        rp.swingDir = s.swingDir;
        rp.value = nv(v);
        rp.measured = std::isfinite(v);
        rp.fired = false;
        rp.picked = i == c.index;
        pts.append(rp);
        xs.push_back(double(i + 1));
        ys.push_back(v);
    }
    out.scoreBox = boxOf(sb, scoreText);
    WU::ScoreRun &run = out.scoreRun;
    run.readingId = QStringLiteral("wristScore");
    run.label = QStringLiteral("Wrist score");
    run.caption = QStringLiteral("Wrist score, %1 by %1 (0–100)").arg(c.one);
    run.points = pts;
    run.box = boxOf(sb, scoreText);
    run.yRange = WU::YRange{ 0, 100 };
    run.drift = wristDriftOf(pearson(xs, ys), c.n);

    // THE STYLE TABLE
    QList<WU::StyleColumn> cols;
    QList<WU::StyleRow> srows;
    QList<WU::StyleCount> counts;
    for (int i = 0; i < c.n; ++i) {
        const ShotSeries &s = *c.snap->shots[size_t(i)];
        WU::StyleColumn col;
        col.ordinal = s.ordinal;
        col.shotId = s.shotId;
        col.swingDir = s.swingDir;
        col.label = W::cap1(c.one) + QStringLiteral(" %1").arg(W::shotNumber(s.shotId, s.ordinal));
        col.shortText = QString::number(W::shotNumber(s.shotId, s.ordinal));   // the header when columns are narrow
        col.picked = i == c.index;
        cols.append(col);
    }
    bool anyIndex = false;
    for (int i = 0; i < c.n; ++i) anyIndex = anyIndex || (c.profs[size_t(i)].has && c.profs[size_t(i)].resemblance.contains(QStringLiteral("neutral")));
    static const char *const keys[] = { "neutral", "bowed", "cupped" };
    for (int k = 0; k < 3 && anyIndex; ++k) {   // no index on any shot: one quiet line, not a grid of "-"
        QList<WU::StyleCell> cells;
        for (int i = 0; i < c.n; ++i) {
            const Profile &p = c.profs[size_t(i)];
            const QVariant v = p.resemblance.value(QLatin1String(keys[k]));
            cells.append(WU::StyleCell{ p.has && v.isValid() ? QString::number(v.toInt()) : QStringLiteral("-"), p.has && p.archetype == k });
        }
        srows.append(WU::StyleRow{ QString::fromLatin1(keys[k]), wristArchetypeName(k), cells });
    }
    for (const auto &sl : styleList) counts.append(WU::StyleCount{ sl.first, sl.second });
    WU::Styles &sty = out.styles;
    sty.columns = anyIndex ? cols : QList<WU::StyleColumn>{};
    sty.rows = srows;
    sty.counts = counts;
    sty.hasIndex = anyIndex;
    sty.note = anyIndex ? QString() : QStringLiteral("No %1 in this session carries a resemblance index, so there is no style to compare.").arg(c.one);

    // WHAT KEEPS FIRING
    struct Rec { const Cond *k; int count; QList<WU::RecurringMark> marks; QList<QPair<QString, int>> pos; };
    std::vector<Rec> recs;
    for (const Cond &k : c.w.conds) {
        Rec r{ &k, 0, {}, {} };
        for (int i = 0; i < c.n; ++i) {
            const ShotSeries &s = *c.snap->shots[size_t(i)];
            const bool f = std::find(vs[size_t(i)].fired.begin(), vs[size_t(i)].fired.end(), &k) != vs[size_t(i)].fired.end();
            r.marks.append(WU::RecurringMark{ s.ordinal, s.shotId, s.swingDir, f });
            if (!f) continue;
            ++r.count;
            const int ph = condPhase(s, k);
            const QString tag = ph >= 0 ? c.tagOf(ph, c.profs[size_t(i)].has ? c.profs[size_t(i)] : *wash) : QString();
            if (tag.isEmpty()) continue;
            bool found = false;
            for (auto &pp : r.pos) if (pp.first == tag) { ++pp.second; found = true; }
            if (!found) r.pos.append({ tag, 1 });
        }
        if (r.count > 0) recs.push_back(std::move(r));
    }
    std::stable_sort(recs.begin(), recs.end(), [](const Rec &a, const Rec &b) { return a.count > b.count; });
    QList<WU::Recurring> recurring;
    for (const Rec &r : recs) {
        QList<WU::RecurringPosition> pos;
        QStringList tags;
        for (const auto &pp : r.pos) { pos.append(WU::RecurringPosition{ pp.first, pp.second }); tags << pp.first; }
        const QString tier = sessionTier(c, r.k->id, true);
        WU::Recurring rc;
        rc.id = r.k->id;
        rc.name = r.k->label;
        rc.severity = tier == QLatin1String("pattern") ? QStringLiteral("pattern") : QStringLiteral("watching");
        rc.tier = tier;
        rc.count = r.count;
        rc.countText = QStringLiteral("%1 of %2 %3").arg(r.count).arg(c.ready).arg(c.many) + (tags.isEmpty() ? QString() : QStringLiteral(", at ") + tags.join(QStringLiteral(", ")));
        rc.positions = pos;
        rc.marks = r.marks;
        recurring.append(rc);
    }
    out.recurring = recurring;
    out.recurringEmpty = QStringLiteral("No wrist finding fired on any %1.").arg(c.one);
    out.trust = trustOf(c, nullptr, nullptr, nullptr, &vs, R.sessionParts.value(QStringLiteral("sizeLine")).toString());
    R.session = out;
}

// ── What moves with the lead wrist at impact ───────────────────────────────────────────────

void buildMovesWith(Ctx &c, ChartsWristPresenter::Result &R)
{
    QString key;
    if (const Dof *d = c.pick.dof(QStringLiteral("leadWristFlexExt"))) key = d->key;
    if (key.isEmpty()) for (const Profile &p : c.profs) if (const Dof *d = p.dof(QStringLiteral("leadWristFlexExt"))) { key = d->key; break; }
    RelationsMovesWith m;
    if (key.isEmpty() || !c.snap) {
        m.available = false;
        m.refusal = relationsSentence(W::Sentence().append(QStringLiteral("No %1 of this session reads the lead wrist's bow / cup.").arg(c.one)));
    } else {
        const QString id = key + QStringLiteral("@%1").arg(phase::Impact);
        if (c.w.mwSnap == c.snap.get() && c.w.mwRev == c.snap->revision && c.w.mwId == id) {
            m = c.w.mw;
        } else {
            m = ChartsRelationsPresenter::movesWith(c.snap, id, 4, 3);
            c.w.mwSnap = c.snap.get(); c.w.mwRev = c.snap->revision; c.w.mwId = id; c.w.mw = m;
        }
        m.relationsUpId = id;
    }
    m.sub = QStringLiteral("bow / cup at P7 against every reading, launch monitor included");
    R.movesWith = m;
}

// ── the choices ─────────────────────────────────────────────────────────────────────────────

void buildChoices(Ctx &c, ChartsWristPresenter::Result &R)
{
    WU::Choices out;
    const QString scope = c.in.scope == QLatin1String("session") ? QStringLiteral("session") : QStringLiteral("shot");
    out.scope = scope;
    out.scopes = { WU::Option{ QStringLiteral("shot"), QStringLiteral("This shot"), scope == QLatin1String("shot") },
                   WU::Option{ QStringLiteral("session"), QStringLiteral("Session"), scope == QLatin1String("session") } };

    const QString ag = c.in.against;
    const int pi = c.index >= 0 ? c.index - 1 : c.n - 1;
    const bool prevOk = pi >= 0 && pi < c.n && c.profs[size_t(pi)].has;
    const bool refSet = !c.in.referenceSwingDir.trimmed().isEmpty();
    const bool refOk = c.ref && !c.refIsPicked && c.refProf.has;
    const QString refWhy = !refSet ? QStringLiteral("no reference swing is set: ★ marks one")
                         : !c.ref ? c.refWhy
                         : c.refIsPicked ? QStringLiteral("this swing is your reference")
                         : QStringLiteral("the reference swing carries no wrist reading");
    QList<WU::AgainstOption> ags;
    const auto addA = [&](const QString &id, const QString &label, bool enabled, const QString &why) {
        ags.append(WU::AgainstOption{ id, label, ag == id, enabled, enabled ? QString() : why });
    };
    addA(QStringLiteral("address"), QStringLiteral("address"), true, QString());
    addA(QStringLiteral("previous"), QStringLiteral("previous"), prevOk, QStringLiteral("the swing before this one has no wrist reading"));
    addA(QStringLiteral("reference"), QStringLiteral("reference ★"), refOk, refWhy);
    addA(QStringLiteral("median"), QStringLiteral("session median"), c.ready >= 3,
         QStringLiteral("three shots with a wrist reading are needed; this session has %1").arg(W::countWord(c.ready)));
    out.against = ag;
    out.againstOptions = ags;

    out.curvesBehind = c.in.curvesBehind;
    out.curves = { WU::Option{ QStringLiteral("off"), QStringLiteral("off"), !c.in.curvesBehind },
                   WU::Option{ QStringLiteral("on"), QStringLiteral("on"), c.in.curvesBehind } };

    QList<WU::ArchetypeOption> arch;
    static const char *const names[] = { "Auto", "Neutral", "Bowed", "Cupped" };
    for (int a = -1; a <= 2; ++a)
        arch.append(WU::ArchetypeOption{ a, QString::fromLatin1(names[a + 1]), c.in.archetype == a });
    out.archetype = c.in.archetype;
    out.archetypes = arch;
    const QString after = c.in.archetype < 0 && c.pick.has ? QStringLiteral("→ ") + c.pick.archetypeName : QString();
    out.archetypeAfter = after;

    const bool isThis = c.refIsPicked;
    WU::Reference &ref = out.reference;
    ref.set = refSet;
    ref.swingDir = cleanDir(c.in.referenceSwingDir);
    ref.label = c.refLabel;
    ref.isThisShot = isThis;
    ref.linkText = isThis ? QStringLiteral("★ this swing is your reference") : QStringLiteral("★ set this swing as your reference");
    ref.canSet = bool(c.shot);

    QString agLabel = QStringLiteral("address");
    for (const WU::AgainstOption &a : ags) if (a.current) agLabel = a.label;
    out.summary = QStringLiteral("Bands %1%2  ·  against %3  ·  curves %4")
                      .arg(QString::fromLatin1(names[c.in.archetype + 1]), after.isEmpty() ? QString() : QStringLiteral(" ") + after, agLabel,
                           c.in.curvesBehind ? QStringLiteral("on") : QStringLiteral("off"));
    R.choices = out;
}

} // namespace

// ── the job ─────────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<ChartsWristPresenter::Result> computeWrist(ChartsWristPresenter::Worker &w, const ChartsWristPresenter::Inputs &in)
{
    QMutexLocker lk(&w.mutex);
    w.ensure();
    auto R = std::make_shared<ChartsWristPresenter::Result>();
    std::shared_ptr<const SessionSnapshot> snap = in.snapshot;
    if (!snap) snap = std::make_shared<SessionSnapshot>();
    Ctx c(w, in, snap);
    c.n = c.snap->count();
    // every shot's profile against the asked archetype (the model's re-grade may not have landed yet)
    c.profs.reserve(size_t(c.n));
    for (const auto &s : c.snap->shots) {
        c.profs.push_back(parseProfile(shotWristPayload(*s, in.archetype, c.snap->wristPendingWhy)));
        if (c.profs.back().has) ++c.ready;
        else if (c.profs.back().pending) ++c.waiting;
    }
    // the picked shot: the snapshot's, else its own detail (a live shot, another session's)
    const QString want = cleanDir(in.pickedSwingDir);
    if (!want.isEmpty())
        for (int i = 0; i < c.n; ++i)
            if (cleanDir(c.snap->shots[size_t(i)]->swingDir) == want) { c.index = i; break; }
    if (c.index >= 0) {
        c.shot = c.snap->shots[size_t(c.index)];
        c.pick = c.profs[size_t(c.index)];
    } else if (!want.isEmpty() && !in.pickedDetail.value(QStringLiteral("series")).toList().isEmpty()) {
        auto sh = wristShotFromDetail(in.pickedDetail, want);
        sh->ordinal = c.n + 1;
        // not in the snapshot yet: its findings have not landed, so its checkpoints are not graded yet (J-58, W4)
        sh->wristResemblance = resemblanceOfDetail(in.pickedDetail);
        c.pick = parseProfile(shotWristPayload(*sh, in.archetype,
                                               QStringLiteral("This swing's diagnostics haven't landed yet: its wrist checkpoints are graded with them.")));
        c.shot = sh;
    }
    // the ★ reference: picked on the GUI thread through its own session's series (referencePickOf), handed in by value
    if (!in.referenceSwingDir.trimmed().isEmpty()) {
        c.ref = in.reference.shot;
        c.refWhy = in.reference.why;
        if (c.ref) {
            c.refProf = parseProfile(referenceWristPayload(*c.ref));   // its Δ only (J-ii3)
            c.refLabel = referenceLabelOf(c.ref->swingDir);
            c.refIsPicked = c.shot && cleanDir(c.ref->swingDir) == cleanDir(c.shot->swingDir);
        }
    } else {
        c.refWhy = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions.");
    }

    const std::vector<DofAgg> aggs = aggregates(c);
    R->available = c.ready > 0 || c.waiting > 0 || c.pick.shown();
    buildThisShot(c, aggs, *R);
    buildSession(c, aggs, *R);
    buildMovesWith(c, *R);
    buildChoices(c, *R);
    return R;
}

// ── the session report's Wrist page ─────────────────────────────────────────────────────────

namespace {

// A Sentence re-worded in the report's noun: "shot(s)" as whole words → `one` / `many` (and capitalised). Used
// ONLY on the Relations presenter's refusal, whose words count "shots" and take no noun of their own.
QVariantMap nounSentence(QVariantMap sentence, const QString &one, const QString &many)
{
    if (one == QLatin1String("shot")) return sentence;
    const auto swap = [&](QString t) {
        static const QRegularExpression pl(QStringLiteral("\\bshots\\b")), Pl(QStringLiteral("\\bShots\\b")),
                                        sg(QStringLiteral("\\bshot\\b")), Sg(QStringLiteral("\\bShot\\b"));
        t.replace(pl, many).replace(Pl, W::cap1(many)).replace(sg, one).replace(Sg, W::cap1(one));
        return t;
    };
    sentence.insert(QStringLiteral("text"), swap(sentence.value(QStringLiteral("text")).toString()));
    // its first clause, as the words module cuts every sentence (refactor 5.5b): from the re-worded text
    sentence.insert(QStringLiteral("clause"), W::firstClauseOf(sentence.value(QStringLiteral("text")).toString()));
    QVariantList runs = sentence.value(QStringLiteral("runs")).toList();
    for (QVariant &r : runs) {
        QVariantMap m = r.toMap();
        m.insert(QStringLiteral("text"), swap(m.value(QStringLiteral("text")).toString()));
        r = m;
    }
    sentence.insert(QStringLiteral("runs"), runs);
    return sentence;
}

} // namespace

WristSessionReport wristSessionReport(const SessionSnapshot &snapshot, int archetypeMode, const QString &noun)
{
    const int mode = archetypeMode < -1 || archetypeMode > 2 ? -1 : archetypeMode;
    const QString many = noun.trimmed().isEmpty() ? QStringLiteral("shots") : noun.trimmed();
    const QString one = many.endsWith(QLatin1Char('s')) ? many.chopped(1) : many;

    // a worker of our own (the pack's wrist conditions, the timeline words): this runs on the caller's thread
    ChartsWristPresenter::Worker w;
    QMutexLocker lk(&w.mutex);
    w.ensure();
    ChartsWristPresenter::Inputs in;
    in.scope = QStringLiteral("session");
    in.archetype = mode;
    const std::shared_ptr<const SessionSnapshot> snap(&snapshot, [](const SessionSnapshot *) {});   // not ours: no delete
    Ctx c(w, in, snap);
    c.many = many;
    c.one = one;
    c.paper = true;
    c.n = snapshot.count();
    c.profs.reserve(size_t(c.n));
    for (const auto &s : snapshot.shots) {
        c.profs.push_back(parseProfile(shotWristPayload(*s, mode, snapshot.wristPendingWhy)));
        if (c.profs.back().has) ++c.ready;
        else if (c.profs.back().pending) ++c.waiting;
    }
    // THE DOMINANT STYLE (dominantStyle: the style most shots were graded as)
    std::array<int, 3> graded{};
    for (const Profile &p : c.profs) if (p.has) ++graded[size_t(std::clamp(p.archetype, 0, 2))];
    const int dom = dominantStyle(c);
    if (c.ready > 0) c.washStyle = dom;

    const std::vector<DofAgg> aggs = aggregates(c);
    ChartsWristPresenter::Result R;
    buildSession(c, aggs, R);
    buildMovesWith(c, R);
    // the screen's typed session and ledger, then paper's own figures beside them (reportPayloadOf puts them in the map)
    WristSessionReport out;
    out.session = R.session;
    out.movesWith = R.movesWith;
    out.one = one;
    out.many = many;
    out.ready = c.ready > 0;
    if (!out.ready) return out;
    // the score run's value axis on paper: the presenter's ticks, made here (PpChRunBox asks a ticker; the report
    // has no Relations presenter to ask, so the ticks travel in the payload)
    out.scoreTicks = ChartsRelationsPresenter::axisTicks(R.session.scoreRun.yRange.lo, R.session.scoreRun.yRange.hi, 3,
                                                         R.session.scoreRun.unit);

    // the dominant style's band at a checkpoint (dominantBandAt: the one the session's consistency is graded against)
    std::map<QString, Profile> regraded;
    const auto bandAt = [&](const QString &id, int p, double *lo, double *hi) { return dominantBandAt(c, dom, regraded, id, p, lo, hi); };

    for (const WU::Row &row : R.session.rows) {
        const QString &id = row.dof;
        const DofAgg *a = aggOf(aggs, id);
        // the row's source on paper: the one most shots read it from
        QMap<QString, int> bySource;
        for (const Profile &pf : c.profs) if (const Dof *d = pf.has ? pf.dof(id) : nullptr) ++bySource[d->source];
        QString source;
        int best = 0;
        for (auto it = bySource.cbegin(); it != bySource.cend(); ++it) if (it.value() > best) { best = it.value(); source = it.key(); }
        WristSessionReport::PaperRow pr;
        Scale sc;
        for (int p = 0; p < kP; ++p) {
            double lo = kNaN, hi = kNaN;
            const bool banded = bandAt(id, p, &lo, &hi);
            pr.banded.append(banded);
            pr.bandLo.append(lo);
            pr.bandHi.append(hi);
            if (banded) { sc.add(lo); sc.add(hi); }
            if (a) { sc.add(a->box[size_t(p)].min); sc.add(a->box[size_t(p)].max); }
            if (p < row.boxes.size()) {
                const QVariant mv = row.boxes.at(p).median;
                const double med = mv.isNull() ? kNaN : mv.toDouble();
                // P1 is address, the reference every Δ is taken from (the engine's "ref" cell): never outside
                const bool outside = p > 0 && banded && std::isfinite(med) && (med > hi || med < lo);
                if (outside) ++pr.outCount;
                pr.outside.append(outside);
                pr.ringWord.append(outside ? (med > hi ? row.poleTop : row.poleBottom) : QString());
            }
        }
        sc.finish();
        pr.yLo = sc.lo;
        pr.yHi = sc.hi;
        pr.label = W::cap1(longName(id));
        pr.source = source;
        pr.camera = sourceLabel(source);
        out.rows.append(pr);
    }
    const QString domName = wristArchetypeName(dom);
    const int k = graded[size_t(dom)];
    // no swing reads the lead wrist's bow / cup (a camera-only session): nothing chose the style, so the caption
    // names the band alone and the trust line says why
    bool anyFe = false;
    for (const Profile &pf : c.profs) anyFe = anyFe || (pf.has && pf.dof(QStringLiteral("leadWristFlexExt")));
    out.caption = mode >= 0 ? QStringLiteral("Shaded: the %1 band, set by hand; every %2 is graded against it.").arg(domName, one)
                : !anyFe ? QStringLiteral("Shaded: the %1 band.").arg(domName)
                : k == c.ready ? QStringLiteral("Shaded: the %1 band, the style every %2 was graded as.").arg(domName, one)
                : QStringLiteral("Shaded: the %1 band, the style %2 of %3 %4 were graded as. Each %5 was graded against its own style’s band.")
                      .arg(domName).arg(k).arg(c.ready).arg(many, one);
    out.sub = QStringLiteral("Δ from address, degrees  ·  a box per checkpoint, n %1  ·  under each: median, and how wide the middle half is").arg(c.ready);
    out.dominant = dom;
    out.dominantName = domName;
    out.dominantN = k;
    out.dominantOf = c.ready;
    out.hasParts = !R.sessionParts.isEmpty();
    out.scoreLine = R.sessionParts.value(QStringLiteral("scoreLine")).toString();
    out.styleLine = R.sessionParts.value(QStringLiteral("styleLine")).toString();
    out.repeat = R.sessionParts.value(QStringLiteral("repeat")).toString();
    out.sizeLine = R.sessionParts.value(QStringLiteral("sizeLine")).toString();
    return out;
}

QVariantMap reportPayloadOf(const WristSessionReport &page)
{
    // the screen's typed session as the report's map: every key the screen has, a grade (VariabilityResultView) as its
    // fields, then paper's own keys below
    QVariantMap out = chartsGadgetMap(page.session);
    if (!page.ready) return out;
    {
        QVariantMap sr = out.value(QStringLiteral("scoreRun")).toMap();
        sr.insert(QStringLiteral("ticks"), page.scoreTicks);
        out.insert(QStringLiteral("scoreRun"), sr);
    }
    QVariantList rows;
    const QVariantList in = out.value(QStringLiteral("rows")).toList();
    for (int r = 0; r < in.size(); ++r) {
        QVariantMap row = in[r].toMap();
        if (r >= page.rows.size()) { rows.append(row); continue; }
        const WristSessionReport::PaperRow &pr = page.rows.at(r);
        QVariantList points, boxes = row.value(QStringLiteral("boxes")).toList();
        for (int p = 0; p < pr.banded.size(); ++p) {
            const bool banded = pr.banded.at(p);
            points.append(QVariantMap{ { QStringLiteral("available"), false }, { QStringLiteral("value"), nullv() },
                                       { QStringLiteral("valueText"), QStringLiteral("-") }, { QStringLiteral("banded"), banded },
                                       { QStringLiteral("bandLo"), nv(pr.bandLo.at(p)) }, { QStringLiteral("bandHi"), nv(pr.bandHi.at(p)) },
                                       { QStringLiteral("wash"), banded ? QStringLiteral("band") : QStringLiteral("none") },
                                       { QStringLiteral("out"), false } });
        }
        for (int p = 0; p < boxes.size() && p < pr.outside.size(); ++p) {
            QVariantMap bm = boxes[p].toMap();
            bm.insert(QStringLiteral("outside"), pr.outside.at(p));
            bm.insert(QStringLiteral("ringWord"), pr.ringWord.at(p));
            boxes[p] = bm;
        }
        row.insert(QStringLiteral("points"), points);
        row.insert(QStringLiteral("boxes"), boxes);
        row.insert(QStringLiteral("yLo"), pr.yLo);
        row.insert(QStringLiteral("yHi"), pr.yHi);
        row.insert(QStringLiteral("outCount"), pr.outCount);
        row.insert(QStringLiteral("label"), pr.label);
        row.insert(QStringLiteral("source"), pr.source);
        row.insert(QStringLiteral("camera"), pr.camera);
        row.insert(QStringLiteral("unit"), QStringLiteral("°"));
        rows.append(row);
    }
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("caption"), page.caption);
    out.insert(QStringLiteral("sub"), page.sub);
    out.insert(QStringLiteral("dominant"), QVariantMap{ { QStringLiteral("archetype"), page.dominant }, { QStringLiteral("name"), page.dominantName },
                                                        { QStringLiteral("n"), page.dominantN }, { QStringLiteral("of"), page.dominantOf } });
    if (page.hasParts) {
        out.insert(QStringLiteral("scoreLine"), page.scoreLine);
        out.insert(QStringLiteral("styleLine"), page.styleLine);
        out.insert(QStringLiteral("repeat"), page.repeat);
        out.insert(QStringLiteral("sizeLine"), page.sizeLine);
    }
    QVariantMap mw = chartsGadgetMap(page.movesWith);
    if (!page.movesWith.available)   // a refusal is there only when it is not available
        mw.insert(QStringLiteral("refusal"), nounSentence(mw.value(QStringLiteral("refusal")).toMap(), page.one, page.many));
    out.insert(QStringLiteral("movesWith"), mw);
    return out;
}

// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsWristPresenter::ChartsWristPresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    schedule();
}

ChartsWristPresenter::~ChartsWristPresenter()
{
    // No wait: a job still running finishes into nothing (charts_work.h: the lane is cancelled, #173).
}

SeriesSource *ChartsWristPresenter::model() const { return m_model.data(); }

void ChartsWristPresenter::setModel(SeriesSource *m)
{
    if (m_model == m) return;
    SeriesSource *old = m_model.data();
    m_model = m;
    wireSources(old, m_referenceModel.data());
    emit inputsChanged();
    schedule();
}

void ChartsWristPresenter::setReferenceModel(SeriesSource *m)
{
    if (m_referenceModel == m) return;
    SeriesSource *old = m_referenceModel.data();
    m_referenceModel = m;
    m_refMemo = ReferenceMemo();
    wireSources(m_model.data(), old);
    emit inputsChanged();
    schedule();
}

// THE TWO SERIES (refactor 5.5c): this session's, and the ★ swing's session's — often the same object. Each is
// disconnected and connected again as a whole, so changing one never cuts the other's: the model's snapshot re-runs
// the job; the ★'s session re-runs it on its snapshot and when it finishes a read (a ★ "still being read" resolves:
// a read that changes nothing ends with busyChanged alone, SessionSeries::start). UniqueConnection: one connection
// each when the two are one object.
void ChartsWristPresenter::wireSources(SeriesSource *oldModel, SeriesSource *oldReference)
{
    for (SeriesSource *o : { oldModel, oldReference })
        if (o) disconnect(o, nullptr, this, nullptr);
    if (m_model)
        connect(m_model, &SeriesSource::snapshotChanged, this, &ChartsWristPresenter::schedule, Qt::UniqueConnection);
    if (m_referenceModel) {
        connect(m_referenceModel, &SeriesSource::snapshotChanged, this, &ChartsWristPresenter::schedule, Qt::UniqueConnection);
        connect(m_referenceModel, &SeriesSource::busyChanged, this, &ChartsWristPresenter::schedule, Qt::UniqueConnection);
    }
}

std::shared_ptr<const SessionSnapshot> ChartsWristPresenter::currentSnapshot() const
{
    return chosenSnapshot(m_directSnapshot, m_model.data());   // P2: a direct snapshot wins
}

void ChartsWristPresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot)
{
    m_directSnapshot = std::move(snapshot);
    schedule();
}

#define PP_SETTER(Type, name, field)                                       \
    void ChartsWristPresenter::name(Type v)                                \
    {                                                                      \
        if (m_in.field == v) return;                                       \
        m_in.field = v;                                                    \
        emit inputsChanged();                                              \
        schedule();                                                        \
    }
PP_SETTER(const QString &, setPickedSwingDir, pickedSwingDir)
PP_SETTER(const QString &, setRegisterName, reg)
PP_SETTER(const QString &, setScope, scope)
PP_SETTER(const QString &, setAgainst, against)
PP_SETTER(bool, setCurvesBehind, curvesBehind)
PP_SETTER(const QString &, setReferenceSwingDir, referenceSwingDir)
#undef PP_SETTER

void ChartsWristPresenter::setArchetype(int a)
{
    const int v = (a < -1 || a > 2) ? -1 : a;
    if (m_in.archetype == v) return;
    m_in.archetype = v;
    emit inputsChanged();
    schedule();
}

void ChartsWristPresenter::setPickedDetail(const QVariantMap &d)
{
    if (m_in.pickedDetail.isSharedWith(d)) return;
    m_in.pickedDetail = d;
    emit inputsChanged();
    schedule();
}

void ChartsWristPresenter::setPlayheadUs(qint64 us)
{
    if (m_playheadUs == us) return;
    m_playheadUs = us;
    emit playheadChanged();
    updateSelected(false);
}

bool ChartsWristPresenter::available() { flushIfSynchronous(); return m_result && m_result->available; }
WU::Choices ChartsWristPresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : WU::Choices{}; }
WU::ThisShot ChartsWristPresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : WU::ThisShot{}; }
WU::Session ChartsWristPresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : WU::Session{}; }
RelationsMovesWith ChartsWristPresenter::movesWith() { flushIfSynchronous(); return m_result ? m_result->movesWith : RelationsMovesWith{}; }

WU::Sentence ChartsWristPresenter::standfirst() const
{
    if (!m_result || m_selected < 0 || m_selected >= m_result->standfirsts.size()) return {};
    return m_result->standfirsts.at(m_selected);
}

qint64 ChartsWristPresenter::seekUsFor(const QString &tag) const
{
    if (!m_result) return -1;
    const int i = int(m_result->tags.indexOf(tag));
    return i >= 0 && i < kP ? m_result->cpUs[size_t(i)] : -1;
}

void ChartsWristPresenter::startJob()
{
    if (!m_sched.begin()) return;     // synchronous, or a job in flight: apply() re-runs a dirty presenter
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    in.reference = referencePickOf(m_in.referenceSwingDir, m_referenceModel.data(), in.snapshot, m_refMemo);
    emit busyChanged();
    auto w = m_worker;
    askLane(m_pool, [w, in]() { return computeWrist(*w, in); },
            [this](auto r) { apply(r); });
}

void ChartsWristPresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_sched.land();
    m_result = std::move(r);
    emit resultsChanged();
    updateSelected(true);
    m_sched.afterLanding(wasRunning);
}

void ChartsWristPresenter::flushIfSynchronous()
{
    if (!m_sched.takeFlush()) return;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    in.reference = referencePickOf(m_in.referenceSwingDir, m_referenceModel.data(), in.snapshot, m_refMemo);
    m_result = computeWrist(*m_worker, in);
    updateSelected(true);
}

void ChartsWristPresenter::setSynchronous(bool on)
{
    m_sched.setSynchronousFlag(on);
    if (on) {
        m_pool.waitForDone();
        QCoreApplication::processEvents();
        m_sched.markDirty();
    } else if (m_sched.dirty()) {
        schedule();
    }
}

bool ChartsWristPresenter::waitForIdle(int msTimeout)
{
    return m_sched.waitForIdle(m_pool, msTimeout, [this] { flushIfSynchronous(); });
}

// The playhead's checkpoint: the latest present checkpoint at or before it, else the first present one
// (the retired WristDiagnosticsModel's rule, recomputeSelected). Fires only when it moves to another checkpoint.
void ChartsWristPresenter::updateSelected(bool force)
{
    int best = -1;
    if (m_result) {
        for (int i = 0; i < kP; ++i)
            if (m_result->cpUs[size_t(i)] >= 0 && m_result->cpUs[size_t(i)] <= m_playheadUs) best = i;
        if (best < 0)
            for (int i = 0; i < kP; ++i) if (m_result->cpUs[size_t(i)] >= 0) { best = i; break; }
    }
    if (best < 0) best = 0;
    if (best != m_selected || force) {
        m_selected = best;
        emit selectedChanged();
    }
}

} // namespace pinpoint::charts

// moc for the typed results' gadgets (charts_wrist_types.h has no .cpp of its own)
#include "moc_charts_wrist_types.cpp"
