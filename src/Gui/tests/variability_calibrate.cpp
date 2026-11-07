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

// variability_calibrate <fixtures-root> <ks-root> <out-dir>: the variability grader (docs/design/variability-mark/spec.md
// §8) over every session of a read-only extract, for calibrating the §8.10 constants. READ-ONLY: it reads
// <fixtures-root>/<athlete>/*.json (tools/diagnostics/consistency_extract.py's shape, the athlete taken from the
// folder name) and <ks-root>/<athlete>/<session>.json (the app's stored kinematic-sequence verdicts), and writes
// only into <out-dir>:
//
//   variability_calibration.csv  one row per graded or ungraded item, per session and club
//   variability_trend.csv        §8.8 across each athlete's sessions
//   gallery_fixture.json         real numbers for the mark gallery, from Mark-Liversedge's 7-iron sessions
//   typicality_scalar.csv        R28: each swing's robust z against its club's other swings (R8, R22), per reading
//   typicality_trace.csv         R28: the same for each trace: RMS z(t), timing z and deviation, amplitude z
//   trace_descriptors.csv        R28: each trace key's descriptor (draws a curve, where its peak matters)
//
// Each session is loaded and aligned as the Consistency tests load a fixture, and its readings are consistencyInput's. Every
// grade is WITHIN ONE CLUB: clubs are never pooled (spec §9.1, §9.5.1). A blank club label is a club of its own.
//
//   Scalar (§8.2)       every Position reading, flagged phaseListed (phaseCheck). A guarded shot is left out where consistencyGuardCovers says the
//                       reading rests on the P-ladder. Scale: the provider's corridor, else variabilityScale; else
//                       ungraded (no CV fallback, R26). The reading's ownSigma is its propagatedSigma (R3): never
//                       subtracted, but an observed SD no larger than it sets measurementLimited.
//   Measurement (§8.5)  beside each scalar row: ownSigma (a PROPAGATED σ, fed as the nearest rung, the live
//                       residual RMSE; the CSV says so) over measurementTolerance, else MEAS_TOL_FRACTION × the
//                       corridor. Its grade sets the scalar row's measurementLimited (§8.6) and nothing else.
//   Trace (§8.3)        every TimeSeries metric with curves: top (canonical P4) → impact on the P-aligned axis,
//                       101 points. Guarded shots are left out (the window rests on the P-ladder). Time-to-peak is
//                       the |value| peak inside the same window, in REAL ms from impact on the shot's own clock.
//                       Amplitude scale: variabilityScale, else TRACE_AMP_FRACTION × the mean peak. There is NO
//                       metric → associated-scalar mapping in the code, so the corridor rung is never used here.
//   Ordinal (§8.4)      the kinematic-sequence order AS THE APP STORED IT, from a separate read-only extract
//                       (<ks-root>/<athlete>/<session>.json). Only resolved orders, and only the swings whose placed
//                       set is the club's modal set: a calibration-only choice, see ordinal().
//
// NaN is written as an empty CSV field and as JSON null. Numbers are never rounded.

#include "charts/consistency_env.h"
#include "charts/session_consistency.h"
#include "charts/session_curves.h"
#include "charts/session_sequence.h"
#include "charts/session_traces.h"
#include "charts/session_warp.h"
#include "charts/variability_env.h"
#include "charts_maths_fixture.h"
#include "variability/variability_facade.h"       // the shared token spellings

#include "Metrics/metric_catalogue.h"
#include "Metrics/variability/variability_constants.h"
#include "Metrics/variability/variability_grader.h"
#include "Metrics/variability/variability_result.h"
#include "Metrics/variability/variability_trend.h"
#include "Metrics/variability/variability_typicality.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

using namespace pinpoint::charts;
namespace pa = pinpoint::analysis;
namespace pv = pinpoint::variability;

