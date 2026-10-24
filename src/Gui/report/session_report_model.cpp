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

#include "session_report_model.h"

#include "report_meta.h"
#include "report_posture.h"
#include "report_wrist.h"
#include "report_stills.h"
#include "report_words.h"

#include "../athlete/athlete_controller.h"
#include "../charts/charts_posture_presenter.h"
#include "../charts/charts_wrist_presenter.h"
#include "../charts/session_series_model.h"
#include "../charts/session_wrist.h"
#include "../diagnostics/session_diagnostics_model.h"
#include "../launchmonitor/lm_session_model.h"
#include "../shot/shot_list_model.h"
#include "../../Coaching/session_report.h"
#include "../../Coaching/work_ons.h"
#include "../../Core/pp_debug.h"
#include "../../Core/pp_settings.h"
#include "../../Core/studio_profile.h"
#include "../../Diagnostics/packs/drill_pack.h"
#include "../../Diagnostics/packs/gate_words_pack.h"
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Export/swing_paths.h"
#include "../../Export/swing_store.h"
#include "../../LaunchMonitor/launch_monitor_reading.h"
#include "../../LaunchMonitor/lm_session_reductions.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <limits>

using namespace pinpoint::analysis;
namespace R = pinpoint::analysis::report;
namespace M = pinpoint::report;
namespace W = pinpoint::report::words;

namespace {

constexpr int kNoteBelowSwings  = 3;      // §3.1: fewer than three swings is a one-page note
constexpr int kGridHabitCap     = 5;      // §9.1 rule 4
constexpr int kGridSeenCap      = 4;
constexpr int kGridBallCap      = 4;
constexpr int kGridMinFired     = 3;      // "watching rows that fired on at least 3 swings" (§3)
constexpr int kTodayCap         = 6;
constexpr int kTodayEarlierRows = 2;      // habits earlier that day that are not on today's list
constexpr int kStrengthsCap     = 3;
constexpr int kWatchingCap      = 5;
constexpr int kBallGroupCap     = 4;
constexpr int kNotCheckedCap    = 4;
constexpr int kPracticeCap      = 6;
constexpr int kPrintTimeoutMs   = 20 * 60 * 1000;
constexpr double kStillAspect   = 2.0 / 3.0;   // PpStill's frame: height 1.5 × width, filled by a centre crop

const QString kNA = QStringLiteral("-");
// A map key that is there and empty: QML reads it as null (an invalid QVariant would read as undefined,
// and vanish from a JSON dump). REPORT_MAP.md: "null: no block".
const QVariant kNull = QVariant::fromValue(nullptr);

// The small blocks of one swing document — read on a worker thread (SwingStore::loadAnalysis skips the
// per-frame tracks, so this is milliseconds a swing), never the whole 28 MB document.
struct DocFacts {
    QString     dir;
    QJsonObject lm;
    QString     athleteName, athleteUuid, hand;
    qint64      wallMs = 0;
    QStringList videoAliases;
};

// THE CHARTS PAGES' READING (posture_chart_design.md §5.7; the Wrist page): the Charts panel's own session
// snapshot of the swings (a private SessionSeriesModel, run inline on this worker: the analysis' series, phases
// and the ledger rows, never the pose blob), built ONCE, and over it the Posture presenter's session payload
// (nothing picked, Auto pattern) and the Wrist presenter's (nothing picked, Auto style, the dominant style's
// wash). The same numbers and words the Charts Posture and Wrist sections draw for the session. { posture, wrist },
// each empty when nothing reads.
QVariantMap readChartsPages(const QString &sessionDir)
{
    pinpoint::charts::SessionSeriesModel series;
    series.setSynchronous(true);
    series.activateSession(sessionDir);
    const std::shared_ptr<const pinpoint::charts::SessionSnapshot> snap = series.snapshot();
    if (!snap || snap->count() == 0) return {};
    return { { QStringLiteral("posture"), pinpoint::charts::postureSessionReportPayload(*snap, -1, QStringLiteral("swings")) },   // the report says swings
             { QStringLiteral("wrist"), pinpoint::charts::wristSessionReportPayload(*snap, -1, QStringLiteral("swings")) } };
}

QVector<DocFacts> readDocs(const QString &sessionDir)
{
    QVector<DocFacts> out;
    const QDir d(sessionDir);
    const QStringList swings = d.entryList(QStringList{ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : swings) {
        const QString dir = d.filePath(name);
        if (!pinpoint::SwingStore::hasDocument(dir)) continue;
        const QJsonObject root = pinpoint::SwingStore::loadAnalysis(dir);
        DocFacts f;
        f.dir = QDir::cleanPath(dir);
        f.lm = root.value(QStringLiteral("launchMonitor")).toObject();
        const QJsonObject a = root.value(QStringLiteral("athlete")).toObject();
        f.athleteName = a.value(QStringLiteral("name")).toString();
        f.athleteUuid = a.value(QStringLiteral("uuid")).toString();
        f.hand        = a.value(QStringLiteral("handedness")).toString();
        const QString wall = root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("wallclock")).toString();
        const QDateTime t = QDateTime::fromString(wall, Qt::ISODateWithMs);
        f.wallMs = t.isValid() ? t.toMSecsSinceEpoch() : 0;
        for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
            const QJsonObject s = v.toObject();
            if (s.value(QStringLiteral("kind")).toString() == QLatin1String("video")) {
                const QString alias = s.value(QStringLiteral("alias")).toString();
                if (!alias.isEmpty() && !f.videoAliases.contains(alias)) f.videoAliases << alias;
            }
        }
        out << f;
    }
    return out;
}

double lmValue(const QJsonObject &lm, const char *key)
{
    const QJsonValue v = lm.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

// A launch-monitor figure: the true minus, "-" for none, thousands grouped where asked ("5,009").
QString lmFig(double v, int dp, bool thousands = false)
{
    if (!std::isfinite(v)) return kNA;
    const double p = std::pow(10.0, dp);
    double x = std::round(v * p) / p;
    if (std::fabs(x) < 0.5 / p) x = 0.0;
    const QString body = (thousands && dp == 0) ? W::english().toString(qlonglong(std::llround(std::fabs(x))))
                                                : QString::number(std::fabs(x), 'f', dp);
    return x < 0 ? W::minusSign() + body : body;
}
QString lmSigned(double v, int dp)
{
    const QString f = lmFig(v, dp);
    if (f == kNA) return f;
    const double p = std::pow(10.0, dp);
    return std::round(v * p) / p > 0 ? QStringLiteral("+") + f : f;
}

QString fileUrl(const QString &path) { return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString(); }

QString cameraOfAlias(const QString &alias)
{
    const QString l = alias.toLower();
    if (l.contains(QLatin1String("dtl")) || l.contains(QLatin1String("down"))) return QStringLiteral("dtl");
    if (l.contains(QLatin1String("face"))) return QStringLiteral("faceOn");
    return QString();
}

// A golfer-facing segment name for the still's note ("your trail thigh").
QString lineWords(const QString &line)
{
    static const QHash<QString, QString> w{
        { QStringLiteral("trailThigh"), QStringLiteral("your trail thigh, hip to knee") },
        { QStringLiteral("leadThigh"), QStringLiteral("your lead thigh, hip to knee") },
        { QStringLiteral("trailShin"), QStringLiteral("your trail shin, knee to ankle") },
        { QStringLiteral("leadShin"), QStringLiteral("your lead shin, knee to ankle") },
        { QStringLiteral("spine"), QStringLiteral("your spine, hips to shoulders") },
        { QStringLiteral("shoulders"), QStringLiteral("your shoulder line") },
        { QStringLiteral("hips"), QStringLiteral("your hip line") },
        { QStringLiteral("leadArm"), QStringLiteral("your lead arm") },
        { QStringLiteral("leadUpperArm"), QStringLiteral("your lead upper arm") },
        { QStringLiteral("leadForearm"), QStringLiteral("your lead forearm") },
        { QStringLiteral("trailArm"), QStringLiteral("your trail arm") },
        { QStringLiteral("head"), QStringLiteral("your head over your shoulders") },
        { QStringLiteral("stance"), QStringLiteral("your stance, ankle to ankle") },
        { QStringLiteral("shaft"), QStringLiteral("the club shaft") } };
    return w.value(line);
}

int checkCode(R::CouldBeCheck c)
{
    switch (c) {
    case R::CouldBeCheck::PhysicalTest: return 1;
    case R::CouldBeCheck::CoachCanLook: return 2;
    case R::CouldBeCheck::HiddenToday:  return 3;
    default: return 0;
    }
}

QDate dateOfFolder(const QString &sessionDir)
{
    return QDate::fromString(QFileInfo(sessionDir).fileName().left(10), Qt::ISODate);
}

QString gradePolicyName()
{
    const QString p = ppSettings().value(QStringLiteral("ui/diagnosticsGradePolicy"), QStringLiteral("standard")).toString();
    return p.isEmpty() ? QStringLiteral("standard") : p;
}

// One planned still: which slot of the map it fills.
constexpr int kPostureStillSlot = -2;        // StillSlot::workOn for the posture picture's frames
constexpr int kPostureFramesPerGroup = 16;   // StillSlot::index = group × this + frame

struct StillSlot {
    int     workOn = -1;   // index into the work-ons list; -1 = the reference swing; kPostureStillSlot = the posture picture
    int     index  = 0;    // slot within it
    QString outPath;
};

} // namespace

// ── The job: one make (or prepare) in flight ─────────────────────────────────────────────────────

struct SessionReportModel::Job {
    QString     dir;
    QVariantMap options;
    bool        print = false;
    bool        stills = true;
    bool        modelReady = false;
    bool        docsReady = false;
    bool        chartsReady = false;
    bool        built = false;
    QFutureWatcher<QVector<DocFacts>> docsWatcher;
    QVector<DocFacts> docs;
    QFutureWatcher<QVariantMap> chartsWatcher;      // the Posture and Wrist pages' payloads (readChartsPages)
    QVariantMap posture, wrist;

    QVariantMap         map;
    QVector<StillRequest> requests;
    QVector<StillSlot>  stillSlots;
    QString             pdfPath;
    QString             fingerprint;
    bool                photos = true;
    qint64              preparedMs = 0;
    int                 refShotId = -1;
    QString             detailId;
    QTimer              watchdog;
};

// ── Construction, wiring ─────────────────────────────────────────────────────────────────────────

SessionReportModel::SessionReportModel(QObject *parent) : QObject(parent) {}

SessionReportModel::~SessionReportModel()
{
    if (m_job) { m_job->docsWatcher.waitForFinished(); m_job->chartsWatcher.waitForFinished(); }
}

void SessionReportModel::setStudioProfile(StudioProfile *studio) { m_studio = studio; }
void SessionReportModel::setAthleteController(AthleteController *athletes) { m_athletes = athletes; }

bool    SessionReportModel::busy() const { return bool(m_job); }
QString SessionReportModel::busySession() const { return m_job ? m_job->dir : QString(); }
int     SessionReportModel::progressPage() const { return m_progressPage; }
int     SessionReportModel::progressPages() const { return m_progressPages; }
QObject *SessionReportModel::printModel() const { return m_printing ? m_model : nullptr; }
int     SessionReportModel::printShotId() const { return m_printing ? m_printShotId : -1; }

SessionDiagnosticsModel *SessionReportModel::model()
{
    if (!m_model) {
        m_model = new SessionDiagnosticsModel(this);
        // REVIEWING, as WorkOnsController's model is: it reads recorded sessions, never the live one, and
        // a reviewed session's footer is final, so rebuild() never runs the live debounce walk. The rows,
        // the ledgers and sessionWorkOns() read the same with it on or off.
        m_model->setReviewing(true);
        // A report READS the session and never writes it (design §8.1): diagnostics.json stays as the
        // panel and the work-ons catch-up left it, even when this model's rows are fresher.
        m_model->setPersistEnabled(false);
        // QUEUED: the model emits busyChanged before it appends the row that made it idle.
        connect(m_model, &SessionDiagnosticsModel::busyChanged, this, &SessionReportModel::onModelSettled, Qt::QueuedConnection);
    }
    return m_model;
}

QString SessionReportModel::contentStamp() const
{
    if (m_contentStamp.isEmpty()) m_contentStamp = const_cast<SessionReportModel *>(this)->model()->contentStamp();
    return m_contentStamp;
}

// ── Cheap reads: preflight, state, the fingerprint ───────────────────────────────────────────────

QVariantMap SessionReportModel::athleteFor(const QString &sessionDir, const QString &uuid, const QString &name) const
{
    if (!m_athletes) return {};
    if (!uuid.isEmpty()) {
        const QVariantMap d = m_athletes->reportDetails(uuid);
        if (!d.isEmpty()) return d;
    }
    if (!name.isEmpty()) {
        const QVariantMap d = m_athletes->reportDetails(name);
        if (!d.isEmpty()) return d;
    }
    // The library names the athlete's folder after the sanitised name.
    const QString folder = QFileInfo(QFileInfo(sessionDir).absolutePath()).fileName();
    for (const QVariant &v : m_athletes->athletes()) {
        const QVariantMap a = v.toMap();
        const QString n = a.value(QStringLiteral("name")).toString();
        if (!n.isEmpty() && pinpoint::SwingPaths::sanitise(n) == folder)
            return m_athletes->reportDetails(a.value(QStringLiteral("uuid")).toString());
    }
    return {};
}

// The photo decision for a session (§7.3), off the folder's date so that state() can answer without a
// swing document: the date in a session folder's name IS its first swing's day (SwingPaths names it so).
static R::PhotoDecision photoDecisionFor(const QVariantMap &athlete, const QDate &sessionDate)
{
    const QDate dob = QDate::fromString(athlete.value(QStringLiteral("dob")).toString().trimmed(), Qt::ISODate);
    return R::photoRule(dob, sessionDate, athlete.value(QStringLiteral("photosApproved")).toBool());
}

QString SessionReportModel::inputsStamp(const QString &sessionDir) const
{
    const R::PhotoDecision p = photoDecisionFor(athleteFor(sessionDir), dateOfFolder(sessionDir));
    return (m_studio ? m_studio->stamp() : QString()) + QStringLiteral("|photos:%1:%2").arg(p.included ? 1 : 0).arg(int(p.reason));
}

QString SessionReportModel::fingerprintOf(const QString &sessionDir) const
{
    return M::reportFingerprint(sessionDir, kWorkOnRuleVersion, contentStamp(), inputsStamp(sessionDir));
}

