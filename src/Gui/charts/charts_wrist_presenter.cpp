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

#include "charts_relations_presenter.h"     // movesWith(): the Relations ledger, asked our own question
#include "box_map.h"
#include "charts_words.h"
#include "session_curves.h"
#include "session_relations.h"              // boxStats, pearson
#include "session_series_model.h"
#include "session_warp.h"
#include "session_wrist.h"

#include "../../Core/pp_debug.h"
#include "../../Diagnostics/engine/condition_reading.h"
#include "../../Diagnostics/engine/wrist_domain_score.h"
#include "../../Diagnostics/norms/norm.h"            // NormSource: where the wrist bands come from (TRUST)
#include "../../Diagnostics/norms/norm_provider.h"   // sharedNormProvider: the bands' own rows
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Export/swing_store.h"
#include "../../Wrist/wrist_assessment_types.h"   // dofForMetricKey
#include "../../Wrist/reference_bands.h"          // NormBandProvider::cellMeasureId, BandContext
#include "../review/timeline_labels.h"
#include "../../Metrics/gate_catalogue.h"   // the gate behind an empty wrist value (its words)

#include <QCoreApplication>
#include <QDate>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>
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
    int     archetype = 0, mode = -1, engineScore = -1;
    QString archetypeName;
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

// ── the result and the worker ───────────────────────────────────────────────────────────────

struct ChartsWristPresenter::Result {
    bool available = false;
    QVariantMap choices, thisShot, session, movesWith;
    // the session standfirst's parts, for paper (wristSessionReportPayload): scoreLine, styleLine, repeat,
    // sizeLine. Not in `session`: the screen's map is unchanged by them.
    QVariantMap sessionParts;
    QVariantList standfirsts;                 // 8 Sentences (thisShot.standfirsts)
    std::array<qint64, kP> cpUs{};            // the picked shot's checkpoint µs, −1 none
    QStringList tags;
};

struct ChartsWristPresenter::Worker {
    QMutex mutex;                                            // held for a whole job
    std::unique_ptr<TimelineLabels> tl;
    std::unique_ptr<pa::ICharacteristicPackProvider> pack;  // the shipped pack, for the conditions' words
    bool condsBuilt = false;
    std::vector<Cond> conds;                                 // the wrist domain, in pack order

    // movesWith, per snapshot revision and reading
    const SessionSnapshot *mwSnap = nullptr;
    quint64 mwRev = 0;
    QString mwId;
    QVariantMap mw;

    // the ★ reference swing, by its cleaned dir and document stamp
    QString refDir;
    qint64  refSize = -1, refMtimeMs = -1;
    std::shared_ptr<const ShotSeries> refShot;
    QString refWhy;

