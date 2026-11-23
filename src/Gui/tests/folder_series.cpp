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

#include "folder_series.h"
#include "../../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S)
#include "../../DomainModel/rule_data.h"   // the packs and norms: RuleData's snapshot (refactor step 3.2)

#include "../../Vocabulary/Analysis/swing_analysis.h"              // Phase::Impact
#include "../../Vocabulary/Analysis/uncertainty_json.h"            // copyPhaseSampleSigma — σ keys, one spelling
#include "../../Coaching/diagnostic_ledger.h"
#include "../../Coaching/work_ons_pack.h"           // the work-on order (SessionSnapshot::workOnOrder)
#include "../../Coaching/sequence_checks.h"         // the KS checks off the ledger's signal rows           // the ledger's one deserialiser
#include "../../Core/club_vocabulary.h"                 // swingDocDeclaredClub
#include "../../Core/pp_debug.h"
#include "../../Diagnostics/engine/condition_reading.h" // condition → { metricKey, phase }
#include "../../Diagnostics/packs/pack_provider.h"      // condition names and groups
#include "../../Export/swing_store.h"
#include "../../Export/swing_paths.h"
#include "../../Export/session_diagnostics_files.h"   // diagnostics.json's one reader (step 2.5)
#include "../../Kinematics/kinematic_sequence_json.h"  // retimeKinematicSequence — one shape, four paths
#include "../charts/session_wrist.h"                               // the wrist checkpoint profile (CONTRACT I3)

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QMetaObject>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::charts {

namespace {

// The session's diagnostics ledger, beside its swing folders, is read through its one reader
// (SessionDiagnosticsFiles, step 2.5). READ here, never written: the diagnostics model writes it.

// The same folder spelt one way. The live processor and the carousel spell one session
// differently ("…/lib//athlete/…" against "…/lib/athlete/…"), and the cache is keyed by it.
QString cleanDir(const QString &p)
{
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath());
}

QString parentDir(const QString &cleanSwingDir)
{
    return QFileInfo(cleanSwingDir).absolutePath();
}

// A number, or NaN. QJsonValue::toDouble() answers 0 for a null, and a missing reading must
// never become a zero one (CONTRACT rule 6: missing is "-", never 0).
double num(const QJsonValue &v)
{
    return v.isDouble() ? v.toDouble() : kNaN;
}

bool sameNumber(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

bool sameDiagnostics(const ChartShotFindings &a, const ChartShotFindings &b)
{
    if (a.club != b.club || a.dataWarning != b.dataWarning
        || a.fired != b.fired || a.clean != b.clean || a.values.size() != b.values.size())
        return false;
    if (a.drivenBy.size() != b.drivenBy.size()) return false;
    if (a.sequenceChecks != b.sequenceChecks) return false;
    if (a.wrist != b.wrist) return false;
    for (auto it = a.drivenBy.constBegin(); it != a.drivenBy.constEnd(); ++it) {
        const auto o = b.drivenBy.constFind(it.key());
        if (o == b.drivenBy.constEnd() || o->measureId != it->measureId || o->metricKey != it->metricKey
            || o->phase != it->phase || o->reducer != it->reducer)
            return false;
    }
    for (auto it = a.values.constBegin(); it != a.values.constEnd(); ++it) {
        const auto o = b.values.constFind(it.key());
        if (o == b.values.constEnd()) return false;
        const ChartShotFindings::Value &x = it.value(), &y = o.value();
        if (!sameNumber(x.value, y.value) || !sameNumber(x.z, y.z) || !sameNumber(x.lo, y.lo)
            || !sameNumber(x.hi, y.hi) || x.dir != y.dir || !sameNumber(x.confidence, y.confidence))
            return false;
    }
    return true;
}

// One shot's ledger row as the panel reads it. Fired and clean rows carry their graded value; a
// not-assessable row carries nothing, because there is no value to carry (the fixture's rule,
// build_fixture.py, which the mocks were drawn from).
// The reading a condition is drawn at: the one `measureId` names when the condition reads it,
// else the condition's first reading with a curve key, else its first reading (found == false
// when it has none — a latent condition, or one the pack no longer holds).
pinpoint::analysis::ConditionReading readingFor(const pinpoint::analysis::CharacteristicPack *pack,
                                                const QString &conditionId, const QString &measureId)
{
    using pinpoint::analysis::ConditionReading;
    if (!pack) return ConditionReading{};
    const std::vector<ConditionReading> all = pinpoint::analysis::conditionReadings(*pack, conditionId);
    if (!measureId.isEmpty())
        for (const ConditionReading &r : all)
            if (r.measureId == measureId) return r;
    for (const ConditionReading &r : all)
        if (r.hasKey()) return r;
    return all.empty() ? ConditionReading{} : all.front();
}

ChartShotFindings diagnosticsOf(const pinpoint::analysis::ShotRecord &s,
                              const pinpoint::analysis::CharacteristicPack *pack)
{
    using pinpoint::analysis::ShotState;
    ChartShotFindings d;
    d.club        = s.club;
    d.dataWarning = s.dataWarning;
    for (const pinpoint::analysis::ConditionRow &r : s.rows)
        if (r.state == ShotState::NotAssessable && !r.notJudgedSubject.isEmpty()) {
            const pinpoint::analysis::Condition *c = pack ? pack->condition(r.conditionId) : nullptr;
            QStringList &l = d.notJudgedBy[r.notJudgedSubject];
            const QString name = (c && !c->label.isEmpty()) ? c->label : r.conditionId;
            if (!l.contains(name)) l << name;
        }
    for (const pinpoint::analysis::ConditionRow &r : s.rows) {
        if (r.state == ShotState::Fired)      d.fired << r.conditionId;
        else if (r.state == ShotState::Clean) d.clean << r.conditionId;
        else continue;
        ChartShotFindings::Value v;
        v.value = r.value;
        v.z     = r.z;
        // ⚠ The row's corridorLo/Hi, which design §7 already records are NOT the corridor the
        // norm draws (only the stored band the row was graded against). Carried as the ledger
        // has them; the panel must not draw them as the corridor.
        v.lo    = r.corridorLo;
        v.hi    = r.corridorHi;
        v.dir   = r.direction;
        v.confidence = std::isfinite(r.confidence) ? double(r.confidence) : kNaN;
        d.values.insert(r.conditionId, v);
        const pinpoint::analysis::ConditionReading cr = readingFor(pack, r.conditionId, r.drivingMeasureId);
        ChartShotFindings::Reading rd;
        rd.measureId = r.drivingMeasureId;
        rd.metricKey = cr.metricKey;
        rd.phase     = cr.found ? cr.phase : -1;
        rd.reducer   = cr.found ? int(cr.reducer) : -1;
        rd.contextId = r.contextId;
        if (pack && !r.drivingMeasureId.isEmpty())
            if (const pinpoint::analysis::Measure *ms = pack->measure(r.drivingMeasureId)) {
                using pinpoint::analysis::ReducerKind;
                rd.shape        = int(ms->shape);
                rd.sameQuantity = ms->kind != pinpoint::analysis::MeasureKind::Composed
                               && (ms->reducer.kind == ReducerKind::At
                                   || (ms->reducer.kind == ReducerKind::Extremum && !ms->reducer.anchor));
            }
        d.gradedBy.insert(r.conditionId, rd);
        if (r.state == ShotState::Fired) d.drivenBy.insert(r.conditionId, rd);
    }
    d.fired.sort();
    d.clean.sort();
    d.sequenceChecks = pinpoint::analysis::sequenceChecksToVariant(pinpoint::analysis::sequenceChecksOf(s));
    d.wrist          = s.wrist;   // the wrist checkpoint profile, part of the findings (schema 16; DEC-123)
    return d;
}

qint64 stringBytes(const QString &s)
{
    // QArrayData header (~16 B) plus UTF-16 payload; a shared string is counted where it is held.
    return s.isEmpty() ? 0 : 16 + qint64(s.capacity()) * 2;
}

} // namespace

// ── The pure halves ─────────────────────────────────────────────────────────────────────────

qint64 FolderSeries::relUs(const QJsonValue &v, qint64 originUs)
{
    // The one rule (windowTimeUs), against the document's analysis origin — what the replay's map
    // (analysisDetailFor) is built with, and it has to be: the panel seeks shotReplay with these
    // numbers. A stamped document's origin is 0 (read as written); an older one's is clock.t0_us.
    return qint64(pinpoint::analysis::windowTimeUs(int64_t(v.toDouble()), originUs));
}

int FolderSeries::folderShotId(const QString &swingDir)
{
    // The swing number (ID-SWING-NUMBER, DEC-91), read by SwingPaths — the one reader of a swing folder's
    // name (step 2.7b, lint A3), as the diagnostics model reads it (step 2.7a).
    const int n = pinpoint::SwingPaths::swingNumberOf(QFileInfo(QDir::cleanPath(swingDir)).fileName());
    return n > 0 ? n : -1;
}

QString FolderSeries::swingLabelOf(const QString &swingDir)
{
    const int id = folderShotId(swingDir);
    const QDate d = swingDir.isEmpty() ? QDate()
                  : pinpoint::SessionDiagnosticsFiles::identityOf(QFileInfo(QDir::cleanPath(swingDir)).absolutePath()).localDay();
    QStringList parts;
    if (id >= 0) parts << QStringLiteral("shot %1").arg(id);
    if (d.isValid()) parts << QLocale(QLocale::English).toString(d, QStringLiteral("d MMM yyyy"));
    return parts.join(QStringLiteral(", "));
}

std::shared_ptr<ShotSeries> FolderSeries::extractShot(const QJsonObject &root,
                                                           const QString &swingDir, QString *whyNot)
{
    const auto fail = [whyNot](const QString &why) {
        if (whyNot) *whyNot = why;
        return std::shared_ptr<ShotSeries>();
    };
    if (root.isEmpty())
        return fail(QStringLiteral("the swing document could not be read"));
    const QJsonValue anv = root.value(QStringLiteral("analysis"));
    if (!anv.isObject())
        return fail(QStringLiteral("not analysed: the swing carries no analysis"));
    const QJsonObject an = anv.toObject();
    const QJsonArray metrics = an.value(QStringLiteral("metrics")).toArray();
    // GATED ON DATA (CONTRACT rule 7). A launch-monitor-only swing carries lm.* metrics and one
    // phase and nothing else; it is a shot with readings, so it is in, and every figure that
    // needs a curve or a P-position finds it has none.
    if (metrics.isEmpty())
        return fail(QStringLiteral("not analysed: the analysis produced no metrics"));

    // The analysis origin (documentAnalysisOriginUs, the one place a document without
    // analysis.timeBase is guessed) — every time below goes through relUs against it.
    const qint64 t0 = qint64(pinpoint::analysis::documentAnalysisOriginUs(root));

    auto shot = std::make_shared<ShotSeries>();
    shot->swingDir = swingDir;
    shot->shotId   = folderShotId(swingDir);
    // The club the SWING declares (review.club, else capture.club.name) — and empty when it
    // declares none, never the DRIVER stub the carousel shows: a facet "by club" must not invent
    // a driver for every camera swing recorded before capture.club.name existed.
    shot->club     = pinpoint::swingDocDeclaredClub(root);

    // ── analysis.metrics ────────────────────────────────────────────────────────────────
    // The field-by-field twin of DiskReplaySource::load's series loop (and so of ShotProcessor's
    // live map): the same keys, the same re-timing, the same "absent means not characterised"
    // rule for σ and validity. Two differences, both towards honesty: a non-number value is NaN
    // rather than 0, and an all-ones `valid` is stored empty, which the types define as the same.
    shot->metrics.reserve(metrics.size());
    for (const QJsonValue &mv : metrics) {
        const QJsonObject m = mv.toObject();
        MetricSeries s;
        s.key = m.value(QStringLiteral("key")).toString();
        if (s.key.isEmpty()) continue;
        if (shot->metrics.contains(s.key)) {
            ppWarn() << "[Charts] duplicate metric" << s.key << "in" << swingDir << "- kept the first";
            continue;
        }
        s.label = m.value(QStringLiteral("label")).toString();
        s.unit  = m.value(QStringLiteral("unit")).toString();

        const QJsonArray ts = m.value(QStringLiteral("t_us")).toArray();
        const QJsonArray vs = m.value(QStringLiteral("value")).toArray();
        const qsizetype n = std::min(ts.size(), vs.size());
        if (ts.size() != vs.size())
            ppWarn() << "[Charts]" << s.key << "in" << swingDir << "has" << ts.size() << "times and"
                     << vs.size() << "values - kept" << n;
        s.tUs.reserve(size_t(n));
        s.value.reserve(size_t(n));
        for (qsizetype i = 0; i < n; ++i) {
            s.tUs.push_back(relUs(ts.at(i), t0));
            s.value.push_back(num(vs.at(i)));
        }
        // Per-sample validity (design §5.1): index-parallel, 0 = bridged. ABSENT means every
        // sample is valid, which is how every swing analysed before the field existed reads.
        if (m.contains(QStringLiteral("valid"))) {
            const QJsonArray vd = m.value(QStringLiteral("valid")).toArray();
            const qsizetype nv = std::min(vd.size(), n);
            s.valid.reserve(size_t(nv));
            bool allValid = nv == n;
            for (qsizetype i = 0; i < nv; ++i) {
                const uint8_t ok = vd.at(i).toInt() != 0 ? 1 : 0;
                allValid = allValid && ok;
                s.valid.push_back(ok);
            }
            if (allValid) { s.valid.clear(); s.valid.shrink_to_fit(); }
        }
        if (m.contains(QStringLiteral("sigma")))
            s.sigma = num(m.value(QStringLiteral("sigma")));
        if (m.contains(QStringLiteral("sigmaKind")))
            s.sigmaKind = m.value(QStringLiteral("sigmaKind")).toInt(-1);
        s.lineAngle = m.value(QStringLiteral("lineAngle")).toBool();
        s.routeId   = m.value(QStringLiteral("routeId")).toString();

        const QJsonArray ps = m.value(QStringLiteral("phaseSamples")).toArray();
        s.phaseSamples.reserve(size_t(ps.size()));
        for (const QJsonValue &pv : ps) {
            const QJsonObject p = pv.toObject();
            PhaseSample x;
            x.phase = p.value(QStringLiteral("phase")).toInt(-1);
            x.tUs   = relUs(p.value(QStringLiteral("t_us")), t0);
            x.value = num(p.value(QStringLiteral("value")));
            x.band  = p.value(QStringLiteral("band")).toString();
            // The reload path's own σ rule, by calling it rather than restating it: present only
            // when the producer characterised one.
            QVariantMap sig;
            pinpoint::analysis::copyPhaseSampleSigma(p, sig);
            const QVariant sv = sig.value(QStringLiteral("sigma"));
            x.sigma = (sv.isValid() && !sv.isNull()) ? sv.toDouble() : kNaN;
            s.phaseSamples.push_back(std::move(x));
        }

        shot->metricOrder << s.key;
        shot->metrics.insert(s.key, std::move(s));
    }

    // ── analysis.phases, and impact ─────────────────────────────────────────────────────
    const int kImpact = int(pinpoint::analysis::Phase::Impact);
    const QJsonArray phases = an.value(QStringLiteral("phases")).toArray();
    shot->phases.reserve(size_t(phases.size()));
    for (const QJsonValue &pv : phases) {
        const QJsonObject p = pv.toObject();
        PhaseMark mk;
        mk.phase = p.value(QStringLiteral("phase")).toInt(-1);
        mk.tUs   = relUs(p.value(QStringLiteral("t_us")), t0);
        mk.conf  = num(p.value(QStringLiteral("conf")));
        if (mk.phase == kImpact && shot->impactUs < 0) shot->impactUs = mk.tUs;   // the replay's m_impactUs
        shot->phases.push_back(mk);
    }

    // ── analysis.kinematicSequence — the reload's one re-timing, so the strip and the panel
    //    read the same instants (kinematic_sequence_json.h).
    if (an.contains(QStringLiteral("kinematicSequence"))) {
        shot->kinematicSequence = pinpoint::analysis::retimeKinematicSequence(
            an.value(QStringLiteral("kinematicSequence")).toObject(),
            [t0](const QJsonValue &v) { return relUs(v, t0); });
        // The sequence's impact is the same instant on the same clock; it stands in only when the
        // phase ladder has none. Never the replay's thumbnail/midpoint fallback: that is a place
        // to park a playhead, not an impact, and impactUs −1 is the honest "none".
        if (shot->impactUs < 0 && shot->kinematicSequence.contains(QStringLiteral("impactUs")))
            shot->impactUs = shot->kinematicSequence.value(QStringLiteral("impactUs")).toLongLong();
    }
    // ── analysis.gates — why readings were refused (gate_record.h); impact-relative, kept as stored.
    if (an.contains(QStringLiteral("gatesVersion"))) {
        shot->gatesVersion = an.value(QStringLiteral("gatesVersion")).toInt();
        shot->gates        = an.value(QStringLiteral("gates")).toArray().toVariantList();
    }

    // ── the wrist resemblance index: the analysis' own (analysis.score.resemblance, kept by loadAnalysis: the score object
    //    is inline in the root chunk). The checkpoint profile itself is the findings' (diagnostics.wrist, schema 16; DEC-123):
    //    nothing is graded here.
    shot->wristResemblance = wristResemblanceOf(an);
    return shot;
}

qint64 FolderSeries::approxBytes(const ShotSeries &s)
{
    qint64 b = qint64(sizeof(ShotSeries));
    b += stringBytes(s.swingDir) + stringBytes(s.club);
    b += qint64(s.phases.capacity() * sizeof(PhaseMark));
    // QHash: a span slot plus a node (key + value) per entry; the keys are shared with metricOrder.
    b += qint64(s.metrics.capacity()) * qint64(sizeof(QString) + sizeof(MetricSeries) + 8);
    for (const MetricSeries &m : s.metrics) {
        b += stringBytes(m.key) + stringBytes(m.label) + stringBytes(m.unit);
        b += qint64(m.tUs.capacity() * sizeof(qint64) + m.value.capacity() * sizeof(double)
                    + m.valid.capacity() * sizeof(uint8_t)
                    + m.phaseSamples.capacity() * sizeof(PhaseSample));
        for (const PhaseSample &p : m.phaseSamples) b += stringBytes(p.band);
    }
    b += qint64(s.metricOrder.size()) * qint64(sizeof(QString));
    // The sequence is a nested QVariantMap; its compact JSON text is a fair proxy, doubled for
    // the QVariant/QMap node overhead around each value.
    if (!s.kinematicSequence.isEmpty())
        b += 2 * qint64(QJsonDocument(QJsonObject::fromVariantMap(s.kinematicSequence))
                            .toJson(QJsonDocument::Compact).size());
    const ChartShotFindings &d = s.diagnostics;
    // the wrist checkpoint profile (≤ 5 DOFs × 8 cells × 3 styles, small)
    b += qint64(d.wrist.rows.size()) * qint64(sizeof(pinpoint::analysis::WristProfileRow) + 3 * 8 * 16);
    b += stringBytes(d.club);
    b += qint64(d.fired.size() + d.clean.size()) * qint64(sizeof(QString));
    b += qint64(d.values.capacity()) * qint64(sizeof(QString) + sizeof(ChartShotFindings::Value) + 8);
    return b;
}

// ── Construction ────────────────────────────────────────────────────────────────────────────

FolderSeries::FolderSeries(QObject *parent)
    : SeriesSource(parent)
    , m_snapshot(std::make_shared<SessionSnapshot>())
{
    // ONE worker thread (decision 2). Two would let a re-analysed shot's read overtake the
    // activation that put its session in place, and the cache would be written by both.
    m_pool.setMaxThreadCount(1);
}

FolderSeries::~FolderSeries()
{
    // The jobs capture `this`. Their deliveries die with the object (queued to it), but a job
    // still running must not outlive the members it reads.
    m_pool.waitForDone();
}

// THE DOUBLE'S READ DOOR (refactor 5.5c; SnapshotShotReader): the app's is SessionSeries', by ShotRef through the session
// repository; a test's snapshot reads its fixture folders straight from the store.
namespace {
class FolderShotReader final : public SnapshotShotReader
{
public:
    SnapshotShotRead stamp(const QString &swingDir) const override
    {
        SnapshotShotRead out;
        const pinpoint::SwingStore::DocInfo i = pinpoint::SwingStore::info(QDir::cleanPath(swingDir));
        out.exists = i.exists(); out.size = i.size; out.mtimeMs = i.mtimeMs;
        return out;
    }
    SnapshotShotRead analysis(const QString &swingDir) const override { return read(swingDir, false); }
    SnapshotShotRead document(const QString &swingDir) const override { return read(swingDir, true); }

private:
    static SnapshotShotRead read(const QString &swingDir, bool whole)
    {
        const QString dir = QDir::cleanPath(swingDir);
        SnapshotShotRead out;
        const pinpoint::SwingStore::DocInfo i = pinpoint::SwingStore::info(dir);
        out.exists = i.exists(); out.size = i.size; out.mtimeMs = i.mtimeMs;
        if (!out.exists) { out.error = QStringLiteral("no swing document"); return out; }
        out.root = whole ? pinpoint::SwingStore::load(dir, &out.error) : pinpoint::SwingStore::loadAnalysis(dir, &out.error);
        return out;
    }
};
} // namespace

std::shared_ptr<const SnapshotShotReader> folderShotReader()
{
    static const std::shared_ptr<const SnapshotShotReader> r = std::make_shared<FolderShotReader>();
    return r;
}

QString FolderSeries::skipReasonOf(const QString &swingDir) const
{
    const QString dir = QDir::cleanPath(swingDir.trimmed());
    for (const Skip &k : m_skipped)
        if (QDir::cleanPath(k.swingDir) == dir) return k.reason.isEmpty() ? QStringLiteral("the swing could not be read") : k.reason;
    return {};
}

QVariantList FolderSeries::skippedList() const
{
    QVariantList out;
    out.reserve(qsizetype(m_skipped.size()));
    for (const Skip &s : m_skipped)
        out.append(QVariantMap{ { QStringLiteral("swingDir"), s.swingDir },
                                { QStringLiteral("shotId"),   s.shotId },
                                { QStringLiteral("reason"),   s.reason } });
    return out;
}

void FolderSeries::setSynchronous(bool on)
{
    m_synchronous = on;
}

void FolderSeries::setDocLoader(DocLoader loader)
{
    QMutexLocker lk(&m_workerMutex);
    m_loader = std::move(loader);
}

FolderSeries::LoadStats FolderSeries::loadStats() const
{
    QMutexLocker lk(&m_workerMutex);
    return m_stats;
}

bool FolderSeries::waitForIdle(int msTimeout)
{
    if (m_synchronous) return true;
    QDeadlineTimer deadline(msTimeout);
    while ((m_pending > 0 || m_refreshPosted) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return m_pending == 0 && !m_refreshPosted;
}

// ── Feeding (owner thread) ──────────────────────────────────────────────────────────────────

void FolderSeries::activateSession(const QString &sessionDir)
{
    const QString dir = sessionDir.trimmed().isEmpty() ? QString() : cleanDir(sessionDir);
    ++m_generation;
    if (dir != m_sessionDir) {
        // A DIFFERENT SESSION WITHDRAWS THE OLD ONE AT ONCE. Its shots under the new session's
        // name, even for the second the read takes, would be one session's curves captioned as
        // another's. The empty interim is a real snapshot with its own revision.
        m_sessionDir = dir;
        auto empty = std::make_shared<SessionSnapshot>();
        empty->sessionDir = dir;
        empty->revision = ++m_revision;
        m_snapshot = empty;
        m_skipped.clear();
        emit sessionChanged();
        emit snapshotChanged();
    }
    queue(Job{ JobKind::Activate, m_generation, dir, -1 });
}

void FolderSeries::ingestShot(int shotId, const QString &swingDir)
{
    if (swingDir.isEmpty() || m_sessionDir.isEmpty()) return;
    const QString dir = cleanDir(swingDir);
    // A swing of another session is that session's to read when it is activated.
    if (parentDir(dir) != m_sessionDir) return;
    queue(Job{ JobKind::Ingest, m_generation, dir, shotId });
}

void FolderSeries::regradeShot(const QString &swingDir)
{
    if (swingDir.isEmpty() || m_sessionDir.isEmpty()) return;
    const QString dir = cleanDir(swingDir);
    if (parentDir(dir) != m_sessionDir) return;
    queue(Job{ JobKind::Regrade, m_generation, dir, -1 });
}

void FolderSeries::forgetShot(const QString &swingDir)
{
    if (swingDir.isEmpty() || m_sessionDir.isEmpty()) return;
    const QString dir = cleanDir(swingDir);
    if (parentDir(dir) != m_sessionDir) return;
    queue(Job{ JobKind::Forget, m_generation, dir, -1 });
}

void FolderSeries::refreshDiagnostics()
{
    if (m_sessionDir.isEmpty()) return;
    if (m_synchronous) {
        queue(Job{ JobKind::RefreshLedger, m_generation, QString(), -1 });
        return;
    }
    // POSTED, NOT RUN. The natural cue is SessionDiagnosticsModel::busyChanged going false, and
    // that model emits it a few statements BEFORE it writes diagnostics.json, on this same
    // thread. Started from inside that handler, the worker could stat the file before the write
    // and the shot would sit undiagnosed until the next refresh. Posting runs it once the
    // emitting call has returned — after the write. Repeated calls in one turn coalesce.
    if (m_refreshPosted) return;
    m_refreshPosted = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_refreshPosted = false;
        if (!m_sessionDir.isEmpty())
            queue(Job{ JobKind::RefreshLedger, m_generation, QString(), -1 });
    }, Qt::QueuedConnection);
}