QVariantMap SessionReportModel::state(const QString &sessionDir) const
{
    const std::optional<M::ReportMeta> meta = M::readReportMeta(sessionDir);
    const M::ReportState st = M::reportState(sessionDir, meta, meta ? fingerprintOf(sessionDir) : QString());
    QVariantMap out;
    out[QStringLiteral("state")] = st == M::ReportState::None ? QStringLiteral("none")
                                 : st == M::ReportState::Current ? QStringLiteral("current") : QStringLiteral("stale");
    out[QStringLiteral("pdfPath")]    = meta && st != M::ReportState::None ? M::reportPdfPath(sessionDir, *meta) : QString();
    out[QStringLiteral("preparedMs")] = meta ? double(meta->preparedMs) : 0.0;
    out[QStringLiteral("sizeBytes")]  = meta ? double(meta->sizeBytes) : 0.0;
    out[QStringLiteral("words")]      = M::reportStateWords(st, meta);
    QVariantMap last;
    if (meta && !meta->sends.isEmpty()) {
        last[QStringLiteral("ms")]     = double(meta->sends.last().ms);
        last[QStringLiteral("method")] = meta->sends.last().method;
    }
    out[QStringLiteral("lastSend")]    = last;
    out[QStringLiteral("sentDiffers")] = meta ? M::sentDiffers(*meta) : false;
    return out;
}

QVariantMap SessionReportModel::preflight(const QString &sessionDir) const
{
    const QDir d(sessionDir);
    int swings = 0;
    for (const QString &name : d.entryList(QStringList{ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot))
        if (pinpoint::SwingStore::hasDocument(d.filePath(name))) ++swings;

    const QVariantMap athlete = athleteFor(sessionDir);
    const R::PhotoDecision p = photoDecisionFor(athlete, dateOfFolder(sessionDir));
    QString first = athlete.value(QStringLiteral("greeting")).toString();
    if (first.isEmpty()) first = QStringLiteral("The golfer");
    const QDate approvedOn = QDate::fromString(athlete.value(QStringLiteral("photosApprovedOn")).toString(), Qt::ISODate);

    QVariantList missing = m_studio ? m_studio->missingDetails() : QVariantList();
    if (athlete.isEmpty())
        missing.append(QVariantMap{ { QStringLiteral("text"), QStringLiteral("This golfer has no details on this machine — photos are included and the report cannot be emailed") },
                                    { QStringLiteral("where"), QStringLiteral("athlete") } });

    const std::optional<M::ReportMeta> meta = M::readReportMeta(sessionDir);
    return {
        { QStringLiteral("canReport"), swings >= kNoteBelowSwings },
        { QStringLiteral("swings"), swings },
        { QStringLiteral("photos"), QVariantMap{ { QStringLiteral("included"), p.included },
                                                 { QStringLiteral("reason"), W::photoReason(int(p.reason), first, W::shortDate(approvedOn)) } } },
        { QStringLiteral("missing"), missing },
        { QStringLiteral("lastNote"), meta ? meta->note : QString() },
    };
}

void SessionReportModel::recordSend(const QString &sessionDir, const QString &method)
{
    std::optional<M::ReportMeta> meta = M::readReportMeta(sessionDir);
    if (!meta) { ppWarn() << "[Report] send recorded for a session with no report:" << sessionDir; return; }
    meta->sends.append({ QDateTime::currentMSecsSinceEpoch(), method, meta->sha256 });
    if (!M::writeReportMeta(sessionDir, *meta)) ppWarn() << "[Report] could not write the send log in" << sessionDir;
    emit stateChanged(sessionDir);
}

void SessionReportModel::printProgress(int page, int pages)
{
    if (page == m_progressPage && pages == m_progressPages) return;
    m_progressPage = page;
    m_progressPages = pages;
    emit progressChanged();
}

// ── A make: start, wait for the model and the documents ─────────────────────────────────────────

void SessionReportModel::make(const QString &sessionDir, const QVariantMap &options) { start(sessionDir, options, true); }
void SessionReportModel::prepare(const QString &sessionDir, const QVariantMap &options) { start(sessionDir, options, false); }

void SessionReportModel::start(const QString &sessionDir, const QVariantMap &options, bool print)
{
    const QString dir = QDir::cleanPath(sessionDir);
    if (m_job) {
        const QString why = QStringLiteral("A report is already being made for another session.");
        if (print) emit made(dir, false, QString(), why);
        else       emit prepared(dir, false, {}, why);
        return;
    }
    if (dir.isEmpty() || !QDir(dir).exists()) {
        const QString why = QStringLiteral("The session folder is not there.");
        if (print) emit made(dir, false, QString(), why);
        else       emit prepared(dir, false, {}, why);
        return;
    }
    m_job = std::make_unique<Job>();
    m_job->dir = dir;
    m_job->options = options;
    m_job->print = print;
    m_job->stills = options.value(QStringLiteral("stills"), true).toBool();
    m_progressPage = m_progressPages = 0;
    emit busyChanged();
    emit progressChanged();
    ppInfo() << "[Report]" << (print ? "making" : "preparing") << "the report for" << dir;

    connect(&m_job->docsWatcher, &QFutureWatcher<QVector<DocFacts>>::finished, this, &SessionReportModel::onDocsRead);
    m_job->docsWatcher.setFuture(QtConcurrent::run([dir] { return readDocs(dir); }));
    // The Posture and Wrist pages' reading, alongside. The wrist grader's shared norm set is built here first, on
    // this thread, as SessionSeriesModel's constructor does for its own worker (session_wrist.h).
    pinpoint::charts::warmWristBands();
    connect(&m_job->chartsWatcher, &QFutureWatcher<QVariantMap>::finished, this, &SessionReportModel::onChartsRead);
    m_job->chartsWatcher.setFuture(QtConcurrent::run([dir] { return readChartsPages(dir); }));

    SessionDiagnosticsModel *m = model();
    m->setGradePolicy(gradePolicyName());
    m->setSelectedSwingDir(QString());
    m->closeDetail();
    // NOTHING OF OUR OWN IS WRITTEN. activateSession() reads diagnostics.json, reconciles it against the
    // swing folders and regrades rows that went stale — and that reconcile persists, as it does for the
    // panel and the work-ons catch-up. There is no read-only switch on SessionDiagnosticsModel; on a
    // session the catch-up has already brought current (the normal case: it runs at launch and after
    // every session) nothing is stale and nothing is written.
    m->activateSession(dir);
    if (!m->busy())
        QTimer::singleShot(0, this, [this] { if (m_job && !m_job->modelReady && m_model && !m_model->busy()) { m_job->modelReady = true; maybeBuild(); } });
}

void SessionReportModel::onModelSettled()
{
    if (!m_job || m_job->modelReady || !m_model || m_model->busy()) return;
    m_job->modelReady = true;
    maybeBuild();
}

void SessionReportModel::onDocsRead()
{
    if (!m_job) return;
    m_job->docs = m_job->docsWatcher.result();
    m_job->docsReady = true;
    // Not from inside the watcher's own signal: the build may end the job, and the watcher with it.
    QTimer::singleShot(0, this, [this] { maybeBuild(); });
}

void SessionReportModel::onChartsRead()
{
    if (!m_job) return;
    const QVariantMap pages = m_job->chartsWatcher.result();
    m_job->posture = pages.value(QStringLiteral("posture")).toMap();
    m_job->wrist = pages.value(QStringLiteral("wrist")).toMap();
    m_job->chartsReady = true;
    QTimer::singleShot(0, this, [this] { maybeBuild(); });
}

void SessionReportModel::maybeBuild()
{
    // The three halves call here as they land, and the last may find the build already begun.
    if (!m_job || !m_job->modelReady || !m_job->docsReady || !m_job->chartsReady || m_job->built) return;
    m_job->built = true;
    build();
}

void SessionReportModel::fail(const QString &error)
{
    if (!m_job) return;
    const QString dir = m_job->dir;
    const bool print = m_job->print;
    ppWarn() << "[Report] could not make the report for" << dir << ":" << error;
    finishJob();
    if (print) emit made(dir, false, QString(), error);
    else       emit prepared(dir, false, {}, error);
}

void SessionReportModel::finishJob()
{
    if (m_model) {
        m_model->closeDetail();
        m_model->setSelectedSwingDir(QString());
        m_model->activateSession(QString());      // let the session's rows go
    }
    const bool wasPrinting = m_printing;
    m_printing = false;
    m_printShotId = -1;
    if (m_job) { m_job->docsWatcher.waitForFinished(); m_job->chartsWatcher.waitForFinished(); }
    m_job.reset();
    if (wasPrinting) emit printModelChanged();
    emit busyChanged();
}

// ── The map ──────────────────────────────────────────────────────────────────────────────────────

namespace {

// Everything the builders below share, gathered once per make.
struct Ctx {
    const CharacteristicPack *pack = nullptr;
    const DrillSet *drills = nullptr;
    const GateWordSet *gateWords = nullptr;   // what we could not check, in golfer words (gate_words_pack.h)
    R::ReportPack rpack;
    const SessionDiagnosticsModel *model = nullptr;
    const std::vector<ShotRecord> *shots = nullptr;
    const std::vector<ConditionLedger> *ledgers = nullptr;
    SessionWorkOns workOns;

    struct Swing { int idx = -1; int shotId = -1; QString dir; QJsonObject lm; QString club; qint64 wallMs = 0; };
    std::vector<Swing> swings;          // in the order hit (folder order); the report's swing n = index + 1
    int N = 0;
    bool hasLm = false;
    int  lmSwings = 0;
    QString lmDevice;
    QStringList cameraAliases;
    QStringList clubs;                  // the ledger's spelling, first seen first
    bool leftHanded = false;
    QString name, uuid;           // the golfer
    QDate sessionDate, preparedDate;

    const Condition *cond(const QString &id) const { return pack ? pack->condition(id) : nullptr; }
    const Measure *measure(const QString &id) const { return pack ? pack->measure(id) : nullptr; }
    const ConditionLedger *ledger(const QString &id) const { return ledgerFor(*ledgers, id); }
    bool held(const QString &id) const { const Condition *c = cond(id); return c && !c->reportHold.isEmpty(); }

    QString label(const QString &id) const { const Condition *c = cond(id); return c && !c->label.isEmpty() ? c->label : id; }
    // What we saw, in the golfer's words (lower case as authored), else the coach's term.
    QString golfer(const QString &id) const
    {
        const Condition *c = cond(id);
        if (c && !c->golfer.trimmed().isEmpty()) return c->golfer.trimmed();
        return label(id);
    }
    QString goal(const QString &id) const
    {
        const Condition *c = cond(id);
        if (c && !c->goal.trimmed().isEmpty()) return c->goal.trimmed();
        if (c && !c->golferWell.trimmed().isEmpty()) return W::cap1(c->golferWell.trimmed());
        return label(id);
    }
    // A cause's name: a body quality by the golfer's name for it ("Lead-hip mobility"), a habit by what
    // it looks like ("Your shoulders start down before your hips").
    QString causeName(const QString &id) const
    {
        const Condition *c = cond(id);
        if (c && !c->golfer.trimmed().isEmpty()) return W::cap1(c->golfer.trimmed());
        return label(id);
    }
    // The state of a condition on report swing k.
    ShotState stateOn(const ConditionLedger &l, int k) const
    {
        const int idx = swings[size_t(k)].idx;
        return (idx >= 0 && idx < int(l.run.size())) ? l.run[size_t(idx)] : ShotState::NotAssessable;
    }
    QString markOn(const ConditionLedger &l, int k) const
    {
        switch (stateOn(l, k)) {
        case ShotState::Fired: return QStringLiteral("fired");
        case ShotState::Clean: return QStringLiteral("clean");
        default: return QStringLiteral("notAssessable");
        }
    }
    int firedOnSwings(const ConditionLedger &l) const
    {
        int n = 0;
        for (int k = 0; k < N; ++k) if (stateOn(l, k) == ShotState::Fired) ++n;
        return n;
    }
    bool sessionLevel(const QString &id) const
    {
        const ConditionLedger *l = ledger(id);
        if (l && l->sessionScope) return true;
        const WorkOnEntry *e = workOns.entry(id);
        return e && e->reasons.swingPos >= kWorkOnSwingPosSession;
    }
    double swingPos(const QString &id) const
    {
        const WorkOnEntry *e = workOns.entry(id);
        return e ? e->reasons.swingPos : kWorkOnSwingPosSession;
    }
    bool listable(const Condition *c) const
    {
        return c && (c->kind == ConditionKind::Fault || c->kind == ConditionKind::Setup);
    }
    bool retired(const Condition *c) const
    {
        return c && (c->state == ConditionState::Retired || c->state == ConditionState::Superseded);
    }
};

// One habit's run of readings against the corridor its chart draws, in report swing order.
struct Run {
    R::HabitRun h;
    QString measureId, unit, golferName, golferLow;
    int decimals = 0, sign = 1;
    QVariantMap spread;
    // A COMPOSITE habit (posture_metrics_design.md §8): its number is never shown, so the run carries
    // no values, no corridor and no name to quote one under — the page says how often, not how far.
    bool composite = false;
};

Run runOf(const Ctx &x, const QString &id)
{
    Run r;
    r.h.id = id;
    r.spread = x.model->spreadOf(id);
    const ConditionLedger *l = x.ledger(id);
    r.measureId = l ? l->drivingMeasureId : QString();
    const Measure *m = x.measure(r.measureId);
    if (m && m->kind == MeasureKind::Composite) {
        r.composite = true;
        r.h.values.assign(size_t(x.N), std::numeric_limits<double>::quiet_NaN());
        return r;
    }
    r.unit = r.spread.value(QStringLiteral("unit")).toString();
    if (r.unit.isEmpty() && m) r.unit = m->unit;
    r.decimals = r.spread.value(QStringLiteral("decimals")).toInt();
    r.sign = (m && m->golferSign < 0) ? -1 : 1;
    r.golferName = m && !m->golferName.trimmed().isEmpty() ? m->golferName.trimmed()
                 : m ? W::low1(m->label.section(QLatin1Char(','), 0, 0).trimmed()) : QString();
    r.golferLow = m ? m->golferLow.trimmed() : QString();
    r.h.corridor = x.model->corridorOf(id);
    QHash<int, double> byShot;
    for (const QVariant &v : r.spread.value(QStringLiteral("run")).toList()) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("assessable")).toBool())
            byShot.insert(p.value(QStringLiteral("shotId")).toInt(), p.value(QStringLiteral("value")).toDouble());
    }
    r.h.values.assign(size_t(x.N), std::numeric_limits<double>::quiet_NaN());
    for (int k = 0; k < x.N; ++k) {
        const auto it = byShot.constFind(x.swings[size_t(k)].shotId);
        if (it != byShot.constEnd()) r.h.values[size_t(k)] = it.value();
    }
    return r;
}

double medianOfRun(const R::HabitRun &h)
{
    std::vector<double> v;
    for (double d : h.values) if (std::isfinite(d)) v.push_back(d);
    return v.empty() ? std::numeric_limits<double>::quiet_NaN() : medianOf(v);
}