namespace {

// ── tokens ──────────────────────────────────────────────────────────────────────────────────────

// The façade's spellings (src/Gui/variability/variability_facade.h), linked rather than copied, so the CSV and QML
// name a basis and a reason with the same word.
QString basisToken(pv::Basis b) { return pv::VariabilityResultView::basisToken(b); }
QString reasonToken(pv::UngradedReason r) { return pv::VariabilityResultView::ungradedReasonToken(r); }

// The scale sources and the tool's own reasons have no façade token; they follow its lower-camel casing.
QString sourceToken(pv::ScaleSource s)
{
    switch (s) {
    case pv::ScaleSource::None:                 return QStringLiteral("none");
    case pv::ScaleSource::Corridor:             return QStringLiteral("corridor");
    case pv::ScaleSource::VariabilityScale:     return QStringLiteral("variabilityScale");
    case pv::ScaleSource::PeakFraction:         return QStringLiteral("peakFraction");
    case pv::ScaleSource::TimingDefault:        return QStringLiteral("timingDefault");
    case pv::ScaleSource::MeasurementTolerance: return QStringLiteral("measurementTolerance");
    case pv::ScaleSource::CorridorFraction:     return QStringLiteral("corridorFraction");
    }
    return {};
}

QString uncertaintyToken(pv::UncertaintyKind k)
{
    // The one source this tool has is the producer's PROPAGATED σ. It is fed as the nearest §8.5 rung, the
    // live residual RMSE; the token says both, so nobody reads it as a validation RMSE.
    switch (k) {
    case pv::UncertaintyKind::None:             return {};
    case pv::UncertaintyKind::ValidationRmse:   return QStringLiteral("validationRmse");
    case pv::UncertaintyKind::LiveResidualRmse: return QStringLiteral("propagatedSigma~liveResidualRmse");
    case pv::UncertaintyKind::StationarySd:     return QStringLiteral("stationarySd");
    }
    return {};
}

QString trendWord(pv::TrendDirection d)
{
    switch (d) {
    case pv::TrendDirection::Insufficient: return QStringLiteral("Insufficient");
    case pv::TrendDirection::Holding:      return QStringLiteral("Holding");
    case pv::TrendDirection::Tightening:   return QStringLiteral("Tightening");
    case pv::TrendDirection::Loosening:    return QStringLiteral("Loosening");
    }
    return {};
}

QString typicalityToken(pv::TypicalityBand b)
{
    switch (b) {
    case pv::TypicalityBand::Usual:      return QStringLiteral("usual");
    case pv::TypicalityBand::ALittleOff: return QStringLiteral("aLittleOff");
    case pv::TypicalityBand::Unusual:    return QStringLiteral("unusual");
    }
    return {};
}

QString phaseLabel(int column) { return QStringLiteral("P%1").arg(column + 1); }

// Never rounded: the shortest text that reads back as the same double. NaN is empty.
QString csvNum(double x)
{
    return std::isfinite(x) ? QString::number(x, 'g', QLocale::FloatingPointShortest) : QString();
}

QString csvField(const QString &s)
{
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n'))) return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QJsonValue jnum(double x) { return std::isfinite(x) ? QJsonValue(x) : QJsonValue(QJsonValue::Null); }

QJsonArray jarray(const std::vector<double> &v)
{
    QJsonArray a;
    for (double x : v) a.append(jnum(x));
    return a;
}

QString clubLabel(const QString &club) { return club.isEmpty() ? QStringLiteral("(none)") : club; }

// ── what one session yields ─────────────────────────────────────────────────────────────────────

struct CalibrationRow {
    QString athlete, session, date, club, metric, label, unit, phase;
    int     column = -1;
    pv::ScaleType     scaleType = pv::ScaleType::Interval;
    pv::VariabilityResult r;
    double  rhoM = kNaN;                  // scalar rows: the coupled §8.5 ρm
    int     offered = 0;                  // shots of the club offered to the grader
    int     excluded = 0;                 // shots of the club left out (the guard; warm-ups count, R13)
    QString detail;                       // ordinal: the modal order
    QString ksSegments;                   // ordinal: the window's placed-segment set, "leadArm,club"
    int     ksExcluded = -1;              // ordinal: resolved swings with another set, plus unresolved ones
    QString note;
    QString toolReason;                   // why this tool could not offer the grader a window; wins over the grader's
    bool    positionRow = false;          // a scalar or measurement row of a Position reading
    bool    launchMonitor = false;        // source "lm": a launch-monitor reading, one value per shot
    double  peakAtTopShare = kNaN;        // trace: share of window swings peaking within one grid step of the top
    double  peakAtImpactShare = kNaN;     // ... and of impact
    int     phaseListed = -1;             // 1 / 0: the read phase is / is not listed by the descriptor; -1 no descriptor
    QString phaseNote;
    std::vector<double> values;           // Mark-Liversedge 7-iron scalar rows only: the gallery's dispersion
};

// One session's material for the gallery's typicality and trace envelopes.
struct GalleryScalar {
    QString key, label, unit, phase;
    pv::ScaleType scaleType = pv::ScaleType::Interval;
    pv::VariabilityResult r;
    bool listed = false;                  // phaseListed = 1: only these are gallery picks
    QStringList swings;
    std::vector<double> values;
};

struct GalleryTrace {
    QString key, label, unit;
    pv::VariabilityResult r;
    double topCanonMs = kNaN;
    QStringList swings;                   // the kept curves only (whole on the grid, finite peak)
    std::vector<std::vector<double>> curves;
    std::vector<double> peakTimesMs;
};

struct SessionGallery {
    QString session, date;
    int shots = 0;
    std::vector<GalleryScalar> scalars;
    std::vector<GalleryTrace>  traces;
};

// R28 calibration pass: one leave-one-out typicality per swing (R8: same club, this swing out, ≥ N_MIN others; R22
// robust z), for setting the trace thresholds against the scalar ones. Written to typicality_{scalar,trace}.csv.
struct TypicalityRow {
    QString athlete, session, club, metric, phase, swing;
    int     n = 0;                        // the window: the other swings
    int     listed = -1;                  // scalar: phaseListed
    double  z = kNaN;                     // scalar z; trace RMS z(t)
    double  timingZ = kNaN, timingDevMs = kNaN, amplitudeZ = kNaN;
};

const QString kGalleryAthlete = QStringLiteral("Mark-Liversedge");
const QString kGalleryClub    = QStringLiteral("7 IRON");
const QString kGalleryDate    = QStringLiteral("2026-10-07");

std::optional<double> some(double x) { return std::isfinite(x) ? std::optional<double>(x) : std::nullopt; }

// THE PHASE FILTER for Position readings. consistencyInput reads every curve metric at P1 and every curve-less one
// at each P it has a sample at; a metric's descriptor says where it is READ: `phases` ("phases sampled
// (PointInTime) / where peak matters (TimeSeries)"), all inside its `domain`. A FLAG, never a filter: every row is
// kept and graded, and phaseListed says whether the phase it was read at is one of those and inside the domain. For
// a curve metric `phases` lists where its PEAK matters, so an address posture reading is unlisted though it may well
// mean something; the flag lets the calibration be read both ways. Downswing and ArmParallelDown, and Release and
// ShaftParallelThrough, are one instant spelled twice (metric_descriptor.h's ladder), so either spelling matches.
enum class PhaseCheck { Pass, NotAtPhase, NoPhasesStated, NoDescriptor };

bool sameInstant(pa::Phase a, pa::Phase b)
{
    const auto p5 = [](pa::Phase p) { return p == pa::Phase::Downswing || p == pa::Phase::ArmParallelDown; };
    const auto p8 = [](pa::Phase p) { return p == pa::Phase::Release || p == pa::Phase::ShaftParallelThrough; };
    return a == b || (p5(a) && p5(b)) || (p8(a) && p8(b));
}

PhaseCheck phaseCheck(const pa::MetricDescriptor *d, int phaseId)
{
    if (!d) return PhaseCheck::NoDescriptor;
    if (d->phases.empty()) return PhaseCheck::NoPhasesStated;
    const pa::Phase at = static_cast<pa::Phase>(phaseId);
    if (!pa::phaseInDomain(d->domain, at)) return PhaseCheck::NotAtPhase;
    for (pa::Phase p : d->phases)
        if (sameInstant(p, at)) return PhaseCheck::Pass;
    return PhaseCheck::NotAtPhase;
}

QString phaseName(pa::Phase p)
{
    switch (p) {
    case pa::Phase::Address:              return QStringLiteral("Address");
    case pa::Phase::Takeaway:             return QStringLiteral("Takeaway");
    case pa::Phase::ShaftParallelBack:    return QStringLiteral("P2");
    case pa::Phase::MidBackswing:         return QStringLiteral("P3");
    case pa::Phase::Transition:           return QStringLiteral("Transition");
    case pa::Phase::Top:                  return QStringLiteral("Top");
    case pa::Phase::ArmParallelDown:      return QStringLiteral("P5");
    case pa::Phase::Downswing:            return QStringLiteral("Downswing");
    case pa::Phase::Delivery:             return QStringLiteral("P6");
    case pa::Phase::MaxSpeed:             return QStringLiteral("MaxSpeed");
    case pa::Phase::Impact:               return QStringLiteral("Impact");
    case pa::Phase::ShaftParallelThrough: return QStringLiteral("P8");
    case pa::Phase::Release:              return QStringLiteral("Release");
    case pa::Phase::FollowThrough:        return QStringLiteral("P9");
    case pa::Phase::Finish:               return QStringLiteral("Finish");
    }
    return QStringLiteral("?");
}

// The phaseNote of a Position row: where it was read, and what the descriptor lists.
QString phaseNoteOf(PhaseCheck c, const pa::MetricDescriptor *d, int readPhase)
{
    const QString at = QStringLiteral("read at ") + phaseName(static_cast<pa::Phase>(readPhase));
    if (c == PhaseCheck::NoDescriptor) return at + QStringLiteral("; no descriptor");
    QStringList listed;
    for (pa::Phase p : d->phases) listed << phaseName(p);
    const QString domain = QStringLiteral("domain ") + phaseName(d->domain.first) + QLatin1Char('-')
                         + phaseName(d->domain.last);
    const QString tail = QStringLiteral("; descriptor phases {") + listed.join(QLatin1Char(' ')) + QStringLiteral("}, ")
                       + domain;
    switch (c) {
    case PhaseCheck::Pass:           return at + QStringLiteral("; listed") + tail;
    case PhaseCheck::NotAtPhase:
        return at + (pa::phaseInDomain(d->domain, static_cast<pa::Phase>(readPhase))
                         ? QStringLiteral("; not a descriptor phase") : QStringLiteral("; outside the domain")) + tail;
    case PhaseCheck::NoPhasesStated: return at + QStringLiteral("; descriptor states no phase") + tail;
    case PhaseCheck::NoDescriptor:   break;
    }
    return at;
}

// The app's stored kinematic-sequence verdicts for one session (the KS extract, <ks-root>/<athlete>/<session>.json).
// The KS extract only globs the `2026-*` session folders, as consistency_extract.py does, so the two cover the same
// sessions; a fixture with no KS file gets no ordinal rows, and the run says so.
struct KsSwing {
    QStringList order;
    QStringList placed;
    bool        resolved = false;
};
using KsSession = QHash<QString, KsSwing>;   // swing folder -> verdict

KsSession loadKs(const QString &path, bool *found)
{
    KsSession out;
    QFile f(path);
    *found = f.open(QIODevice::ReadOnly);
    if (!*found) return out;
    const QJsonObject swings = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("swings")).toObject();
    for (auto it = swings.begin(); it != swings.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        KsSwing k;
        for (const QJsonValue &v : o.value(QStringLiteral("order")).toArray()) k.order << v.toString();
        for (const QJsonValue &v : o.value(QStringLiteral("placed")).toArray()) k.placed << v.toString();
        k.resolved = o.value(QStringLiteral("orderResolved")).toBool();
        out.insert(it.key(), k);
    }
    return out;
}

int factorial(int m)
{
    int k = 1;
    for (int i = 2; i <= m; ++i) k *= i;
    return k;
}

// ── the graders over one session ────────────────────────────────────────────────────────────────

class SessionCalibration {
public:
    SessionCalibration(const SessionSnapshot &s, const SessionAlignment &al, const ConsistencyInput &in,
                       const IVariabilityScaleProvider &provider, const pa::MetricCatalogue &catalogue)
        : m_s(s), m_al(al), m_in(in), m_provider(provider), m_catalogue(catalogue)
        , m_scalar(pv::makeVariabilityGrader(pv::Basis::Scalar))
        , m_trace(pv::makeVariabilityGrader(pv::Basis::Trace))
        , m_ordinal(pv::makeVariabilityGrader(pv::Basis::Ordinal))
        , m_measurement(pv::makeVariabilityGrader(pv::Basis::Measurement))
    {
        QSet<QString> seen;
        for (const ConsistencyShot &sh : m_in.shots)
            if (!seen.contains(sh.club)) { seen.insert(sh.club); m_clubs << sh.club; }
        std::sort(m_clubs.begin(), m_clubs.end());
    }