void FolderSeries::queue(const Job &jobIn)
{
    const Job job = jobIn;
    if (m_synchronous) {
        m_pool.waitForDone();        // an earlier asynchronous job must not run after this one
        apply(runJob(job), false);
        return;
    }
    const bool wasBusy = busy();
    ++m_pending;
    if (!wasBusy) emit busyChanged();
    // QThreadPool::start rather than QtConcurrent::run: the result comes back through the queued
    // invocation, so there is no future to keep.
    m_pool.start([this, job]() {
        const Result r = runJob(job);
        QMetaObject::invokeMethod(this, [this, r]() { apply(r, true); }, Qt::QueuedConnection);
    });
}

void FolderSeries::apply(const Result &r, bool counted)
{
    if (counted) m_pending = std::max(0, m_pending - 1);
    // A delivery for an activation that has since been replaced is dropped: the job queued by the
    // newer activation runs after it on the one thread and publishes the session as it is now.
    if (r.generation == m_generation && r.changed && r.snapshot) {
        m_snapshot = r.snapshot;
        m_skipped  = r.skipped;
        pinpoint::PostShotTrace::Scope trace("charts");   // [PostShot] (3.7-S): the charts' presenters and QML redraw
        emit snapshotChanged();
    }
    if (counted && m_pending == 0) emit busyChanged();
}