// A value in the golfer's words: a time against impact; a low-tail reading that would print negative
// under upward words, as its magnitude the other way; otherwise raw × sign.
QString valueWords(const Run &r, double raw)
{
    if (!std::isfinite(raw)) return kNA;
    const double v = raw * r.sign;
    if (W::isInstantVsImpact(r.unit, r.golferName)) return W::timeVsImpact(v);
    const R::PassBand p = R::passBandOf(r.h.corridor);
    const bool lowTail = p.known && !p.lowOpen && raw < p.lo;
    // …unless the measure's own name already says what a minus means ("head rise, minus meaning a dip").
    if (r.sign > 0 && lowTail && v < 0 && !r.golferLow.isEmpty() && !r.golferName.contains(QLatin1String("minus meaning")))
        return W::lowTailValue(W::withUnit(spreadNumber(-v, r.decimals), r.unit), r.golferLow);
    return W::golferValue(raw, r.unit, r.decimals, r.sign);
}

// The run chart, in the golfer's sign (the target band mirrored too when the words carry the sign).
QVariant figureOf(const Ctx &x, const Run &r)
{
    if (!r.spread.value(QStringLiteral("corridorKnown")).toBool()) return kNull;
    QVariantList bands, points;
    for (const QVariant &v : r.spread.value(QStringLiteral("bands")).toList()) {
        const QVariantMap b = v.toMap();
        double lo = b.value(QStringLiteral("lo")).toDouble(), hi = b.value(QStringLiteral("hi")).toDouble();
        if (r.sign < 0) { const double t = lo; lo = -hi; hi = -t; }
        bands << QVariantMap{ { QStringLiteral("grade"), b.value(QStringLiteral("grade")) }, { QStringLiteral("lo"), lo }, { QStringLiteral("hi"), hi } };
    }
    int judged = 0;
    for (int k = 0; k < x.N; ++k) {
        const double raw = r.h.values[size_t(k)];
        const ConditionLedger *l = x.ledger(r.h.id);
        const QString state = l ? x.markOn(*l, k) : QStringLiteral("notAssessable");
        QVariantMap p{ { QStringLiteral("swing"), k + 1 }, { QStringLiteral("state"), state } };
        if (std::isfinite(raw)) {
            ++judged;
            p[QStringLiteral("value")] = raw * r.sign;
            p[QStringLiteral("text")]  = W::isInstantVsImpact(r.unit, r.golferName) ? W::timeVsImpact(raw * r.sign) : W::golferValue(raw, r.unit, r.decimals, r.sign);
        } else {
            p[QStringLiteral("value")] = kNull;
            p[QStringLiteral("text")]  = kNA;
        }
        points << p;
    }
    if (judged == 0) return kNull;
    const double med = medianOfRun(r.h);
    return QVariantMap{ { QStringLiteral("unit"), r.unit }, { QStringLiteral("decimals"), r.decimals },
                        { QStringLiteral("bands"), bands }, { QStringLiteral("points"), points },
                        { QStringLiteral("medianText"), W::isInstantVsImpact(r.unit, r.golferName) ? W::timeVsImpact(med * r.sign)
                                                                              : W::golferValue(med, r.unit, r.decimals, r.sign) } };
}

// The commonest gate behind a condition's unmeasured swings, in golfer words ("" when none speaks).
QString unmeasuredReason(const Ctx &x, const QString &id)
{
    QHash<QString, int> n;
    QString best;
    for (int k = 0; k < x.N; ++k) {
        const ShotRecord &s = (*x.shots)[size_t(x.swings[size_t(k)].idx)];
        const ConditionRow *r = rowFor(s, id);
        if (!r || r->state != ShotState::NotAssessable || r->notJudgedGate.isEmpty()) continue;
        if (!x.gateWords || gateWordsFor(*x.gateWords, r->notJudgedGate).isEmpty()) continue;
        if (++n[r->notJudgedGate] > n.value(best)) best = r->notJudgedGate;
    }
    return best.isEmpty() ? QString() : gateWordsFor(*x.gateWords, best);
}

QVariantMap drillMap(const Drill &d)
{
    return { { QStringLiteral("label"), d.label }, { QStringLiteral("instruction"), d.instruction },
             { QStringLiteral("targets"), d.targets }, { QStringLiteral("note"), d.note },
             { QStringLiteral("equipment"), d.equipment.join(QStringLiteral(", ")) } };
}

QVariantList drillsOf(const Ctx &x, const QString &id)
{
    QVariantList out;
    const Condition *c = x.cond(id);
    if (!c || !x.drills) return out;
    for (const QString &did : c->drills)
        if (const Drill *d = x.drills->drill(did)) out << drillMap(*d);
    return out;
}

// ── The ball (§3: eight figures, per club when several) ──
struct LmCol { const char *key; QString head, unit; int dp; bool thousands; };

const std::vector<LmCol> &lmCols()
{
    static const std::vector<LmCol> c{
        { "carryDistance", QStringLiteral("Carry"), QStringLiteral("yd"), 0, false },
        { "totalDistance", QStringLiteral("Total"), QStringLiteral("yd"), 0, false },
        { "ballSpeed", QStringLiteral("Ball speed"), QStringLiteral("mph"), 0, false },
        { "clubheadSpeed", QStringLiteral("Club speed"), QStringLiteral("mph"), 0, false },
        { "smashFactor", QStringLiteral("Smash"), QString(), 2, false },
        { "launchAngle", QStringLiteral("Launch"), QStringLiteral("°"), 0, false },
        { "spinRate", QStringLiteral("Spin"), QStringLiteral("rpm"), 0, true },
        { "clubPath", QStringLiteral("Path"), QStringLiteral("°"), 1, false },
        { "faceAngle", QStringLiteral("Face"), QStringLiteral("°"), 1, false },
        { "faceToPath", QStringLiteral("Face to path"), QStringLiteral("°"), 1, false },
        { "attackAngle", QStringLiteral("Attack"), QStringLiteral("°"), 1, false },
        { "offline", QStringLiteral("Offline"), QStringLiteral("yd"), 0, false } };
    return c;
}

std::vector<double> lmSeries(const Ctx &x, const char *key, const QString &club)
{
    std::vector<double> v;
    for (const Ctx::Swing &s : x.swings) {
        if (!club.isEmpty() && s.club != club) continue;
        const double d = lmValue(s.lm, key);
        if (std::isfinite(d)) v.push_back(d);
    }
    return v;
}

QVariantList ballTiles(const Ctx &x, const QString &club)
{
    QVariantList tiles;
    auto range = [](const std::vector<double> &v, int dp, bool th) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        return lmFig(*lo, dp, th) + QChar(0x2013) + lmFig(*hi, dp, th);
    };
    auto tile = [&](const char *key, const QString &label, const QString &unit, int dp, bool th, bool degrees) {
        const std::vector<double> v = lmSeries(x, key, club);
        if (v.empty()) return;
        QString value = lmFig(medianOf(v), dp, th);
        if (degrees) value += QStringLiteral("°");
        tiles << QVariantMap{ { QStringLiteral("label"), label }, { QStringLiteral("value"), value },
                              { QStringLiteral("unit"), unit }, { QStringLiteral("range"), range(v, dp, th) } };
    };
    tile("carryDistance", QStringLiteral("Carry"), QStringLiteral("yd"), 0, false, false);
    tile("ballSpeed", QStringLiteral("Ball speed"), QStringLiteral("mph"), 0, false, false);
    tile("clubheadSpeed", QStringLiteral("Club speed"), QStringLiteral("mph"), 0, false, false);
    tile("smashFactor", QStringLiteral("Smash"), QString(), 2, false, false);
    tile("launchAngle", QStringLiteral("Launch"), QString(), 0, false, true);
    tile("spinRate", QStringLiteral("Spin"), QStringLiteral("rpm"), 0, true, false);
    {
        const std::vector<double> v = lmSeries(x, "clubPath", club);
        if (!v.empty()) {
            const double med = medianOf(v);
            // GC Quad's path is + to the right of the target line: in-to-out for a right-hander.
            const bool square = std::fabs(med) < 0.5;
            const bool inToOut = x.leftHanded ? med < 0 : med > 0;
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            tiles << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Club path") },
                                  { QStringLiteral("value"), lmFig(std::fabs(med), 0) + QStringLiteral("°") },
                                  { QStringLiteral("unit"), square ? QStringLiteral("square") : inToOut ? QStringLiteral("in-to-out") : QStringLiteral("out-to-in") },
                                  { QStringLiteral("range"), lmSigned(*lo, 1) + QStringLiteral(" to ") + lmSigned(*hi, 1) } };
        }
    }
    {
        const std::vector<double> v = lmSeries(x, "offline", club);
        if (!v.empty()) {
            double widest = 0.0;
            for (double d : v) widest = std::max(widest, std::fabs(d));
            tiles << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Offline") }, { QStringLiteral("value"), lmFig(widest, 0) },
                                  { QStringLiteral("unit"), QStringLiteral("yd") }, { QStringLiteral("range"), QStringLiteral("widest, either side") } };
        }
    }
    return tiles;
}

QVariant ballOf(const Ctx &x)
{
    if (!x.hasLm) return kNull;
    QVariantList groups;
    const bool several = x.clubs.size() > 1;
    if (!several) {
        groups << QVariantMap{ { QStringLiteral("club"), QString() }, { QStringLiteral("tiles"), ballTiles(x, QString()) } };
    } else {
        for (int i = 0; i < x.clubs.size() && i < kBallGroupCap; ++i)
            groups << QVariantMap{ { QStringLiteral("club"), W::cap1(W::clubWord(x.clubs[i])) }, { QStringLiteral("tiles"), ballTiles(x, x.clubs[i]) } };
    }
    return QVariantMap{ { QStringLiteral("device"), x.lmDevice },
                        { QStringLiteral("byline"), W::ballByline(x.lmDevice, x.lmSwings, x.N) },
                        { QStringLiteral("groups"), groups },
                        { QStringLiteral("more"), W::ballMore(std::max<int>(0, int(x.clubs.size()) - kBallGroupCap)) },
                        { QStringLiteral("note"), W::ballNote(x.N, several) } };
}

QVariant numbersOf(const Ctx &x, const QStringList &perSwingWorkOns, bool habits)
{
    if (!x.hasLm) return kNull;
    const bool several = x.clubs.size() > 1;
    std::vector<const LmCol *> cols;
    for (const LmCol &c : lmCols()) if (!lmSeries(x, c.key, QString()).empty()) cols.push_back(&c);

    QStringList heads{ QStringLiteral("Swing") }, units{ QString() };
    if (several) { heads << QStringLiteral("Club"); units << QString(); }
    for (const LmCol *c : cols) { heads << c->head; units << c->unit; }

    QVariantList rows;
    for (int k = 0; k < x.N; ++k) {
        const Ctx::Swing &s = x.swings[size_t(k)];
        QStringList cells{ QString::number(k + 1) };
        if (several) cells << W::clubWord(s.club);
        for (const LmCol *c : cols) cells << lmFig(lmValue(s.lm, c->key), c->dp, c->thousands);
        QVariantMap row{ { QStringLiteral("cells"), cells } };
        if (habits && !perSwingWorkOns.isEmpty()) {
            int fired = 0;
            for (const QString &id : perSwingWorkOns)
                if (const ConditionLedger *l = x.ledger(id)) if (x.stateOn(*l, k) == ShotState::Fired) ++fired;
            row[QStringLiteral("habitsText")] = W::habitsCell(fired, int(perSwingWorkOns.size()));
        }
        rows << row;
    }
    QVariantList typical;
    if (x.N >= kNoteBelowSwings) {
        const QStringList groups = several ? x.clubs : QStringList{ QString() };
        for (const QString &club : groups) {
            QStringList cells{ QStringLiteral("Typical") };
            if (several) cells << W::clubWord(club);
            for (const LmCol *c : cols) {
                const std::vector<double> v = lmSeries(x, c->key, club);
                cells << (v.empty() ? kNA : lmFig(medianOf(v), c->dp, c->thousands));
            }
            typical << QVariantMap{ { QStringLiteral("cells"), cells } };
        }
    }
    return QVariantMap{ { QStringLiteral("cols"), heads }, { QStringLiteral("units"), units }, { QStringLiteral("rows"), rows },
                        { QStringLiteral("typical"), typical },
                        { QStringLiteral("habitsHead"), habits && !perSwingWorkOns.isEmpty() ? QStringLiteral("Habits") : QString() },
                        { QStringLiteral("note"), W::numbersNote(x.leftHanded, int(perSwingWorkOns.size()), x.N,
                                                                 habits && !perSwingWorkOns.isEmpty(), several) } };
}


// ── The launch monitor appendix (§3, appendix A) ──
// From the launch monitor panel's own model (LmSessionModel over the session's shots), so the figures, the shot
// tables and the summary agree with the panel: the same effective club per shot, the same shots left out, the same
// per-club statistics (lmSessionStats: the n−1 SD, the extremes). The register draws the figures from `graphics`
// and sets the tables from the strings here.
const pinpoint::lm::FieldDef *lmDef(const QString &key)
{
    for (const pinpoint::lm::FieldDef &d : pinpoint::lm::fieldDefs()) if (key == QLatin1String(d.key)) return &d;
    return nullptr;
}
QString lmCell(double v, const QString &unit)
{
    return lmFig(v, lmDecimals(unit), unit == QStringLiteral("rpm") || unit == QStringLiteral("°/s"));
}
bool lmSameClub(const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0; }