    CalibrationRow base;                  // athlete, session, date filled by the caller
    std::vector<CalibrationRow> rows;
    std::vector<TypicalityRow>  scalarTypicalityRows, traceTypicalityRows;
    SessionGallery *gallery = nullptr;    // non-null for the gallery's session
    const KsSession *ks = nullptr;        // the session's stored KS verdicts; null: no extract, no ordinal rows

    void run()
    {
        scalars();
        launchMonitor();
        traces();
        ordinal();
    }

private:
    bool galleryClub(const QString &club) const
    {
        return base.athlete == kGalleryAthlete && club == kGalleryClub;
    }

    // §8.2 + §8.5 over every Position reading, club by club.
    void scalars()
    {
        for (const ConsistencyReading &reading : m_in.readings) {
            if (reading.kind != ConsistencyReadingKind::Position) continue;
            const bool covered = consistencyGuardCovers(reading);
            const int phase = kConsistencyPhases[reading.column];     // the phase normSigmaOf reads the norm at
            // The phase the value was READ at: the column's P, except the backswing timing (the one Position
            // reading the guard covers), which is stamped at impact and filed under P3-P4.
            const int readPhase = covered ? int(pa::Phase::Impact) : phase;
            const PhaseCheck check = phaseCheck(m_catalogue.descriptor(reading.key), readPhase);
            const pv::ScaleType scaleType = m_provider.scaleType(reading.key);
            const double noise = m_provider.noiseSigma(reading);

            for (const QString &club : m_clubs) {
                std::vector<double> values;
                QStringList swings;
                int offered = 0, excluded = 0, finite = 0;
                for (size_t i = 0; i < m_in.shots.size(); ++i) {
                    if (m_in.shots[i].club != club) continue;
                    if (covered && m_in.shots[i].guarded) { ++excluded; continue; }
                    ++offered;
                    const double v = reading.at(int(i), 0);
                    values.push_back(v);
                    swings << m_s.shots[i]->swingDir;
                    finite += std::isfinite(v) ? 1 : 0;
                }
                if (!finite) continue;       // the club never had this reading: not an item

                const VariabilityScaleAnswer scale = m_provider.scaleFor(reading.key, phase, club);
                const double corridor = m_provider.corridorHalfWidth(reading.key, phase, club);

                // §8.5 first, because its grade is the §8.6 coupling.
                pv::VariabilityInput mi;
                mi.liveResidualRmse = some(noise);
                mi.measurementTolerance = m_provider.measurementTolerance(reading.key);
                mi.corridorHalfWidth = some(corridor);
                const pv::VariabilityResult meas = m_measurement->grade(mi);

                pv::VariabilityInput si;
                if (scale.source == pv::ScaleSource::Corridor) si.corridorHalfWidth = scale.scale;
                if (scale.source == pv::ScaleSource::VariabilityScale) si.variabilityScale = scale.scale;
                si.scaleType = scaleType;
                si.propagatedSigma = some(noise);   // R3: never σnoise
                if (meas.graded && meas.grade >= 1) si.sourceNeedleGrade = meas.grade;
                si.values = values;
                const pv::VariabilityResult res = m_scalar->grade(si);

                CalibrationRow row = base;
                row.club = club; row.metric = reading.key; row.label = reading.label; row.unit = reading.unit;
                row.column = reading.column; row.phase = phaseLabel(reading.column);
                row.scaleType = scaleType; row.offered = offered; row.excluded = excluded;
                row.r = res; row.rhoM = meas.rhoM;
                row.positionRow = true;
                row.phaseListed = check == PhaseCheck::NoDescriptor ? -1 : check == PhaseCheck::Pass ? 1 : 0;
                row.phaseNote = phaseNoteOf(check, m_catalogue.descriptor(reading.key), readPhase);
                if (galleryClub(club)) row.values = values;
                // R28: each swing against the club's others (leave-one-out, robust z).
                for (size_t i = 0; i < values.size(); ++i) {
                    if (!std::isfinite(values[i])) continue;
                    std::vector<double> others;
                    for (size_t j = 0; j < values.size(); ++j)
                        if (j != i && std::isfinite(values[j])) others.push_back(values[j]);
                    const auto t = pv::scalarTypicality(values[i], others, scaleType);
                    if (!t) continue;
                    TypicalityRow tr{ base.athlete, base.session, club, reading.key, phaseLabel(reading.column),
                                      swings.at(int(i)) };
                    tr.n = t->n; tr.listed = row.phaseListed; tr.z = t->z;
                    scalarTypicalityRows.push_back(tr);
                }
                CalibrationRow mrow = row;
                mrow.r = meas; mrow.rhoM = meas.rhoM; mrow.values.clear();
                rows.push_back(std::move(row));
                rows.push_back(std::move(mrow));

                if (gallery && club == kGalleryClub)
                    gallery->scalars.push_back(GalleryScalar{ reading.key, reading.label, reading.unit,
                                                              phaseLabel(reading.column), scaleType, res,
                                                              check == PhaseCheck::Pass, swings, values });
            }
        }
    }

    // LAUNCH-MONITOR READINGS (§8.2, §9.6): every lm.* metric, one value per shot, club by club, through the same
    // scalar grader. consistencyInput leaves lm.* out (it reads the camera), and consistencyLmRows lists only the
    // readings past a fence, so the value is read as the fixture loader exposes it and as session_consistency's own
    // ballReading reads one: the first finite phase sample, whatever its phase. Scale: the env's corridor for the key
    // at Impact (the core pack carries lm.* measures), else no scale (no CV fallback, R26). No σnoise and
    // no measurement row: these are the vendor's figures, which the Needle never grades (§9.4) and a golfer's SD is
    // never corrected by (§9.6). No guard either: an LM reading does not rest on the P-ladder.
    void launchMonitor()
    {
        std::map<QString, std::pair<QString, QString>> keys;      // key -> label, unit; sorted
        for (const auto &sh : m_s.shots)
            for (auto it = sh->metrics.constBegin(); it != sh->metrics.constEnd(); ++it)
                if (it.key().startsWith(QLatin1String("lm.")) && !keys.count(it.key()))
                    keys[it.key()] = { it.value().label, it.value().unit };

        for (const auto &[key, facts] : keys) {
            const pv::ScaleType scaleType = m_provider.scaleType(key);
            for (const QString &club : m_clubs) {
                std::vector<double> values;
                int offered = 0, excluded = 0, finite = 0;
                for (size_t i = 0; i < m_s.shots.size(); ++i) {
                    if (m_in.shots[i].club != club) continue;
                    ++offered;
                    double v = kNaN;
                    if (const MetricSeries *m = m_s.shots[i]->metric(key))
                        for (const PhaseSample &ps : m->phaseSamples)
                            if (std::isfinite(ps.value)) { v = ps.value; break; }
                    values.push_back(v);
                    finite += std::isfinite(v) ? 1 : 0;
                }
                if (!finite) continue;

                const VariabilityScaleAnswer scale = m_provider.scaleFor(key, phase::Impact, club);
                pv::VariabilityInput si;
                if (scale.source == pv::ScaleSource::Corridor) si.corridorHalfWidth = scale.scale;
                if (scale.source == pv::ScaleSource::VariabilityScale) si.variabilityScale = scale.scale;
                si.scaleType = scaleType;
                si.values = values;

                CalibrationRow row = base;
                row.club = club; row.metric = key; row.label = facts.first; row.unit = facts.second;
                row.phase = QStringLiteral("LM");
                row.scaleType = scaleType; row.offered = offered; row.excluded = excluded;
                row.launchMonitor = true;
                row.r = m_scalar->grade(si);
                rows.push_back(std::move(row));
            }
        }
    }