    void ensure()
    {
        if (!tl) tl = std::make_unique<TimelineLabels>();
        if (!pack) pack = pa::makeCharacteristicPackProvider();
        if (condsBuilt) return;
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


QVariantList numList(const std::vector<double> &v)
{
    QVariantList out;
    for (double x : v) out.append(nv(x));
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
    out.engineScore = p.value(QStringLiteral("engineScore"), -1).toInt();
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
    out.has = !out.dofs.empty();
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
    sh->shotId = SessionSeriesModel::folderShotId(dir);
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

// The reference swing, read on the worker (ChartsTracesPresenter's rule): the session's own copy when
// it is one of its shots, else SwingStore::loadAnalysis → extractShot, cached by the document's stamp.
std::shared_ptr<const ShotSeries> referenceShot(ChartsWristPresenter::Worker &w, const QString &rawDir,
                                                const std::shared_ptr<const SessionSnapshot> &snap, QString *why)
{
    const QString dir = cleanDir(rawDir);
    if (dir.isEmpty()) { *why = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions."); return {}; }
    if (snap)
        for (const auto &s : snap->shots) if (cleanDir(s->swingDir) == dir) { why->clear(); return s; }
    const pinpoint::SwingStore::DocInfo info = pinpoint::SwingStore::info(dir);
    if (dir == w.refDir && info.size == w.refSize && info.mtimeMs == w.refMtimeMs) { *why = w.refWhy; return w.refShot; }
    w.refDir = dir; w.refSize = info.size; w.refMtimeMs = info.mtimeMs;
    w.refShot.reset(); w.refWhy.clear();
    if (!info.exists()) {
        w.refWhy = QStringLiteral("The reference swing is no longer on disk.");
    } else {
        QString err, whyNot;
        const QJsonObject root = pinpoint::SwingStore::loadAnalysis(dir, &err);
        std::shared_ptr<ShotSeries> shot;
        if (!root.isEmpty()) shot = SessionSeriesModel::extractShot(root, dir, &whyNot);
        if (shot) w.refShot = shot;
        else w.refWhy = QStringLiteral("The reference swing cannot be read: %1.")
                            .arg(whyNot.isEmpty() ? QStringLiteral("the swing document could not be read") : whyNot);
        if (!err.isEmpty()) ppWarn() << "[Charts] wrist reference" << dir << err;
    }
    *why = w.refWhy;
    return w.refShot;
}

QString referenceLabelOf(const QString &dir)
{
    const QFileInfo fi(dir);
    const int id = SessionSeriesModel::folderShotId(dir);
    const QDate d = QDate::fromString(QFileInfo(fi.absolutePath()).fileName().left(10), QStringLiteral("yyyy-MM-dd"));
    QStringList parts;
    if (id >= 0) parts << QStringLiteral("shot %1").arg(id);
    if (d.isValid()) parts << QLocale(QLocale::English).toString(d, QStringLiteral("d MMM yyyy"));
    return parts.join(QStringLiteral(", "));
}

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
    int ready = 0;                               // shots with a wrist DOF
    std::shared_ptr<const ShotSeries> ref;
    Profile refProf;
    QString refWhy, refLabel;
    bool refIsPicked = false;
    // THE NOUN the session's sentences count in: "shots" on Charts, "swings" in the report (its own word).
    QString many = QStringLiteral("shots"), one = QStringLiteral("shot");
    // the report's band wash (wristSessionReportPayload): the dominant style, −1 = the screen (the picked shot's)
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

QVariantMap findingMap(const Ctx &c, const ShotSeries &s, const Profile &p, const Cond &k, const Verdicts &v, bool strength,
                       const QStringList &corroborated)
{
    const int ph = condPhase(s, k);
    const QString tag = ph >= 0 ? c.tagOf(ph, p) : QString();
    const qint64 seek = ph >= 0 ? phaseUsOf(s, ph) : -1;
    const ShotDiagnostics::Value val = s.diagnostics.values.value(k.id);
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
    QVariantList more;
    const auto addMore = [&](const QString &kk, const QString &vv) {
        if (!vv.isEmpty()) more.append(QVariantMap{ { QStringLiteral("k"), kk }, { QStringLiteral("v"), vv } });
    };
    addMore(strength ? QStringLiteral("Protect") : QStringLiteral("Coaching"), strength ? protect : coaching);
    addMore(QStringLiteral("Linked to"), k.linked.join(QStringLiteral(", ")));
    addMore(QStringLiteral("Corroborated by"), corroborated.join(QStringLiteral(", ")));
    const int mag = std::isfinite(outBy) && unit == QStringLiteral("°") ? int(std::lround(outBy)) : 0;
    return {
        { QStringLiteral("id"), k.id },
        { QStringLiteral("name"), strength ? W::cap1(k.golferWell) : k.label },
        { QStringLiteral("severity"), severity },
        { QStringLiteral("severityWord"), strength ? QStringLiteral("working") : severity },
        { QStringLiteral("tier"), tier },
        { QStringLiteral("pointsLost"), lost },
        { QStringLiteral("pointsText"), lost > 0 ? QString(QChar(0x2212)) + QString::number(lost) + QStringLiteral(" points") : QString() },
        { QStringLiteral("positions"), tag.isEmpty() ? QStringList{} : QStringList{ tag } },
        { QStringLiteral("positionsText"), tag.isEmpty() ? QString() : QStringLiteral("at ") + tag },
        { QStringLiteral("phase"), ph },
        { QStringLiteral("seekUs"), qlonglong(seek) },
        { QStringLiteral("value"), strength ? nullv() : nv(val.value) },
        { QStringLiteral("valueText"), strength ? QString() : W::figUnit(val.value, unit) },
        { QStringLiteral("outBy"), strength ? nullv() : nv(outBy) },
        { QStringLiteral("magnitude"), strength ? 0 : mag },
        { QStringLiteral("magnitudeText"), !strength && mag > 0 ? degBare(mag) : QString() },
        { QStringLiteral("confidence"), conf },
        { QStringLiteral("pips"), pips(conf) },
        { QStringLiteral("lowConfidence"), conf < 0.5 },
        { QStringLiteral("explanation"), explanation },
        { QStringLiteral("coaching"), coaching },
        { QStringLiteral("protect"), protect },
        { QStringLiteral("linkedTo"), k.linked },
        { QStringLiteral("corroboratedBy"), corroborated },
        { QStringLiteral("more"), more } };
}

// Findings (fired), severity then confidence then how far out; strengths (an axis clean on every tail
// the shot assessed, said in its golfer words), in pack order.
void ledgersOf(const Ctx &c, const ShotSeries &s, const Profile &p, const Verdicts &v, QVariantList *findings, QVariantList *strengths)
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
        findings->append(findingMap(c, s, p, *f.k, v, false, corr));
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
        strengths->append(findingMap(c, s, p, *speak, v, true, {}));
    }
}

// ── the profile rows ────────────────────────────────────────────────────────────────────────

QVariantList positionsOf(const Ctx &c, const Profile &p)
{
    QVariantList out;
    for (int i = 0; i < kP; ++i) {
        const Checkpoint &k = p.cps[size_t(i)];
        out.append(QVariantMap{ { QStringLiteral("index"), i }, { QStringLiteral("tag"), k.tag.isEmpty() ? QStringLiteral("P%1").arg(i + 1) : k.tag },
                                { QStringLiteral("phase"), k.phase }, { QStringLiteral("name"), k.phase >= 0 ? c.phaseName(k.phase) : QString() },
                                { QStringLiteral("note"), k.note }, { QStringLiteral("available"), k.available },
                                { QStringLiteral("tUs"), qlonglong(k.tUs) }, { QStringLiteral("seekUs"), qlonglong(k.available ? k.tUs : -1) } });
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
QVariantMap pointMap(const Dof &d, const Cell &c, bool shotScope)
{
    const bool out = shotScope && c.out() && c.banded;
    const bool high = out && c.delta > c.hi;
    const double by = out ? (high ? c.delta - c.hi : c.lo - c.delta) : kNaN;
    const QString pole = out ? (high ? d.poleTop : d.poleBottom) : QString();
    const QString sev = c.rag == QLatin1String("red") ? QStringLiteral("fault") : QStringLiteral("watch");
    return { { QStringLiteral("available"), c.available }, { QStringLiteral("value"), nv(c.delta) },
             { QStringLiteral("valueText"), c.available ? degText(c.delta) : QStringLiteral("-") },
             { QStringLiteral("rag"), c.rag }, { QStringLiteral("banded"), c.banded },
             { QStringLiteral("bandLo"), nv(c.lo) }, { QStringLiteral("bandHi"), nv(c.hi) },
             { QStringLiteral("out"), out }, { QStringLiteral("fault"), out && c.rag == QLatin1String("red") },
             { QStringLiteral("watch"), out && c.rag == QLatin1String("amber") },
             { QStringLiteral("outBy"), nv(by) }, { QStringLiteral("outPole"), pole },
             { QStringLiteral("word"), out ? sev + QStringLiteral(" · ") + degBare(by) + QLatin1Char(' ') + pole : QString() },
             { QStringLiteral("wordShort"), out ? sev + QLatin1Char(' ') + pole : QString() } };
}

// The 40 ms windowed curve behind a row: the shot's own curve, its time stretched piecewise-linearly
// between the checkpoint ticks (x = the checkpoint index) and offset so it passes the first measured
// checkpoint's Δ (the profile's numbers are Δ from address; the curve is the raw angle).
QVariantList traceFor(const ShotSeries &s, const Profile &p, const Dof &d)
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
    QVariantList out;
    for (size_t j = 0; j < idx.size(); j += stride) {
        const size_t i = idx[j];
        const bool ok = curve.ok[i] && std::isfinite(curve.v[i]);
        out.append(QVariantMap{ { QStringLiteral("x"), warp.warpMs(curve.t[i]) }, { QStringLiteral("v"), ok ? QVariant(curve.v[i] + off) : nullv() } });
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

QVariantMap styleMap(const Profile &p)
{
    QVariantMap m;
    const bool has = p.resemblance.contains(QStringLiteral("neutral"));
    const bool byHand = p.mode >= 0;
    QVariantList items;
    W::Sentence line;
    static const char *const keys[] = { "neutral", "bowed", "cupped" };
    if (has) line.append(QStringLiteral("Resembles "));
    else line.append(QStringLiteral("Style resemblance: - (this swing’s analysis carries no resemblance index); "));
    for (int i = 0; i < 3; ++i) {
        const QString k = QLatin1String(keys[i]);
        const QVariant pv = p.resemblance.value(k);
        const QString pct = has ? QString::number(pv.toInt()) + QChar(0x00A0) + QStringLiteral("%") : QStringLiteral("-");
        items.append(QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("name"), wristArchetypeName(i) },
                                  { QStringLiteral("pct"), has ? pv : nullv() }, { QStringLiteral("pctText"), pct },
                                  { QStringLiteral("graded"), p.archetype == i } });
        if (has) {
            if (i > 0) line.append(QStringLiteral(" · "));
            line.append(wristArchetypeName(i) + QLatin1Char(' ')).figure(pct);
        }
    }
    if (has) line.append(byHand ? QStringLiteral("; bands set to %1 by hand.").arg(p.archetypeName)
                                : QStringLiteral("; the bands are %1’s.").arg(p.archetypeName));
    else line.append(byHand ? QStringLiteral("bands set to %1 by hand.").arg(p.archetypeName)
                            : QStringLiteral("Auto resolved the bands to %1.").arg(p.archetypeName));
    m.insert(QStringLiteral("hasIndex"), has);
    m.insert(QStringLiteral("archetype"), p.archetype);
    m.insert(QStringLiteral("archetypeName"), p.archetypeName);
    m.insert(QStringLiteral("mode"), p.mode);
    m.insert(QStringLiteral("byHand"), byHand);
    m.insert(QStringLiteral("items"), items);
    m.insert(QStringLiteral("line"), line.toVariant());
    return m;
}

QVariantMap scoreMap(const Verdicts &v, const Profile &p)
{
    QVariantMap m;
    const bool ok = v.graded && !v.refused && v.score.score >= 0;
    double pen = 0.0;
    QVariantList cs;
    for (const pa::WristDomainContribution &c : v.score.contributions) {
        pen += c.penalty;
        const int pr = int(std::lround(c.penalty));
        cs.append(QVariantMap{ { QStringLiteral("id"), c.id }, { QStringLiteral("label"), c.label }, { QStringLiteral("penalty"), pr },
                               { QStringLiteral("penaltyText"), QString(QChar(0x2212)) + QString::number(pr) } });
    }
    const QString sum = ok ? QStringLiteral("%1 %2 %3 = %4").arg(v.score.base).arg(QChar(0x2212)).arg(int(std::lround(pen))).arg(v.score.score) : QString();
    m.insert(QStringLiteral("score"), ok ? QVariant(v.score.score) : nullv());
    m.insert(QStringLiteral("scoreText"), ok ? QString::number(v.score.score) : QStringLiteral("-"));
    m.insert(QStringLiteral("band"), ok ? v.score.band : QString());
    m.insert(QStringLiteral("base"), v.score.base);
    m.insert(QStringLiteral("sumText"), sum);
    m.insert(QStringLiteral("contributions"), ok ? cs : QVariantList());
    m.insert(QStringLiteral("line"), ok ? QStringLiteral("Wrist score %1 of 100, %2  ·  %3").arg(v.score.score).arg(v.score.band, sum)
                                        : v.refused ? QStringLiteral("Wrist score -")
                                        : v.graded ? QStringLiteral("Wrist score -: no wrist condition could be read on this swing")
                                                   : QStringLiteral("Wrist score -: diagnostics have not graded this swing yet"));
    m.insert(QStringLiteral("engineScore"), p.has && p.engineScore >= 0 ? QVariant(p.engineScore) : nullv());
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
QVariantMap trustOf(const Ctx &c, const ShotSeries *shot, const Profile *p, const Verdicts *v,
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
        return { { QStringLiteral("lines"), lines } };
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
    if (c.ready < c.n)
        lines << QStringLiteral("No wrist reading %1.").arg(onCount(c, c.n - c.ready, c.n));
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
    return { { QStringLiteral("lines"), lines } };
}

void buildThisShot(Ctx &c, const std::vector<DofAgg> &aggs, ChartsWristPresenter::Result &R)
{
    QVariantMap out;
    R.cpUs.fill(-1);
    for (int i = 0; i < kP; ++i) R.tags << QStringLiteral("P%1").arg(i + 1);
    const bool has = c.shot && c.pick.has;
    out.insert(QStringLiteral("available"), has);
    if (!has) {
        out.insert(QStringLiteral("empty"), (c.shot || !c.in.pickedSwingDir.trimmed().isEmpty()
                                                 ? QStringLiteral("No wrist metrics for this swing.") : QStringLiteral("Select a swing to review."))
                                                + emptyTail());
        R.thisShot = out;
        return;
    }
    const ShotSeries &s = *c.shot;
    const Profile &p = c.pick;
    for (int i = 0; i < kP; ++i) { R.cpUs[size_t(i)] = p.cps[size_t(i)].available ? p.cps[size_t(i)].tUs : -1; R.tags[i] = p.cps[size_t(i)].tag; }
    const bool inSession = c.index >= 0;
    out.insert(QStringLiteral("empty"), QString());
    out.insert(QStringLiteral("ordinal"), s.ordinal);
    out.insert(QStringLiteral("shotId"), s.shotId);
    out.insert(QStringLiteral("swingDir"), s.swingDir);
    out.insert(QStringLiteral("inSession"), inSession);
    const QString club = s.club.toLower();
    out.insert(QStringLiteral("kicker"), (inSession ? QStringLiteral("Shot %1 of %2").arg(s.ordinal).arg(c.n) : QStringLiteral("This swing"))
                                             + (club.isEmpty() ? QString() : QStringLiteral("  ·  ") + club) + QStringLiteral("  ·  Wrist"));

    const Verdicts v = verdictsOf(c, s);
    QVariantList findings, strengths;
    ledgersOf(c, s, p, v, &findings, &strengths);

    int outCells = 0;
    for (const Dof &d : p.dofs) if (d.hasStrip) for (const Cell &cell : d.cells) if (cell.out()) ++outCells;
    out.insert(QStringLiteral("outCells"), outCells);
    out.insert(QStringLiteral("graded"), v.graded);

    // THE HEAD: the first finding; or, with none, the cells said honestly (W-D6)
    W::Sentence head;
    const QString cells = QStringLiteral("%1 checkpoint%2").arg(outCells).arg(outCells == 1 ? QString() : QStringLiteral("s"));
    // a gate refused the wrist: no all-clear without the readings to back it (Mark, 17 Oct 2026); a finding still heads
    if (v.refused && findings.isEmpty()) {
    } else if (!v.graded) {
        if (outCells == 0) head.append(QStringLiteral("The wrist stayed inside its bands at every checkpoint; diagnostics have not graded this swing yet"));
        else head.figure(W::cap1(cells)).append(QStringLiteral(" sat outside a band; diagnostics have not graded this swing yet"));
    } else if (findings.isEmpty()) {
        if (outCells == 0) head.append(QStringLiteral("The wrist stayed inside its bands at every checkpoint"));
        else head.append(QStringLiteral("No finding fired; ")).figure(cells).append(outCells == 1 ? QStringLiteral(" sat outside a band") : QStringLiteral(" sat outside a band"));
    } else {
        const QVariantMap f0 = findings.first().toMap();
        head.append(f0.value(QStringLiteral("name")).toString());
        const QStringList pos = f0.value(QStringLiteral("positions")).toStringList();
        if (!pos.isEmpty()) head.append(QStringLiteral(" at ")).figure(pos.join(QStringLiteral(", ")));
        if (findings.size() > 1) head.append(QStringLiteral(", and ") + W::word(int(findings.size()) - 1) + QStringLiteral(" more"));
    }
    out.insert(QStringLiteral("head"), head.toVariant());

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
                if (!g.words.isEmpty()) st.append(shortName(d.id) + QLatin1Char(' ') + g.words);
                else st.append(shortName(d.id) + QLatin1Char(' ')).figure(cell.available ? degText(cell.delta) : QStringLiteral("-"));
                if (cell.out() && cell.banded)
                    st.append(QStringLiteral(" outside its band, ") + (cell.delta > cell.hi ? d.poleTop : d.poleBottom));
            }
            st.append(QStringLiteral(", from address."));
        }
        R.standfirsts.append(st.toVariant());
    }
    out.insert(QStringLiteral("standfirsts"), R.standfirsts);
    {
        // NO SCORE BECAUSE A GATE REFUSED THE WRIST: a plain "-"; the standfirst says what was out of sight and the
        // section's tail line names it (said once; Mark, 17 Oct 2026)
        out.insert(QStringLiteral("score"), scoreMap(v, p));
    }
    out.insert(QStringLiteral("style"), styleMap(p));

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
    QVariantList rows;
    QStringList present;
    for (const Dof &d : p.dofs) {
        present << d.id;
        if (!d.hasStrip) {
            QVariantList cells;
            for (int i = 0; i < kP; ++i) {
                const Cell &cell = d.cells[size_t(i)];
                QVariantMap cm{ { QStringLiteral("available"), cell.available }, { QStringLiteral("value"), nv(cell.delta) },
                                { QStringLiteral("valueText"), cell.available ? degText(cell.delta) : QStringLiteral("-") } };
                cells.append(cm);
            }
            rows.append(QVariantMap{ { QStringLiteral("numbersOnly"), true }, { QStringLiteral("dof"), d.id }, { QStringLiteral("key"), d.key },
                                     { QStringLiteral("name"), d.name }, { QStringLiteral("sub"), QStringLiteral("no corridor: the angle only") },
                                     { QStringLiteral("cells"), cells } });
            continue;
        }
        Scale sc;
        QVariantList points;
        int outCount = 0;
        for (int i = 0; i < kP; ++i) {
            const Cell &cell = d.cells[size_t(i)];
            QVariantMap pm = pointMap(d, cell, true);
            points.append(pm);
            sc.add(cell.delta);
            if (cell.banded) { sc.add(cell.lo); sc.add(cell.hi); }
            if (cell.out()) ++outCount;
        }
        QVariant ghost = nullv();
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
        const QVariantList trace = c.in.curvesBehind ? traceFor(s, p, d) : QVariantList{};
        for (const QVariant &t : trace) { const QVariant tv = t.toMap().value(QStringLiteral("v")); if (!tv.isNull()) sc.add(tv.toDouble()); }
        sc.finish();
        rows.append(QVariantMap{
            { QStringLiteral("numbersOnly"), false }, { QStringLiteral("dof"), d.id }, { QStringLiteral("key"), d.key },
            { QStringLiteral("name"), d.name }, { QStringLiteral("sub"), d.sub }, { QStringLiteral("source"), d.source },
            { QStringLiteral("confidence"), d.conf }, { QStringLiteral("pips"), pips(d.conf) },
            { QStringLiteral("sourceText"), d.source.isEmpty() ? QString() : d.source + QLatin1Char(' ') + pips(d.conf) },
            { QStringLiteral("short"), shortName(d.id) },
            { QStringLiteral("poleTop"), d.poleTop }, { QStringLiteral("poleBottom"), d.poleBottom },
            { QStringLiteral("poleTopText"), d.poleTop.isEmpty() ? QString() : QStringLiteral("↑ ") + d.poleTop },
            { QStringLiteral("poleBottomText"), d.poleBottom.isEmpty() ? QString() : QStringLiteral("↓ ") + d.poleBottom },
            { QStringLiteral("points"), points }, { QStringLiteral("ghost"), ghost }, { QStringLiteral("trace"), trace },
            { QStringLiteral("boxes"), nullv() }, { QStringLiteral("yLo"), sc.lo }, { QStringLiteral("yHi"), sc.hi },
            { QStringLiteral("outCount"), outCount } });
    }
    QStringList missing;
    // The lead forearm's ROLL (the radioulnar pronation strip) is a different quantity from the forearm's
    // rotation from address, which the shot may still carry and Beside your session lists: say which is
    // absent, and that the rotation is monitored, not graded (rule 2), rather than "not measured".
    const bool forearmRotation = s.metric(QStringLiteral("hm.forearmRotation")) || s.metric(QStringLiteral("forearmRotation"));
    bool rollNote = false;
    // each missing DOF a gate refused names the gate's words, and opens it (missingParts: the QML links them)
    QStringList missingNamed;
    QVariantList missingParts;
    for (const QString &id : dofOrder()) {
        if (present.contains(id)) continue;
        if (id == QLatin1String("leadForearmRot") && forearmRotation) { rollNote = true; continue; }
        missing << longName(id);
        const WristGate g = wristGate(&s, id, QString(), kNaN);
        missingNamed << (g.words.isEmpty() ? longName(id) : longName(id) + QStringLiteral(" (") + g.words + QLatin1Char(')'));
        missingParts << QVariantMap{ { QStringLiteral("name"), longName(id) }, { QStringLiteral("words"), g.words },
                                     { QStringLiteral("subject"), g.subject } };
    }
    QString missingText = missing.isEmpty() ? QString() : QStringLiteral("Not measured on this swing: ") + missingNamed.join(QStringLiteral(", ")) + QStringLiteral(".");
    const QString rollText = rollNote ? QStringLiteral("The lead forearm's roll (pronation) has no strip here; its rotation from address is shown beside your session, monitored, not graded.")
                                      : QString();
    if (rollNote)
        missingText += (missingText.isEmpty() ? QString() : QStringLiteral(" ")) + rollText;
    QString sub = QStringLiteral("Δ from address, degrees");
    if (!ghostWord.isEmpty()) sub += QStringLiteral("  ·  dashed: ") + ghostWord;
    if (c.in.curvesBehind) sub += QStringLiteral("  ·  faint: the 40 ms curve, stretched between checkpoints");
    out.insert(QStringLiteral("profile"), QVariantMap{
        { QStringLiteral("positions"), positionsOf(c, p) }, { QStringLiteral("rows"), rows },
        { QStringLiteral("ghostWord"), ghostWord }, { QStringLiteral("ghostRefusal"), ghostRefusal }, { QStringLiteral("sub"), sub },
        { QStringLiteral("caption"), QStringLiteral("Shaded: the %1 band at each checkpoint. A ring and a word under the value: outside it, "
                                                    "graded by how far (fault, watch). A finding's word is the session's: pattern, it keeps "
                                                    "firing across the session; watching, not yet.").arg(p.archetypeName) },
        { QStringLiteral("missing"), missing },
        { QStringLiteral("missingParts"), missingParts },
        { QStringLiteral("missingTail"), rollText },
        { QStringLiteral("missingText"), missingText } });
    out.insert(QStringLiteral("findings"), findings);
    out.insert(QStringLiteral("strengths"), strengths);
    out.insert(QStringLiteral("findingsEmpty"), v.graded ? QStringLiteral("No faults and nothing to watch.") : QStringLiteral("Diagnostics have not graded this swing yet."));
    out.insert(QStringLiteral("strengthsEmpty"), QStringLiteral("Nothing stood out as a strength on this swing."));
    out.insert(QStringLiteral("trust"), trustOf(c, &s, &p, &v, nullptr, QString()));
    R.thisShot = out;
}

// ── Session ─────────────────────────────────────────────────────────────────────────────────

QVariantMap driftMap(const Correlation &d, int n)
{
    const auto r2 = [](double r) {
        if (!std::isfinite(r)) return QStringLiteral("-");
        QString t = QString::number(std::abs(r), 'f', 2);
        if (t.startsWith(QLatin1Char('0'))) t = t.mid(1);
        return (r < 0 ? QString(QChar(0x2212)) : QString()) + t;
    };
    QVariantMap out{ { QStringLiteral("defined"), d.defined }, { QStringLiteral("r"), nv(d.r) }, { QStringLiteral("lo"), nv(d.lo) },
                     { QStringLiteral("hi"), nv(d.hi) }, { QStringLiteral("clear"), d.clear }, { QStringLiteral("rText"), r2(d.r) },
                     { QStringLiteral("ciText"), d.defined ? QStringLiteral("(%1 to %2)").arg(r2(d.lo), r2(d.hi)) : QString() },
                     { QStringLiteral("slope"), nv(d.slope) }, { QStringLiteral("intercept"), nv(d.intercept) } };
    out.insert(QStringLiteral("line"), d.defined && d.clear
        ? QVariant(QVariantMap{ { QStringLiteral("x0"), 1 }, { QStringLiteral("y0"), d.intercept + d.slope },
                                { QStringLiteral("x1"), n }, { QStringLiteral("y1"), d.intercept + d.slope * n } })
        : nullv());
    return out;
}

void buildSession(Ctx &c, const std::vector<DofAgg> &aggs, ChartsWristPresenter::Result &R)
{
    QVariantMap out;
    out.insert(QStringLiteral("n"), c.n);
    out.insert(QStringLiteral("ready"), c.ready);
    out.insert(QStringLiteral("small"), c.n < 10);
    out.insert(QStringLiteral("available"), c.ready > 0);
    QStringList clubs;
    for (int i = 0; i < c.n; ++i) { const QString k = c.snap->shots[size_t(i)]->club.toLower(); if (!k.isEmpty() && !clubs.contains(k)) clubs << k; }
    out.insert(QStringLiteral("kicker"), QStringLiteral("%1 %2").arg(c.n).arg(c.many)
                                             + (clubs.size() == 1 ? QStringLiteral("  ·  ") + clubs.first() : clubs.size() > 1 ? QStringLiteral("  ·  mixed clubs") : QString())
                                             + QStringLiteral("  ·  Wrist"));
    if (c.ready == 0) {
        out.insert(QStringLiteral("head"), W::Sentence().append(QStringLiteral("No %1 in this session carries a wrist reading").arg(c.one)).toVariant());
        out.insert(QStringLiteral("recurringEmpty"), QStringLiteral("No wrist finding fired on any %1.").arg(c.one));
        R.session = out;
        return;
    }
    // the picked shot's profile for the wash and the positions, else the first shot with one
    const Profile *wash = c.pick.has ? &c.pick : nullptr;
    if (!wash) for (const Profile &p : c.profs) if (p.has) { wash = &p; break; }
    out.insert(QStringLiteral("positions"), positionsOf(c, *wash));

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
    out.insert(QStringLiteral("head"), head.toVariant());

    // THE STANDFIRST: the score, the most and least repeatable checkpoint, the resemblance index, the n
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
    struct Spread { QString name, tag; double w; };
    std::vector<Spread> spreads;
    for (const DofAgg &a : aggs)
        for (int p = 2; p < kP; ++p)
            if (a.box[size_t(p)].n >= 3)
                spreads.push_back({ longName(a.id), wash->cps[size_t(p)].tag.isEmpty() ? QStringLiteral("P%1").arg(p + 1) : wash->cps[size_t(p)].tag,
                                    a.box[size_t(p)].q3 - a.box[size_t(p)].q1 });
    std::stable_sort(spreads.begin(), spreads.end(), [](const Spread &a, const Spread &b) { return a.w < b.w; });
    if (spreads.size() >= 2) {
        const Spread &a = spreads.front(), &b = spreads.back();
        stRepeat.append(QStringLiteral("From P3 on, most repeatable: %1 at %2 (middle half ").arg(a.name, a.tag)).figure(degBare(a.w))
          .append(QStringLiteral(" wide); least: %1 at %2 (").arg(b.name, b.tag)).figure(degBare(b.w)).append(QStringLiteral("). "));
    }
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
    out.insert(QStringLiteral("standfirst"), st.toVariant());

    // THE CHECKPOINT BOXES
    QVariantList rows;
    for (const DofAgg &a : aggs) {
        const Dof *meta = c.pick.dof(a.id);
        if (!meta) for (const Profile &p : c.profs) if ((meta = p.dof(a.id))) break;
        const Dof *mine = c.pick.dof(a.id);
        Scale sc;
        QVariantList points, boxes;
        for (int p = 0; p < kP; ++p) {
            const BoxStats &b = a.box[size_t(p)];
            if (mine) {
                const Cell &cell = mine->cells[size_t(p)];
                QVariantMap pm = pointMap(*mine, cell, false);
                points.append(pm);
                if (cell.banded) { sc.add(cell.lo); sc.add(cell.hi); }
            } else {
                QVariantMap pm{ { QStringLiteral("available"), false }, { QStringLiteral("banded"), false }, { QStringLiteral("value"), nullv() },
                                { QStringLiteral("valueText"), QStringLiteral("-") }, { QStringLiteral("out"), false } };
                points.append(pm);
            }
            sc.add(b.min); sc.add(b.max);
            const double med = b.n >= 3 ? b.med : kNaN, wide = b.n >= 3 ? b.q3 - b.q1 : kNaN;
            boxes.append(QVariantMap{ { QStringLiteral("box"), boxMap(b, degText) }, { QStringLiteral("values"), numList(a.values[size_t(p)]) },
                                      { QStringLiteral("n"), b.n }, { QStringLiteral("median"), nv(med) },
                                      { QStringLiteral("medianText"), b.n >= 3 ? degText(med) : QStringLiteral("-") },
                                      { QStringLiteral("wide"), nv(wide) },
                                      { QStringLiteral("wideText"), b.n >= 3 ? W::fig(wide) + QStringLiteral("° wide") : QString() },
                                      { QStringLiteral("picked"), mine ? nv(mine->cells[size_t(p)].delta) : nullv() } });
        }
        sc.finish();
        rows.append(QVariantMap{
            { QStringLiteral("numbersOnly"), false }, { QStringLiteral("dof"), a.id }, { QStringLiteral("key"), meta->key },
            { QStringLiteral("name"), meta->name }, { QStringLiteral("sub"), meta->sub }, { QStringLiteral("source"), mine ? mine->source : QString() },
            { QStringLiteral("confidence"), mine ? mine->conf : 0.0 }, { QStringLiteral("pips"), mine ? pips(mine->conf) : QString() },
            { QStringLiteral("sourceText"), mine && !mine->source.isEmpty() ? mine->source + QLatin1Char(' ') + pips(mine->conf) : QString() },
            { QStringLiteral("short"), shortName(a.id) },
            { QStringLiteral("poleTop"), meta->poleTop }, { QStringLiteral("poleBottom"), meta->poleBottom },
            { QStringLiteral("poleTopText"), meta->poleTop.isEmpty() ? QString() : QStringLiteral("↑ ") + meta->poleTop },
            { QStringLiteral("poleBottomText"), meta->poleBottom.isEmpty() ? QString() : QStringLiteral("↓ ") + meta->poleBottom },
            { QStringLiteral("points"), points }, { QStringLiteral("ghost"), nullv() }, { QStringLiteral("trace"), QVariantList{} },
            { QStringLiteral("boxes"), boxes }, { QStringLiteral("yLo"), sc.lo }, { QStringLiteral("yHi"), sc.hi }, { QStringLiteral("outCount"), 0 } });
    }
    out.insert(QStringLiteral("rows"), rows);
    const QString pickedWord = c.index >= 0 ? QStringLiteral("%1 %2’s").arg(c.one).arg(c.shot->ordinal) : QStringLiteral("this swing’s");
    out.insert(QStringLiteral("caption"), wash == &c.pick
        ? QStringLiteral("Shaded: %1 %2 band. ").arg(pickedWord, c.pick.archetypeName)
              + (c.in.archetype < 0 ? QStringLiteral("Each %1 is graded against its own style’s band on This shot.").arg(c.one)
                                    : QStringLiteral("Every %1 is graded against the %2 band.").arg(c.one, wristArchetypeName(c.in.archetype)))
        : QStringLiteral("No band is shaded: the picked swing has no wrist reading."));
    out.insert(QStringLiteral("sub"), QStringLiteral("Δ from address, degrees  ·  a box per checkpoint, n %1, this %2 ringed  ·  under each: median, and how wide the middle half is").arg(c.ready).arg(c.one));

    // THE SCORE RUN (PpChRunBox's RUN)
    QVariantList pts;
    std::vector<double> xs, ys;
    for (int i = 0; i < c.n; ++i) {
        const ShotSeries &s = *c.snap->shots[size_t(i)];
        const double v = scores[size_t(i)];
        pts.append(QVariantMap{ { QStringLiteral("ordinal"), s.ordinal }, { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("swingDir"), s.swingDir },
                                { QStringLiteral("value"), nv(v) }, { QStringLiteral("measured"), std::isfinite(v) }, { QStringLiteral("fired"), false },
                                { QStringLiteral("picked"), i == c.index } });
        xs.push_back(double(i + 1));
        ys.push_back(v);
    }
    out.insert(QStringLiteral("scoreBox"), boxMap(sb, scoreText));
    out.insert(QStringLiteral("scoreRun"), QVariantMap{
        { QStringLiteral("readingId"), QStringLiteral("wristScore") }, { QStringLiteral("key"), QString() },
        { QStringLiteral("label"), QStringLiteral("Wrist score") }, { QStringLiteral("tag"), QString() },
        { QStringLiteral("unit"), QString() }, { QStringLiteral("unitText"), QString() },
        { QStringLiteral("caption"), QStringLiteral("Wrist score, %1 by %1 (0–100)").arg(c.one) }, { QStringLiteral("points"), pts },
        { QStringLiteral("box"), boxMap(sb, scoreText) },
        { QStringLiteral("yRange"), QVariantMap{ { QStringLiteral("lo"), 0 }, { QStringLiteral("hi"), 100 } } },
        { QStringLiteral("drift"), driftMap(pearson(xs, ys), c.n) } });

    // THE STYLE TABLE
    QVariantList cols, srows, counts;
    for (int i = 0; i < c.n; ++i) {
        const ShotSeries &s = *c.snap->shots[size_t(i)];
        cols.append(QVariantMap{ { QStringLiteral("ordinal"), s.ordinal }, { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("swingDir"), s.swingDir },
                                 { QStringLiteral("label"), W::cap1(c.one) + QStringLiteral(" %1").arg(s.ordinal) },
                                 { QStringLiteral("short"), QString::number(s.ordinal) },   // the header when columns are narrow
                                 { QStringLiteral("picked"), i == c.index } });
    }
    bool anyIndex = false;
    for (int i = 0; i < c.n; ++i) anyIndex = anyIndex || (c.profs[size_t(i)].has && c.profs[size_t(i)].resemblance.contains(QStringLiteral("neutral")));
    static const char *const keys[] = { "neutral", "bowed", "cupped" };
    for (int k = 0; k < 3 && anyIndex; ++k) {   // no index on any shot: one quiet line, not a grid of "-"
        QVariantList cells;
        for (int i = 0; i < c.n; ++i) {
            const Profile &p = c.profs[size_t(i)];
            const QVariant v = p.resemblance.value(QLatin1String(keys[k]));
            cells.append(QVariantMap{ { QStringLiteral("text"), p.has && v.isValid() ? QString::number(v.toInt()) : QStringLiteral("-") },
                                      { QStringLiteral("graded"), p.has && p.archetype == k } });
        }
        srows.append(QVariantMap{ { QStringLiteral("key"), QString::fromLatin1(keys[k]) }, { QStringLiteral("name"), wristArchetypeName(k) }, { QStringLiteral("cells"), cells } });
    }
    for (const auto &sl : styleList) counts.append(QVariantMap{ { QStringLiteral("name"), sl.first }, { QStringLiteral("n"), sl.second } });
    out.insert(QStringLiteral("styles"), QVariantMap{
        { QStringLiteral("columns"), anyIndex ? cols : QVariantList{} }, { QStringLiteral("rows"), srows }, { QStringLiteral("counts"), counts },
        { QStringLiteral("hasIndex"), anyIndex },
        { QStringLiteral("note"), anyIndex ? QString() : QStringLiteral("No %1 in this session carries a resemblance index, so there is no style to compare.").arg(c.one) } });

    // WHAT KEEPS FIRING
    struct Rec { const Cond *k; int count; QVariantList marks; QList<QPair<QString, int>> pos; };
    std::vector<Rec> recs;
    for (const Cond &k : c.w.conds) {
        Rec r{ &k, 0, {}, {} };
        for (int i = 0; i < c.n; ++i) {
            const ShotSeries &s = *c.snap->shots[size_t(i)];
            const bool f = std::find(vs[size_t(i)].fired.begin(), vs[size_t(i)].fired.end(), &k) != vs[size_t(i)].fired.end();
            r.marks.append(QVariantMap{ { QStringLiteral("ordinal"), s.ordinal }, { QStringLiteral("shotId"), s.shotId },
                                        { QStringLiteral("swingDir"), s.swingDir }, { QStringLiteral("fired"), f } });
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
    QVariantList recurring;
    for (const Rec &r : recs) {
        QVariantList pos;
        QStringList tags;
        for (const auto &pp : r.pos) { pos.append(QVariantMap{ { QStringLiteral("tag"), pp.first }, { QStringLiteral("n"), pp.second } }); tags << pp.first; }
        const QString tier = sessionTier(c, r.k->id, true);
        recurring.append(QVariantMap{
            { QStringLiteral("id"), r.k->id }, { QStringLiteral("name"), r.k->label },
            { QStringLiteral("severity"), tier == QLatin1String("pattern") ? QStringLiteral("pattern") : QStringLiteral("watching") },
            { QStringLiteral("tier"), tier }, { QStringLiteral("count"), r.count },
            { QStringLiteral("countText"), QStringLiteral("%1 of %2 %3").arg(r.count).arg(c.ready).arg(c.many) + (tags.isEmpty() ? QString() : QStringLiteral(", at ") + tags.join(QStringLiteral(", "))) },
            { QStringLiteral("positions"), pos }, { QStringLiteral("marks"), r.marks } });
    }
    out.insert(QStringLiteral("recurring"), recurring);
    out.insert(QStringLiteral("recurringEmpty"), QStringLiteral("No wrist finding fired on any %1.").arg(c.one));
    out.insert(QStringLiteral("trust"), trustOf(c, nullptr, nullptr, nullptr, &vs, R.sessionParts.value(QStringLiteral("sizeLine")).toString()));
    R.session = out;
}

// ── What moves with the lead wrist at impact ───────────────────────────────────────────────

void buildMovesWith(Ctx &c, ChartsWristPresenter::Result &R)
{
    QString key;
    if (const Dof *d = c.pick.dof(QStringLiteral("leadWristFlexExt"))) key = d->key;
    if (key.isEmpty()) for (const Profile &p : c.profs) if (const Dof *d = p.dof(QStringLiteral("leadWristFlexExt"))) { key = d->key; break; }
    QVariantMap m;
    if (key.isEmpty() || !c.snap) {
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("refusal"), W::Sentence().append(QStringLiteral("No %1 of this session reads the lead wrist's bow / cup.").arg(c.one)).toVariant());
    } else {
        const QString id = key + QStringLiteral("@%1").arg(phase::Impact);
        if (c.w.mwSnap == c.snap.get() && c.w.mwRev == c.snap->revision && c.w.mwId == id) {
            m = c.w.mw;
        } else {
            m = ChartsRelationsPresenter::movesWith(c.snap, id, 4, 3);
            c.w.mwSnap = c.snap.get(); c.w.mwRev = c.snap->revision; c.w.mwId = id; c.w.mw = m;
        }
        m.insert(QStringLiteral("relationsUpId"), id);
    }
    m.insert(QStringLiteral("sub"), QStringLiteral("bow / cup at P7 against every reading, launch monitor included"));
    R.movesWith = m;
}

// ── the choices ─────────────────────────────────────────────────────────────────────────────

void buildChoices(Ctx &c, ChartsWristPresenter::Result &R)
{
    QVariantMap out;
    const QString scope = c.in.scope == QLatin1String("session") ? QStringLiteral("session") : QStringLiteral("shot");
    out.insert(QStringLiteral("scope"), scope);
    out.insert(QStringLiteral("scopes"), QVariantList{
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("shot") }, { QStringLiteral("label"), QStringLiteral("This shot") }, { QStringLiteral("current"), scope == QLatin1String("shot") } },
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("session") }, { QStringLiteral("label"), QStringLiteral("Session") }, { QStringLiteral("current"), scope == QLatin1String("session") } } });

    const QString ag = c.in.against;
    const int pi = c.index >= 0 ? c.index - 1 : c.n - 1;
    const bool prevOk = pi >= 0 && pi < c.n && c.profs[size_t(pi)].has;
    const bool refSet = !c.in.referenceSwingDir.trimmed().isEmpty();
    const bool refOk = c.ref && !c.refIsPicked && c.refProf.has;
    const QString refWhy = !refSet ? QStringLiteral("no reference swing is set: ★ marks one")
                         : !c.ref ? c.refWhy
                         : c.refIsPicked ? QStringLiteral("this swing is your reference")
                         : QStringLiteral("the reference swing carries no wrist reading");
    QVariantList ags;
    const auto addA = [&](const QString &id, const QString &label, bool enabled, const QString &why) {
        ags.append(QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label }, { QStringLiteral("current"), ag == id },
                                { QStringLiteral("enabled"), enabled }, { QStringLiteral("why"), enabled ? QString() : why } });
    };
    addA(QStringLiteral("address"), QStringLiteral("address"), true, QString());
    addA(QStringLiteral("previous"), QStringLiteral("previous"), prevOk, QStringLiteral("the swing before this one has no wrist reading"));
    addA(QStringLiteral("reference"), QStringLiteral("reference ★"), refOk, refWhy);
    addA(QStringLiteral("median"), QStringLiteral("session median"), c.ready >= 3,
         QStringLiteral("three shots with a wrist reading are needed; this session has %1").arg(W::countWord(c.ready)));
    out.insert(QStringLiteral("against"), ag);
    out.insert(QStringLiteral("againstOptions"), ags);

    out.insert(QStringLiteral("curvesBehind"), c.in.curvesBehind);
    out.insert(QStringLiteral("curves"), QVariantList{
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("off") }, { QStringLiteral("label"), QStringLiteral("off") }, { QStringLiteral("current"), !c.in.curvesBehind } },
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("on") }, { QStringLiteral("label"), QStringLiteral("on") }, { QStringLiteral("current"), c.in.curvesBehind } } });

    QVariantList arch;
    static const char *const names[] = { "Auto", "Neutral", "Bowed", "Cupped" };
    for (int a = -1; a <= 2; ++a)
        arch.append(QVariantMap{ { QStringLiteral("id"), a }, { QStringLiteral("label"), QString::fromLatin1(names[a + 1]) },
                                 { QStringLiteral("current"), c.in.archetype == a } });
    out.insert(QStringLiteral("archetype"), c.in.archetype);
    out.insert(QStringLiteral("archetypes"), arch);
    const QString after = c.in.archetype < 0 && c.pick.has ? QStringLiteral("→ ") + c.pick.archetypeName : QString();
    out.insert(QStringLiteral("archetypeAfter"), after);

    const bool isThis = c.refIsPicked;
    out.insert(QStringLiteral("reference"), QVariantMap{
        { QStringLiteral("set"), refSet }, { QStringLiteral("swingDir"), cleanDir(c.in.referenceSwingDir) },
        { QStringLiteral("label"), c.refLabel }, { QStringLiteral("isThisShot"), isThis },
        { QStringLiteral("linkText"), isThis ? QStringLiteral("★ this swing is your reference") : QStringLiteral("★ set this swing as your reference") },
        { QStringLiteral("canSet"), bool(c.shot) } });

    QString agLabel = QStringLiteral("address");
    for (const QVariant &a : ags) if (a.toMap().value(QStringLiteral("current")).toBool()) agLabel = a.toMap().value(QStringLiteral("label")).toString();
    out.insert(QStringLiteral("summary"), QStringLiteral("Bands %1%2  ·  against %3  ·  curves %4")
                                              .arg(QString::fromLatin1(names[c.in.archetype + 1]), after.isEmpty() ? QString() : QStringLiteral(" ") + after, agLabel,
                                                   c.in.curvesBehind ? QStringLiteral("on") : QStringLiteral("off")));
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
        c.profs.push_back(parseProfile(regradeWristPayload(*s, s->wrist, in.archetype)));
        if (c.profs.back().has) ++c.ready;
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
        sh->wrist = shotWristPayload(*sh, in.archetype, resemblanceOfDetail(in.pickedDetail));
        c.pick = parseProfile(sh->wrist);
        c.shot = sh;
    }
    // the ★ reference
    if (!in.referenceSwingDir.trimmed().isEmpty()) {
        c.ref = referenceShot(w, in.referenceSwingDir, c.snap, &c.refWhy);
        if (c.ref) {
            c.refProf = parseProfile(c.ref->wrist);
            c.refLabel = referenceLabelOf(c.ref->swingDir);
            c.refIsPicked = c.shot && cleanDir(c.ref->swingDir) == cleanDir(c.shot->swingDir);
        }
    } else {
        c.refWhy = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions.");
    }

    const std::vector<DofAgg> aggs = aggregates(c);
    R->available = c.ready > 0 || c.pick.has;
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