// ── The worker ──────────────────────────────────────────────────────────────────────────────

struct FolderSeries::SharedCache {
    QMutex mutex;
    QHash<QString, CacheEntry> entries;      // key: cleaned absolute swing dir
    QStringList recent;                      // sessions, most recently activated first, at most two
};

FolderSeries::SharedCache &FolderSeries::shared()
{
    static SharedCache c;
    return c;
}

void FolderSeries::clearSharedCache()
{
    QMutexLocker lk(&shared().mutex);
    shared().entries.clear();
    shared().recent.clear();
}

static void annotateForBoard(ShotSeries &shot, const pinpoint::analysis::BoardAnnotation &a);   // below (J-45)

FolderSeries::Result FolderSeries::runJob(const Job &job)
{
    QMutexLocker lk(&m_workerMutex);
    // Always in this order (instance, then shared), so two instances' jobs cannot deadlock. A job
    // holds both for its whole length: two panels alive at once (a rebuild's overlap) take turns.
    QMutexLocker sharedLock(&shared().mutex);
    // The catalogue for condition names and groups, assembled on first use and on this thread:
    // a golfer who never opens the panel never parses it, and the GUI thread never waits on it.
    // The diagnostics model's own source (makeCharacteristicPackProvider: core + the user's
    // packs), so a name here is the name on the diagnostics panel.
    if (!m_pack) m_pack = pinpoint::analysis::makeCharacteristicPackProvider();

    QElapsedTimer wall;
    wall.start();
    const LoadStats before = m_stats;

    switch (job.kind) {
    case JobKind::Activate: {
        // The ledger's stamp survives re-activating the SAME session, so an unchanged
        // diagnostics.json is not parsed again; a different session starts from nothing.
        if (job.dir != m_wSessionDir) {
            m_ledger.clear();
            m_ledgerRows.clear();
            m_ledgerJoined = false;
            m_ledgerSize = m_ledgerMtimeMs = -1;
            m_own.clear();                      // this instance's shots are the new session's only
        }
        m_wSessionDir = job.dir;
        m_wDirs.clear();
        m_callerIds.clear();
        if (job.dir.isEmpty())
            return buildSnapshot(job.generation, true);
        shared().recent.removeAll(job.dir);
        shared().recent.prepend(job.dir);
        while (shared().recent.size() > 2) shared().recent.removeLast();
        evictOtherSessions();
        // The carousel's own enumeration: THE one listing of a session's swings (SwingPaths, step 2.7b).
        const QStringList names = pinpoint::SwingPaths::swingFolders(job.dir);
        for (const QString &n : names) m_wDirs << job.dir + QLatin1Char('/') + n;
        for (const QString &d : std::as_const(m_wDirs)) ensureShot(d, false);
        readLedgerIfChanged();
        Result r = buildSnapshot(job.generation, true);
        const QString msg =
            QStringLiteral("[Charts] session %1: %2 shots, %3 skipped; %4 read, %5 cached; "
                           "%6 ms (load %7 ms, extract %8 ms)")
                .arg(QFileInfo(job.dir).fileName())
                .arg(r.snapshot ? r.snapshot->count() : 0)
                .arg(r.skipped.size())
                .arg(m_stats.docReads - before.docReads)
                .arg(m_stats.cacheHits - before.cacheHits)
                .arg(wall.elapsed())
                .arg((m_stats.loadNs - before.loadNs) / 1000000)
                .arg((m_stats.extractNs - before.extractNs) / 1000000);
        ppInfo() << qUtf8Printable(msg);
        return r;
    }
    case JobKind::Ingest:
    case JobKind::Regrade: {
        // The owner checked the session against ITS directory; the worker re-checks against the
        // session it is holding, which differs only while an activation is queued behind this.
        if (m_wSessionDir.isEmpty() || parentDir(job.dir) != m_wSessionDir)
            return Result{ job.generation, false, m_wLast, m_wLastSkipped };
        if (!m_wDirs.contains(job.dir)) m_wDirs << job.dir;
        if (job.callerShotId >= 0 && folderShotId(job.dir) < 0)
            m_callerIds.insert(job.dir, job.callerShotId);
        ensureShot(job.dir, job.kind == JobKind::Regrade);
        readLedgerIfChanged();
        return buildSnapshot(job.generation, false);
    }
    case JobKind::Forget: {
        if (m_wSessionDir.isEmpty() || !m_wDirs.contains(job.dir))
            return Result{ job.generation, false, m_wLast, m_wLastSkipped };
        // Out of the order, and out of both caches: a later swing of the same number is a
        // different swing, and must be read, never served this one's curves.
        m_wDirs.removeAll(job.dir);
        m_callerIds.remove(job.dir);
        m_own.remove(job.dir);
        shared().entries.remove(job.dir);
        // The ledger drops the swing's row too and rewrites the file; refreshDiagnostics()
        // re-joins it when it has. Until then the row is simply unused — no shot carries it.
        return buildSnapshot(job.generation, false);
    }
    case JobKind::RefreshLedger:
        if (m_wSessionDir.isEmpty())
            return Result{ job.generation, false, m_wLast, m_wLastSkipped };
        readLedgerIfChanged();
        return buildSnapshot(job.generation, false);
    }
    return Result{ job.generation, false, m_wLast, m_wLastSkipped };
}