    // The propagated σ of a curve: the series σ its span readings carry (consistencyInput's seriesSigma).
    double traceNoise(const QString &key) const
    {
        for (const ConsistencyReading &r : m_in.readings)
            if (r.key == key && r.kind == ConsistencyReadingKind::Span) return m_provider.noiseSigma(r);
        return kNaN;
    }

    // §8.3 over every TimeSeries metric with curves, club by club.
    void traces()
    {
        std::map<QString, std::pair<QString, QString>> keys;      // key -> label, unit; sorted
        for (const auto &sh : m_s.shots)
            for (auto it = sh->metrics.constBegin(); it != sh->metrics.constEnd(); ++it) {
                if (!it.value().hasCurve() || keys.count(it.key())) continue;
                const pa::MetricDescriptor *d = m_catalogue.descriptor(it.key());
                if (!d || d->type != pa::MetricType::TimeSeries) continue;
                keys[it.key()] = { it.value().label, it.value().unit };
            }

        const CanonicalPhase *top = m_al.canonicalOf(phase::Top);
        const double topMs = top ? top->tMs : kNaN;
        const bool haveWindow = std::isfinite(topMs) && topMs < 0.0;
        std::vector<double> grid(size_t(pv::kTraceGridPoints), kNaN);
        if (haveWindow)
            for (int j = 0; j < pv::kTraceGridPoints; ++j)
                grid[size_t(j)] = j == pv::kTraceGridPoints - 1
                                      ? 0.0 : topMs * (1.0 - double(j) / double(pv::kTraceGridPoints - 1));

        for (const auto &[key, facts] : keys) {
            const std::vector<TraceCurve> aligned = haveWindow ? alignedCurves(m_s, m_al, key) : std::vector<TraceCurve>{};
            const double noise = traceNoise(key);
            for (const QString &club : m_clubs) {
                pv::VariabilityInput ti;
                ti.variabilityScale = m_provider.variabilityScale(key);
                ti.scaleType = m_provider.scaleType(key);
                ti.propagatedSigma = some(noise);   // R3: never σnoise
                QStringList swings;
                std::vector<double> peaksCanon;           // each offered swing's peak on the canonical axis
                int offered = 0, excluded = 0, withCurve = 0;
                for (size_t i = 0; i < m_s.shots.size(); ++i) {
                    if (m_in.shots[i].club != club) continue;
                    const MetricSeries *m = m_s.shots[i]->metric(key);
                    if (!m || !m->hasCurve()) continue;
                    ++withCurve;
                    if (m_in.shots[i].guarded) { ++excluded; continue; }
                    ++offered;
                    if (!haveWindow) continue;
                    const TraceCurve &c = aligned[i];
                    std::vector<double> onGrid(size_t(pv::kTraceGridPoints), kNaN);
                    double peakMs = kNaN, peakCanonMs = kNaN;
                    // As the Charts presenter reads a trace (charts_traces_presenter.cpp variabilityTraceOf): only a
                    // shot lined up by its P-anchors or landmarks, and a window end that falls short of the curve by
                    // less than one of its own sample steps reads the curve's end sample (when measured).
                    const ShotWarp *sw = m_al.warpFor(m_s.shots[i]->ordinal);
                    const bool routed = sw && (sw->route == AlignRoute::PAnchors || sw->route == AlignRoute::Landmarks);
                    if (!c.empty() && routed && c.t.size() >= 2 && c.ok.size() == c.t.size()) {
                        const size_t last = c.t.size() - 1;
                        const auto at = [&](double x) {
                            if (x < c.t.front() && c.t.front() - x < c.t[1] - c.t.front()) return c.ok.front() ? c.v.front() : kNaN;
                            if (x > c.t.back() && x - c.t.back() < c.t.back() - c.t[last - 1]) return c.ok.back() ? c.v.back() : kNaN;
                            return valueAt(c, x);
                        };
                        for (int j = 0; j < pv::kTraceGridPoints; ++j) onGrid[size_t(j)] = at(grid[size_t(j)]);
                        // The |value| peak inside the window, the earliest of equals (seqPeakIndex's rule), on the
                        // canonical axis, then back to the shot's own clock: real ms from impact.
                        double best = -1.0, bestT = kNaN;
                        for (size_t k = 0; k < c.t.size(); ++k) {
                            if (c.t[k] < topMs || c.t[k] > 0.0) continue;
                            if (k < c.ok.size() && !c.ok[k]) continue;
                            if (!std::isfinite(c.v[k])) continue;
                            const double mag = pv::peakMagnitude(c.v[k], ti.scaleType);   // |x|, wrapped if Circular
                            if (mag > best) { best = mag; bestT = c.t[k]; }
                        }
                        const ShotWarp *w = m_al.warpFor(m_s.shots[i]->ordinal);
                        if (std::isfinite(bestT) && w) { peakMs = w->unwarpMs(bestT); peakCanonMs = bestT; }
                    }
                    ti.curves.push_back(std::move(onGrid));
                    ti.peakTimesMs.push_back(peakMs);
                    peaksCanon.push_back(peakCanonMs);
                    swings << m_s.shots[i]->swingDir;
                }
                if (!withCurve) continue;

                CalibrationRow row = base;
                row.club = club; row.metric = key; row.label = facts.first; row.unit = facts.second;
                row.phase = QStringLiteral("P4-P7");
                row.scaleType = m_provider.scaleType(key);
                row.offered = offered; row.excluded = excluded;
                if (haveWindow) {
                    row.r = m_trace->grade(ti);
                    // R28: each whole swing against the club's other whole swings (leave-one-out, robust z).
                    std::vector<size_t> whole;
                    for (size_t i = 0; i < ti.curves.size(); ++i) {
                        const auto &cv = ti.curves[i];
                        if (std::isfinite(ti.peakTimesMs[i])
                            && std::all_of(cv.begin(), cv.end(), [](double x) { return std::isfinite(x); }))
                            whole.push_back(i);
                    }
                    for (size_t i : whole) {
                        pv::TraceTypicalityInput in;
                        in.curve = ti.curves[i];
                        in.peakTimeMs = ti.peakTimesMs[i];
                        in.scaleType = ti.scaleType;
                        for (size_t j : whole) {
                            if (j == i) continue;
                            in.windowCurves.push_back(ti.curves[j]);
                            in.windowPeakTimesMs.push_back(ti.peakTimesMs[j]);
                        }
                        const auto tt = pv::traceTypicality(in);
                        if (!tt) continue;
                        TypicalityRow tr{ base.athlete, base.session, club, key, QStringLiteral("P4-P7"), swings.at(int(i)) };
                        tr.n = tt->n; tr.z = tt->rmsZ; tr.timingZ = tt->timingZ;
                        tr.timingDevMs = tt->timingDeviationMs; tr.amplitudeZ = tt->amplitudeZ;
                        traceTypicalityRows.push_back(tr);
                    }
                    // WHERE THE PEAKS SIT. Not part of the grade: an angle or a position often peaks at the window's
                    // edge, and then ρₜ times the downswing, not a peak. Over the swings the grader used.
                    const double step = -topMs / double(pv::kTraceGridPoints - 1);
                    int used = 0, atTop = 0, atImpact = 0;
                    for (size_t i = 0; i < ti.curves.size(); ++i) {
                        const auto &cv = ti.curves[i];
                        if (!std::isfinite(ti.peakTimesMs[i])
                            || !std::all_of(cv.begin(), cv.end(), [](double x) { return std::isfinite(x); }))
                            continue;
                        ++used;
                        atTop    += peaksCanon[i] - topMs <= step ? 1 : 0;
                        atImpact += -peaksCanon[i] <= step ? 1 : 0;
                    }
                    if (used) {
                        row.peakAtTopShare = double(atTop) / used;
                        row.peakAtImpactShare = double(atImpact) / used;
                    }
                } else {
                    row.r.basis = pv::Basis::Trace;
                    row.toolReason = QStringLiteral("noCanonicalTop");    // the session has no canonical P4: no window
                }
                if (gallery && club == kGalleryClub && haveWindow) {
                    GalleryTrace g{ key, facts.first, facts.second, row.r, topMs, {}, {}, {} };
                    for (size_t i = 0; i < ti.curves.size(); ++i) {
                        const auto &cv = ti.curves[i];
                        const bool whole = std::isfinite(ti.peakTimesMs[i])
                                        && std::all_of(cv.begin(), cv.end(), [](double x) { return std::isfinite(x); });
                        if (!whole) continue;
                        g.swings << swings.at(int(i));
                        g.curves.push_back(cv);
                        g.peakTimesMs.push_back(ti.peakTimesMs[i]);
                    }
                    gallery->traces.push_back(std::move(g));
                }
                rows.push_back(std::move(row));
            }
        }
    }