QVariant lmAppendixOf(const Ctx &x, const QVariantMap &g, const QHash<int, int> &swingNoOfShot)
{
    if (!x.hasLm) return kNull;
    const QVariantList shots = g.value(QStringLiteral("shots")).toList();
    const QVariantList clubs = g.value(QStringLiteral("clubs")).toList();
    if (shots.isEmpty() || clubs.isEmpty()) return kNull;

    // The two tables' columns, in the order a coach reads them (an editorial list, like the panel's headline):
    // the ball as it launched, spun and flew; the club as it was delivered and where it struck.
    static const char *kBall[] = { "lm.ballSpeed", "lm.launchAngle", "lm.launchDirection", "lm.spinRate", "lm.backSpin",
                                   "lm.sideSpin", "lm.spinAxis", "lm.carryDistance", "lm.totalDistance", "lm.offline",
                                   "lm.peakHeight", "lm.descentAngle", "lm.distanceToPin" };
    static const char *kClub[] = { "lm.clubheadSpeed", "lm.smashFactor", "lm.attackAngle", "lm.clubPath", "lm.faceAngle",
                                   "lm.faceToPath", "lm.dynamicLoft", "lm.spinLoft", "lm.lieAngle", "lm.closureRate",
                                   "lm.lowPointAhead", "lm.strikeLocation", "lm.strikeHeight" };
    auto valueOf = [](const QVariantMap &shot, const QString &key) {
        const QVariant v = shot.value(QStringLiteral("values")).toMap().value(key);
        return v.isValid() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
    };
    auto keysPresent = [&](const char *const *keys, int n) {
        QStringList out;
        for (int i = 0; i < n; ++i) {
            const QString k = QString::fromLatin1(keys[i]);
            if (!lmDef(k)) continue;
            for (const QVariant &s : shots) if (std::isfinite(valueOf(s.toMap(), k))) { out << k; break; }
        }
        return out;
    };
    const QStringList ballKeys = keysPresent(kBall, int(std::size(kBall)));
    const QStringList clubKeys = keysPresent(kClub, int(std::size(kClub)));
    auto unitOf = [](const QString &key) { const pinpoint::lm::FieldDef *d = lmDef(key); return d ? QString::fromUtf8(d->unit) : QString(); };
    auto shownUnit = [&](const QString &key) { const QString u = unitOf(key); return u == QLatin1String("ratio") ? QString() : u; };
    auto labelOf = [](const QString &key) {
        const pinpoint::lm::FieldDef *d = lmDef(key);
        return d ? QString::fromUtf8(d->label).remove(QStringLiteral(" (measured)")) : key;
    };

    // the clubs, in the panel's (bag) order; then any club whose shots were all left out
    QStringList groupClubs;
    for (const QVariant &c : clubs) groupClubs << c.toMap().value(QStringLiteral("club")).toString();
    for (const QVariant &v : shots) {
        const QString c = v.toMap().value(QStringLiteral("club")).toString();
        if (std::none_of(groupClubs.cbegin(), groupClubs.cend(), [&](const QString &k) { return lmSameClub(k, c); })) groupClubs << c;
    }
    auto clubName = [](const QString &c) { return c.isEmpty() ? QStringLiteral("Club not recorded") : W::cap1(W::clubWord(c)); };
    auto swingNo = [&](const QVariantMap &s) {
        const int id = s.value(QStringLiteral("id")).toInt();
        return swingNoOfShot.value(id, s.value(QStringLiteral("number")).toInt());
    };

    bool anyOut = false, anyRelabelled = false;
    QList<int> noReading;
    for (const QVariant &v : shots) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("values")).toMap().isEmpty()) noReading << swingNo(s);
    }
    std::sort(noReading.begin(), noReading.end());
    auto table = [&](const QString &id, const QStringList &keys) -> QVariant {
        if (keys.isEmpty()) return kNull;
        QVariantList cols;
        for (const QString &k : keys) {
            const pinpoint::lm::FieldDef *d = lmDef(k);
            cols << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("head"), QString::fromUtf8(d->abbrev) },
                                 { QStringLiteral("unit"), shownUnit(k) } };
        }
        QVariantList groups;
        for (const QString &club : std::as_const(groupClubs)) {
            QVariantList rows;
            for (const QVariant &v : shots) {
                const QVariantMap s = v.toMap();
                if (!lmSameClub(s.value(QStringLiteral("club")).toString(), club)) continue;
                QStringList cells;
                bool any = false;
                for (const QString &k : keys) {
                    const double d = valueOf(s, k);
                    any = any || std::isfinite(d);
                    cells << lmCell(d, unitOf(k));
                }
                if (!any) continue;
                const bool out = s.value(QStringLiteral("excluded")).toBool();
                const bool relab = !lmSameClub(s.value(QStringLiteral("recordedClub")).toString(), club);
                anyOut = anyOut || out;
                anyRelabelled = anyRelabelled || relab;
                rows << QVariantMap{ { QStringLiteral("swing"), QString::number(swingNo(s)) + (relab ? QStringLiteral("*") : QString()) },
                                     { QStringLiteral("out"), out }, { QStringLiteral("cells"), cells } };
            }
            if (rows.isEmpty()) continue;
            groups << QVariantMap{ { QStringLiteral("club"), clubName(club) },
                                   { QStringLiteral("count"), rows.size() == 1 ? QStringLiteral("1 shot") : QStringLiteral("%1 shots").arg(rows.size()) },
                                   { QStringLiteral("rows"), rows } };
        }
        if (groups.isEmpty()) return kNull;
        return QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("cols"), cols }, { QStringLiteral("groups"), groups } };
    };
    const QVariant ball = table(QStringLiteral("ball"), ballKeys);
    const QVariant club = table(QStringLiteral("club"), clubKeys);

    // the summary: per club, every reading the club's included shots carried
    QVariantList statGroups, figures;
    for (const QVariant &cv : clubs) {
        const QVariantMap c = cv.toMap();
        const QString cl = c.value(QStringLiteral("club")).toString();
        const QVariantMap stats = c.value(QStringLiteral("stats")).toMap();
        QVariantList rows;
        auto addRows = [&](const QString &sub, const QStringList &keys) {
            QVariantList part;
            for (const QString &k : keys) {
                const QVariantMap st = stats.value(k).toMap();
                const int n = st.value(QStringLiteral("n")).toInt();
                if (n <= 0) continue;
                const QString u = unitOf(k);
                const double lo = st.value(QStringLiteral("min")).toDouble(), hi = st.value(QStringLiteral("max")).toDouble();
                part << QVariantMap{ { QStringLiteral("label"), labelOf(k) }, { QStringLiteral("unit"), shownUnit(k) },
                                     { QStringLiteral("cells"), QStringList{
                                           QString::number(n),
                                           lmCell(st.value(QStringLiteral("mean")).toDouble(), u),
                                           st.value(QStringLiteral("hasSpread")).toBool() ? lmCell(st.value(QStringLiteral("sd")).toDouble(), u) : kNA,
                                           lmCell(lo, u), lmCell(hi, u),
                                           n >= 2 ? lmCell(hi - lo, u) : kNA } } };
            }
            if (part.isEmpty()) return;
            rows << QVariantMap{ { QStringLiteral("sub"), sub } };
            rows << part;
        };
        addRows(QStringLiteral("Ball"), ballKeys);
        addRows(QStringLiteral("Club"), clubKeys);
        const int n = c.value(QStringLiteral("n")).toInt();
        if (!rows.isEmpty())
            statGroups << QVariantMap{ { QStringLiteral("club"), clubName(cl) },
                                       { QStringLiteral("count"), n == 1 ? QStringLiteral("1 shot") : QStringLiteral("%1 shots").arg(n) },
                                       { QStringLiteral("rows"), rows } };
        const QVariantMap carry = stats.value(QStringLiteral("lm.carryDistance")).toMap();
        const int cn = carry.value(QStringLiteral("n")).toInt();
        figures << QVariantMap{
            { QStringLiteral("key"), cl.toLower() },
            { QStringLiteral("name"), clubName(cl) },
            { QStringLiteral("headline"), W::lmClubHeadline(
                  cn > 0 ? lmFig(carry.value(QStringLiteral("mean")).toDouble(), 0) : QString(),
                  cn >= 2 ? lmFig(carry.value(QStringLiteral("max")).toDouble() - carry.value(QStringLiteral("min")).toDouble(), 0) : QString(), n) } };
    }
    QStringList clubWords;
    for (const QString &c : std::as_const(groupClubs)) if (!c.isEmpty()) clubWords << c;

    return QVariantMap{
        { QStringLiteral("kicker"), QStringLiteral("Appendix") },
        { QStringLiteral("title"), QStringLiteral("Launch monitor") },
        { QStringLiteral("standfirst"), W::lmAppendixStandfirst(x.lmDevice, x.lmSwings, x.N, clubWords) },
        { QStringLiteral("graphics"), g },
        { QStringLiteral("figures"), figures },
        { QStringLiteral("ball"), ball },
        { QStringLiteral("club"), club },
        { QStringLiteral("tablesNote"), W::lmTablesNote(x.leftHanded, anyOut, anyRelabelled, W::lmNoReading(noReading)) },
        { QStringLiteral("stats"), statGroups.isEmpty() ? kNull : QVariant(QVariantMap{ { QStringLiteral("groups"), statGroups } }) },
        { QStringLiteral("statsNote"), W::lmStatsNote(anyOut) } };
}

} // namespace