void FolderSeries::evictOtherSessions()
{
    // The cache holds what the last two activations read (decision 5): the live session and the
    // one being reviewed, the pair a golfer flips between. Anything older is a session nobody is
    // looking at, and fifty swings of curves are worth giving back.
    for (auto it = shared().entries.begin(); it != shared().entries.end();) {
        if (shared().recent.contains(it->sessionDir)) ++it;
        else it = shared().entries.erase(it);
    }
}

void FolderSeries::ensureShot(const QString &swingDir, bool force)
{
    // STAMPED BEFORE THE READ (the firing read's order). A re-analysis that
    // rewrites the document after this point leaves a stamp that no longer matches, so the next
    // look reads it again; the opposite order would file the new stamp against the old reading.
    const pinpoint::SwingStore::DocInfo info = pinpoint::SwingStore::info(swingDir);
    if (!force) {
        // This instance's own reading first: it is the source of truth for what it published,
        // whatever another instance has done to the shared cache since.
        const auto own = m_own.constFind(swingDir);
        if (own != m_own.constEnd() && own->size == info.size && own->mtimeMs == info.mtimeMs) {
            ++m_stats.cacheHits;
            return;
        }
        // Then the process-wide cache — the optimisation that makes a rebuilt panel free.
        const auto sh = shared().entries.constFind(swingDir);
        if (sh != shared().entries.constEnd() && sh->size == info.size && sh->mtimeMs == info.mtimeMs) {
            m_own.insert(swingDir, sh.value());
            ++m_stats.cacheHits;
            return;
        }
    }

    CacheEntry e;
    e.sessionDir = parentDir(swingDir);
    e.size       = info.size;
    e.mtimeMs    = info.mtimeMs;
    if (!info.exists()) {
        e.skipReason = QStringLiteral("no swing document");
        keep(swingDir, e);
        return;
    }

    QElapsedTimer t;
    t.start();
    QString err;
    // The analysis without the per-frame tracks (SwingStore::loadAnalysis): the root chunk and
    // /analysis/metrics, ~20× cheaper than the whole document and never the pose (decision 3).
    QJsonObject root = m_loader ? m_loader(swingDir, &err) : pinpoint::SwingStore::loadAnalysis(swingDir, &err);
    ++m_stats.docReads;
    m_stats.loadNs += t.nsecsElapsed();

    t.restart();
    QString why;
    const bool readOk = !root.isEmpty();
    std::shared_ptr<ShotSeries> shot = readOk ? extractShot(root, swingDir, &why) : nullptr;
    // The board's annotations are the document's review (a board edit rewrites it, so the cache stamp moves with them).
    if (readOk) e.board = pinpoint::analysis::boardAnnotationOfDocument(root);
    if (shot) annotateForBoard(*shot, e.board);
    root = QJsonObject();          // the document goes NOW, before the next one is read (decision 3)
    m_stats.extractNs += t.nsecsElapsed();

    if (!readOk) {
        e.skipReason = QStringLiteral("the swing document could not be read");
        ppWarn() << "[Charts]" << err;
    } else if (!shot) {
        e.skipReason = why;
    } else {
        e.fresh = std::move(shot);
    }
    keep(swingDir, e);
}