    // §8.4: the kinematic-sequence order per swing, AS THE APP STORED IT (the KS extract: order, orderResolved,
    // placed), never re-derived from the curves.
    //
    // CALIBRATION-ONLY CHOICE, FOR MARK. The app's order lists the PLACED segments only, and which are placed
    // varies by swing (mostly leadArm,club; sometimes with pelvis). The spec has no rule for partial orders, and
    // an order of two segments and one of three are different questions. So the window is the resolved swings
    // whose placed SET equals the session-club's modal set, k = m! for that set, and every other swing (another
    // set, or unresolved, or absent from the extract, or guarded) is counted in ksExcluded.
    void ordinal()
    {
        if (!ks) return;
        for (const QString &club : m_clubs) {
            int unresolved = 0, guarded = 0;
            std::vector<std::pair<QString, QString>> resolved;     // (set, order) per resolved swing
            for (size_t i = 0; i < m_s.shots.size(); ++i) {
                if (m_in.shots[i].club != club) continue;
                // The stored order rests on the same P-ladder timing a trace does, so a guarded swing is out.
                if (m_in.shots[i].guarded) { ++guarded; continue; }
                const auto it = ks->constFind(m_s.shots[i]->swingDir);
                if (it == ks->constEnd() || !it->resolved || it->order.isEmpty()) { ++unresolved; continue; }
                resolved.emplace_back(segmentSet(it->placed), it->order.join(QLatin1Char('>')));
            }
            if (resolved.empty() && !unresolved && !guarded) continue;

            // The modal set: the most swings; a tie goes to the set that sorts first, and the note says so.
            QMap<QString, int> sets;
            for (const auto &[set, order] : resolved) ++sets[set];
            QString modal;
            bool tied = false;
            for (auto it = sets.constBegin(); it != sets.constEnd(); ++it) {
                if (modal.isEmpty() || it.value() > sets.value(modal)) { modal = it.key(); tied = false; }
                else if (it.value() == sets.value(modal)) tied = true;
            }
            const int m = modal.isEmpty() ? 0 : int(modal.count(QLatin1Char(','))) + 1;

            pv::VariabilityInput oi;
            oi.k = factorial(m);
            for (const auto &[set, order] : resolved)
                if (set == modal) oi.categories << order;
            const pv::VariabilityResult res = m_ordinal->grade(oi);

            QMap<QString, int> counts;
            for (const QString &c : oi.categories) ++counts[c];
            QString modalOrder;
            for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
                if (modalOrder.isEmpty() || it.value() > counts.value(modalOrder)) modalOrder = it.key();

            CalibrationRow row = base;
            row.club = club; row.metric = QStringLiteral("kinematicSequence");
            row.label = QStringLiteral("Kinematic sequence order (the app's stored order, placed segments)");
            row.phase = QStringLiteral("P4-P8");
            row.scaleType = pv::ScaleType::Ordinal;
            row.offered = int(oi.categories.size());
            row.excluded = guarded;
            row.r = res;
            row.detail = modalOrder;
            row.ksSegments = modal;
            row.ksExcluded = int(resolved.size()) - int(oi.categories.size()) + unresolved + guarded;
            row.note = QStringLiteral("calibration-only: window = resolved swings whose placed set is the "
                                      "session-club modal set; the spec has no rule for partial orders (for Mark)");
            if (tied) row.note += QStringLiteral("; modal set tied, the first in sort order taken");
            if (modal.isEmpty()) row.toolReason = QStringLiteral("noResolvedOrder");   // no swing of the club resolved
            rows.push_back(std::move(row));
        }
    }

    // A placed-segment list as a set, in the sequence's own segment order: "leadArm,club".
    static QString segmentSet(const QStringList &placed)
    {
        QStringList out;
        for (const QString &id : sequenceSegmentIds()) if (placed.contains(id)) out << id;
        QStringList other;
        for (const QString &id : placed)
            if (!out.contains(id) && !other.contains(id)) other << id;
        std::sort(other.begin(), other.end());
        return (out + other).join(QLatin1Char(','));
    }