QVariantMap wristSessionReportPayload(const SessionSnapshot &snapshot, int archetypeMode, const QString &noun)
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
        c.profs.push_back(parseProfile(regradeWristPayload(*s, s->wrist, mode)));
        if (c.profs.back().has) ++c.ready;
    }
    // THE DOMINANT STYLE: the style most shots were graded as; a tie goes to the earlier of Neutral, Bowed, Cupped
    std::array<int, 3> graded{};
    for (const Profile &p : c.profs) if (p.has) ++graded[size_t(std::clamp(p.archetype, 0, 2))];
    int dom = 0;
    for (int s = 1; s < 3; ++s) if (graded[size_t(s)] > graded[size_t(dom)]) dom = s;
    if (c.ready > 0) c.washStyle = dom;

    const std::vector<DofAgg> aggs = aggregates(c);
    ChartsWristPresenter::Result R;
    buildSession(c, aggs, R);
    buildMovesWith(c, R);
    QVariantMap out = R.session;
    if (c.ready == 0) return out;
    // the score run's value axis on paper: the presenter's ticks, made here (PpChRunBox asks a ticker; the report
    // has no Relations presenter to ask, so the ticks travel in the payload)
    {
        QVariantMap sr = out.value(QStringLiteral("scoreRun")).toMap();
        const QVariantMap yr = sr.value(QStringLiteral("yRange")).toMap();
        sr.insert(QStringLiteral("ticks"), ChartsRelationsPresenter::axisTicks(yr.value(QStringLiteral("lo")).toDouble(), yr.value(QStringLiteral("hi")).toDouble(), 3,
                                                                               sr.value(QStringLiteral("unit")).toString()));
        out.insert(QStringLiteral("scoreRun"), sr);
    }

    // the dominant style's band at a checkpoint: off a shot graded as it, else a shot carrying the row re-graded to it
    std::map<QString, Profile> regraded;
    const auto bandAt = [&](const QString &id, int p, double *lo, double *hi) {
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
                    const ShotSeries &s = *snapshot.shots[size_t(i)];
                    rp = parseProfile(regradeWristPayload(s, s.wrist, dom));
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
    };

    QVariantList rows;
    for (const QVariant &rv : out.value(QStringLiteral("rows")).toList()) {
        QVariantMap row = rv.toMap();
        const QString id = row.value(QStringLiteral("dof")).toString();
        const DofAgg *a = aggOf(aggs, id);
        // the row's source on paper: the one most shots read it from
        QMap<QString, int> bySource;
        for (const Profile &pf : c.profs) if (const Dof *d = pf.has ? pf.dof(id) : nullptr) ++bySource[d->source];
        QString source;
        int best = 0;
        for (auto it = bySource.cbegin(); it != bySource.cend(); ++it) if (it.value() > best) { best = it.value(); source = it.key(); }
        const QString poleTop = row.value(QStringLiteral("poleTop")).toString(), poleBottom = row.value(QStringLiteral("poleBottom")).toString();
        QVariantList points, boxes = row.value(QStringLiteral("boxes")).toList();
        Scale sc;
        int outCount = 0;
        for (int p = 0; p < kP; ++p) {
            double lo = kNaN, hi = kNaN;
            const bool banded = bandAt(id, p, &lo, &hi);
            points.append(QVariantMap{ { QStringLiteral("available"), false }, { QStringLiteral("value"), nullv() },
                                       { QStringLiteral("valueText"), QStringLiteral("-") }, { QStringLiteral("banded"), banded },
                                       { QStringLiteral("bandLo"), nv(lo) }, { QStringLiteral("bandHi"), nv(hi) },
                                       { QStringLiteral("wash"), banded ? QStringLiteral("band") : QStringLiteral("none") },
                                       { QStringLiteral("out"), false } });
            if (banded) { sc.add(lo); sc.add(hi); }
            if (a) { sc.add(a->box[size_t(p)].min); sc.add(a->box[size_t(p)].max); }
            if (p < boxes.size()) {
                QVariantMap bm = boxes[p].toMap();
                const QVariant mv = bm.value(QStringLiteral("median"));
                const double med = mv.isNull() ? kNaN : mv.toDouble();
                // P1 is address, the reference every Δ is taken from (the engine's "ref" cell): never outside
                const bool outside = p > 0 && banded && std::isfinite(med) && (med > hi || med < lo);
                if (outside) ++outCount;
                bm.insert(QStringLiteral("outside"), outside);
                bm.insert(QStringLiteral("ringWord"), outside ? (med > hi ? poleTop : poleBottom) : QString());
                boxes[p] = bm;
            }
        }
        sc.finish();
        row.insert(QStringLiteral("points"), points);
        row.insert(QStringLiteral("boxes"), boxes);
        row.insert(QStringLiteral("yLo"), sc.lo);
        row.insert(QStringLiteral("yHi"), sc.hi);
        row.insert(QStringLiteral("outCount"), outCount);
        row.insert(QStringLiteral("label"), W::cap1(longName(id)));
        row.insert(QStringLiteral("source"), source);
        row.insert(QStringLiteral("camera"), sourceLabel(source));
        row.insert(QStringLiteral("unit"), QStringLiteral("°"));
        rows.append(row);
    }
    out.insert(QStringLiteral("rows"), rows);
    const QString domName = wristArchetypeName(dom);
    const int k = graded[size_t(dom)];
    // no swing reads the lead wrist's bow / cup (a camera-only session): nothing chose the style, so the caption
    // names the band alone and the trust line says why
    bool anyFe = false;
    for (const Profile &pf : c.profs) anyFe = anyFe || (pf.has && pf.dof(QStringLiteral("leadWristFlexExt")));
    out.insert(QStringLiteral("caption"),
               mode >= 0 ? QStringLiteral("Shaded: the %1 band, set by hand; every %2 is graded against it.").arg(domName, one)
               : !anyFe ? QStringLiteral("Shaded: the %1 band.").arg(domName)
               : k == c.ready ? QStringLiteral("Shaded: the %1 band, the style every %2 was graded as.").arg(domName, one)
               : QStringLiteral("Shaded: the %1 band, the style %2 of %3 %4 were graded as. Each %5 was graded against its own style’s band.")
                     .arg(domName).arg(k).arg(c.ready).arg(many, one));
    out.insert(QStringLiteral("sub"), QStringLiteral("Δ from address, degrees  ·  a box per checkpoint, n %1  ·  under each: median, and how wide the middle half is").arg(c.ready));
    out.insert(QStringLiteral("dominant"), QVariantMap{ { QStringLiteral("archetype"), dom }, { QStringLiteral("name"), domName },
                                                        { QStringLiteral("n"), k }, { QStringLiteral("of"), c.ready } });
    for (auto it = R.sessionParts.cbegin(); it != R.sessionParts.cend(); ++it) out.insert(it.key(), it.value());
    QVariantMap mw = R.movesWith;
    if (mw.contains(QStringLiteral("refusal")))
        mw.insert(QStringLiteral("refusal"), nounSentence(mw.value(QStringLiteral("refusal")).toMap(), one, many));
    out.insert(QStringLiteral("movesWith"), mw);
    return out;
}

// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsWristPresenter::ChartsWristPresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("ChartsWristPresenter"));
    schedule();
}

ChartsWristPresenter::~ChartsWristPresenter()
{
    m_pool.waitForDone();
}

QObject *ChartsWristPresenter::model() const { return m_model.data(); }

void ChartsWristPresenter::setModel(QObject *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m))
        connect(sm, &SessionSeriesModel::snapshotChanged, this, &ChartsWristPresenter::schedule);
    forwardArchetype();
    emit inputsChanged();
    schedule();
}

std::shared_ptr<const SessionSnapshot> ChartsWristPresenter::currentSnapshot() const
{
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot();
    return m_directSnapshot;
}

void ChartsWristPresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot)
{
    m_directSnapshot = std::move(snapshot);
    schedule();
}

// The Bands choice is session-wide: the model re-grades every shot's profile against it.
void ChartsWristPresenter::forwardArchetype()
{
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) sm->setWristArchetype(m_in.archetype);
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
    forwardArchetype();
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
QVariantMap ChartsWristPresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : QVariantMap{}; }
QVariantMap ChartsWristPresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : QVariantMap{}; }
QVariantMap ChartsWristPresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : QVariantMap{}; }
QVariantMap ChartsWristPresenter::movesWith() { flushIfSynchronous(); return m_result ? m_result->movesWith : QVariantMap{}; }