// THE LAUNCH MONITOR BOARD ON A SWING'S SERIES (Mark, 21 Nov: J-45 — the Charts tab follows the board, as the report
// does). The one rule's parts (board_annotations.h: annotatedClub, isLaunchMonitorKey), as the session reduction reads a
// shot (annotatedShot): the swing under the board's club; a reading left out read as one the monitor never made — its
// launch monitor series ("lm." keys) not read. Its ledger rows follow in joinLedger. A swing the board said nothing of is
// unchanged.
static void annotateForBoard(ShotSeries &shot, const pinpoint::analysis::BoardAnnotation &a)
{
    using namespace pinpoint::analysis;
    if (!a.any()) return;
    shot.club = annotatedClub(shot.club, a);
    if (!a.excluded) return;
    for (auto m = shot.metrics.begin(); m != shot.metrics.end();)
        m = isLaunchMonitorKey(m.key()) ? shot.metrics.erase(m) : std::next(m);
    shot.metricOrder.removeIf([](const QString &k) { return isLaunchMonitorKey(k); });
}

void FolderSeries::keep(const QString &swingDir, const CacheEntry &e)
{
    m_own.insert(swingDir, e);
    // The shared cache gets a reading only once it is complete (a fresh shot is finished in
    // buildSnapshot, which shares it then), and only for a session it is keeping (decision 5).
    if (!e.fresh && shared().recent.contains(e.sessionDir))
        shared().entries.insert(swingDir, e);
}