    const SessionSnapshot           &m_s;
    const SessionAlignment          &m_al;
    const ConsistencyInput          &m_in;
    const IVariabilityScaleProvider &m_provider;
    const pa::MetricCatalogue       &m_catalogue;
    QStringList                      m_clubs;
    std::unique_ptr<pv::IVariabilityGrader> m_scalar, m_trace, m_ordinal, m_measurement;
};

// ── the CSVs ────────────────────────────────────────────────────────────────────────────────────

const char *kCalibrationHeader =
    "athlete,session,date,club,metric,phase,basis,n,scale,scaleSource,scaleType,sd,rho,rhoAmp,rhoTiming,sdPeakMs,"
    "sPrime,rhoM,grade,graded,ungradedReason,measurementLimited,"
    // beyond the brief's columns, for review:
    "column,label,unit,sdObserved,timingScale,timingScaleSource,k,uncertainty,uncertaintyKind,offered,excluded,"
    "detail,basisString,ksSegments,ksExcluded,note,phaseListed,phaseNote,source,peakAtTopShare,peakAtImpactShare";

QString calibrationLine(const CalibrationRow &w)
{
    const pv::VariabilityResult &r = w.r;
    const QString reason = !w.toolReason.isEmpty() ? w.toolReason : reasonToken(r.ungradedReason);
    QStringList f;
    f << w.athlete << w.session << w.date << clubLabel(w.club) << w.metric << w.phase << basisToken(r.basis)
      << QString::number(r.n) << csvNum(r.scale) << sourceToken(r.scaleSource)
      << QString::fromLatin1(pv::scaleTypeToken(w.scaleType).data(), qsizetype(pv::scaleTypeToken(w.scaleType).size()))
      << csvNum(r.sd) << csvNum(r.rho) << csvNum(r.rhoAmp) << csvNum(r.rhoTiming) << csvNum(r.sdPeakMs)
      << csvNum(r.sPrime) << csvNum(w.rhoM) << QString::number(r.grade) << (r.graded ? "true" : "false") << reason
      << (r.measurementLimited ? "true" : "false")
      << (w.column >= 0 ? QString::number(w.column) : QString()) << w.label << w.unit << csvNum(r.sdObserved)
      << csvNum(r.timingScaleMs) << (r.basis == pv::Basis::Trace ? sourceToken(r.timingScaleSource) : QString())
      << (r.k ? QString::number(r.k) : QString()) << csvNum(r.uncertainty) << uncertaintyToken(r.uncertaintyKind)
      << QString::number(w.offered) << QString::number(w.excluded)
      << w.detail << r.basisString(w.unit)
      << w.ksSegments << (w.ksExcluded >= 0 ? QString::number(w.ksExcluded) : QString()) << w.note
      << (w.positionRow && w.phaseListed >= 0 ? QString::number(w.phaseListed) : QString())
      << (w.positionRow ? w.phaseNote
                        : w.launchMonitor ? QStringLiteral("launch monitor: one reading per shot, no P-position")
                                          : QStringLiteral("not a Position reading"))
      << (w.launchMonitor ? QStringLiteral("lm") : QStringLiteral("swing"))
      << csvNum(w.peakAtTopShare) << csvNum(w.peakAtImpactShare);
    for (QString &s : f) s = csvField(s);
    return f.join(QLatin1Char(','));
}

double trendRho(const pv::VariabilityResult &r)
{
    return r.basis == pv::Basis::Ordinal ? r.sPrime : r.basis == pv::Basis::Measurement ? r.rhoM : r.rho;
}

bool writeText(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(text.toUtf8());
    return true;
}

// ── the gallery ─────────────────────────────────────────────────────────────────────────────────

QJsonArray galleryFunnel(const std::vector<CalibrationRow> &rows)
{
    // Graded 1..5 scalar rows of the athlete's 7-iron, grouped by (metric, phase), oldest first.
    std::map<std::pair<QString, QString>, std::vector<const CalibrationRow *>> groups;
    for (const CalibrationRow &w : rows)
        if (w.athlete == kGalleryAthlete && w.club == kGalleryClub && w.r.basis == pv::Basis::Scalar && w.phaseListed == 1
            && w.r.graded && w.r.grade >= 1)
            groups[{ w.metric, w.phase }].push_back(&w);
    std::vector<std::pair<std::pair<QString, QString>, size_t>> bySessions;
    for (const auto &[k, v] : groups) bySessions.emplace_back(k, v.size());
    std::stable_sort(bySessions.begin(), bySessions.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

    QJsonArray out;
    QSet<QString> keysTaken;
    for (const auto &[k, count] : bySessions) {
        if (out.size() >= 6) break;
        if (keysTaken.contains(k.first)) continue;          // six different metrics
        keysTaken.insert(k.first);
        const std::vector<const CalibrationRow *> &pts = groups[k];
        std::vector<pv::TrendPoint> tp;
        QJsonArray sessions;
        for (const CalibrationRow *w : pts) {
            const QString breakKey = clubLabel(w->club) + QLatin1Char('|') + sourceToken(w->r.scaleSource);
            sessions.append(QJsonObject{ { "date", w->date }, { "session", w->session }, { "rho", jnum(w->r.rho) },
                                         { "grade", w->r.grade }, { "n", w->r.n }, { "breakKey", breakKey },
                                         { "sd", jnum(w->r.sd) }, { "scale", jnum(w->r.scale) } });
            tp.push_back(pv::TrendPoint{ w->r.rho, w->r.n, w->club, pv::Basis::Scalar, sourceToken(w->r.scaleSource) });
        }
        const pv::TrendResult t = pv::variabilityTrend(tp);
        out.append(QJsonObject{ { "metric", k.first }, { "phase", k.second }, { "label", pts.front()->label },
                                { "unit", pts.front()->unit }, { "club", kGalleryClub }, { "sessions", sessions },
                                { "trend", trendWord(t.direction) }, { "trendBasis", t.basisString() } });
    }
    return out;
}

QJsonArray galleryTypicality(const SessionGallery &g)
{
    // Six scalar readings graded 1..5 with the most swings, six different metrics, corridor-scaled first.
    std::vector<const GalleryScalar *> pick;
    for (const GalleryScalar &s : g.scalars) if (s.listed && s.r.graded && s.r.grade >= 1) pick.push_back(&s);
    std::stable_sort(pick.begin(), pick.end(), [](const GalleryScalar *a, const GalleryScalar *b) {
        const bool ca = a->r.scaleSource == pv::ScaleSource::Corridor, cb = b->r.scaleSource == pv::ScaleSource::Corridor;
        if (ca != cb) return ca;
        return a->r.n > b->r.n;
    });
    QJsonArray out;
    QSet<QString> keysTaken;
    for (const GalleryScalar *s : pick) {
        if (out.size() >= 6) break;
        if (keysTaken.contains(s->key)) continue;
        keysTaken.insert(s->key);
        QJsonArray shots;
        double mean = kNaN, sd = kNaN;
        for (size_t i = 0; i < s->values.size(); ++i) {
            if (!std::isfinite(s->values[i])) continue;
            const auto t = pv::scalarTypicality(s->values[i], s->values, s->scaleType);
            if (!t) continue;
            mean = t->usual; sd = t->sd;
            shots.append(QJsonObject{ { "swing", s->swings.at(int(i)) }, { "value", jnum(s->values[i]) },
                                      { "z", jnum(t->z) }, { "band", typicalityToken(t->band) } });
        }
        out.append(QJsonObject{ { "metric", s->key }, { "label", s->label }, { "phase", s->phase }, { "unit", s->unit },
                                { "scaleType", QString::fromLatin1(pv::scaleTypeToken(s->scaleType).data(),
                                                                   qsizetype(pv::scaleTypeToken(s->scaleType).size())) },
                                { "mean", jnum(mean) }, { "sd", jnum(sd) }, { "n", s->r.n }, { "grade", s->r.grade },
                                { "rho", jnum(s->r.rho) }, { "basisString", s->r.basisString(s->unit) },
                                { "shots", shots } });
    }
    return out;
}

QJsonArray galleryTraces(const SessionGallery &g)
{
    // Three graded TimeSeries metrics: the preferred three if graded, then the most swings.
    static const QStringList preferred{ QStringLiteral("clubheadSpeed"), QStringLiteral("pelvisRotation"),
                                        QStringLiteral("thoraxRotation") };
    std::vector<const GalleryTrace *> pick;
    for (const GalleryTrace &t : g.traces) if (t.r.graded && t.r.grade >= 1) pick.push_back(&t);
    std::stable_sort(pick.begin(), pick.end(), [](const GalleryTrace *a, const GalleryTrace *b) {
        const int pa_ = preferred.indexOf(a->key), pb = preferred.indexOf(b->key);
        if ((pa_ >= 0) != (pb >= 0)) return pa_ >= 0;
        if (pa_ >= 0) return pa_ < pb;
        return a->r.n > b->r.n;
    });
    QJsonArray out;
    for (const GalleryTrace *t : pick) {
        if (out.size() >= 3) break;
        const size_t n = t->curves.size();
        const int P = pv::kTraceGridPoints;
        std::vector<double> mean(size_t(P), kNaN), sd(size_t(P), kNaN), grid(size_t(P), kNaN), u(size_t(P), 0.0);
        for (int j = 0; j < P; ++j) {
            std::vector<double> col;
            for (const auto &c : t->curves) col.push_back(c[size_t(j)]);
            double sum = 0.0;
            for (double x : col) sum += x;
            mean[size_t(j)] = n ? sum / double(n) : kNaN;
            sd[size_t(j)] = pv::sampleSd(col);
            grid[size_t(j)] = j == P - 1 ? 0.0 : t->topCanonMs * (1.0 - double(j) / double(P - 1));
            u[size_t(j)] = double(j) / double(P - 1);
        }
        // The swing shown on the envelope: the least typical one (largest RMS z), so the gallery shows the
        // envelope doing its job. Its own curve stays in the window, as the session's typicality reads it.
        int best = -1;
        std::optional<pv::TraceTypicality> bestT;
        for (size_t i = 0; i < n; ++i) {
            pv::TraceTypicalityInput in;
            in.curve = t->curves[i];
            in.peakTimeMs = t->peakTimesMs[i];
            in.windowCurves = t->curves;
            in.windowPeakTimesMs = t->peakTimesMs;
            const auto tt = pv::traceTypicality(in);
            if (tt && (!bestT || tt->rmsZ > bestT->rmsZ)) { bestT = tt; best = int(i); }
        }
        QJsonObject swing;
        if (best >= 0) {
            swing = QJsonObject{ { "swing", t->swings.at(best) }, { "trace", jarray(t->curves[size_t(best)]) },
                                 { "peakTimeMs", jnum(t->peakTimesMs[size_t(best)]) }, { "rmsZ", jnum(bestT->rmsZ) },
                                 { "band", typicalityToken(bestT->band) },
                                 { "timingDeviationMs", jnum(bestT->timingDeviationMs) },
                                 { "timingZ", jnum(bestT->timingZ) }, { "amplitudeZ", jnum(bestT->amplitudeZ) },
                                 { "amplitudeSign", bestT->amplitudeSign }, { "basisString", bestT->basisString() },
                                 { "selection", "largest rmsZ in the session" } };
        }
        out.append(QJsonObject{ { "metric", t->key }, { "label", t->label }, { "unit", t->unit },
                                { "window", "P4-P7 (canonical top to impact)" }, { "topCanonMs", jnum(t->topCanonMs) },
                                { "gridCanonMs", jarray(grid) }, { "gridUnit", jarray(u) }, { "mean", jarray(mean) },
                                { "sd", jarray(sd) }, { "n", int(n) }, { "grade", t->r.grade },
                                { "rho", jnum(t->r.rho) }, { "rhoAmp", jnum(t->r.rhoAmp) },
                                { "rhoTiming", jnum(t->r.rhoTiming) }, { "sdPeakMs", jnum(t->r.sdPeakMs) },
                                { "scaleSource", sourceToken(t->r.scaleSource) },
                                { "basisString", t->r.basisString(t->unit) }, { "swing", swing } });
    }
    return out;
}

QJsonArray galleryDispersion(const std::vector<CalibrationRow> &rows)
{
    // Two per grade 5..1 where there are two: the most swings first, each a different metric.
    QJsonArray out;
    QSet<QString> keysTaken;
    for (int grade = 5; grade >= 1; --grade) {
        std::vector<const CalibrationRow *> cand;
        for (const CalibrationRow &w : rows)
            if (w.athlete == kGalleryAthlete && w.club == kGalleryClub && w.r.basis == pv::Basis::Scalar && w.phaseListed == 1
                && w.r.graded && w.r.grade == grade)
                cand.push_back(&w);
        std::stable_sort(cand.begin(), cand.end(), [](const CalibrationRow *a, const CalibrationRow *b) {
            return a->r.n > b->r.n;
        });
        int taken = 0;
        for (const CalibrationRow *w : cand) {
            if (taken >= 2) break;
            if (keysTaken.contains(w->metric)) continue;
            keysTaken.insert(w->metric);
            ++taken;
            out.append(QJsonObject{ { "session", w->session }, { "date", w->date }, { "metric", w->metric },
                                    { "label", w->label }, { "phase", w->phase }, { "unit", w->unit },
                                    { "grade", w->r.grade }, { "rho", jnum(w->r.rho) }, { "sd", jnum(w->r.sd) },
                                    { "sdObserved", jnum(w->r.sdObserved) }, { "scale", jnum(w->r.scale) },
                                    { "scaleSource", sourceToken(w->r.scaleSource) }, { "n", w->r.n },
                                    { "measurementLimited", w->r.measurementLimited },
                                    { "basisString", w->r.basisString(w->unit) }, { "values", jarray(w->values) } });
        }
    }
    return out;
}

void defaultEnv(const char *name, const char *file)
{
    if (!qEnvironmentVariableIsSet(name)) qputenv(name, QByteArray(PP_DIAGNOSTICS_DIR "/") + file);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 4) {
        std::fprintf(stderr, "usage: variability_calibrate <fixtures-root> <ks-root> <out-dir>\n");
        return 2;
    }
    defaultEnv("PINPOINT_CORE_PACK", "core.json");
    defaultEnv("PINPOINT_CORE_NORMS", "norms.json");
    defaultEnv("PINPOINT_CORE_CONTEXTS", "contexts.json");

    const QDir root(QString::fromLocal8Bit(argv[1]));
    const QDir ksRoot(QString::fromLocal8Bit(argv[2]));
    const QString outDir = QString::fromLocal8Bit(argv[3]);
    if (!root.exists()) {
        std::fprintf(stderr, "variability_calibrate: no such folder %s\n", argv[1]);
        return 1;
    }
    if (!QDir().mkpath(outDir)) {
        std::fprintf(stderr, "variability_calibrate: cannot make %s\n", argv[3]);
        return 1;
    }

    // The GUI thread's first call (consistency_env.h): this is the main thread.
    const ConsistencyEnv env = consistencyEnvFromPacks();
    const std::unique_ptr<IVariabilityScaleProvider> provider = makeVariabilityScaleProvider();
    pa::MetricCatalogue catalogue;
    pa::installMetricManifest(catalogue);

    QElapsedTimer total;
    total.start();
    std::vector<CalibrationRow> rows;
    std::vector<TypicalityRow> scalarTyp, traceTyp;      // R28: leave-one-out typicality per swing
    SessionGallery gallery;
    int sessions = 0, shots = 0;

    // <root>/<athlete>/*.json, one level, both sorted: a session's name starts with its date, so files come
    // oldest first within an athlete, which is the order the trend reads them in.
    for (const QFileInfo &athleteDir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString athlete = athleteDir.fileName();
        const QDir dir(athleteDir.absoluteFilePath());
        for (const QFileInfo &file : dir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
            QElapsedTimer timer;
            timer.start();
            const charts_test::Fixture fx = charts_test::loadFixture(file.absoluteFilePath());
            if (!fx.ok) {
                std::fprintf(stderr, "variability_calibrate: cannot read %s\n", qPrintable(file.absoluteFilePath()));
                return 1;
            }
            const qint64 loadMs = timer.restart();
            const SessionAlignment alignment = alignSession(fx.session);
            const ConsistencyInput input = consistencyInput(fx.session, alignment, env);

            const QString name = fx.name.isEmpty() ? file.completeBaseName() : fx.name;
            SessionCalibration cal(fx.session, alignment, input, *provider, catalogue);
            bool ksFound = false;
            const KsSession ks = loadKs(ksRoot.filePath(athlete + QLatin1Char('/') + name + QStringLiteral(".json")), &ksFound);
            if (ksFound) cal.ks = &ks;
            else std::printf("  no KS extract for %s/%s: no ordinal rows\n", qPrintable(athlete), qPrintable(name));
            cal.base.athlete = athlete;
            cal.base.session = name;
            cal.base.date = name.left(10);
            const bool galleryHere = athlete == kGalleryAthlete && cal.base.date == kGalleryDate
                                  && gallery.session.isEmpty();
            if (galleryHere) {
                gallery.session = name;
                gallery.date = cal.base.date;
                gallery.shots = int(fx.session.shots.size());
                cal.gallery = &gallery;
            }
            cal.run();
            const qint64 gradeMs = timer.elapsed();
            int guarded = 0;
            for (const ConsistencyShot &s : input.shots) guarded += s.guarded ? 1 : 0;
            std::printf("%s/%s: %zu shots (%d guarded), %zu rows, load %lld ms, align+grade %lld ms\n",
                        qPrintable(athlete), qPrintable(name), fx.session.shots.size(), guarded, cal.rows.size(),
                        qint64(loadMs), qint64(gradeMs));
            std::fflush(stdout);
            ++sessions;
            shots += int(fx.session.shots.size());
            for (CalibrationRow &w : cal.rows) rows.push_back(std::move(w));
            scalarTyp.insert(scalarTyp.end(), cal.scalarTypicalityRows.begin(), cal.scalarTypicalityRows.end());
            traceTyp.insert(traceTyp.end(), cal.traceTypicalityRows.begin(), cal.traceTypicalityRows.end());
        }
    }

    // ---- variability_calibration.csv
    QString csv = QString::fromLatin1(kCalibrationHeader) + QLatin1Char('\n');
    for (const CalibrationRow &w : rows) csv += calibrationLine(w) + QLatin1Char('\n');
    if (!writeText(outDir + QStringLiteral("/variability_calibration.csv"), csv)) {
        std::fprintf(stderr, "variability_calibrate: cannot write the calibration CSV\n");
        return 1;
    }

    // ---- variability_trend.csv: per (athlete, metric, phase, club, basis), oldest first. The scale source is
    // each point's scaleKey, so a change of scale is a BREAK inside variabilityTrend (§8.8), which then reads the
    // latest unbroken run. Measurement rows are not a consistency trend (§8.8 grades ρ of the golfer) and are left
    // out; ordinal series go in and come back Insufficient, as the trend refuses an s′.
    std::map<QStringList, std::vector<const CalibrationRow *>> series;
    for (const CalibrationRow &w : rows) {
        if (w.r.basis == pv::Basis::Measurement) continue;
        if (!std::isfinite(trendRho(w.r))) continue;
        series[{ w.athlete, w.metric, w.phase, clubLabel(w.club), basisToken(w.r.basis) }].push_back(&w);
    }
    QString trend = QStringLiteral("athlete,metric,phase,club,basis,sessions,rhoFirst,rhoLast,change,se,word,"
                                   "sessionsWithReading,scaleSourceLast,phaseListed\n");
    QMap<QString, int> trendWords;
    for (const auto &[key, pts] : series) {
        std::vector<pv::TrendPoint> tp;
        for (const CalibrationRow *w : pts)
            tp.push_back(pv::TrendPoint{ trendRho(w->r), w->r.n, clubLabel(w->club), w->r.basis,
                                         sourceToken(w->r.scaleSource) });
        const pv::TrendResult t = pv::variabilityTrend(tp);
        QString word = trendWord(t.direction);
        if (t.ordinalBasis) word += QStringLiteral(" (ordinal)");
        ++trendWords[key.at(4) + QLatin1Char(' ') + word];
        QStringList f = key;
        const bool has = t.direction != pv::TrendDirection::Insufficient;
        f << QString::number(t.sessions) << (has ? csvNum(t.firstRho) : QString()) << (has ? csvNum(t.lastRho) : QString())
          << (has ? csvNum(t.change) : QString()) << (has ? csvNum(t.changeSe) : QString()) << word
          << QString::number(int(pts.size())) << sourceToken(pts.back()->r.scaleSource)
          << (pts.back()->positionRow && pts.back()->phaseListed >= 0 ? QString::number(pts.back()->phaseListed) : QString());
        for (QString &s : f) s = csvField(s);
        trend += f.join(QLatin1Char(',')) + QLatin1Char('\n');
    }
    if (!writeText(outDir + QStringLiteral("/variability_trend.csv"), trend)) {
        std::fprintf(stderr, "variability_calibrate: cannot write the trend CSV\n");
        return 1;
    }

    // ---- R28: typicality_scalar.csv, typicality_trace.csv (one leave-one-out row per swing) and
    // trace_descriptors.csv (what each trace key's descriptor says: draws a curve, where its peak matters)
    {
        const auto typCsv = [](const std::vector<TypicalityRow> &v) {
            QString t = QStringLiteral("athlete,session,club,metric,phase,swing,n,phaseListed,z,timingZ,timingDevMs,amplitudeZ\n");
            for (const TypicalityRow &w : v) {
                QStringList f;
                f << w.athlete << w.session << clubLabel(w.club) << w.metric << w.phase << w.swing << QString::number(w.n)
                  << (w.listed >= 0 ? QString::number(w.listed) : QString()) << csvNum(w.z) << csvNum(w.timingZ)
                  << csvNum(w.timingDevMs) << csvNum(w.amplitudeZ);
                for (QString &x : f) x = csvField(x);
                t += f.join(QLatin1Char(',')) + QLatin1Char('\n');
            }
            return t;
        };
        QSet<QString> traceKeys;
        for (const CalibrationRow &w : rows) if (w.r.basis == pv::Basis::Trace) traceKeys.insert(w.metric);
        QStringList sorted(traceKeys.begin(), traceKeys.end());
        sorted.sort();
        QString d = QStringLiteral("metric,drawsCurve,scaleType,variabilityScale,phases,domain\n");
        for (const QString &k : sorted) {
            const pa::MetricDescriptor *md = catalogue.descriptor(k);
            QStringList ph;
            if (md) for (pa::Phase p : md->phases) ph << phaseName(p);
            QStringList f;
            f << k << (md ? (md->card.drawsCurve() ? "1" : "0") : "")
              << QString::fromLatin1(pv::scaleTypeToken(provider->scaleType(k)).data(),
                                     qsizetype(pv::scaleTypeToken(provider->scaleType(k)).size()))
              << (provider->variabilityScale(k) ? csvNum(*provider->variabilityScale(k)) : QString())
              << ph.join(QLatin1Char(' '))
              << (md ? phaseName(md->domain.first) + QLatin1Char('-') + phaseName(md->domain.last) : QString());
            for (QString &x : f) x = csvField(x);
            d += f.join(QLatin1Char(',')) + QLatin1Char('\n');
        }
        if (!writeText(outDir + QStringLiteral("/typicality_scalar.csv"), typCsv(scalarTyp))
            || !writeText(outDir + QStringLiteral("/typicality_trace.csv"), typCsv(traceTyp))
            || !writeText(outDir + QStringLiteral("/trace_descriptors.csv"), d)) {
            std::fprintf(stderr, "variability_calibrate: cannot write the R28 CSVs\n");
            return 1;
        }
    }

    // ---- gallery_fixture.json
    const QJsonObject galleryJson{
        { "source", "variability_calibrate over a read-only extract; Mark-Liversedge, 7 IRON only" },
        { "athlete", kGalleryAthlete }, { "club", kGalleryClub },
        { "typicalitySession", gallery.session }, { "typicalitySessionShots", gallery.shots },
        { "funnel", galleryFunnel(rows) }, { "typicality", galleryTypicality(gallery) },
        { "traceEnvelopes", galleryTraces(gallery) }, { "dispersion", galleryDispersion(rows) } };
    QFile gf(outDir + QStringLiteral("/gallery_fixture.json"));
    if (!gf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "variability_calibrate: cannot write the gallery fixture\n");
        return 1;
    }
    gf.write(QJsonDocument(galleryJson).toJson(QJsonDocument::Indented));
    gf.close();