QVariantMap ChartsWristPresenter::standfirst() const
{
    if (!m_result || m_selected < 0 || m_selected >= m_result->standfirsts.size()) return {};
    return m_result->standfirsts.at(m_selected).toMap();
}

qint64 ChartsWristPresenter::seekUsFor(const QString &tag) const
{
    if (!m_result) return -1;
    const int i = int(m_result->tags.indexOf(tag));
    return i >= 0 && i < kP ? m_result->cpUs[size_t(i)] : -1;
}

void ChartsWristPresenter::schedule()
{
    m_dirty = true;
    if (m_synchronous || m_scheduled) return;
    m_scheduled = true;
    QMetaObject::invokeMethod(this, &ChartsWristPresenter::startJob, Qt::QueuedConnection);
}

void ChartsWristPresenter::startJob()
{
    m_scheduled = false;
    if (m_synchronous || m_running) return;     // apply() re-runs a dirty presenter
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    m_dirty = false;
    m_running = true;
    emit busyChanged();
    auto w = m_worker;
    m_pool.start([this, w, in]() {
        auto r = computeWrist(*w, in);
        QMetaObject::invokeMethod(this, [this, r]() { apply(r); }, Qt::QueuedConnection);
    });
}

void ChartsWristPresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_running;
    m_running = false;
    m_result = std::move(r);
    emit resultsChanged();
    updateSelected(true);
    if (m_dirty && !m_synchronous) startJob();
    else if (wasRunning) emit busyChanged();
}

void ChartsWristPresenter::flushIfSynchronous()
{
    if (!m_synchronous || !m_dirty) return;
    m_dirty = false;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    m_result = computeWrist(*m_worker, in);
    updateSelected(true);
}

void ChartsWristPresenter::setSynchronous(bool on)
{
    m_synchronous = on;
    if (on) {
        m_pool.waitForDone();
        QCoreApplication::processEvents();
        m_dirty = true;
    } else if (m_dirty) {
        schedule();
    }
}

bool ChartsWristPresenter::waitForIdle(int msTimeout)
{
    if (m_synchronous) { flushIfSynchronous(); return true; }
    QDeadlineTimer deadline(msTimeout);
    while ((m_running || m_dirty || m_scheduled) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_running && !m_dirty;
}

// The playhead's checkpoint: the latest present checkpoint at or before it, else the first present one
// (WristDiagnosticsModel::recomputeSelected's rule). Fires only when it moves to another checkpoint.
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