bool FolderSeries::readLedgerIfChanged()
{
    const QFileInfo fi(pinpoint::SessionFiles::diagnosticsFile(m_wSessionDir));
    const bool exists = fi.exists();
    const qint64 size  = exists ? fi.size() : -2;
    const qint64 mtime = exists ? fi.lastModified().toMSecsSinceEpoch() : -2;
    if (size == m_ledgerSize && mtime == m_ledgerMtimeMs) return false;
    m_ledgerSize    = size;
    m_ledgerMtimeMs = mtime;
    m_ledgerRows.clear();
    m_ledgerJoined = false;         // joined again (joinLedger) at the next snapshot
    m_ledgerWhy = QStringLiteral("This session's diagnostics haven't been worked out yet, so its wrist checkpoints aren't graded.");
    if (!exists) return true;      // no diagnostics yet: every shot reads with none

    const pinpoint::DiagnosticsRead read = pinpoint::SessionDiagnosticsFiles::readDiagnostics(m_wSessionDir);
    if (read.state == pinpoint::DiagnosticsRead::State::Missing) return true;   // gone since the stat
    if (read.state == pinpoint::DiagnosticsRead::State::Unopenable) {
        ppWarn() << "[Charts] cannot read" << fi.absoluteFilePath() << read.error;
        m_ledgerWhy = QStringLiteral("This session's diagnostics can't be read just now, so its wrist checkpoints aren't graded.");
        return true;
    }
    if (!read.parsed()) {
        // Possibly caught mid-write by a non-atomic writer elsewhere; the stamp is kept, so the
        // next refresh after the file settles reads it again.
        ppWarn() << "[Charts] cannot parse" << fi.absoluteFilePath() << read.error;
        m_ledgerSize = m_ledgerMtimeMs = -1;
        m_ledgerWhy = QStringLiteral("This session's diagnostics can't be read just now, so its wrist checkpoints aren't graded.");
        return true;
    }
    m_ledgerWhy = read.state == pinpoint::DiagnosticsRead::State::Newer
        ? QStringLiteral("This session's diagnostics were saved by a newer version of PinPoint Studio, which this version "
                         "can't add to, so its wrist checkpoints aren't graded here.")
        : QStringLiteral("Diagnostics have not graded the wrist checkpoints yet.");
    // The ledger's own deserialiser (diagnostic_ledger.h fromJson), so a row field added there
    // reaches this file without a second parser to update.
    m_ledgerOpt  = pinpoint::analysis::LedgerOptions();
    m_ledgerRows = pinpoint::analysis::fromJson(read.envelope.ledger, &m_ledgerOpt);
    return true;
}