    // ---- the summary
    std::printf("\n%d sessions, %d shots, %zu rows, %lld ms\n", sessions, shots, rows.size(), qint64(total.elapsed()));
    if (gallery.session.isEmpty())
        std::printf("gallery: no %s session of %s; typicality and trace envelopes are empty\n",
                    qPrintable(kGalleryDate), qPrintable(kGalleryAthlete));
    // Twice: every row, then the Position rows whose read phase the descriptor lists (trace and ordinal rows are
    // not Position readings and are the same in both).
    const auto summarise = [&](const char *title, const std::function<bool(const CalibrationRow &)> &take) {
        std::printf("\n==== %s\n", title);
        for (pv::Basis b : { pv::Basis::Scalar, pv::Basis::Trace, pv::Basis::Ordinal, pv::Basis::Measurement }) {
            int count = 0, ungraded = 0;
            std::array<int, 6> grades{};
            QMap<QString, int> reasons;
            for (const CalibrationRow &w : rows) {
                if (w.r.basis != b || !take(w)) continue;
                ++count;
                if (w.r.graded) ++grades[size_t(w.r.grade)];
                else ++ungraded;
                const QString reason = !w.toolReason.isEmpty() ? w.toolReason : reasonToken(w.r.ungradedReason);
                if (!reason.isEmpty()) ++reasons[reason];
            }
            std::printf("\n%-12s rows %d\n  grades: 5:%d 4:%d 3:%d 2:%d 1:%d 0 (grade-0 mark):%d  not graded:%d\n",
                        qPrintable(basisToken(b)), count, grades[5], grades[4], grades[3], grades[2], grades[1],
                        grades[0], ungraded);
            for (auto it = reasons.constBegin(); it != reasons.constEnd(); ++it)
                std::printf("  %-22s %6d  %5.1f %%\n", qPrintable(it.key()), it.value(),
                            count ? 100.0 * it.value() / count : 0.0);
        }
    };
    summarise("ALL ROWS", [](const CalibrationRow &) { return true; });
    summarise("LISTED: phaseListed = 1 Position rows + every lm, trace and ordinal row",
              [](const CalibrationRow &w) { return !w.positionRow || w.phaseListed == 1; });
    QMap<QString, int> listedCounts;
    for (const CalibrationRow &w : rows)
        if (w.positionRow && w.r.basis == pv::Basis::Scalar)
            ++listedCounts[w.phaseListed < 0 ? QStringLiteral("blank (no descriptor)") : QString::number(w.phaseListed)];
    std::printf("\nscalar rows by phaseListed:");
    for (auto it = listedCounts.constBegin(); it != listedCounts.constEnd(); ++it)
        std::printf("  %s: %d", qPrintable(it.key()), it.value());
    std::printf("\n");
    std::printf("\ntrend words:\n");
    for (auto it = trendWords.constBegin(); it != trendWords.constEnd(); ++it)
        std::printf("  %-30s %d\n", qPrintable(it.key()), it.value());

    return 0;
}