void SessionReportModel::build()
{
    Job &job = *m_job;
    SessionDiagnosticsModel *m = m_model;

    // ── the pack, the ledger, the fresh work-on list ──
    // The pack as it stands now (a coach's pack edit reaches the next report), loaded per make.
    const std::unique_ptr<ICharacteristicPackProvider> packProv = makeCharacteristicPackProvider();
    Ctx x;
    x.pack    = &packProv->pack();
    x.drills  = &sharedDrillSet();
    // The gate words as they stand now, for the same reason as the pack: a studio's edit on the Gates
    // page reaches the next report. The Diagnostic Model drops this cache when it saves, but so does
    // every make, because the user layer is a file in the data directory and nothing else watches it.
    // The drills are left to save(): the work-ons controller holds that set too, and a reset here is not
    // this builder's to make. The gate words have no reader but this one, on this (the UI) thread, so
    // dropping them cannot pull the set out from under anything. One small file, read once per report.
    resetSharedGateWordSet();
    x.gateWords = &sharedGateWordSet();
    x.rpack   = R::reportPackOf(*x.pack);
    x.model   = m;
    x.shots   = &m->shotRecords();
    x.ledgers = &m->ledgerRows();
    x.workOns = m->sessionWorkOns();       // FRESH — never work_ons.json (§1)

    // ── the swings, in the order hit, each with its launch-monitor block ──
    QHash<int, int> idxOfShot;
    for (int i = 0; i < int(x.shots->size()); ++i) idxOfShot.insert((*x.shots)[size_t(i)].shotId, i);
    for (const DocFacts &d : std::as_const(job.docs)) {
        const int sid = m->shotIdForSwingDir(d.dir);
        if (sid < 0 || !idxOfShot.contains(sid)) continue;
        Ctx::Swing s;
        s.idx = idxOfShot.value(sid);
        s.shotId = sid;
        s.dir = d.dir;
        s.lm = d.lm;
        s.club = (*x.shots)[size_t(s.idx)].club;
        s.wallMs = d.wallMs;
        x.swings.push_back(s);
        if (std::isfinite(lmValue(d.lm, "carryDistance")) || std::isfinite(lmValue(d.lm, "ballSpeed"))) {
            ++x.lmSwings;
            if (x.lmDevice.isEmpty()) {
                const QString k = d.lm.value(QStringLiteral("kind")).toString();
                x.lmDevice = k == QLatin1String("gcquad") ? QStringLiteral("GC Quad") : k == QLatin1String("nova") ? QStringLiteral("Nova") : k;
            }
        }
        for (const QString &a : d.videoAliases) if (!x.cameraAliases.contains(a)) x.cameraAliases << a;
        if (x.name.isEmpty()) { x.name = d.athleteName; x.uuid = d.athleteUuid; x.leftHanded = d.hand.compare(QLatin1String("left"), Qt::CaseInsensitive) == 0; }
        if (!s.club.isEmpty() && !x.clubs.contains(s.club)) x.clubs << s.club;
    }
    x.N = int(x.swings.size());
    x.hasLm = x.lmSwings > 0;
    std::sort(x.cameraAliases.begin(), x.cameraAliases.end());
    if (x.N == 0) { fail(QStringLiteral("The session has no swings the diagnostics could read.")); return; }

    qint64 firstMs = 0;
    for (const Ctx::Swing &s : x.swings) if (s.wallMs > 0) firstMs = firstMs == 0 ? s.wallMs : std::min(firstMs, s.wallMs);
    x.sessionDate  = firstMs > 0 ? QDateTime::fromMSecsSinceEpoch(firstMs).date() : dateOfFolder(job.dir);   // §8.4: local zone
    job.preparedMs = QDateTime::currentMSecsSinceEpoch();
    x.preparedDate = QDateTime::fromMSecsSinceEpoch(job.preparedMs).date();
    if (x.name.isEmpty()) x.name = QFileInfo(QFileInfo(job.dir).absolutePath()).fileName().replace(QLatin1Char('-'), QLatin1Char(' '));

    const QVariantMap athlete = athleteFor(job.dir, x.uuid, x.name);
    QString first = athlete.value(QStringLiteral("greeting")).toString();
    if (first.isEmpty()) first = x.name.section(QLatin1Char(' '), 0, 0);
    const R::PhotoDecision photo = photoDecisionFor(athlete, dateOfFolder(job.dir).isValid() ? dateOfFolder(job.dir) : x.sessionDate);
    job.photos = photo.included;
    job.fingerprint = fingerprintOf(job.dir);

    const QString kind = x.N < kNoteBelowSwings ? QStringLiteral("note")
                       : x.workOns.entries.empty() ? QStringLiteral("clean") : QStringLiteral("report");
    const bool isReport = kind == QLatin1String("report"), isNote = kind == QLatin1String("note");
    const QString clubWords = W::clubsWords(x.clubs);

    // The listed work-ons, and which of them are read swing by swing.
    std::vector<const WorkOnEntry *> wos;
    if (!isNote) for (const WorkOnEntry &e : x.workOns.entries) wos.push_back(&e);
    QStringList perSwingIds, sessionIds;
    for (const WorkOnEntry *e : wos) (x.sessionLevel(e->id) ? sessionIds : perSwingIds) << e->id;
    std::vector<Run> runs;                 // per work-on, in rank order (empty spread for session-level)
    for (const WorkOnEntry *e : wos) runs.push_back(x.sessionLevel(e->id) ? Run{} : runOf(x, e->id));

    // ── same-day siblings (the kicker's "Session 3 of 3", and Earlier today) ──
    struct Sibling { QString dir; SessionWorkOns rec; bool ok = false; bool self = false; int swings = 0; };
    std::vector<Sibling> day;
    {
        const QString prefix = QFileInfo(job.dir).fileName().left(10);
        const QDir lib(QFileInfo(job.dir).absolutePath());
        for (const QFileInfo &fi : lib.entryInfoList(QStringList{ prefix + QStringLiteral("_*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            Sibling s;
            s.dir = QDir::cleanPath(fi.absoluteFilePath());
            s.self = s.dir == job.dir;
            if (!s.self) {
                QFile f(QDir(s.dir).filePath(QStringLiteral("work_ons.json")));
                // A record from an earlier rule or schema is still that session's EVIDENCE (state, fired,
                // assessable are the ledger's, not the order's), and "Earlier today" reads only that and the
                // ids — so it is read as it stands rather than waiting for the catch-up to re-derive it.
                if (f.open(QIODevice::ReadOnly)) {
                    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
                    if (!root.isEmpty()) {
                        root[QStringLiteral("schemaVersion")] = kWorkOnSchemaVersion;
                        root[QStringLiteral("ruleVersion")]   = kWorkOnRuleVersion;
                    }
                    s.rec = sessionWorkOnsFromJson(root, &s.ok);
                }
                s.swings = s.ok ? s.rec.shotCount : 0;
                if (s.swings <= 0) {      // no record yet: count the swings so the session still numbers
                    s.swings = QDir(s.dir).entryList(QStringList{ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot).size();
                }
                if (s.swings <= 0) continue;
            } else {
                s.swings = x.N;
                s.ok = true;
            }
            day.push_back(s);
        }
    }
    int selfIndex = 0;
    for (size_t i = 0; i < day.size(); ++i) if (day[i].self) selfIndex = int(i) + 1;
    const QString ofDay = W::sessionOfDay(selfIndex, int(day.size()), x.preparedDate == x.sessionDate);

    QVariantMap map;
    map[QStringLiteral("kind")] = kind;

    // ── brand ──
    {
        const StudioProfile *st = m_studio;
        const QString studio = st && !st->studioName().trimmed().isEmpty() ? st->studioName().trimmed() : QStringLiteral("PinPoint Studio");
        const QString coach = st ? st->coachName().trimmed() : QString();
        const QString title = st ? st->coachTitle().trimmed() : QString();
        const QString email = st ? st->coachEmail().trimmed() : QString();
        const QString phone = st ? (st->coachPhone().trimmed().isEmpty() ? st->phone().trimmed() : st->coachPhone().trimmed()) : QString();
        const QString mail = email.isEmpty() && st ? st->email().trimmed() : email;
        QStringList coachLine, contact, foot;
        if (!coach.isEmpty()) coachLine << coach;
        if (!title.isEmpty()) coachLine << title;
        if (!mail.isEmpty()) contact << mail;
        if (!phone.isEmpty()) contact << phone;
        foot << (coach.isEmpty() ? studio : coach + QStringLiteral(", ") + studio);
        foot << contact;
        map[QStringLiteral("brand")] = QVariantMap{
            { QStringLiteral("studio"), studio },
            { QStringLiteral("monogram"), st ? st->monogram() : QStringLiteral("PS") },
            { QStringLiteral("logo"), st ? fileUrl(st->logoPath()) : QString() },
            { QStringLiteral("accent"), st ? st->accent() : QString() },
            { QStringLiteral("coach"), coach },
            { QStringLiteral("coachTitle"), title },
            { QStringLiteral("coachLine"), coachLine.join(QStringLiteral("  ·  ")) },
            { QStringLiteral("contact"), contact.join(QStringLiteral("  ·  ")) },
            { QStringLiteral("footLine"), foot.join(QStringLiteral("  ·  ")) },
            { QStringLiteral("web"), st ? st->website().trimmed() : QString() },
            { QStringLiteral("signoff"), W::signoff(st ? st->booking() : QString(), !email.isEmpty()) },
            { QStringLiteral("coachPhoto"), st ? fileUrl(st->coachPhotoPath()) : QString() },
            // The register's cache-buster: the logo and photo keep one file name when replaced.
            { QStringLiteral("stamp"), st ? st->stamp() : QString() } };
    }

    // ── meta (§8.4: both dates, never confused) ──
    const QString fileName = M::reportFileName(x.sessionDate, x.name);
    {
        QString subject = QStringLiteral("Your session report — ") + W::dayMonthYear(x.sessionDate);
        if (m_studio && !m_studio->emailSubject().isEmpty())
            subject = m_studio->fill(m_studio->emailSubject(), { { QStringLiteral("first"), first },
                                                                 { QStringLiteral("date"), W::dayMonthYear(x.sessionDate) },
                                                                 { QStringLiteral("coach"), m_studio->coachName() },
                                                                 { QStringLiteral("studio"), m_studio->studioName() } });
        map[QStringLiteral("meta")] = QVariantMap{
            { QStringLiteral("golfer"), x.name }, { QStringLiteral("first"), first },
            { QStringLiteral("date"), W::longDate(x.sessionDate) }, { QStringLiteral("dateShort"), W::shortDate(x.sessionDate) },
            { QStringLiteral("prepared"), W::dayMonthYear(x.preparedDate) }, { QStringLiteral("preparedShort"), W::shortDate(x.preparedDate) },
            { QStringLiteral("pdfTitle"), W::pdfTitle(x.name, x.sessionDate) }, { QStringLiteral("fileName"), fileName },
            { QStringLiteral("emailSubject"), subject },
            { QStringLiteral("club"), clubWords }, { QStringLiteral("swings"), x.N }, { QStringLiteral("sessionOfDay"), ofDay },
            { QStringLiteral("hand"), x.leftHanded ? QStringLiteral("left-handed") : QStringLiteral("right-handed") },
            { QStringLiteral("devices"), W::devicesWords(x.cameraAliases, x.hasLm ? x.lmDevice : QString()) },
            { QStringLiteral("mastheadDate"), W::mastheadDate(x.preparedDate) }, { QStringLiteral("folioDate"), W::folioDate(x.preparedDate) },
            { QStringLiteral("kicker"), W::kicker(x.sessionDate, clubWords, x.N, ofDay) },
            { QStringLiteral("runningHead"), W::runningHead(x.name, x.sessionDate, clubWords) } };
    }

    // ── headline, note, photos, held, appendix ──
    {
        QString headline;
        if (isNote) headline = W::headlineNote(x.N, x.hasLm);
        else if (!isReport) {
            int watching = 0;
            for (const ConditionLedger &l : *x.ledgers)
                if (!l.sessionScope && l.tier != Tier::Pattern && x.firedOnSwings(l) > 0 && x.listable(x.cond(l.id)) && !x.held(l.id)) ++watching;
            headline = W::headlineClean(watching);
        } else {
            double least = 1.0;
            for (const QString &id : perSwingIds)
                if (const ConditionLedger *l = x.ledger(id)) if (l->assessable > 0) least = std::min(least, double(l->fired) / l->assessable);
            headline = W::headline(int(perSwingIds.size()), int(sessionIds.size()), least, W::partWords(x.swingPos(wos.front()->id)), x.goal(wos.front()->id));
        }
        map[QStringLiteral("headline")]  = headline;
        map[QStringLiteral("coachNote")] = job.options.value(QStringLiteral("note")).toString().trimmed();
        map[QStringLiteral("photos")]    = QVariantMap{ { QStringLiteral("included"), photo.included },
                                                        { QStringLiteral("reason"), W::photoReason(int(photo.reason), first,
                                                              W::shortDate(QDate::fromString(athlete.value(QStringLiteral("photosApprovedOn")).toString(), Qt::ISODate))) } };
        QVariantList held;
        if (!isNote)
            for (const ConditionLedger &l : *x.ledgers)
                if (l.tier == Tier::Pattern && x.held(l.id))
                    held << QVariantMap{ { QStringLiteral("name"), x.label(l.id) }, { QStringLiteral("reason"), W::low1(x.cond(l.id)->reportHold.trimmed()) } };
        map[QStringLiteral("held")] = held;
        map[QStringLiteral("appendix")] = !isNote;      // Mark, 20 Oct: always — the one-page note excepted
    }

    // ── the cover's columns ──
    {
        QVariantList strengths;
        if (!isNote)
            for (const R::StrengthPick &p : R::pickStrengths(*x.ledgers, x.N, x.workOns, x.rpack)) {
                if (strengths.size() >= kStrengthsCap) break;
                const Condition *c = x.cond(p.id);
                strengths << QVariantMap{ { QStringLiteral("text"), W::cap1(c->golferWell.trimmed()) + QLatin1Char('.') },
                                          { QStringLiteral("count"), W::strengthCount(p.outcome, p.judged, x.N) } };
            }
        map[QStringLiteral("strengths")] = strengths;

        QVariantList found, brief;
        for (const WorkOnEntry *e : wos) {
            found << QVariantMap{ { QStringLiteral("name"), x.label(e->id) }, { QStringLiteral("golfer"), W::cap1(x.golfer(e->id)) },
                                  { QStringLiteral("count"), x.sessionLevel(e->id) ? QStringLiteral("over the session") : W::countOf(e->fired, e->assessable) } };
            const QVariantList dr = drillsOf(x, e->id);
            brief << QVariantMap{ { QStringLiteral("rank"), e->rank }, { QStringLiteral("goal"), x.goal(e->id) },
                                  { QStringLiteral("drill"), dr.isEmpty() ? QString() : dr.first().toMap().value(QStringLiteral("label")).toString() } };
        }
        map[QStringLiteral("found")] = found;
        map[QStringLiteral("workOnsBrief")] = brief;

        QVariantList watching;
        if (kind == QLatin1String("clean")) {
            std::vector<const ConditionLedger *> ws;
            for (const ConditionLedger &l : *x.ledgers)
                if (!l.sessionScope && x.firedOnSwings(l) > 0 && x.listable(x.cond(l.id)) && !x.held(l.id) && !x.retired(x.cond(l.id))) ws.push_back(&l);
            std::stable_sort(ws.begin(), ws.end(), [&](const ConditionLedger *a, const ConditionLedger *b) {
                const int fa = x.firedOnSwings(*a), fb = x.firedOnSwings(*b);
                return fa != fb ? fa > fb : a->id < b->id;
            });
            for (const ConditionLedger *l : ws) {
                if (watching.size() >= kWatchingCap) break;
                watching << QVariantMap{ { QStringLiteral("golfer"), W::cap1(x.golfer(l->id)) }, { QStringLiteral("count"), W::countOf(l->fired, l->assessable) } };
            }
        }
        map[QStringLiteral("watching")] = watching;
        map[QStringLiteral("ball")] = ballOf(x);
    }

    // ── the reference swing (§6) ──
    R::ReferenceSwing ref;
    {
        std::vector<R::HabitRun> hs;
        for (size_t i = 0; i < wos.size(); ++i) if (!runs[i].h.id.isEmpty()) hs.push_back(runs[i].h);
        std::vector<double> carry;
        for (const Ctx::Swing &s : x.swings) carry.push_back(lmValue(s.lm, "carryDistance"));
        if (!hs.empty()) ref = R::referenceSwing(hs, carry);
    }
    const QString stillsDir = [&] {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString h = QString::fromLatin1(QCryptographicHash::hash((job.dir + job.fingerprint).toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
        return QDir(base.isEmpty() ? QDir::tempPath() : base).filePath(QStringLiteral("report-stills/") + h);
    }();
    const bool wantStills = job.stills && photo.included && !isNote;
    const QString defaultCamera = [&] {
        for (const QString &a : x.cameraAliases) if (cameraOfAlias(a) == QLatin1String("dtl")) return QStringLiteral("dtl");
        for (const QString &a : x.cameraAliases) if (cameraOfAlias(a) == QLatin1String("faceOn")) return QStringLiteral("faceOn");
        return QString();
    }();
    auto hasCamera = [&](const QString &cam) {
        for (const QString &a : x.cameraAliases) if (cameraOfAlias(a) == cam) return true;
        return false;
    };
    auto planStill = [&](int workOn, int slot, int swing, const QString &camera, const QString &phase, const QString &line, bool addressLine) {
        StillRequest r;
        r.swingDir = x.swings[size_t(swing)].dir;
        r.camera = camera;
        r.phase = phase;
        r.line = line;
        r.addressLine = addressLine;
        r.aspect = kStillAspect;
        r.outPath = QDir(stillsDir).filePath(QStringLiteral("s%1_%2_%3%4%5.jpg").arg(x.swings[size_t(swing)].shotId, 4, 10, QLatin1Char('0'))
                                                 .arg(camera, phase, line.isEmpty() ? QString() : QLatin1Char('_') + line,
                                                      addressLine ? QStringLiteral("_a") : QString()));
        job.requests << r;
        job.stillSlots << StillSlot{ workOn, slot, r.outPath };
        return fileUrl(r.outPath);
    };

    // ── Swing by swing (page 2) ──
    QStringList overCap;                  // habits the list's cap left off
    if (!isNote) {
        QVariantList rows;
        QVariantMap more;
        bool anyStub = false;
        auto rowOf = [&](const QString &group, const ConditionLedger &l) {
            QStringList marks;
            for (int k = 0; k < x.N; ++k) { marks << x.markOn(l, k); if (marks.last() == QLatin1String("notAssessable")) anyStub = true; }
            int judged = 0;
            for (int k = 0; k < x.N; ++k) if (x.stateOn(l, k) != ShotState::NotAssessable) ++judged;
            return QVariantMap{ { QStringLiteral("group"), group }, { QStringLiteral("name"), x.label(l.id) },
                                { QStringLiteral("marks"), marks }, { QStringLiteral("count"), W::gridCount(x.firedOnSwings(l), judged) } };
        };
        int habitRows = 0;
        for (const QString &id : perSwingIds) if (const ConditionLedger *l = x.ledger(id)) { if (habitRows++ < kGridHabitCap) rows << rowOf(QStringLiteral("habit"), *l); }
        // habits left off by the cap, then the watch list's movements and the ball's, each by how often
        std::vector<const ConditionLedger *> seen, ball;
        for (const ConditionLedger &l : *x.ledgers) {
            const Condition *c = x.cond(l.id);
            if (!c || l.sessionScope || x.held(l.id) || x.retired(c) || x.workOns.entry(l.id)) continue;
            const int fired = x.firedOnSwings(l);
            if (x.listable(c)) {
                if (l.tier == Tier::Pattern) { overCap << l.id; continue; }
                if (fired >= kGridMinFired) seen.push_back(&l);
            } else if (c->kind == ConditionKind::Outcome || c->kind == ConditionKind::Delivery) {
                if (fired >= kGridMinFired) ball.push_back(&l);
            }
        }
        auto byFired = [&](const ConditionLedger *a, const ConditionLedger *b) {
            const int fa = x.firedOnSwings(*a), fb = x.firedOnSwings(*b);
            if (fa != fb) return fa > fb;
            const Condition *ca = x.cond(a->id), *cb = x.cond(b->id);
            if (ca->prominence != cb->prominence) return int(ca->prominence) > int(cb->prominence);
            return a->id < b->id;
        };
        std::stable_sort(seen.begin(), seen.end(), byFired);
        std::stable_sort(ball.begin(), ball.end(), byFired);
        for (int i = 0; i < int(seen.size()) && i < kGridSeenCap; ++i) rows << rowOf(QStringLiteral("seen"), *seen[size_t(i)]);
        if (x.hasLm || !ball.empty())
            for (int i = 0; i < int(ball.size()) && i < kGridBallCap; ++i) rows << rowOf(QStringLiteral("ball"), *ball[size_t(i)]);
        const int habitMore = std::max(0, habitRows - kGridHabitCap) + int(overCap.size());
        if (habitMore > 0) more[QStringLiteral("habit")] = W::gridMore(QStringLiteral("habit"), habitMore);
        if (int(seen.size()) > kGridSeenCap) more[QStringLiteral("seen")] = W::gridMore(QStringLiteral("seen"), int(seen.size()) - kGridSeenCap);
        if (int(ball.size()) > kGridBallCap) more[QStringLiteral("ball")] = W::gridMore(QStringLiteral("ball"), int(ball.size()) - kGridBallCap);

        QVariantList groups;
        for (const QString &key : { QStringLiteral("habit"), QStringLiteral("seen"), QStringLiteral("ball") }) {
            bool any = false;
            for (const QVariant &r : std::as_const(rows)) if (r.toMap().value(QStringLiteral("group")).toString() == key) any = true;
            if (any) groups << QVariantMap{ { QStringLiteral("key"), key }, { QStringLiteral("title"), W::gridGroupTitle(key, int(wos.size())) } };
        }
        bool everyOnEvery = !perSwingIds.isEmpty();
        for (const QString &id : perSwingIds) if (const ConditionLedger *l = x.ledger(id)) everyOnEvery = everyOnEvery && l->fired == l->assessable;
        QStringList sessionNotes;
        for (const WorkOnEntry *e : wos) if (x.sessionLevel(e->id)) sessionNotes << W::sessionLevelGridNote(x.label(e->id), e->rank);

        QVariantList swingNos, carry;
        for (int k = 0; k < x.N; ++k) swingNos << k + 1;
        if (x.hasLm) for (const Ctx::Swing &s : x.swings) { const double c = lmValue(s.lm, "carryDistance"); carry << (std::isfinite(c) ? double(std::lround(c)) : 0.0); }

        QVariantMap grid{
            { QStringLiteral("title"), W::gridTitle(everyOnEvery, int(perSwingIds.size()), !isReport) },
            { QStringLiteral("standfirst"), W::gridStandfirst(x.N, anyStub) },
            { QStringLiteral("swings"), swingNos }, { QStringLiteral("groups"), groups }, { QStringLiteral("rows"), rows },
            { QStringLiteral("more"), more }, { QStringLiteral("carry"), carry },
            { QStringLiteral("carryLabel"), x.hasLm ? QStringLiteral("Carry, yards") : QString() },
            { QStringLiteral("sessionNote"), sessionNotes.join(QLatin1Char(' ')) },
            { QStringLiteral("best"), ref.index >= 0 ? ref.index + 1 : 0 },
            { QStringLiteral("bestText"), QString() }, { QStringLiteral("bestStills"), QVariantList() }, { QStringLiteral("bestLabels"), QVariantList() } };
        if (ref.index >= 0) {
            const Ctx::Swing &s = x.swings[size_t(ref.index)];
            grid[QStringLiteral("bestText")] = W::referenceText(ref.index + 1, x.hasLm, lmFig(lmValue(s.lm, "carryDistance"), 0),
                                                               lmFig(lmValue(s.lm, "ballSpeed"), 0), ref.longest,
                                                               int(ref.inside.size()), ref.judgedOn);
            job.refShotId = s.shotId;
            if (wantStills && !defaultCamera.isEmpty()) {
                QVariantList srcs, labels;
                const QStringList phases{ QStringLiteral("P1"), QStringLiteral("P4"), QStringLiteral("P7"), QStringLiteral("P10") };
                for (int i = 0; i < phases.size(); ++i) {
                    srcs << planStill(-1, i, ref.index, defaultCamera, phases[i], QString(), false);
                    labels << W::positionCaption(phases[i]);
                }
                grid[QStringLiteral("bestStills")] = srcs;
                grid[QStringLiteral("bestLabels")] = labels;
            }
        }
        map[QStringLiteral("grid")] = grid;
    } else {
        map[QStringLiteral("grid")] = kNull;
    }

    // ── Earlier today ──
    {
        QVariant today = kNull;
        std::vector<const Sibling *> cols;
        for (const Sibling &s : day) { if (s.self) { cols.push_back(&s); break; } if (s.ok && s.swings >= kNoteBelowSwings) cols.push_back(&s); }
        if (!isNote && cols.size() > 1) {
            const int more = std::max(0, int(cols.size()) - kTodayCap);
            if (more > 0) cols.erase(cols.begin(), cols.begin() + more);
            // the rows: today's work-ons, then habits that made an earlier list and not today's
            QStringList ids;
            for (const WorkOnEntry *e : wos) ids << e->id;
            QStringList earlier;
            for (const Sibling *s : cols) if (!s->self) for (const WorkOnEntry &e : s->rec.entries)
                if (!ids.contains(e.id) && !earlier.contains(e.id) && !x.held(e.id)) earlier << e.id;
            ids << earlier.mid(0, kTodayEarlierRows);

            QVariantList heads, rows;
            int colNo = 0;
            for (const Sibling *s : cols) {
                int idx = 0;
                for (size_t i = 0; i < day.size(); ++i) if (&day[i] == s) idx = int(i) + 1;
                heads << W::todayColumn(idx, s->swings);
                ++colNo;
            }
            for (const QString &id : std::as_const(ids)) {
                const bool scope = x.sessionLevel(id);
                QVariantList marks;
                for (const Sibling *s : cols) {
                    if (s->self) {
                        const ConditionLedger *l = x.ledger(id);
                        const SessionEvidence ev = l ? sessionEvidence(*l) : SessionEvidence::NotMeasured;
                        marks << QVariantMap{ { QStringLiteral("state"), ev == SessionEvidence::NotMeasured ? QStringLiteral("none") : sessionEvidenceToString(ev) },
                                              { QStringLiteral("text"), l ? W::todayCell(ev == SessionEvidence::Pattern, l->fired, l->assessable, scope) : kNA } };
                    } else if (const WorkOnEvidence *e = s->rec.evidenceFor(id)) {
                        marks << QVariantMap{ { QStringLiteral("state"), sessionEvidenceToString(e->state) },
                                              { QStringLiteral("text"), W::todayCell(e->state == SessionEvidence::Pattern, e->fired, e->assessable, scope) } };
                    } else {
                        marks << QVariantMap{ { QStringLiteral("state"), QStringLiteral("none") }, { QStringLiteral("text"), kNA } };
                    }
                }
                rows << QVariantMap{ { QStringLiteral("name"), x.label(id) }, { QStringLiteral("marks"), marks } };
            }
            // the sentence: what was already there first thing, and what the earlier habits did here
            QStringList note;
            const Sibling *firstOfDay = nullptr;
            for (const Sibling &s : day) if (!s.self && s.ok && s.swings >= kNoteBelowSwings) { firstOfDay = &s; break; }
            if (firstOfDay && !wos.empty()) {
                int already = 0;
                for (const WorkOnEntry *e : wos)
                    if (const WorkOnEvidence *ev = firstOfDay->rec.evidenceFor(e->id)) if (ev->state == SessionEvidence::Pattern) ++already;
                note << W::todayAlready(already, int(wos.size()));
            }
            for (const QString &id : earlier.mid(0, kTodayEarlierRows)) {
                const ConditionLedger *l = x.ledger(id);
                note << W::todayEarlierHabit(x.label(id), l ? l->fired : 0, l ? l->assessable : 0, R::kJudgedMinSwings);
            }
            today = QVariantMap{ { QStringLiteral("sessions"), heads }, { QStringLiteral("rows"), rows },
                                 { QStringLiteral("note"), note.join(QLatin1Char(' ')) }, { QStringLiteral("more"), W::todayMore(more) } };
        }
        map[QStringLiteral("today")] = today;
    }

    // ── Why this order (page 3) ──
    bool anyRuledOut = false;
    if (isReport) {
        const R::OrderExplanation ox = R::explainOrder(x.workOns, *x.ledgers, x.rpack);
        const int why = ox.whyNot == R::LinksUntestable::Testable ? 0 : ox.whyNot == R::LinksUntestable::NoLinks ? 1
                      : ox.whyNot == R::LinksUntestable::CauseOnEverySwing ? 2 : 3;
        int movementsFirst = 0;
        for (const R::OrderStep &s : ox.steps) if (s.decidedBy == R::OrderRule::MovementsFirst) ++movementsFirst;
        int deliveries = 0;
        for (const WorkOnEntry *e : wos) if (e->cls == WorkOnClass::Delivery) ++deliveries;

        QVariantList rules;
        auto rule = [&](const QString &t, const QString &b, const QString &d) {
            rules << QVariantMap{ { QStringLiteral("title"), t }, { QStringLiteral("body"), b }, { QStringLiteral("today"), d } };
        };
        rule(W::ruleStillHappeningTitle(), W::ruleStillHappeningBody(), W::ruleStillHappeningToday(ox.resolving, int(wos.size())));
        rule(W::ruleCauseFirstTitle(), W::ruleCauseFirstBody(), W::ruleCauseFirstToday(why, ox.behindCause));
        if (!ox.linksTestable) rule(W::ruleSwingOrderTitle(), W::ruleSwingOrderBody(), W::ruleSwingOrderToday(ox.bySwingOrder));
        rule(W::ruleBiggestMissTitle(), W::ruleBiggestMissBody(), W::ruleBiggestMissToday(!ox.linksTestable, ox.byMagnitude));
        if (movementsFirst > 0 || deliveries > 0) rule(W::ruleMovementsFirstTitle(), W::ruleMovementsFirstBody(), W::ruleMovementsFirstToday(deliveries));

        double maxEx = 1e-9;
        for (const WorkOnEntry *e : wos) maxEx = std::max(maxEx, e->reasons.excess);
        QVariantList items;
        for (const WorkOnEntry *e : wos)
            items << QVariantMap{ { QStringLiteral("rank"), e->rank }, { QStringLiteral("name"), x.label(e->id) }, { QStringLiteral("goal"), x.goal(e->id) },
                                  { QStringLiteral("phase"), W::phaseCode(e->reasons.swingPos) },
                                  { QStringLiteral("share"), std::round(1000.0 * e->reasons.excess / maxEx) / 1000.0 },
                                  { QStringLiteral("count"), x.sessionLevel(e->id) ? QStringLiteral("over the session") : W::countOf(e->fired, e->assessable) } };

        const R::Timeline tl = R::workOnTimeline(x.workOns);
        QVariantList timeline;
        for (const R::TimelineMark &mk : tl.marks) timeline << QVariantMap{ { QStringLiteral("rank"), mk.rank }, { QStringLiteral("pos"), mk.stop } };
        QStringList sessionNames;
        for (const WorkOnEntry *e : wos) if (x.sessionLevel(e->id)) sessionNames << x.label(e->id);
        const bool firstBehindLink = !wos.empty() && !wos.front()->reasons.rootBecause.isEmpty();

        // Linked, not proven: authored edges among the listed habits, at the grade the session earned.
        QVariantList links;
        for (const Edge &ed : x.pack->edges) {
            if (ed.type != EdgeType::Causes || ed.from == ed.to) continue;
            const WorkOnEntry *from = x.workOns.entry(ed.from), *to = x.workOns.entry(ed.to);
            if (!from || !to) continue;
            const ConditionLedger *lf = x.ledger(ed.from), *lt = x.ledger(ed.to);
            if (!lf || !lt) continue;
            int grade = 3, n = 0, gone = 0;
            const RootBecause t = linkPairs(*lf, *lt);
            if (ed.sharedEvidence)                grade = 4;   // the panel's cap: measured from one reading, untested
            else if (to->causedBy.contains(ed.from)) { grade = 0; n = t.c + t.d; gone = t.d; }
            else if (lf->rangeRestricted)      grade = 2;
            else if (linkTestable(*lf, t))      grade = 1;
            links << QVariantMap{ { QStringLiteral("from"), x.label(ed.from) }, { QStringLiteral("to"), x.label(ed.to) },
                                  { QStringLiteral("words"), W::linkWords(grade, x.label(ed.from), x.label(ed.to), n, gone) } };
        }

        // Not on the list, and why.
        QVariantList notListed;
        for (const ConditionLedger &l : *x.ledgers) {
            const Condition *c = x.cond(l.id);
            if (!c || l.tier != Tier::Pattern || l.sessionScope || x.workOns.entry(l.id) || x.held(l.id) || x.retired(c)) continue;
            if (c->kind != ConditionKind::Outcome && c->kind != ConditionKind::Delivery) continue;
            QString linked; int linkedRank = 0;
            for (const Edge &ed : x.pack->edges)
                if (ed.type == EdgeType::Causes && ed.to == l.id)
                    if (const WorkOnEntry *we = x.workOns.entry(ed.from)) if (linkedRank == 0 || we->rank < linkedRank) { linked = x.label(ed.from); linkedRank = we->rank; }
            notListed << QVariantMap{ { QStringLiteral("name"), W::notListedName(x.label(l.id), l.fired, l.assessable) },
                                      { QStringLiteral("why"), W::notListedResult(linked, linkedRank) } };
        }
        for (const QString &id : std::as_const(overCap))
            if (const ConditionLedger *l = x.ledger(id))
                notListed << QVariantMap{ { QStringLiteral("name"), W::notListedName(x.label(id), l->fired, l->assessable) },
                                          { QStringLiteral("why"), W::notListedOverCap(kWorkOnMaxPerSession) } };
        {
            int shown = 0;
            for (const QVariant &rv : map.value(QStringLiteral("grid")).toMap().value(QStringLiteral("rows")).toList()) {
                const QVariantMap r = rv.toMap();
                if (r.value(QStringLiteral("group")).toString() != QLatin1String("ball")) continue;
                // the ball rows that are not habits (a habit of the ball is listed above)
                bool pattern = false;
                for (const ConditionLedger &l : *x.ledgers) if (x.label(l.id) == r.value(QStringLiteral("name")).toString() && l.tier == Tier::Pattern) pattern = true;
                if (pattern || shown >= 2) continue;
                notListed << QVariantMap{ { QStringLiteral("name"), r.value(QStringLiteral("name")).toString() + QStringLiteral(" (") + r.value(QStringLiteral("count")).toString() + QLatin1Char(')') },
                                          { QStringLiteral("why"), W::notListedBallWatch() } };
                ++shown;
            }
        }

        map[QStringLiteral("order")] = QVariantMap{
            { QStringLiteral("title"), QStringLiteral("One thing first, and why it is that one") },
            { QStringLiteral("intro"), W::orderIntro(int(rules.size())) },
            { QStringLiteral("rules"), rules }, { QStringLiteral("items"), items },
            { QStringLiteral("barsNote"), W::barsNote(ox.linksTestable ? 0 : ox.byMagnitude > 0 ? 1 : 2) },
            { QStringLiteral("timeline"), timeline },
            { QStringLiteral("timelineNote"), W::timelineNote(!tl.marks.empty(), tl.firstIsEarliest, firstBehindLink ? 0 : 1, sessionNames) },
            { QStringLiteral("links"), links }, { QStringLiteral("notListed"), notListed } };

        // Worth checking next time.
        QVariantList checks;
        for (const R::WorthCheckingGroup &g : R::worthChecking(x.workOns, *x.ledgers, x.rpack)) {
            bool everyTwo = true;
            QStringList parts;
            for (const R::WorthCheck &c : g.items) { everyTwo = everyTwo && c.ranks.size() >= 2; parts << W::checksItem(x.causeName(c.id), c.ranks); }
            if (g.more > 0) parts << W::checksMore(g.more);
            checks << QVariantMap{ { QStringLiteral("title"), W::checksTitle(checkCode(g.check), everyTwo && wos.size() > 1) },
                                   { QStringLiteral("body"), parts.join(QStringLiteral(" · ")) } };
        }
        map[QStringLiteral("checks")] = checks;
    } else {
        map[QStringLiteral("order")] = kNull;
        map[QStringLiteral("checks")] = QVariantList();
    }

    // ── The work-on pages ──
    {
        QVariantList pages;
        const int of = int(wos.size());
        for (size_t i = 0; i < wos.size() && isReport; ++i) {
            const WorkOnEntry &e = *wos[i];
            const Condition *c = x.cond(e.id);
            const ConditionLedger *l = x.ledger(e.id);
            const Run &r = runs[i];
            const bool scope = x.sessionLevel(e.id);
            const QString goal = x.goal(e.id);
            QVariantMap w{ { QStringLiteral("rank"), e.rank }, { QStringLiteral("id"), e.id }, { QStringLiteral("name"), x.label(e.id) },
                           { QStringLiteral("goal"), goal },
                           { QStringLiteral("phase"), W::phaseCode(e.reasons.swingPos) }, { QStringLiteral("phaseWords"), W::phaseWords(e.reasons.swingPos) },
                           { QStringLiteral("count"), scope ? QStringLiteral("over the session") : W::countOf(e.fired, e.assessable) },
                           { QStringLiteral("also"), c ? W::aliasesWords(c->aliases) : QString() },
                           { QStringLiteral("kicker"), W::workOnKicker(e.rank, of, e.reasons.swingPos, scope ? 0 : e.fired, scope ? 0 : e.assessable) },
                           { QStringLiteral("coachTerm"), W::coachTerm(x.label(e.id), goal, c ? c->aliases : QStringList()) } };

            // What we saw, and Next time.
            QString saw, target;
            QVariant figure = kNull;
            if (scope) {
                QStringList parts{ W::sawSessionLevel(x.golfer(e.id)) };
                const QVariantMap sc = m->sequenceConsistency();
                if (sc.value(QStringLiteral("conditionId")).toString() == e.id) {
                    if (sc.value(QStringLiteral("spreadAvailable")).toBool())
                        parts << W::timingSpreadSentence(sc.value(QStringLiteral("spreadSegment")).toString(),
                                                         sc.value(QStringLiteral("spreadValue")).toDouble(), sc.value(QStringLiteral("spreadSigma")).toDouble());
                    parts << W::orderCountSentence(sc.value(QStringLiteral("inOrder")).toInt(), sc.value(QStringLiteral("shareAvailable")).toBool()
                                                                                                 ? sc.value(QStringLiteral("resolved")).toInt() : 0);
                }
                saw = W::esc(parts.join(QLatin1Char(' ')));
                // The session-level target, when the timing spread's corridor is known.
                const Measure *ms = x.measure(l ? l->drivingMeasureId : QString());
                const SpreadCorridor cor = m->corridorOf(e.id);
                if (ms && cor.known)
                    target = W::targetLine(ms->golferName.isEmpty() ? W::low1(ms->label) : ms->golferName,
                                           W::targetWords(cor, ms->unit, ms->golferSign, W::isInstantVsImpact(ms->unit, ms->golferName)), QString(), true);
            } else {
                QString lead;
                if (e.resolving && l) {
                    int last = 0, quiet = 0;
                    for (int k = 0; k < x.N; ++k) if (x.stateOn(*l, k) == ShotState::Fired) last = k + 1;
                    for (int k = last; k < x.N; ++k) if (x.stateOn(*l, k) != ShotState::NotAssessable) ++quiet;
                    lead = W::sawResolving(x.golfer(e.id), e.fired, e.assessable, last, quiet);
                } else {
                    lead = W::sawLead(x.golfer(e.id), W::swingsOn(e.fired, e.assessable),
                                      l ? W::trendWords(l->trendKnown, int(l->trend), l->trendPoints) : QString());
                }
                const double med = medianOfRun(r.h);
                // "Typical" only under the one-corridor rule (§10 rule 2). The record's own test asks it of
                // the FIRINGS' driving readings, which a multi-signal habit mixes (Nick's early extension
                // fired on two measures) — a median of two measures describes neither. The page states the
                // median of the ONE measure it draws, so the rule is asked of that: every reading was
                // judged against the corridor the chart draws when every swing is of one context (one
                // club). Otherwise the record's own answer stands.
                bool oneContext = true;
                for (const Ctx::Swing &sw : x.swings)
                    if ((*x.shots)[size_t(sw.idx)].contextId != (*x.shots)[size_t(x.swings.front().idx)].contextId) oneContext = false;
                const bool typicalOk = std::isfinite(med) && (e.hasTypical || oneContext);
                const QString tw = W::targetWords(r.h.corridor, r.unit, r.sign, W::isInstantVsImpact(r.unit, r.golferName));
                const QString typical = typicalOk ? valueWords(r, med) : QString();
                const QString miss = R::missWords(R::missMagnitude(r.h));
                QStringList parts{ lead };
                if (typicalOk && !r.golferName.isEmpty()) parts << W::typicalClause(r.golferName, typical, tw, miss);
                parts << W::notMeasuredClause(x.N - e.assessable, x.N, unmeasuredReason(x, e.id));
                parts.removeAll(QString());
                saw = W::esc(parts.join(QLatin1Char(' ')));
                if (!r.golferName.isEmpty()) target = W::targetLine(r.golferName, tw, typical, false);
                figure = figureOf(x, r);
            }
            w[QStringLiteral("saw")] = saw;
            w[QStringLiteral("figure")] = figure;
            w[QStringLiteral("figureLabel")] = figure.typeId() == QMetaType::QVariantMap ? W::figureLabel(r.golferName, r.unit) : QString();
            w[QStringLiteral("target")] = target;

            // Pictures (§6): the first three work-ons, photos approved.
            QVariantList stills;
            if (wantStills && !scope && c) {
                const R::StillChoice ch = R::chooseStill(r.h, c->picture.kind, c->picture.phase, e.rank, true);
                QString cam = c->picture.camera;
                if (!hasCamera(cam)) cam = defaultCamera;        // §3.1: the pictures use the camera there is
                if (ch.kind != R::StillKind::None && !cam.isEmpty()) {
                    const QString line = c->picture.line;
                    // A still's caption carries the plain figure in the golfer's sign; the low-side words
                    // live in the sentences, where there is room for them.
                    auto value = [&](int k) {
                        if (k < 0 || r.composite) return QString();
                        const double raw = r.h.values[size_t(k)];
                        return W::isInstantVsImpact(r.unit, r.golferName) ? W::timeVsImpact(raw * r.sign) : W::golferValue(raw, r.unit, r.decimals, r.sign);
                    };
                    if (ch.kind == R::StillKind::Change) {
                        const QString lw = lineWords(line);
                        stills << QVariantMap{ { QStringLiteral("src"), planStill(int(i), 0, ch.first, cam, ch.firstPhase, line, false) },
                                               { QStringLiteral("caption"), QStringLiteral("Address, swing %1").arg(ch.first + 1) },
                                               { QStringLiteral("value"), QString() },
                                               { QStringLiteral("note"), QStringLiteral("Your typical swing (swing %1).").arg(ch.first + 1)
                                                                         + (lw.isEmpty() ? QString() : QStringLiteral(" The line is %1.").arg(lw)) } };
                        stills << QVariantMap{ { QStringLiteral("src"), planStill(int(i), 1, ch.second, cam, ch.secondPhase, line, ch.addressLine) },
                                               { QStringLiteral("caption"), W::positionCaption(ch.secondPhase) },
                                               { QStringLiteral("value"), value(ch.second) },
                                               { QStringLiteral("note"), ch.addressLine ? QStringLiteral("White dashes: the same swing at address.") : QString() } };
                    } else if (ch.kind == R::StillKind::Pair) {
                        stills << QVariantMap{ { QStringLiteral("src"), planStill(int(i), 0, ch.first, cam, ch.firstPhase, line, false) },
                                               { QStringLiteral("caption"), QStringLiteral("Closest, swing %1").arg(ch.first + 1) },
                                               { QStringLiteral("value"), value(ch.first) }, { QStringLiteral("note"), QString() } };
                        stills << QVariantMap{ { QStringLiteral("src"), planStill(int(i), 1, ch.second, cam, ch.secondPhase, line, false) },
                                               { QStringLiteral("caption"), QStringLiteral("Furthest, swing %1").arg(ch.second + 1) },
                                               { QStringLiteral("value"), value(ch.second) }, { QStringLiteral("note"), QString() } };
                    } else {
                        stills << QVariantMap{ { QStringLiteral("src"), planStill(int(i), 0, ch.first, cam, ch.firstPhase, line, false) },
                                               { QStringLiteral("caption"), QStringLiteral("%1, swing %2").arg(W::positionCaption(ch.firstPhase)).arg(ch.first + 1) },
                                               { QStringLiteral("value"), value(ch.first) },
                                               { QStringLiteral("note"), ch.fellBack ? QStringLiteral("Every swing looked much like this one.")
                                                                                     : QStringLiteral("Your typical swing.") } };
                    }
                }
            }
            w[QStringLiteral("stills")] = stills;
            w[QStringLiteral("why")] = c ? W::esc(c->golferWhy.trimmed()) : QString();

            // What came with it.
            {
                QStringList shown, clean;
                bool cleanEvery = true;
                for (const R::CameWithItem &it : R::cameWith(e.id, *x.ledgers, x.workOns, x.rpack)) {
                    if (it.state == R::CameWithState::Showed) shown << W::esc(W::cameWithShown(x.golfer(it.id), it.fired, it.judged));
                    else if (it.state == R::CameWithState::Clean && it.hasGolferWell && clean.size() < 3) {
                        clean << W::esc(W::low1(x.cond(it.id)->golferWell.trimmed()));
                        cleanEvery = cleanEvery && it.judged >= x.N;
                    }
                }
                w[QStringLiteral("cameWith")] = W::cameWithText(shown, clean, cleanEvery);
            }

            // What might be behind it.
            {
                const R::CauseGroups g = R::causeGroups(e.id, *x.ledgers, x.workOns, x.rpack);
                QVariantList rowsC;
                if (!g.spotted.empty()) {
                    QStringList items;
                    for (const R::CauseItem &it : g.spotted) items << W::causesSpottedItem(x.causeName(it.id), it.fired, it.judged, it.workOnRank);
                    rowsC << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("seen") }, { QStringLiteral("title"), W::causesSpottedTitle() },
                                          { QStringLiteral("items"), items }, { QStringLiteral("more"), QString() },
                                          { QStringLiteral("note"), W::causesSpottedNote(g.spottedSupported) } };
                }
                if (!g.couldBe.empty()) {
                    QStringList segs;
                    for (R::CouldBeCheck k : g.couldBeOrder) {
                        QStringList names;
                        for (const R::CauseItem &it : g.couldBe) if (it.check == k) names << W::esc(x.causeName(it.id));
                        if (!names.isEmpty()) segs << QStringLiteral("<i>%1:</i> ").arg(W::causesHow(checkCode(k))) + names.join(QStringLiteral(" · "));
                    }
                    rowsC << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("possible") }, { QStringLiteral("title"), W::causesCouldBeTitle() },
                                          { QStringLiteral("items"), QStringList{ segs.join(QStringLiteral("<br>")) } },
                                          { QStringLiteral("more"), W::causesCouldBeMore(g.couldBeMore) }, { QStringLiteral("note"), QString() } };
                }
                if (!g.ruledOut.empty()) {
                    anyRuledOut = true;
                    QStringList items;
                    for (const R::CauseItem &it : g.ruledOut) items << W::esc(x.causeName(it.id));
                    rowsC << QVariantMap{ { QStringLiteral("kind"), QStringLiteral("ruledOut") }, { QStringLiteral("title"), W::causesRuledOutTitle() },
                                          { QStringLiteral("items"), items }, { QStringLiteral("more"), W::causesRuledOutMore(g.ruledOutMore) },
                                          { QStringLiteral("note"), QString() } };
                }
                const int lead = g.lead == R::CauseLead::NoneAuthored ? 0 : g.lead == R::CauseLead::Spotted ? 1
                               : g.lead == R::CauseLead::Unchecked ? 2 : 3;
                w[QStringLiteral("causes")] = QVariantMap{
                    { QStringLiteral("lead"), W::causesLead(lead, int(g.spotted.size()), g.spottedSupported, g.spottedEverySwing,
                                                            !g.couldBe.empty(), !g.ruledOut.empty(), int(g.all.size())) },
                    { QStringLiteral("rows"), rowsC } };
            }

            w[QStringLiteral("cue")] = c ? c->cue.trimmed() : QString();
            w[QStringLiteral("drills")] = drillsOf(x, e.id);
            w[QStringLiteral("check")] = c ? c->check.trimmed() : QString();
            pages << w;
        }
        map[QStringLiteral("workOns")] = pages;
        job.detailId = wos.empty() ? QString() : wos.front()->id;
    }

    // ── Between now and next time, the numbers, About this report ──
    {
        QVariant practice = kNull;
        if (isReport && m_studio) {
            QVariantList steps = m_studio->practiceSteps();
            while (steps.size() > kPracticeCap) steps.removeLast();
            if (!steps.isEmpty()) practice = QVariantMap{ { QStringLiteral("title"), QStringLiteral("How to practise a change") }, { QStringLiteral("steps"), steps } };
        }
        map[QStringLiteral("practice")] = practice;
        map[QStringLiteral("numbers")] = numbersOf(x, perSwingIds, isReport);

        // The launch monitor appendix: the panel's model over the session's own shots (a private shot list,
        // the cheap load — the readings come off each swing's launch-monitor block, as the panel reads them).
        {
            QVariantMap lmGraphics;
            QHash<int, int> swingNoOfShot;
            if (x.hasLm) {
                ShotListModel lmShots;
                lmShots.loadSessionDir(job.dir);
                LmSessionModel lmModel;
                lmModel.setReviewing(true);
                lmModel.setLeftHanded(x.leftHanded);
                lmModel.setGradePolicy(gradePolicyName());
                lmModel.setDeviceName(x.lmDevice);
                lmModel.setShotModel(&lmShots);          // rebuilds now
                lmGraphics = lmModel.graphics();
                for (int r = 0; r < lmShots.rowCount(); ++r) {
                    const QModelIndex ix = lmShots.index(r);
                    const QString dir = QDir::cleanPath(lmShots.data(ix, ShotListModel::SwingDirRole).toString());
                    for (int k = 0; k < x.N; ++k)
                        if (x.swings[size_t(k)].dir == dir) { swingNoOfShot.insert(lmShots.data(ix, ShotListModel::ShotIdRole).toInt(), k + 1); break; }
                }
                lmModel.setShotModel(nullptr);
            }
            map[QStringLiteral("lmAppendix")] = lmAppendixOf(x, lmGraphics, swingNoOfShot);
        }

        // The posture page (posture_chart_design.md §5.7): the Charts Posture section's session reading, its
        // strip numbered as the report numbers its swings. Not in a note (the cover alone); absent when no
        // swing reads a posture row.
        if (isNote) {
            map[QStringLiteral("posture")] = kNull;
        } else {
            QHash<QString, int> swingNoOfDir;
            for (int k = 0; k < x.N; ++k) swingNoOfDir.insert(QDir::cleanPath(x.swings[size_t(k)].dir), k + 1);
            QVariant po = M::postureReportBlock(job.posture, swingNoOfDir);
            // the picture strip's stills (posture_chart_design.md §5.4): made in the stills pass below, cut at the
            // picture's own crop; with no photos in this report the strip is left out and the page says so
            QVariantMap pm = po.toMap();
            if (!pm.isEmpty() && pm.value(QStringLiteral("picture")).typeId() == QMetaType::QVariantMap) {
                if (!wantStills) {
                    pm[QStringLiteral("picture")] = kNull;
                    pm[QStringLiteral("pictureWhy")] = QStringList{ job.stills && !photo.included
                        ? QStringLiteral("The picture strip is left out: this report carries no photos.")
                        : QStringLiteral("The picture strip is left out: pictures were not made for this report.") };
                } else {
                    QVariantMap pic = pm.value(QStringLiteral("picture")).toMap();
                    QVariantList groups = pic.value(QStringLiteral("groups")).toList();
                    for (int g = 0; g < groups.size(); ++g) {
                        QVariantMap gm = groups[g].toMap();
                        QVariantList frames = gm.value(QStringLiteral("frames")).toList();
                        for (int f = 0; f < frames.size(); ++f) {
                            QVariantMap fm = frames[f].toMap();
                            const QVariantMap still = fm.value(QStringLiteral("still")).toMap();
                            if (still.isEmpty() || !fm.value(QStringLiteral("imageWhy")).toString().isEmpty()) continue;
                            const QString out = QDir(stillsDir).filePath(QStringLiteral("posture_%1_%2_%3x%4.jpg")
                                .arg(QString::fromLatin1(QCryptographicHash::hash(still.value(QStringLiteral("swingDir")).toString().toUtf8(),
                                                                                  QCryptographicHash::Sha1).toHex().left(10)),
                                     fm.value(QStringLiteral("key")).toString())
                                .arg(still.value(QStringLiteral("x")).toInt()).arg(still.value(QStringLiteral("y")).toInt()));
                            job.requests << stillRequestOf(still, out);
                            job.stillSlots << StillSlot{ kPostureStillSlot, g * kPostureFramesPerGroup + f, out };
                            fm[QStringLiteral("src")] = fileUrl(out);
                            frames[f] = fm;
                        }
                        gm[QStringLiteral("frames")] = frames;
                        groups[g] = gm;
                    }
                    pic[QStringLiteral("groups")] = groups;
                    pm[QStringLiteral("picture")] = pic;
                }
                po = pm;
            }
            map[QStringLiteral("posture")] = po;
        }

        // The wrist page: the Charts Wrist section's Session reading, its swings numbered as the report numbers them.
        // Not in a note (the cover alone); absent when no swing reads a wrist DOF (camera-only included: data decides).
        if (isNote) {
            map[QStringLiteral("wrist")] = kNull;
        } else {
            QHash<QString, int> swingNoOfDir;
            for (int k = 0; k < x.N; ++k) swingNoOfDir.insert(QDir::cleanPath(x.swings[size_t(k)].dir), k + 1);
            map[QStringLiteral("wrist")] = M::wristReportBlock(job.wrist, swingNoOfDir);
        }

        if (isNote) {
            map[QStringLiteral("about")] = kNull;
        } else {
            // What we could not check: each gate record that refused a reading, by the reading, counted
            // over the session's swings, in golfer words; a gate with no golfer words is left to the
            // technical appendix rather than printed in the analyst's.
            struct Item { QString text; int count = 0; };
            QHash<QString, Item> items;
            QStringList order;
            bool touches = false;
            QHash<QString, QString> metricName;
            for (const Measure &ms : x.pack->measures) if (!ms.metricKey.isEmpty() && !ms.golferName.isEmpty()) metricName.insert(ms.metricKey, ms.golferName);
            for (const ConditionLedger &l : *x.ledgers) {
                if (l.sessionScope || x.held(l.id)) continue;
                QHash<QString, int> byKey;
                for (int k = 0; k < x.N; ++k) {
                    const ConditionRow *row = rowFor((*x.shots)[size_t(x.swings[size_t(k)].idx)], l.id);
                    if (!row || row->state != ShotState::NotAssessable || row->notJudgedGate.isEmpty()) continue;
                    if (row->notJudgedGate.startsWith(QLatin1String("rule."))) continue;     // an engine rule, not a gate's refusal
                    const QString clause = gateWordsFor(*x.gateWords, row->notJudgedGate);
                    QString subject = subjectWordsFor(*x.gateWords, row->notJudgedSubject);
                    if (subject.isEmpty()) subject = metricName.value(row->notJudgedSubject);
                    if (clause.isEmpty() || subject.isEmpty()) continue;
                    const QString key = W::cap1(subject) + QLatin1Char('\x1f') + clause;
                    ++byKey[key];
                    if (x.workOns.entry(l.id)) touches = true;
                }
                for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it) {
                    if (!items.contains(it.key())) { order << it.key(); items.insert(it.key(), Item{ it.key(), 0 }); }
                    items[it.key()].count = std::max(items[it.key()].count, it.value());
                }
            }
            std::stable_sort(order.begin(), order.end(), [&](const QString &a, const QString &b) { return items[a].count > items[b].count; });
            QStringList lines;
            for (int i = 0; i < order.size() && i < kNotCheckedCap; ++i) {
                const QStringList kv = order[i].split(QLatin1Char('\x1f'));
                lines << W::notCheckedItem(kv.value(0), kv.value(1), items[order[i]].count, x.N);
            }
            if (order.size() > kNotCheckedCap)
                lines << W::notCheckedMore(int(order.size()) - kNotCheckedCap);

            const QVariantList glossary{
                QVariantList{ QStringLiteral("Habit"), QStringLiteral("something that showed on most swings — at least 3, and often enough that it is not chance") },
                QVariantList{ QStringLiteral("Target range"), QStringLiteral("where a good ball-striker's swing usually is at that point") },
                QVariantList{ QStringLiteral("Work-on"), QStringLiteral("a habit you can practise your way out of, in the order to work on it") },
                QVariantList{ QStringLiteral("P1 … P10"), QStringLiteral("the ten positions of the swing, from address (P1) through the top (P4) and impact (P7) to the finish (P10)") } };
            map[QStringLiteral("about")] = QVariantMap{
                { QStringLiteral("dates"), W::aboutDates(x.sessionDate, x.preparedDate) },
                { QStringLiteral("measured"), W::aboutMeasured(x.N, clubWords, x.cameraAliases, x.lmDevice, x.lmSwings) },
                { QStringLiteral("ranges"), W::aboutRanges(anyRuledOut) },
                { QStringLiteral("notChecked"), W::aboutNotChecked(lines, touches) },
                { QStringLiteral("words"), glossary },
                { QStringLiteral("footnote"), W::aboutFootnote(photo.included) } };
        }
    }

    job.map = map;
    job.pdfPath = QDir(M::reportDir(job.dir)).filePath(fileName);

    if (job.requests.isEmpty()) { deliver(); return; }
    QDir().mkpath(stillsDir);
    if (!m_stills) {
        m_stills = new ReportStills(this);
        connect(m_stills, &ReportStills::finished, this, [this](const QVector<StillResult> &results) {
            if (!m_job) return;
            // Each slot's picture: the file the still was written to, or an empty frame with the honest
            // note when it could not be made (PpStill draws a missing file as an empty frame).
            QHash<QString, StillResult> byPath;
            for (const StillResult &r : results) byPath.insert(r.path.isEmpty() ? QString() : QDir::cleanPath(r.path), r);
            QVariantMap &mp = m_job->map;
            QVariantList wos = mp.value(QStringLiteral("workOns")).toList();
            QVariantMap grid = mp.value(QStringLiteral("grid")).toMap();
            QVariantList best = grid.value(QStringLiteral("bestStills")).toList();
            int made = 0;
            for (int i = 0; i < m_job->stillSlots.size(); ++i) {
                const StillSlot &s = m_job->stillSlots[i];
                const StillResult r = i < results.size() ? results[i] : byPath.value(QDir::cleanPath(s.outPath));
                const bool ok = r.ok && QFileInfo::exists(s.outPath);
                if (ok) ++made;
                else ppWarn() << "[Report] still not made:" << s.outPath << r.error;
                if (s.workOn == kPostureStillSlot) {
                    // the posture picture's frame: no picture → an empty frame and the honest note
                    if (ok) continue;
                    QVariantMap pm = mp.value(QStringLiteral("posture")).toMap();
                    QVariantMap pic = pm.value(QStringLiteral("picture")).toMap();
                    QVariantList groups = pic.value(QStringLiteral("groups")).toList();
                    const int g = s.index / kPostureFramesPerGroup, f = s.index % kPostureFramesPerGroup;
                    if (g < groups.size()) {
                        QVariantMap gm = groups[g].toMap();
                        QVariantList frames = gm.value(QStringLiteral("frames")).toList();
                        if (f < frames.size()) {
                            QVariantMap fm = frames[f].toMap();
                            fm[QStringLiteral("src")] = QString();
                            fm[QStringLiteral("imageWhy")] = QStringLiteral("The frame could not be read%1.")
                                .arg(r.error.isEmpty() ? QString() : QStringLiteral(": ") + r.error);
                            frames[f] = fm;
                        }
                        gm[QStringLiteral("frames")] = frames;
                        groups[g] = gm;
                    }
                    pic[QStringLiteral("groups")] = groups;
                    pm[QStringLiteral("picture")] = pic;
                    mp[QStringLiteral("posture")] = pm;
                    continue;
                }
                if (s.workOn < 0) {
                    if (s.index < best.size() && !ok) best[s.index] = QString();
                    continue;
                }
                if (s.workOn >= wos.size()) continue;
                QVariantMap w = wos[s.workOn].toMap();
                QVariantList st = w.value(QStringLiteral("stills")).toList();
                if (s.index < st.size()) {
                    QVariantMap one = st[s.index].toMap();
                    if (!ok) one[QStringLiteral("src")] = QString();
                    if (!r.note.isEmpty()) {
                        QString add = W::cap1(r.note.trimmed());
                        if (!add.endsWith(QLatin1Char('.')) && !add.endsWith(QLatin1Char('!')) && !add.endsWith(QLatin1Char('?'))) add += QLatin1Char('.');
                        const QString n = one.value(QStringLiteral("note")).toString();
                        one[QStringLiteral("note")] = n.isEmpty() ? add : n + QLatin1Char(' ') + add;
                    }
                    st[s.index] = one;
                }
                w[QStringLiteral("stills")] = st;
                wos[s.workOn] = w;
            }
            // A reference swing with no picture at all is left out (empty means absent).
            bool anyBest = false;
            for (const QVariant &b : std::as_const(best)) anyBest = anyBest || !b.toString().isEmpty();
            grid[QStringLiteral("bestStills")] = anyBest ? best : QVariantList();
            if (!anyBest) grid[QStringLiteral("bestLabels")] = QVariantList();
            mp[QStringLiteral("grid")] = grid;
            mp[QStringLiteral("workOns")] = wos;
            ppInfo() << "[Report]" << made << "of" << m_job->stillSlots.size() << "stills made";
            deliver();
        });
    }
    m_stills->renderAll(job.requests);
}

// The map is whole: hand it over (prepare) or to the print host (make).
void SessionReportModel::deliver()
{
    Job &job = *m_job;
    if (!job.print) {
        const QString dir = job.dir;
        const QVariantMap map = job.map;
        finishJob();
        emit prepared(dir, true, map, QString());
        return;
    }
    if (!isSignalConnected(QMetaMethod::fromSignal(&SessionReportModel::printRequested))) {
        fail(QStringLiteral("There is no print host to lay the report out."));
        return;
    }
    if (!QDir().mkpath(M::reportDir(job.dir))) {
        fail(QStringLiteral("The report folder could not be made in the session."));
        return;
    }
    // The appendix reads the reference swing and opens on work-on 1 (§3: "for your coach: the full
    // diagnostics"). Selecting a shot moves the panel's focus — which is why it is set only now, after
    // every figure above was read with no selection, exactly as the work-on record is derived.
    if (m_model) {
        if (job.refShotId >= 0) m_model->setSelectedShotId(job.refShotId);
        if (!job.detailId.isEmpty()) m_model->openDetail(job.detailId);
    }
    m_printShotId = job.refShotId;
    m_printing = true;
    emit printModelChanged();
    job.watchdog.setSingleShot(true);
    connect(&job.watchdog, &QTimer::timeout, this, [this] {
        QTimer::singleShot(0, this, [this] { if (m_printing) fail(QStringLiteral("The print did not finish.")); });
    });
    job.watchdog.start(kPrintTimeoutMs);
    emit printRequested(job.map, job.pdfPath);
}

void SessionReportModel::printFinished(bool ok, const QString &path, int pages, const QString &error)
{
    if (!m_job || !m_job->print || !m_printing) {
        ppWarn() << "[Report] a print finished with no report being made:" << path;
        return;
    }
    Job &job = *m_job;
    job.watchdog.stop();
    const QString dir = job.dir;
    if (!ok) {
        const QString why = error.isEmpty() ? QStringLiteral("The PDF could not be written.") : error;
        ppWarn() << "[Report] print failed for" << dir << ":" << why;
        finishJob();
        emit made(dir, false, path, why);
        return;
    }

    // THE RECORD (§8.1): the note and options, photos, every prepared time, the PDF's hash and size, and
    // what it was made from. No address: the session folder lives on the share.
    M::ReportMeta meta = M::readReportMeta(dir).value_or(M::ReportMeta{});
    const QString oldPdf = M::reportPdfPath(dir, meta);
    meta.note = job.options.value(QStringLiteral("note")).toString().trimmed();
    QVariantMap opts = job.options;
    opts.remove(QStringLiteral("note"));
    opts.insert(QStringLiteral("appendix"), job.map.value(QStringLiteral("appendix")).toBool());
    meta.options = opts;
    meta.photosIncluded = job.photos;
    meta.preparedMs = job.preparedMs;
    meta.preparedHistory.append(job.preparedMs);
    meta.pdfName = QFileInfo(path).fileName();
    meta.sha256 = M::fileSha256(path);
    meta.sizeBytes = QFileInfo(path).size();
    meta.fingerprint = job.fingerprint;
    if (!M::writeReportMeta(dir, meta)) ppWarn() << "[Report] could not write report_meta.json in" << dir;
    // A golfer renamed since the last make: the old file would sit beside the new one forever.
    if (!oldPdf.isEmpty() && QDir::cleanPath(oldPdf) != QDir::cleanPath(path) && QFileInfo::exists(oldPdf)) QFile::remove(oldPdf);
    ppInfo() << "[Report] wrote" << path << "(" << pages << "pages," << meta.sizeBytes << "bytes)";
    finishJob();
    emit made(dir, true, path, QString());
    emit stateChanged(dir);
}