// THE LEDGER JOINED TO THE SHOTS AS THE GOLFER ANNOTATED THEM ON THE BOARD (J-45): the rows through the reduction's own
// rule (board_annotations.h annotateShots: the board's club; a left-out reading's monitor-driven rows not assessable),
// then the shot rows, the session tiers and the work-on order from them — so the Charts tab says what Session diagnostics
// and the report say. Joined again only when the rows were read again or the boards moved.
void FolderSeries::joinLedger(const QHash<int, pinpoint::analysis::BoardAnnotation> &boards)
{
    if (m_ledgerJoined && boards == m_ledgerBoards) return;
    m_ledgerJoined = true;
    m_ledgerBoards = boards;
    m_ledger.clear();
    m_ledgerTiers.clear();
    m_ledgerWorkOnOrder.clear();
    if (m_ledgerRows.empty()) return;
    const pinpoint::analysis::CharacteristicPack *pack = m_pack ? &m_pack->pack() : nullptr;
    std::vector<pinpoint::analysis::ShotRecord> shots = m_ledgerRows;
    if (!boards.isEmpty())
        pinpoint::analysis::annotateShots(shots, boards, pack ? pinpoint::analysis::launchMonitorMeasures(*pack) : QSet<QString>{});
    const pinpoint::analysis::LedgerOptions &opt = m_ledgerOpt;
    for (const pinpoint::analysis::ShotRecord &s : shots)
        m_ledger.insert(s.shotId, diagnosticsOf(s, pack));
    // The session tiers, by the ledger's own reduction over the options the file was written with —
    // the numbers Session diagnostics shows, never a second rule (the Wrist tab's severity reads them).
    using pinpoint::analysis::Tier;
    const std::vector<pinpoint::analysis::ConditionLedger> ledgers = pinpoint::analysis::conditionLedgers(shots, opt);
    for (const pinpoint::analysis::ConditionLedger &l : ledgers)
        m_ledgerTiers.insert(l.id, l.tier == Tier::Pattern ? QStringLiteral("pattern")
                                   : l.tier == Tier::Watching ? QStringLiteral("watching") : QStringLiteral("clean"));
    // The work-on ORDER of every session pattern: the list's own reduction, uncapped (the cap cuts
    // after the sort, so the first five are the list's own five, in its order).
    pinpoint::analysis::WorkOnOptions wopt;
    wopt.maxPerSession = std::numeric_limits<int>::max();
    for (const pinpoint::analysis::WorkOnEntry &e :
         pinpoint::analysis::sessionWorkOnsForPack(pack, shots, ledgers, wopt).entries)
        m_ledgerWorkOnOrder << e.id;
}

FolderSeries::Result FolderSeries::buildSnapshot(int generation, bool forcePublish)
{
    // STRIKE ORDER (decision 7): the folder number, then the folder name for anything unnumbered.
    QStringList dirs = m_wDirs;
    std::stable_sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        const int na = folderShotId(a), nb = folderShotId(b);
        if (na >= 0 && nb >= 0 && na != nb) return na < nb;
        if ((na >= 0) != (nb >= 0)) return na >= 0;
        return a < b;
    });

    // The ledger joined to the swings as the board annotated them (J-45): each swing's board, from its entry.
    {
        QHash<int, pinpoint::analysis::BoardAnnotation> boards;
        for (const QString &dir : std::as_const(dirs)) {
            const auto it = m_own.constFind(dir);
            if (it == m_own.constEnd() || !it->board.any()) continue;
            const int id = folderShotId(dir) >= 0 ? folderShotId(dir) : m_callerIds.value(dir, -1);
            if (id >= 0) boards.insert(id, it->board);
        }
        joinLedger(boards);
    }

    auto snap = std::make_shared<SessionSnapshot>();
    snap->reader     = folderShotReader();   // the double's door reads its folders (refactor 5.5c)
    snap->sessionDir = m_wSessionDir;
    snap->tiers      = m_ledgerTiers;
    snap->workOnOrder = m_ledgerWorkOnOrder;
    snap->wristPendingWhy = m_ledgerWhy;
    snap->shots.reserve(size_t(dirs.size()));
    std::vector<Skip> skipped;

    for (const QString &dir : std::as_const(dirs)) {
        int id = folderShotId(dir);
        if (id < 0) id = m_callerIds.value(dir, -1);
        // THIS INSTANCE'S OWN ENTRIES, never the shared cache: another instance activating two
        // other sessions evicts this one's shots from there, and a snapshot rebuilt from it then
        // held only the shots this job happened to re-read ("Shot 1 of 1" instead of 15). A dir
        // with no entry of its own (not expected) is read now; one that still cannot be is
        // skipped with a reason — never dropped.
        auto it = m_own.find(dir);
        if (it == m_own.end()) {
            ensureShot(dir, false);
            it = m_own.find(dir);
        }
        if (it == m_own.end()) {
            skipped.push_back(Skip{ dir, id, QStringLiteral("the swing could not be read") });
            continue;
        }
        CacheEntry &e = it.value();
        if (!e.shot && !e.fresh) {
            skipped.push_back(Skip{ dir, id, e.skipReason });
            continue;
        }
        const int ordinal = snap->count() + 1;
        const ChartShotFindings diag = id >= 0 ? m_ledger.value(id) : ChartShotFindings();
        if (e.fresh) {
            // Just read and held by nobody else: completed in place, then frozen and shared.
            e.fresh->ordinal     = ordinal;
            e.fresh->shotId      = id;
            e.fresh->diagnostics = diag;
            e.shot = std::move(e.fresh);
            e.fresh.reset();
            if (shared().recent.contains(e.sessionDir)) shared().entries.insert(dir, e);
        } else if (e.shot->ordinal != ordinal || e.shot->shotId != id
                   || !sameDiagnostics(e.shot->diagnostics, diag)) {
            // A PUBLISHED SHOT NEVER CHANGES (decision 6): a new place in the order or a new ledger
            // row (its wrist profile included) is a new shot object. The copy is the series' size,
            // once per change.
            auto copy = std::make_shared<ShotSeries>(*e.shot);
            copy->ordinal     = ordinal;
            copy->shotId      = id;
            copy->diagnostics = diag;
            e.shot = std::move(copy);
        }
        snap->shots.push_back(e.shot);
    }

    // Every condition that fired on some shot, named and grouped by the shipped catalogue, and
    // linked to the reading it grades (condition_reading.h, design §6.4). A condition with several
    // readings (early extension: pelvis thrust OR spine-angle loss) is linked to the one the
    // session's fired rows name most often as their driving measure — ties to the pack's
    // detectedBy order — and, where no row names one, to its first reading with a curve key. Each
    // shot's own driving reading stays on the shot (ChartShotFindings::drivenBy).
    const pinpoint::analysis::CharacteristicPack *pack = m_pack ? &m_pack->pack() : nullptr;
    QHash<QString, QHash<QString, int>> named;          // condition → driving measure → fired rows
    for (const auto &shot : snap->shots)
        for (auto it = shot->diagnostics.drivenBy.constBegin(); it != shot->diagnostics.drivenBy.constEnd(); ++it)
            if (!it->measureId.isEmpty()) ++named[it.key()][it->measureId];
    for (const auto &shot : snap->shots) {
        for (const QString &cid : shot->diagnostics.fired) {
            if (snap->conditions.contains(cid)) continue;
            ConditionInfo ci;
            const pinpoint::analysis::Condition *c = pack ? pack->condition(cid) : nullptr;
            ci.name  = (c && !c->label.isEmpty()) ? c->label : cid;
            ci.group = c ? pinpoint::analysis::conditionGroupName(c->group) : QString();
            QString chosen;
            if (pack) {
                const QHash<QString, int> counts = named.value(cid);
                int best = 0;
                for (const pinpoint::analysis::ConditionReading &r : pinpoint::analysis::conditionReadings(*pack, cid)) {
                    const int n = counts.value(r.measureId);
                    if (n > best) { best = n; chosen = r.measureId; }   // strict: the earlier wins a tie
                }
            }
            const pinpoint::analysis::ConditionReading r = readingFor(pack, cid, chosen);
            ci.metricKey = r.metricKey;
            ci.phase     = r.found ? r.phase : -1;
            ci.reducer   = r.found ? int(r.reducer) : -1;
            snap->conditions.insert(cid, ci);
        }
    }

    const auto sameSkips = [](const std::vector<Skip> &a, const std::vector<Skip> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].swingDir != b[i].swingDir || a[i].shotId != b[i].shotId || a[i].reason != b[i].reason)
                return false;
        return true;
    };
    Result r;
    r.generation = generation;
    if (m_wLast && m_wLast->sessionDir == snap->sessionDir && m_wLast->shots == snap->shots
        && m_wLast->tiers == snap->tiers && m_wLast->workOnOrder == snap->workOnOrder
        && m_wLast->wristPendingWhy == snap->wristPendingWhy && sameSkips(m_wLastSkipped, skipped)) {
        // Nothing moved: the same snapshot, the same revision. An activation publishes it anyway,
        // because the owner may be holding an interim one (activateSession).
        r.changed  = forcePublish;
        r.snapshot = m_wLast;
        r.skipped  = m_wLastSkipped;
        return r;
    }
    snap->revision = ++m_revision;
    m_wLast        = snap;
    m_wLastSkipped = std::move(skipped);
    r.changed  = true;
    r.snapshot = m_wLast;
    r.skipped  = m_wLastSkipped;
    return r;
}

} // namespace pinpoint::charts
