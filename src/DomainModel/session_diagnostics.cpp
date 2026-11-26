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

#include "session_diagnostics.h"
#include "athlete_diagnostics.h"   // the club baseline the freeze takes (step 3.6)
#include "session.h"
#include "shot.h"
#include "summary_rules.h"   // the rule data as the firing reads it; a summary's rules (3.7-e round 2)

#include "../Diagnostics/derived/board_annotations.h"     // the board's annotations, read by the reduction (step 3.6)
#include "../Diagnostics/derived/club_distances.h"        // clubBaselineFromJson
#include "../Diagnostics/derived/strike_from_flight.h"    // sessionFlightUsuals, checkShotAgainstFlight
#include "../Diagnostics/derived/session_day_readings_rules.h"   // the summary's day readings' rules (refactor 5.5d)
#include "../Vocabulary/Club/club_vocabulary.h"
#include "../Core/pp_debug.h"
#include "../Core/post_shot_trace.h"   // [PostShot] (refactor 3.7-S)

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

#include <algorithm>
#include <cmath>

namespace pinpoint {

using analysis::ClubFlightUsual;
using analysis::ShotCorrection;
using analysis::ShotRecord;

namespace {

// ── Field by field, NaN equal to NaN (a club with too few shots has no usual in that reading) ──────────────────
bool sameReading(double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; }
bool sameUsual(const ClubFlightUsual &a, const ClubFlightUsual &b)
{
    return sameReading(a.launch, b.launch) && sameReading(a.smash, b.smash) && sameReading(a.attack, b.attack)
        && sameReading(a.carry, b.carry);
}
bool sameUsuals(const QHash<QString, ClubFlightUsual> &a, const QHash<QString, ClubFlightUsual> &b)
{
    if (a.size() != b.size()) return false;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
        const auto o = b.constFind(it.key());
        if (o == b.constEnd() || !sameUsual(*it, *o)) return false;
    }
    return true;
}

// THE FINDINGS STAMP (R-STAMP-1): firingStamp over the inputs fireFindings reads — the pack, the norms, the grade
// policy, SET-06 (recorded), the club baseline, the cohort, σ — and the rule versions. The usuals and the flight table
// are layer 3's alone (fire_findings.h: "the flight table and the usuals are layer 3's and play no part in the
// findings"), so they are neutral here: a usual's change never makes a shot's findings stale (DEC-191).
// The cohort is each shot's own (the record on the shot's date, DEC-171), so the stamp hashes what decides it: the
// record's date of birth and sex. A shot's date never changes, so the two give its cohort exactly.
QString findingsStampOf(analysis::FiringInputs inputs, const QDate &dob, const QString &sex)
{
    inputs.usuals.clear();
    inputs.flightNorms = analysis::FlightNorms{};
    inputs.cohort      = analysis::Cohort{};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData((dob.toString(Qt::ISODate) + QLatin1Char('|') + sex).toUtf8());
    return analysis::firingStamp(inputs) + QLatin1Char('-') + QString::fromLatin1(h.result().toHex().left(8));
}

// The golfer's word on one shot, by the one rule (golfer_input_rules.h; J-33): the older copy's entry, the review over
// it field by field, the intent run for an intent the shot does not have.
struct ShotWord {
    QString                     intent;
    std::vector<ShotCorrection> corrections;
};
ShotWord wordOf(const FiringSnapshot &snap, int swing, const ShotReview &review)
{
    QHash<int, QString>                     intents;
    QHash<int, std::vector<ShotCorrection>> corrections;
    if (const auto it = snap.legacyIntents.constFind(swing); it != snap.legacyIntents.constEnd())
        intents.insert(swing, *it);
    if (const auto it = snap.legacyCorrections.constFind(swing); it != snap.legacyCorrections.constEnd())
        corrections.insert(swing, *it);
    golfer_input::applyReview(review, swing, intents, corrections);
    return { golfer_input::shotIntentFor(intents, snap.intentRun, swing), corrections.value(swing) };
}

// ── The jobs: values in, values out (§4.4 step 5). They run on a worker; they read nothing but their arguments ──

struct FireOutcome {
    bool                         ok = false;
    QString                      error;
    ShotRecord                   findings;
    bool                         hasLaunchMonitor = false;
    DocumentStamp                document;
    QString                      findingsStamp;
    ShotDerived                  derived;
    ShotReview                   review;
    analysis::Cohort             cohort;
    std::optional<analysis::ShotDeliveryReading> delivery;   // off the grid it read (refactor 5.5d)
    std::optional<analysis::ShotGateRecord>      gates;      // off the same grid (platform refactor P-3b)
};

struct DeriveOutcome {
    ShotDerived               derived;
    std::optional<ShotReview> review;   // read by this job (a result loaded from the file): kept with it
};

// Layer 3 on the findings as fired: the rows checked against the session's usual flight, then the outcome and the
// three factors, with an EMPTY session diagnosis — the "because" is the session's (J-14 (b); the header).
ShotDerived deriveOn(const FiringSnapshot &snap, const ShotRecord &findings, const ShotWord &word)
{
    const std::vector<ShotCorrection> &corrections = word.corrections;
    const QString                     &intent      = word.intent;
    ShotDerived d;
    d.checked = findings;
    analysis::checkShotAgainstFlight(d.checked, snap.inputs.usuals, snap.inputs.flightNorms);
    static const analysis::ShotOutcomePack kNoPack;
    d.data = analysis::deriveShotData(d.checked, corrections, intent, snap.outcomePack ? *snap.outcomePack : kNoPack,
                                      analysis::ShotOutcomeSessionFacts{}, snap.inputs);
    d.usual       = snap.inputs.usuals.value(canonicalClubId(findings.club));
    d.flightKey   = analysis::flightNormsStampFor(snap.inputs.flightNorms, findings.club);
    d.generation  = snap.derivedGeneration;
    d.intent      = intent;
    d.corrections = corrections;
    return d;
}

FireOutcome fireOn(const FiringSnapshot &snap, const ShotRef &shot, const FiringReader &reader)
{
    FireOutcome out;
    if (!snap.inputs.pack || !snap.inputs.norms) {
        out.error = QStringLiteral("no pack or norms to fire against");
        return out;
    }
    FiringRead read = reader.read(shot, snap.inputs.pack->pack());
    out.document = read.stamp;
    if (!read.ok) {   // UNREAD: nothing computed from it (§4.4 step 5)
        out.error = read.error.isEmpty() ? QStringLiteral("the shot could not be read") : read.error;
        return out;
    }
    // The norm cohort on THIS shot's date (DEC-171), from the record facts the snapshot carries.
    analysis::FiringInputs inputs = snap.inputs;
    // Dated by the capture instant the document records, else the session's day — never the file's mtime (review 2).
    inputs.cohort = cohortOnShotDate(snap.dob, snap.sex, read.captureMs, snap.sessionDay);
    // THE SUMMARY'S DELIVERY READINGS off the grid this fire read (refactor 5.5d; DEC-74): taken before the readings move.
    out.delivery = analysis::shotDeliveryReadingOf(analysis::deliveryGridNumbersOf(read.readings.grid), shot.swingNumber(),
                                                   read.readings.grid.ordinal);
    // THE SUMMARY'S GATE RECORD off the same grid (platform refactor P-3b): the swing's gatesVersion and the gates it names.
    out.gates = analysis::shotGateRecordOf(read.readings.grid.gatesVersion, read.readings.grid.gates, shot.swingNumber());
    analysis::FiredShot fired = analysis::fireFindings(std::move(read.readings), inputs);
    if (!fired.ok) {
        out.error = QStringLiteral("no pack or norms to fire against");
        return out;
    }
    fired.record.shotId      = shot.swingNumber();
    fired.record.timestampMs = read.timestampMs;
    out.ok               = true;
    out.findings         = std::move(fired.record);
    out.hasLaunchMonitor = fired.hasLaunchMonitor;
    out.findingsStamp    = snap.findingsStamp;
    out.cohort           = inputs.cohort;
    out.review           = read.review;
    out.derived = deriveOn(snap, out.findings, wordOf(snap, shot.swingNumber(), out.review));
    return out;
}

// ── §4.4 step 7: the reduction's job (values in, values out) ────────────────────────────────────────────────────
struct DayOutcome {
    analysis::SessionDayReadings readings;
    analysis::SessionGates       gates;                 // the summary's gate counts (platform refactor P-3b)
    QHash<int, DayReadingsAsk::Shot>     read;          // swing → the delivery reading and gate record read on the lane
    analysis::SessionCameras     cameras;
    QString                      camerasKey;
};
// The cameras' key: the first kept shot's swing and document (an archive or a re-analysis moves it).
QString camerasKeyOf(const DayReadingsAsk::Shot &s)
{
    return QStringLiteral("%1:%2:%3").arg(s.swing).arg(s.document.size).arg(s.document.mtimeMs);
}
DayOutcome dayReadingsOn(const DayReadingsAsk &ask)
{
    DayOutcome out;
    out.readings.has = true;
    out.readings.address = ask.address;
    std::vector<analysis::ShotGateRecord> gates;   // the swings whose grid read (P-3b)
    for (const DayReadingsAsk::Shot &s : ask.shots) {
        std::optional<analysis::ShotDeliveryReading> d = s.delivery;
        std::optional<analysis::ShotGateRecord>      g = s.gates;
        if (!s.read) {
            // ONE grid read for both: the delivery reading and the gate record (platform refactor P-3b).
            const std::optional<analysis::SwingPhaseGrid> grid = ask.reader && ask.pack ? ask.reader->phaseGrid(s.ref, ask.pack->pack())
                                                                                      : std::nullopt;
            d = grid ? analysis::shotDeliveryReadingOf(analysis::deliveryGridNumbersOf(*grid), s.swing, grid->ordinal) : std::nullopt;
            g = grid ? std::optional<analysis::ShotGateRecord>(analysis::shotGateRecordOf(grid->gatesVersion, grid->gates, s.swing))
                     : std::nullopt;
            DayReadingsAsk::Shot r = s;
            r.read = true;
            r.delivery = d;
            r.gates = g;
            out.read.insert(s.swing, r);
        }
        if (d) out.readings.delivery.push_back(*d);
        if (g) gates.push_back(*g);
    }
    out.gates = analysis::sessionGatesOf(gates);
    if (ask.camerasKnown) {
        out.cameras = ask.cameras;
        out.camerasKey = ask.camerasKey;
    } else if (!ask.shots.empty()) {
        // The first shot whose document reads (the old reader's first swing with a document): its streams.
        out.camerasKey = camerasKeyOf(ask.shots.front());
        for (const DayReadingsAsk::Shot &s : ask.shots) {
            const analysis::SessionCameras c = ask.reader ? ask.reader->cameras(s.ref) : analysis::SessionCameras{};
            if (c.known) { out.cameras = c; break; }
        }
    }
    out.readings.cameras = out.cameras;
    return out;
}

struct ReductionOutcome {
    analysis::SessionDiagnosis diagnosis;
    qint64                     ms = 0;
    DayOutcome                 day;   // refactor 5.5d
};

// One shot's derived data as FILE-SESS-03 keeps it (DEC-135): ids and numbers only (R-DER-9) — the outcome's facts,
// the three factors, the golfer's word and the club's usual it was derived with, and its stamp.
QJsonObject derivedToJson(const ShotDerived &d, const QString &stamp)
{
    return QJsonObject{ { QStringLiteral("stamp"), stamp },
                        { QStringLiteral("intent"), d.intent },
                        { QStringLiteral("corrections"),
                          analysis::shotCorrectionsToJson({ { 0, d.corrections } }).value(QStringLiteral("0")) },
                        { QStringLiteral("usual"), analysis::clubFlightUsualToJson(d.usual) },
                        { QStringLiteral("outcome"), analysis::shotOutcomeFactsToJson(d.data.outcome) },
                        { QStringLiteral("factors"), analysis::shotFactorsToJson(d.data.factors) } };
}

} // namespace

QDate shotCohortDate(qint64 timestampMs, const QDate &sessionDay)
{
    return timestampMs > 0 ? QDateTime::fromMSecsSinceEpoch(timestampMs).date() : sessionDay;
}

analysis::Cohort cohortOnShotDate(const QDate &dob, const QString &sex, qint64 timestampMs, const QDate &sessionDay)
{
    return analysis::cohortFor(dob, sex, shotCohortDate(timestampMs, sessionDay));
}

// ── Made, loaded, let go ───────────────────────────────────────────────────────────────────────────────────

SessionDiagnostics *SessionDiagnostics::attach(Session *session, const DiagnosticsDoors &doors)
{
    if (!session)
        return nullptr;
    if (session->m_diagnostics)
        return session->m_diagnostics;
    if (!doors.receiver || !doors.ruleData || !doors.reader || (!doors.pool && !doors.synchronous) || !session->m_sessions) {
        ppWarn() << "[SessionDiagnostics] not attached to" << session->id().toString()
                 << "- the composition root gave no receiver, rule data, reader or pool";
        return nullptr;
    }
    auto *d = new SessionDiagnostics(session, doors);
    session->m_diagnostics = d;
    d->load();
    return d;
}

SessionDiagnostics::SessionDiagnostics(Session *session, const DiagnosticsDoors &doors)
    : QObject(session), m_session(session), m_sessions(session->m_sessions), m_id(session->id()), m_doors(doors)
{
    // THE INSTANCE'S NUMBER, from the one counter, when the instance is loaded (§4.4 step 3): a released and reloaded
    // session's results can never match this instance's asks (REVIEW-2's race; T1).
    m_instance = nextWorkNumber();
    m_writeTimer.setSingleShot(true);
    connect(&m_writeTimer, &QTimer::timeout, this, [this] { flush(); });
}

SessionDiagnostics::~SessionDiagnostics()
{
    // Destroyed with its Athlete (the app's quit): what it kept is written first (§4.4 step 8). Its Session's own
    // members are gone by now (it is being destroyed as the Session's child): nothing here reads the Session.
    if (!m_retired)
        retire(/*writeFirst=*/true);
}

void SessionDiagnostics::retire(bool writeFirst)
{
    if (m_retired)
        return;
    if (writeFirst)
        flush(/*final=*/true);
    m_retired = true;
    m_writeTimer.stop();
    // Its waiting jobs go unrun; one running finishes and is dropped at the receiver (another instance, or none).
    if (m_doors.pool)
        m_doors.pool->cancelLane(m_instance);
}

void SessionDiagnostics::connectSession()
{
    Session *s = m_session;
    connect(s, &Session::shotAdded, this, [this](const ShotRef &ref) { fire(ref); }, Qt::DirectConnection);     // EVT-01
    connect(s, &Session::shotTrashed, this, [this](const ShotRef &ref) {                                         // EVT-03
        drop(ref.swingNumber());
        reconcile();
        settleIfDone();
    }, Qt::DirectConnection);
    connect(s, &Session::intentRunChanged, this, [this](const SessionId &) { onIntentRunChanged(); },          // EVT-08
            Qt::DirectConnection);
    connect(s, &Session::sessionIntentChanged, this, [this](const SessionId &) { onSessionIntentChanged(); },  // EVT-10
            Qt::DirectConnection);
    connect(s, &Session::sessionEnded, this, [this](const SessionId &) {   // EVT-09: the diagnosis is closed (J-24)
        m_reductionOwed = true;
        settleIfDone();
    }, Qt::DirectConnection);
    connect(s, &Session::shotHeld, this, [this](const ShotRef &ref) {
        if (Shot *shot = m_session->shot(ref)) hearShot(shot);
    }, Qt::DirectConnection);
    connect(s, &Session::releasing, this, [this] { retire(/*writeFirst=*/true); }, Qt::DirectConnection);
    // THE ARCHIVE WINDOW (refactor 3.7-a, review B2): between archiveJob and archiveLanded nothing is asked or written —
    // one writer of the session's files at any moment, and no read of a swing the archive is thinning.
    connect(s, &Session::archiveOut, this, [this] { onArchiveOut(); }, Qt::DirectConnection);
    connect(s, &Session::archiveBack, this, [this](bool ok) { onArchiveBack(ok); }, Qt::DirectConnection);
    if (Athlete *a = s->athlete()) {
        connect(a, &Athlete::sessionTrashed, this, [this](const SessionId &id) {   // EVT-20: nothing more is written
            if (id == m_id) retire(/*writeFirst=*/false);
        }, Qt::DirectConnection);
        connect(a, &Athlete::recordChanged, this, [this] { onAthleteChanged(); }, Qt::DirectConnection);
    }
    // EVT-12 / EVT-13: a new RuleData snapshot → every shot (§4.4 step 2).
    connect(m_doors.ruleData, &RuleData::normsChanged, this, [this](const QString &) { onRulesChanged(); });
    connect(m_doors.ruleData, &RuleData::packChanged, this, [this](const QString &) { onRulesChanged(); });
}

void SessionDiagnostics::onArchiveOut()
{
    m_archiveOut = true;
    m_writeTimer.stop();
    // The jobs still waiting in the lane (a load's, asked in the same turn as the archive job: housekeeping holds a session
    // to archive it) are dropped unrun, and owed. What is already on a worker, or done and not yet delivered, still comes
    // back: those are the OLDEST asks (a lane runs its jobs in order), so their in-flight entries are kept, and a derive
    // among them is still read as a derive when it lands (review R2-M3); the newer entries go with their jobs.
    if (!m_doors.pool || (m_outstanding == 0 && !m_reductionInFlight))
        return;
    // WHICH JOBS WERE DROPPED IS THE POOL'S ANSWER, UNDER ITS LOCK (review R3-M1, then R1-m1 of 3.7-e): each job's tag is its
    // ask number, so the reduction in flight was dropped exactly when its number is among the dropped — never a guess from
    // "nothing running", nor from a flag the job sets after a worker has already taken it.
    std::vector<quint64> tags;
    const int  dropped = m_doors.pool->cancelLane(m_instance, &tags);
    const bool reductionDropped = m_reductionInFlight && std::find(tags.begin(), tags.end(), m_reductionLatest) != tags.end();
    if (m_reductionInFlight) {
        // Whatever became of it, a newer one is owed: a result of it that still comes back is dropped as superseded.
        m_reductionLatest   = nextWorkNumber();
        m_reductionInFlight = false;
        m_reductionOwed     = true;
    }
    const int asksDropped = dropped - (reductionDropped ? 1 : 0);
    if (asksDropped > 0)
        m_owedAfterArchive = true;
    m_outstanding = std::max(0, m_outstanding - asksDropped);
    // The dropped asks' in-flight entries go with them, by their numbers (the pool's answer); a job already taken comes back.
    for (auto it = m_inFlight.begin(); it != m_inFlight.end();)
        it = std::find(tags.begin(), tags.end(), it->number) != tags.end() ? m_inFlight.erase(it) : std::next(it);
}

void SessionDiagnostics::onArchiveBack(bool ok)
{
    m_archiveOut = false;
    if (m_retired)
        return;
    const QSet<int> events   = std::exchange(m_eventSwings, {});
    const bool      eventAll = std::exchange(m_eventAll, false);
    const bool      word     = std::exchange(m_wordWhileOut, false);
    if (ok && !eventAll) {
        // THE ARCHIVE IS MADE OF THE FILES AS THEY WERE: what this hold loaded stands (its file is the archived copy's).
        m_owedAfterArchive = false;
        if (events.isEmpty() && !word) {
            // No golfer command or late part meanwhile: nothing is asked and nothing owed is written — a release now
            // writes nothing (B2). A diagnosis of what was loaded is still owed to a screen that holds the session (on
            // the lane: values only, no file).
            m_archivedThisHold = true;
            m_dirty            = false;
            settleIfDone();
            return;
        }
        // The golfer changed these shots (or the session's word) meanwhile: they alone are asked, from their files as
        // they are now (each command saved to its own shot), and the session is diagnosed and written again. Every
        // other shot's findings stand as loaded: none is judged again from its stub (review R2-B1).
        for (const int swing : events)
            if (Shot *s = m_session->shot(ShotRef(m_id, swing))) askFire(s);
        m_reductionOwed = true;
        m_dirty         = true;
        settleIfDone();
        return;
    }
    // Not archived, or a change to every shot meanwhile (a pack, norm, record or intent-run change): what was owed is
    // asked now, as the load would have asked it.
    for (Shot *s : m_session->shots()) {
        const auto k = m_kept.constFind(s->swingNumber());
        if (events.contains(s->swingNumber())
            || (m_owedAfterArchive && (k == m_kept.constEnd() || m_unread.contains(s->swingNumber())
                                       || k->findingsStamp != m_findingsStamp || k->document != m_doors.reader->stamp(s->ref()))))
            askFire(s);
    }
    m_owedAfterArchive = false;
    m_reductionOwed    = true;
    reconcile();
    settleIfDone();
}

void SessionDiagnostics::hearShot(Shot *shot)
{
    if (!shot || m_heard.contains(shot->swingNumber()))
        return;
    m_heard.insert(shot->swingNumber());
    // Its own commands re-ask it (§4.4 step 2): each saved change to what its findings or derived data read. A label
    // correction and an intent change read no finding, but their values are read with the readings (FiringReader), so
    // they ask a fire too (NOTES-3.5-i.md J5). A rating or note asks nothing (DEC-70).
    const ShotRef ref = shot->ref();
    const auto again = [this, ref](const ShotRef &) { fire(ref); };
    connect(shot, &Shot::declaredClubChanged, this, again, Qt::DirectConnection);   // EVT-21
    connect(shot, &Shot::clubCorrected,       this, again, Qt::DirectConnection);   // EVT-04 (both clubs: J-2, via the usuals)
    connect(shot, &Shot::readingExcluded,     this, again, Qt::DirectConnection);   // EVT-05
    connect(shot, &Shot::labelCorrected,      this, again, Qt::DirectConnection);   // EVT-06
    connect(shot, &Shot::shotIntentChanged,   this, again, Qt::DirectConnection);   // EVT-07
    connect(shot, &Shot::captureCompleted,    this, again, Qt::DirectConnection);   // EVT-22
    connect(shot, &Shot::reanalysed,          this, again, Qt::DirectConnection);   // EVT-02
}

void SessionDiagnostics::load()
{
    // §4.4 step 10.
    m_rules = m_doors.ruleData->snapshot();

    // The club baseline (FILE-SESS-04), read when frozen, else frozen now, at first need (DEC-46; step 3.6).
    loadOrFreezeClubBaseline();
    // The athlete's record facts the norm cohort is read from, each shot's on its own date (D6: DEC-171), and the
    // session's day (the date of a shot that records no capture instant).
    {
        const auto e = m_session->entry();
        m_sessionDay = e ? e->identity.localDay() : QDate();
        const AthleteRecord rec = m_session->athlete() ? m_session->athlete()->record() : AthleteRecord();
        m_dob = QDate::fromString(rec.dob, Qt::ISODate);
        m_sex = rec.sex;
        m_athleteHandedness = m_session->athlete() ? rec.handedness : QString();
    }

    // What was kept (FILE-SESS-03).
    QHash<int, DocumentStamp> stamps;
    QHash<int, QString>       contents;
    const DiagnosticsRead read = m_sessions->readDiagnostics(m_id);
    m_fileState = read.state == DiagnosticsRead::State::Read        ? FileState::Read
                : read.state == DiagnosticsRead::State::Newer       ? FileState::Newer
                : read.state == DiagnosticsRead::State::Unopenable  ? FileState::Unopenable
                : read.state == DiagnosticsRead::State::Unparseable ? FileState::Unparseable
                                                                    : FileState::Missing;
    switch (read.state) {
    case DiagnosticsRead::State::Missing:
        break;
    case DiagnosticsRead::State::Unopenable:
        // Most likely good, met at a bad moment: left where it is, and nothing written this load.
        m_readOnly = true;
        ppWarn() << "[SessionDiagnostics] diagnostics.json could not be opened, nothing is written this load:"
                 << m_id.toString() << "-" << read.error;
        break;
    case DiagnosticsRead::State::Unparseable:
        // The file itself is bad: set aside (when this object writes), the session carries on as one with no file.
        ppWarn() << "[SessionDiagnostics] diagnostics.json does not parse:" << m_id.toString() << "-" << read.error;
        if (m_doors.persist) {
            QString aside;
            const WriteResult w = m_sessions->setAsideDiagnostics(m_id, &aside);
            if (!w.ok()) m_readOnly = true;
        }
        break;
    case DiagnosticsRead::State::Newer:
        m_readOnly = true;   // R-PERS-3: read what can be read, write nothing
        ppWarn() << "[SessionDiagnostics] diagnostics.json is a newer build's, read and not written:" << m_id.toString();
        [[fallthrough]];
    case DiagnosticsRead::State::Read: {
        m_legacyGolfer = read.envelope.legacyGolferInput;
        if (m_doors.rebuild)
            break;   // a regrade (regrade_ledger --engine=new): the golfer keys only, as a file with no rows
        m_sessionBlock = read.envelope.session;
        const analysis::SessionDiagnosisState held = analysis::sessionDiagnosisStateFromJson(m_sessionBlock);
        if (held.hasStage) {   // the ratchet as it was left (rebuild's)
            m_stage              = held.stage;
            m_reachedEstablished = held.reachedEstablished;
            m_stageHeld          = true;
        }
        std::vector<ShotRecord> rows = analysis::fromJson(read.envelope.ledger, &m_ledgerOptions);
        const QSet<int> lm(held.lmShots.begin(), held.lmShots.end());
        for (auto it = held.gradedFrom.constBegin(); it != held.gradedFrom.constEnd(); ++it) {
            stamps.insert(it.key(), DocumentStamp{ true, it->size, it->mtimeMs });
            contents.insert(it.key(), it->content);
        }
        for (const QJsonValue &v : m_sessionBlock.value(QStringLiteral("unread")).toArray())
            m_unread.insert(v.toInt());
        m_loadedChecked.clear();
        for (ShotRecord &r : rows) {
            // The file holds the rows CHECKED against the flight; the findings are kept as fired. The checked rows are
            // the shot's flight check as persisted: loadTail takes them back with its derived data when that is current.
            m_loadedChecked.insert(r.shotId, r);
            analysis::uncheckShotAgainstFlight(r);
            KeptShot k;
            k.hasLaunchMonitor = lm.contains(r.shotId);
            k.document         = stamps.value(r.shotId);
            k.findingsStamp    = contents.value(r.shotId);
            const int swing    = r.shotId;
            k.findings         = std::move(r);
            m_kept.insert(swing, std::move(k));
        }
        break;
    }
    }

    // THE GOLFER'S INPUT, by the one rule (golfer_input_rules.h; J-33): the session's intent run from its new home
    // (FILE-SESS-02), else diagnostics.json's older copy; the older per-shot copy, which each shot's review overrides in
    // its job. The same doors golferInput reads through (the session input by id, the envelope just read, the reviews
    // through the swing door).
    m_intentRun        = golfer_input::resolveIntentRun(m_sessions->readSessionInput(m_id).input, m_legacyGolfer);
    readSessionWord();
    m_legacyIntents     = golfer_input::legacyShotIntents(m_legacyGolfer);
    m_legacyCorrections = golfer_input::legacyShotCorrections(m_legacyGolfer);

    // THE USUALS FROM THE READINGS IT HOLDS, BEFORE ANY SHOT IS ASKED, so the first asks use the session's real usuals.
    m_usuals = usualsFromKept();
    refreshFindingsInputs();
    m_snapshot = makeSnapshot();
    m_flightKeys.clear();   // the keys are of m_snapshot's table
    if (m_doors.pool)
        m_doors.pool->setForeground(m_instance, m_session->runningHere());

    connectSession();
    ++m_holdReconcile;
    const QList<Shot *> shots = m_session->shots();
    QSet<int> present;
    for (Shot *s : shots) {
        present.insert(s->swingNumber());
        hearShot(s);
    }
    // A row for a swing that is no longer the session's (trashed while no object held the session): dropped.
    for (const int swing : m_kept.keys())
        if (!present.contains(swing)) {
            drop(swing);
            m_dirty = true;
        }
    for (const int swing : m_unread.values())
        if (!present.contains(swing)) m_unread.remove(swing);

    loadTail();
}

void SessionDiagnostics::loadTail()
{
    if (m_retired)
        return;
    const QList<Shot *> shots = m_session->shots();
    // RE-ASK EVERY SHOT WHOSE STAMP IS STALE, THAT HAS NO KEPT RESULT, OR THAT IS MARKED UNREAD (§4.4 step 10): the
    // shots a release cancelled, the ones left unread, the ones captured while no object held the session. A shot
    // that is current takes its DERIVED DATA back from the file when the stamp it was derived under still holds
    // (DEC-135; run 3.5-ii); the others derive again (reconcile).
    const QJsonObject persisted = m_sessionBlock.value(QStringLiteral("derived")).toObject();
    const QString     base      = persisted.isEmpty() ? QString() : derivedBaseStamp();
    const QHash<int, ShotRecord> checkedOnFile = std::exchange(m_loadedChecked, {});
    for (Shot *s : shots) {
        const int swing = s->swingNumber();
        const auto k = m_kept.find(swing);
        const bool stale = k == m_kept.end() || m_unread.contains(swing) || k->findingsStamp != m_findingsStamp
                        || k->document != m_doors.reader->stamp(s->ref());
        if (stale) {
            askFire(s);
            continue;
        }
        const QJsonObject e = persisted.value(QString::number(swing)).toObject();
        if (e.isEmpty())
            continue;
        const ClubFlightUsual usual = m_usuals.value(canonicalClubId(k->findings.club));
        const QString flightKey = flightKeyOf(k->findings.club);
        if (e.value(QStringLiteral("stamp")).toString() != derivedStampOf(base, swing, usual, flightKey))
            continue;
        ShotDerived d;
        // ITS FLIGHT CHECK, TAKEN BACK (refactor 6.3a; DEC-122): the rows the file holds for it, checked under exactly the
        // usual, the part of the flight table and the thresholds its stamp names. Never run again here.
        if (const auto c = checkedOnFile.constFind(swing); c != checkedOnFile.constEnd()) {
            d.checked = *c;
        } else {   // never: the rows and the findings are the same file's (the one rule, so nothing can differ)
            d.checked = k->findings;
            analysis::checkShotAgainstFlight(d.checked, m_usuals, m_snapshot->inputs.flightNorms);
        }
        d.data.outcome = analysis::shotOutcomeFactsFromJson(e.value(QStringLiteral("outcome")).toObject());
        d.data.factors = analysis::shotFactorsFromJson(e.value(QStringLiteral("factors")).toObject());
        d.usual        = usual;
        d.flightKey    = flightKey;
        d.generation   = m_derivedGeneration;
        d.intent       = e.value(QStringLiteral("intent")).toString();
        d.corrections  = analysis::shotCorrectionsFromJson(
                             QJsonObject{ { QStringLiteral("0"), e.value(QStringLiteral("corrections")) } }).value(0);
        k->derived = std::move(d);
        ++m_counts.derivedRestored;
    }
    --m_holdReconcile;
    m_reductionOwed = true;   // the diagnosis of what was loaded (§4.4 step 7), once the asks above have settled
    reconcile();
    settleIfDone();
}

// ── The snapshot ───────────────────────────────────────────────────────────────────────────────────────────

void SessionDiagnostics::refreshFindingsInputs()
{
    // Once per change of what the findings read (CARRY: "firingStamp is computed once per snapshot, not per shot").
    const analysis::CharacteristicPack *pack = m_rules && m_rules->pack ? &m_rules->pack->pack() : nullptr;
    m_outcomePack = std::make_shared<const analysis::ShotOutcomePack>(analysis::shotOutcomePackFor(pack));
    const std::shared_ptr<const FiringSnapshot> probe = makeSnapshot();
    m_findingsStamp = findingsStampOf(probe->inputs, m_dob, m_sex);
}

std::shared_ptr<const FiringSnapshot> SessionDiagnostics::makeSnapshot() const
{
    auto s = std::make_shared<FiringSnapshot>();
    s->rules = m_rules;
    analysis::FiringInputs &in = s->inputs;
    // The rule data's part, by the one rule the catch-up's scan uses too (summary_rules.h; refactor 3.7-e round 2): the pack
    // and norms OWNED (3.3 review 1, note 2), SET-15 from the snapshot — never a live read (D-12) —, SET-06, the flight table.
    if (m_rules)
        in = firingInputsOf(*m_rules);
    in.clubBaseline = m_baseline;
    in.usuals       = m_usuals;
    if (m_doors.withSigma) in.withSigma = *m_doors.withSigma;   // the gate report's switch (DiagnosticsDoors); else tuned
    in.athleteHandedness = m_athleteHandedness;   // the words of a shot that records no hand (golferHand; B-3)
    // in.cohort stays unqualified here: each fire sets the shot's own (cohortOnShotDate).
    s->outcomePack       = m_outcomePack;
    s->dob               = m_dob;
    s->sex               = m_sex;
    s->sessionDay        = m_sessionDay;
    s->intentRun         = m_intentRun;
    s->legacyIntents     = m_legacyIntents;
    s->legacyCorrections = m_legacyCorrections;
    s->findingsStamp     = m_findingsStamp;
    s->derivedGeneration = m_derivedGeneration;
    return s;
}

QHash<QString, ClubFlightUsual> SessionDiagnostics::usualsFromKept() const
{
    // DEC-130: from the readings in the kept findings, never from a finding.
    std::vector<ShotRecord> rows;
    rows.reserve(size_t(m_kept.size()));
    for (const KeptShot &k : m_kept)
        rows.push_back(k.findings);
    return analysis::sessionFlightUsuals(rows);
}

// ── The asks (§4.4 steps 2–4) ──────────────────────────────────────────────────────────────────────────────

void SessionDiagnostics::fire(const ShotRef &shot)
{
    if (m_retired || shot.session() != m_id)
        return;
    if (m_archiveOut) m_eventSwings.insert(shot.swingNumber());   // a command or a late part meanwhile: asked when it lands
    m_archivedThisHold = false;
    if (Shot *s = m_session->shot(shot))
        askFire(s);
}

void SessionDiagnostics::fireAll()
{
    if (m_retired)
        return;
    if (m_archiveOut) m_eventAll = true;
    m_archivedThisHold = false;
    for (Shot *s : m_session->shots())
        askFire(s);
}

void SessionDiagnostics::askFire(Shot *shot)
{
    if (m_archiveOut) {   // an archive is thinning the session's files: asked when it lands, unless it is made (B2)
        m_owedAfterArchive = true;
        return;
    }
    const ShotRef ref   = shot->ref();
    const int     swing = ref.swingNumber();
    const quint64 n     = nextWorkNumber();
    shot->recordAsk(n);   // the Shot's latest (§4.4 step 3)
    m_inFlight.insert(swing, InFlight{ false, {}, {}, 0, n });
    ++m_outstanding;
    m_asks.push_back({ swing, false, n });
    // VALUES ONLY: the snapshot pointer, the ShotRef, the reader. No Shot, Session or this.
    std::shared_ptr<const FiringSnapshot> snap = m_snapshot;
    std::shared_ptr<const FiringReader>   reader = m_doors.reader;
    submit(WorkAddress{ m_id.toString(), m_instance, ref.toString(), n },
           [snap, ref, reader]() -> std::any { return fireOn(*snap, ref, *reader); });
}

void SessionDiagnostics::askDerive(Shot *shot, const KeptShot &kept)
{
    if (m_archiveOut) {
        m_owedAfterArchive = true;
        return;
    }
    const ShotRef ref   = shot->ref();
    const int     swing = ref.swingNumber();
    const quint64 n     = nextWorkNumber();
    shot->recordAsk(n);
    const analysis::ClubFlightUsual usual = m_usuals.value(canonicalClubId(kept.findings.club));
    m_inFlight.insert(swing, InFlight{ true, usual, flightKeyOf(kept.findings.club), m_derivedGeneration, n });
    ++m_outstanding;
    m_asks.push_back({ swing, true, n });
    // Values only. A result loaded from the file has no review: the job reads it on the worker (C1), as a fire does.
    std::shared_ptr<const FiringSnapshot> snap     = m_snapshot;
    std::shared_ptr<const FiringReader>   reader   = m_doors.reader;
    const ShotRecord                      findings = kept.findings;
    const std::optional<ShotReview>       review   = kept.review;
    submit(WorkAddress{ m_id.toString(), m_instance, ref.toString(), n },
           [snap, reader, ref, findings, review]() -> std::any {
               DeriveOutcome out;
               ShotReview r;
               if (review) {
                   r = *review;
               } else {
                   r = reader->review(ref);
                   if (r.ok) out.review = r;   // an unreadable review is not kept: the next derive reads it again
               }
               out.derived = deriveOn(*snap, findings, wordOf(*snap, ref.swingNumber(), r));
               return out;
           });
}

void SessionDiagnostics::submit(const WorkAddress &address, std::function<std::any()> work)
{
    if (m_doors.synchronous) {
        // Headless (§4.4 step 11): the same job, then the same checks, on this thread.
        WorkResult r;
        r.address = address;
        try {
            r.value = work();
        } catch (const std::exception &e) {
            r.error = QString::fromUtf8(e.what());
        } catch (...) {
            r.error = QStringLiteral("the job failed");
        }
        m_doors.receiver->accept(std::move(r));
        return;
    }
    m_doors.pool->setForeground(m_instance, m_session->runningHere());   // the live session's lane first (D24)
    if (!postToOwner(*m_doors.pool, *m_doors.receiver, m_instance, address, std::move(work), WorkPriority::Low)) {
        // Refused (the pool is going): this ask will never come back, so it does not hold the batch open.
        ppWarn() << "[SessionDiagnostics] the pool refused the ask for" << address.item;
        if (address.item.isEmpty()) {   // the owner's own work (§4.4 step 7)
            m_reductionInFlight = false;
            return;
        }
        m_outstanding = std::max(0, m_outstanding - 1);
        const ShotRef ref = ShotRef::fromString(address.item);
        m_inFlight.remove(ref.swingNumber());
        settleIfDone();
    }
}

// ── The keep (§4.4 step 6) ─────────────────────────────────────────────────────────────────────────────────

void SessionDiagnostics::deliver(WorkVerdict verdict, WorkResult &&result)
{
    if (m_retired)
        return;
    PostShotTrace::Scope trace("owner");   // [PostShot] (refactor 3.7-S): a result kept, on the owner's thread
    if (result.address.item.isEmpty()) {   // the owner's own work: a reduction (§4.4 step 7)
        deliverReduction(verdict, std::move(result));
        return;
    }
    m_outstanding = std::max(0, m_outstanding - 1);
    const ShotRef ref   = ShotRef::fromString(result.address.item);
    const int     swing = ref.swingNumber();

    // A RESULT THAT LANDS WHILE AN ARCHIVE IS OUT is not kept (review R2-M2): its read may have met a swing being thinned
    // to its stub. The shot's earlier result stays, and it is owed: asked when the archive lands, unless the archive is
    // made from the files as they were (onArchiveBack).
    if (m_archiveOut && verdict == WorkVerdict::Kept) {
        m_inFlight.remove(swing);
        m_owedAfterArchive = true;
        ++m_counts.droppedWhileArchived;
        settleIfDone();
        return;
    }

    switch (verdict) {
    case WorkVerdict::Superseded:
        ++m_counts.superseded;   // the newer ask is still in the lane: its own result settles it
        break;
    case WorkVerdict::ItemGone:
        ++m_counts.itemGone;     // a trashed shot: nothing kept
        m_inFlight.remove(swing);
        break;
    case WorkVerdict::Kept: {
        const InFlight asked = m_inFlight.value(swing);
        const bool wasDerive = asked.derive;
        m_inFlight.remove(swing);
        const FireOutcome *fired = std::any_cast<FireOutcome>(&result.value);
        const DeriveOutcome *derived = std::any_cast<DeriveOutcome>(&result.value);
        if (!result.error.isEmpty() || (!fired && !derived)) {
            // A job that threw: read as unread (work_pool.h), its earlier result kept.
            const QString why = result.error.isEmpty() ? QStringLiteral("the job returned nothing") : result.error;
            ppWarn() << "[SessionDiagnostics] shot" << ref.toString() << (wasDerive ? "derive" : "fire") << "failed:" << why;
            if (!wasDerive) {
                m_unread.insert(swing);
                m_dirty = true;
                ++m_counts.unread;
                emit shotUnread(ref, why);
            } else if (auto it = m_kept.find(swing); it != m_kept.end()) {
                // A DERIVE THAT FAILED is not asked again until one of its inputs moves (C3): its findings (a fire
                // makes a new KeptShot), its club's usual or the layer-3 generation it was asked under.
                it->deriveFailed = DeriveFailure{ asked.usual, asked.flightKey, asked.generation, why };
                ++m_counts.deriveFailures;
            }
            break;
        }
        if (fired && !fired->ok) {
            // UNREAD: nothing kept; the shot's earlier result stays; marked (persisted, DEC-191); re-asked at the next load.
            m_unread.insert(swing);
            m_dirty = true;
            ++m_counts.unread;
            ppWarn() << "[SessionDiagnostics] shot" << ref.toString() << "unread, its earlier findings kept:" << fired->error;
            emit shotUnread(ref, fired->error);
            break;
        }
        if (fired) {
            KeptShot k;
            k.findings         = fired->findings;
            k.hasLaunchMonitor = fired->hasLaunchMonitor;
            k.document         = fired->document;
            k.findingsStamp    = fired->findingsStamp;
            k.cohort           = fired->cohort;
            k.derived          = fired->derived;
            if (fired->review.ok) k.review = fired->review;
            k.deliveryRead     = true;   // off the grid this fire read (refactor 5.5d)
            k.delivery         = fired->delivery;
            k.gates            = fired->gates;   // the same grid (platform refactor P-3b)
            m_kept.insert(swing, std::move(k));
            m_unread.remove(swing);
            m_dirty = true;
            m_reductionOwed = true;
        } else {
            auto it = m_kept.find(swing);
            if (it == m_kept.end()) break;   // its findings left (a trash raced the receiver): nothing to put it on
            it->derived = derived->derived;
            it->deriveFailed.reset();
            if (derived->review) it->review = derived->review;
            m_dirty = true;            // its derived data is persisted (DEC-135)
            m_reductionOwed = true;
        }
        ++m_counts.kept;
        emit resultKept(ref);
        break;
    }
    case WorkVerdict::OwnerGone:
    case WorkVerdict::OtherInstance:
        break;   // never handed here (work_pool.h)
    }
    reconcile();
    settleIfDone();
}

void SessionDiagnostics::reconcile()
{
    if (m_holdReconcile > 0 || m_retired)
        return;
    // §4.4 step 2a: the usuals again, field by field; only a changed value builds a new snapshot.
    const QHash<QString, ClubFlightUsual> now = usualsFromKept();
    if (!sameUsuals(now, m_usuals)) {
        m_usuals = now;
        m_snapshot = makeSnapshot();
    }
    // EVERY KEPT RESULT WHOSE INPUTS ARE NOT CURRENT IS ASKED AGAIN, AND ONLY THOSE (a shot with an ask in flight is
    // judged when that ask comes back): stale findings → a fire; derived data against another usual of its club, or
    // another layer-3 generation → a derive only (DEC-191). A shot marked unread is not fired again until the next
    // load (its read failed: asking at once would only fail again).
    // Judged afresh just before each ask: headless, an ask comes back inline and may already have asked the next one.
    const auto needsFire = [this](int swing) {
        const auto it = m_kept.constFind(swing);
        return it != m_kept.constEnd() && !m_inFlight.contains(swing) && it->findingsStamp != m_findingsStamp
            && !m_unread.contains(swing);
    };
    const auto needsDerive = [this](int swing) {
        const auto it = m_kept.constFind(swing);
        if (it == m_kept.constEnd() || m_inFlight.contains(swing) || it->findingsStamp != m_findingsStamp)
            return false;
        const ClubFlightUsual usual = m_usuals.value(canonicalClubId(it->findings.club));
        const bool current = derivedCurrent(*it);
        const bool failedSo = it->deriveFailed && it->deriveFailed->generation == m_derivedGeneration
                           && sameUsual(it->deriveFailed->usual, usual)
                           && it->deriveFailed->flightKey == flightKeyOf(it->findings.club);   // failed under exactly these inputs (C3)
        return !current && !failedSo;
    };
    const QList<int> swings = m_kept.keys();
    for (const int swing : swings)
        if (needsFire(swing))
            if (Shot *s = m_session->shot(ShotRef(m_id, swing))) askFire(s);
    for (const int swing : swings)
        if (needsDerive(swing))
            if (Shot *s = m_session->shot(ShotRef(m_id, swing))) askDerive(s, *m_kept.constFind(swing));
}

void SessionDiagnostics::settleIfDone()
{
    if (m_retired || m_outstanding > 0 || m_holdReconcile > 0)   // a load settles once, at its end (J18)
        return;
    emit batchSettled();
    // §4.4 step 7: the batch settled, so the session is diagnosed on its lane — once per settle that moved what is kept.
    if (m_reductionOwed && !m_reductionInFlight)
        askReduction();
    // What is kept is written, debounced (§4.4 step 8; again when the reduction is applied). Headless: the tool
    // flushes when it is done, and the release and the destructor write anyway.
    if (m_dirty && m_doors.persist && !m_doors.synchronous && !m_readOnly)
        m_writeTimer.start(std::max(0, m_doors.writeDebounceMs));
}

void SessionDiagnostics::drop(int swing)
{
    if (m_kept.remove(swing) > 0) {
        m_dirty = true;
        m_reductionOwed = true;
    }
    if (m_unread.remove(swing))
        m_dirty = true;
}

// ── Heard ──────────────────────────────────────────────────────────────────────────────────────────────────

// Only the flight table differs between two rule-data snapshots: every other part is the same object, and the settings
// the same (RuleData::assemble shares each part it did not read again).
static bool onlyFlightTableMoved(const RuleDataSnapshot &a, const RuleDataSnapshot &b)
{
    return a.pack == b.pack && a.shippedPack == b.shippedPack && a.norms == b.norms && a.shippedNorms == b.shippedNorms
        && a.userPack == b.userPack && a.userNorms == b.userNorms && a.references == b.references && a.drills == b.drills
        && a.screens == b.screens && a.gateWords == b.gateWords && a.catalogue == b.catalogue
        && a.disabledNormSets == b.disabledNormSets && a.gradePolicy == b.gradePolicy && a.flightNorms != b.flightNorms;
}

void SessionDiagnostics::onRulesChanged()
{
    if (m_retired)
        return;
    const std::shared_ptr<const RuleDataSnapshot> before = m_rules;
    const QString findingsBefore = m_findingsStamp;
    m_rules = m_doors.ruleData->snapshot();
    refreshFindingsInputs();
    if (before && m_rules && m_findingsStamp == findingsBefore && onlyFlightTableMoved(*before, *m_rules)) {
        // A FLIGHT-TABLE EDIT (refactor 6.3a; DEC-122, R-STAMP-1): no finding reads the table, so nothing is fired. Each
        // shot's flight key (the part of the table its club reads) is compared, and only the shots whose key moved derive
        // again; the session is diagnosed again, since its reduction reads the table too.
        if (m_archiveOut) m_eventAll = true;
        m_archivedThisHold = false;
        m_snapshot = makeSnapshot();
        m_flightKeys.clear();   // the keys are of m_snapshot's table
        m_reductionOwed = true;
        reconcile();
        settleIfDone();
        return;
    }
    ++m_derivedGeneration;   // the outcome pack rides in it too
    m_snapshot = makeSnapshot();
    m_flightKeys.clear();
    fireAll();               // a norm or pack change → every shot (EVT-12, EVT-13)
}

void SessionDiagnostics::onAthleteChanged()
{
    if (m_retired || !m_session->athlete())
        return;
    const AthleteRecord &rec = m_session->athlete()->record();
    const QDate dob = QDate::fromString(rec.dob, Qt::ISODate);
    if (dob == m_dob && rec.sex == m_sex) {
        // A handedness edit re-words the shots that record no hand (golferHand; B-3): their derived data only.
        if (rec.handedness == m_athleteHandedness)
            return;   // a record edit that leaves the cohort's facts and the hand where they were asks nothing
        m_athleteHandedness = rec.handedness;
        if (m_archiveOut) m_eventAll = true;   // a change to every shot meanwhile (the record)
        ++m_derivedGeneration;
        m_snapshot = makeSnapshot();
        m_reductionOwed = true;
        reconcile();
        settleIfDone();
        return;
    }
    m_athleteHandedness = rec.handedness;
    m_dob = dob;
    m_sex = rec.sex;
    refreshFindingsInputs();
    m_snapshot = makeSnapshot();
    fireAll();    // a sex or date-of-birth correction re-fires the athlete's sessions (D6: DEC-171)
}

void SessionDiagnostics::onIntentRunChanged()
{
    if (m_retired)
        return;
    if (m_archiveOut) m_eventAll = true;
    m_archivedThisHold = false;
    // The one rule again (J-33): the new home's run, else the older copy's.
    const IntentRun run = golfer_input::resolveIntentRun(m_sessions->readSessionInput(m_id).input, m_legacyGolfer);
    if (run == m_intentRun)
        return;
    m_intentRun = run;
    ++m_derivedGeneration;   // a shot's intent is read by its derived data only: every shot derives again
    m_snapshot = makeSnapshot();
    m_reductionOwed = true;
    reconcile();
    settleIfDone();
}

void SessionDiagnostics::readSessionWord()
{
    const golfer_input::SessionWord w =
        golfer_input::resolveSessionWord(m_sessions->readSessionInput(m_id).input, m_legacyGolfer);
    m_focus   = w.focus;
    m_miss    = w.miss;
    m_screens = w.screens;
}

void SessionDiagnostics::onSessionIntentChanged()
{
    if (m_retired)
        return;
    if (m_archiveOut) m_wordWhileOut = true;
    m_archivedThisHold = false;
    // EVT-10: the focus, the miss or a screen answer — read by the diagnosis only (no finding, no derived data reads
    // them), so the session is diagnosed again and nothing is asked of a shot.
    const golfer_input::SessionWord before{ m_focus, m_miss, m_screens };
    readSessionWord();
    if (before == golfer_input::SessionWord{ m_focus, m_miss, m_screens })
        return;
    m_reductionOwed = true;
    settleIfDone();
}

// THE CLUB BASELINE AT FIRST NEED (refactor step 3.6; OBJ-CLUB-BASELINE; DEC-46, DEC-172; R-STAMP-4). FROZEN MEANS A
// FILE OF THIS RULE IS THERE (the old model's rule): read it — and one that cannot be read, or a newer build's, stays
// frozen and empty (no usual numbers), never rewritten. Otherwise — no file, or an OLDER RULE'S (the one case a frozen
// baseline is written again) — the athlete level computes it from the predecessors' SUMMARIES
// (AthleteDiagnostics::clubBaselineFor), and it is frozen: written by the Session with what it read (and the rewrite
// recorded, EVT-16) when this owner persists; held in memory only when it does not (the parallel run: the old model
// stays the file's writer). What it read is recorded with a rewrite only (FILE-SESS-04). Not computable yet (an earlier session not summed up by this build, or no day): nothing
// is frozen, so nothing wrong is frozen for good, and the session has no usual numbers until the next load.
void SessionDiagnostics::loadOrFreezeClubBaseline()
{
    const JsonFileRead f = m_sessions->readClubBaseline(m_id);
    const int schemaOnDisk = f.exists && f.ok ? f.root.value(QStringLiteral("schema")).toInt(0) : 0;
    const bool outdated = schemaOnDisk > 0 && schemaOnDisk < analysis::kClubBaselineSchemaVersion;
    if (f.exists && !outdated) {
        bool ok = false;
        const analysis::ClubBaselineSnapshot snap = f.ok ? analysis::clubBaselineFromJson(f.root, &ok) : analysis::ClubBaselineSnapshot{};
        if (ok)
            m_baseline = snap.clubs;
        else
            ppWarn() << "[SessionDiagnostics] club baseline unreadable, this session has no usual numbers:" << m_id.toString();
        return;
    }
    AthleteDiagnostics *athlete = AthleteDiagnostics::of(m_session ? m_session->athlete() : nullptr);
    if (!athlete)
        return;
    const ClubBaselineComputation c = athlete->clubBaselineFor(m_id);
    m_baselineWaiting = !c.computable && !c.waitingOn.isEmpty();
    if (!c.computable) {
        ppInfo() << "[SessionDiagnostics] club baseline not frozen yet for" << m_id.toString() << "-" << c.why;
        return;
    }
    m_baseline = c.clubs;
    ++m_counts.baselineFreezes;
    ppInfo() << "[SessionDiagnostics] club baseline for" << m_id.toString() << "from" << c.read.size()
             << "earlier session(s)' summaries:" << c.clubs.size() << "club(s)"
             << (m_doors.persist ? (outdated ? "- rewritten (an older rule's file)" : "- frozen") : "- in memory only");
    if (!m_doors.persist)
        return;   // in memory only (the parallel run): the old model stays the file's one writer
    analysis::ClubBaselineSnapshot snap;
    snap.frozenAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    snap.clubs    = c.clubs;
    if (outdated) {   // the one case a frozen baseline is written again: the rewrite recorded (R-STAMP-4)
        snap.rewrites = analysis::clubBaselineRewritesFromJson(f.root);
        snap.rewrites.push_back(analysis::ClubBaselineSnapshot::Rewrite{ snap.frozenAt, c.read });
    }
    const WriteResult w = m_session->freezeClubBaseline(analysis::clubBaselineToJson(snap), outdated);
    if (!w.ok()) {
        ++m_counts.writeFailures;
        m_lastWriteError = QStringLiteral("The session's club baseline: ") + w.error;
        emit writeFailed(m_lastWriteError);
    }
}

bool SessionDiagnostics::recheckDocuments()
{
    if (m_retired || m_archiveOut || !m_session || !m_doors.reader)
        return false;
    bool asked = false;
    for (Shot *shot : m_session->shots()) {
        const KeptShot *k = kept(shot->swingNumber());
        if (k && k->document == m_doors.reader->stamp(shot->ref()) && !m_unread.contains(shot->swingNumber()))
            continue;
        if (m_inFlight.contains(shot->swingNumber()))
            continue;   // already asked
        askFire(shot);
        asked = true;
    }
    if (asked)
        m_reductionOwed = true;
    return asked;
}

bool SessionDiagnostics::freezeClubBaselineIfNeeded()
{
    if (m_retired || m_archiveOut || m_readOnly || m_baseline)
        return false;
    const bool waitingBefore = m_baselineWaiting;
    loadOrFreezeClubBaseline();   // a file frozen meanwhile is read; else frozen now when computable (the one rule)
    if (m_baselineWaiting != waitingBefore || m_baseline)
        emit clubBaselineChanged();   // the screens' "usual numbers not ready" follows (J-49), shots or none
    if (!m_baseline)
        return false;
    const QString before = m_findingsStamp;
    refreshFindingsInputs();
    if (m_findingsStamp == before)
        return false;
    ++m_derivedGeneration;   // the carry against the usual reads it too
    m_snapshot = makeSnapshot();
    fireAll();               // a baseline frozen → every shot (EVT-16)
    return true;
}

bool SessionDiagnostics::reloadClubBaseline(bool keepWhenAbsent)
{
    if (m_retired)
        return false;
    std::optional<QHash<QString, analysis::ClubBaselineEntry>> next;
    const JsonFileRead f = m_sessions->readClubBaseline(m_id);
    if (keepWhenAbsent && !f.exists)
        return false;   // nothing frozen on disk: what this owner holds stands (3.7-0 round 2, review F3)
    if (f.exists && f.ok) {
        bool ok = false;
        const analysis::ClubBaselineSnapshot snap = analysis::clubBaselineFromJson(f.root, &ok);
        if (ok) next = snap.clubs;
    }
    const QString before = m_findingsStamp;
    const auto keep = m_baseline;
    m_baseline = next;
    refreshFindingsInputs();
    if (m_findingsStamp == before) {
        m_baseline = keep;   // the same numbers (the stamp hashes every entry): nothing to ask
        refreshFindingsInputs();
        return false;
    }
    ++m_derivedGeneration;   // the carry against the usual reads it too
    m_snapshot = makeSnapshot();
    fireAll();               // a baseline rewrite → every shot (EVT-16)
    return true;
}

// ── Written (§4.4 step 8) ──────────────────────────────────────────────────────────────────────────────────

bool SessionDiagnostics::flush(bool final)
{
    m_writeTimer.stop();
    // Nothing is written while an archive is out (B2), nor after it was made from this hold until the golfer changes
    // something: the session's file stays the archive's copy (a restore puts back what the library holds).
    if (m_archiveOut || m_archivedThisHold)
        return true;
    if (!m_doors.persist || m_readOnly || m_retired)
        return true;
    if (final)
        m_finalWrite = true;   // a release or the quit: an owed summary is written now, live or not
    bool ok = true;
    if (m_dirty)
        ok = write();
    if (ok && m_summaryOwed && !summaryWaits())
        ok = writeSummary();
    return ok;
}

// THE CATCH-UP'S WRITE (refactor 3.7-e): what is kept, as flush(), AND the summary even when nothing is owed — the catch-up
// took this session in hand because the summary on disk is stale (an older rule's, a missing one, a folder changed outside
// this process) while the owner itself may be idle with nothing unwritten (a session a screen loaded and diagnosed). One
// summary write at most. A live session's still waits for its End.
bool SessionDiagnostics::flushWithSummary()
{
    const bool attempted = m_dirty;   // a dirty flush writes the summary with what is kept (or fails saying so)
    if (!flush(/*final=*/false))
        return false;
    if (attempted || m_archiveOut || m_archivedThisHold || !m_doors.persist || m_readOnly
        || m_retired || summaryWaits())
        return true;
    return writeSummary();
}

// THE SUMMARY WAITS FOR THE SESSION'S END WHILE IT IS RECORDED HERE (refactor 3.7-e, the checklist's "no per-shot GUI
// cost"): the athlete level reads no live session's summary (the catch-up is paused while a session is live, and every
// baseline window is of earlier days), so a live session's summary is written once it has ended (its End's diagnosis is
// written), or when it is released or the app quits (flush(final)) — never with every debounced write of a live shot.
// Read through the repository's identity map, not the Session object, once a final write has begun (the destructor's).
bool SessionDiagnostics::summaryWaits() const
{
    if (m_finalWrite || !m_session)
        return false;
    return m_session->runningHere() && m_session->inProgress();
}

bool SessionDiagnostics::writeSummary()
{
    if (!m_diagnosis || !m_doors.writeSummary)
        return true;
    PostShotTrace::Scope trace("owner.summary");   // [PostShot]: the session summary's write (3.7-e)
    // R-PERS-3 (refactor 4-e): A NEWER BUILD'S SUMMARY IS READ-ONLY HERE, as diagnostics.json's is. Checked on the file as
    // it stands now, at each write: the library is shared between machines that may run different builds, and this build
    // must never write its older summary over a newer one. Left as it is; the athlete level counts it as a newer build's.
    if (const JsonFileRead on = m_sessions->readWorkOns(m_id);
        on.exists && on.ok && analysis::sessionResultsFromNewerBuild(on.root)) {
        m_summaryOwed = false;
        ++m_counts.summaryNewerKept;
        ppWarn() << "[SessionDiagnostics] work_ons.json is a newer build's, read and not written:" << m_id.toString();
        return true;
    }
    const WriteResult ws = m_sessions->writeWorkOns(m_id, analysis::sessionResultsToJson(summary()));
    m_summaryOwed = false;   // a failed write is said once and tried again with the next diagnosis, as before 3.7-e
    if (ws.ok()) {
        ++m_counts.summaryWrites;
        return true;
    }
    ++m_counts.writeFailures;
    m_lastWriteError = QStringLiteral("The session's work-ons: ") + ws.error;
    ppWarn() << "[SessionDiagnostics] work_ons.json was not written for" << m_id.toString() << "-" << ws.error;
    emit writeFailed(m_lastWriteError);
    return false;
}

bool SessionDiagnostics::write()
{
    PostShotTrace::Scope trace("owner.write");   // [PostShot] (refactor 3.7-a): the debounced write, on the owner's thread
    // TODAY'S SHAPE (FILE-SESS-03): the ledger holds the rows CHECKED against the session's usual flight, as every
    // reader of the file counts them. The session block is FIELD-LIST §2's typed state, every key the old model's
    // sessionBlock() writes, as it writes them (review 1 of 3.5-ii: no key change) — `closed` FROM THE SESSION, the one
    // writer of its status (J-24) — over any key of the file this object does not know (kept as read), with DEC-191's
    // `unread` and each shot's derived data, stamped (DEC-135).
    // ⏱ [PostShot] (refactor 3.7-a, review R2-M4): the part of the write before the file — the rows copied and checked
    // against the flight, the session block, each shot's derived data, the ledger as JSON — all on this thread.
    std::optional<PostShotTrace::Scope> build(std::in_place, "build");
    std::vector<ShotRecord> rows;
    QJsonArray  unread;
    analysis::SessionDiagnosisState st;
    for (auto it = m_kept.constBegin(); it != m_kept.constEnd(); ++it) {
        rows.push_back(checkedOf(*it));   // the shot's flight check as held (DEC-122; refactor 6.3a)
        st.gradedFrom.insert(it.key(), analysis::GradedFrom{ it->document.size, it->document.mtimeMs, it->findingsStamp });
        st.swingDirs.insert(it.key(), m_sessions->swingFolder(ShotRef(m_id, it.key())));
        if (it->hasLaunchMonitor)
            st.lmShots.append(it.key());
    }
    QList<int> marks = m_unread.values();
    std::sort(marks.begin(), marks.end());
    for (const int n : marks)
        unread.append(n);
    {
        const auto e = m_sessions->open(m_id);
        st.sessionDir = e ? e->folder : QString();
        st.sessionId  = QFileInfo(st.sessionDir).fileName();   // the folder's name, as today (F1)
    }
    st.closed             = sessionEnded();
    st.stage              = m_stage;
    st.reachedEstablished = m_reachedEstablished;
    st.shotCount          = int(rows.size());
    st.gradePolicy        = m_snapshot ? m_snapshot->inputs.gradePolicy : QString();
    st.writtenAtMs        = double(QDateTime::currentMSecsSinceEpoch());

    QJsonObject block = m_sessionBlock;
    const QJsonObject typed = analysis::sessionDiagnosisStateToJson(st);
    for (auto it = typed.constBegin(); it != typed.constEnd(); ++it)
        block.insert(it.key(), it.value());
    block.insert(QStringLiteral("unread"), unread);   // DEC-191: the shots whose last read failed
    {
        const QString base = derivedBaseStamp();
        QJsonObject derived;
        for (auto it = m_kept.constBegin(); it != m_kept.constEnd(); ++it)
            if (derivedCurrent(*it)) {
                const ClubFlightUsual usual = m_usuals.value(canonicalClubId(it->findings.club));
                derived.insert(QString::number(it.key()),
                               derivedToJson(*it->derived, derivedStampOf(base, it.key(), usual, it->derived->flightKey)));
            }
        block.insert(QStringLiteral("derived"), derived);
    }

    const QJsonObject ledger = analysis::toJson(rows, m_ledgerOptions);
    build.reset();
    DiagnosticsRead::State found = DiagnosticsRead::State::Missing;
    const WriteResult w = m_sessions->writeDiagnostics(m_id, block, ledger, m_legacyGolfer, &found);
    if (w.ok()) {
        m_sessionBlock = block;
        m_dirty = false;
        ++m_counts.writes;
        // THE SUMMARY (FILE-SESS-05; DEC-55, DEC-192), with the diagnosis it is of: this object is its one writer
        // (step 3.6; in the app from the catch-up's cut-over, 3.7-e). A live session's waits for its End (summaryWaits).
        if (m_diagnosis && m_doors.writeSummary) {
            m_summaryOwed = true;
            if (!summaryWaits())
                writeSummary();
        }
        emit written();
        return true;
    }
    if (found == DiagnosticsRead::State::Newer || found == DiagnosticsRead::State::Unopenable)
        m_readOnly = true;
    ++m_counts.writeFailures;
    m_lastWriteError = QStringLiteral("The session's diagnostics: ") + w.error;
    ppWarn() << "[SessionDiagnostics] diagnostics.json was not written for" << m_id.toString() << "-" << w.error;
    emit writeFailed(m_lastWriteError);
    return false;
}

// ── The diagnosis (§4.4 step 7) ────────────────────────────────────────────────────────────────────────────

// The session's status, as its one writer holds it (J-24), read through the repository's identity map — never through
// the Session object, which is already gone when this object writes from its destructor.
bool SessionDiagnostics::sessionEnded() const
{
    const auto e = m_sessions->open(m_id);
    return !e || e->status != SessionStatus::InProgress;
}

analysis::SessionReductionInput SessionDiagnostics::reductionInput() const
{
    analysis::SessionReductionInput in;
    in.pack        = m_snapshot->inputs.pack;
    in.outcomePack = m_outcomePack;
    in.options     = m_ledgerOptions;
    if (m_doors.softTier) in.options.softTier = *m_doors.softTier;   // the gate report's switch (DiagnosticsDoors)
    in.withSigma   = m_snapshot->inputs.withSigma;
    in.usuals      = m_usuals;
    in.baseline    = m_baseline;
    in.flightNorms = m_snapshot->inputs.flightNorms;
    in.athleteHandedness = m_snapshot->inputs.athleteHandedness;
    in.shots.reserve(size_t(m_kept.size()));
    // THE BOARD'S ANNOTATIONS (step 3.6; Stage 0.5's follow-up; board_annotations.h): the session's results read each
    // shot as the golfer annotated it on the launch monitor board — its club override (the club it is pooled and summed
    // under: the work-on facts, carry today, the factor mix, the club windows the distances and baselines come from,
    // each shot's club in the summary), and a reading left out not read. Asked of each Shot (P10), on this thread.
    const QSet<QString> lmMeasures = in.pack ? analysis::launchMonitorMeasures(in.pack->pack()) : QSet<QString>{};
    for (auto it = m_kept.constBegin(); it != m_kept.constEnd(); ++it) {   // by swing number: strike order
        ShotRecord r = checkedOf(*it);     // the shot's flight check as held: the rows as the file holds them
        const analysis::BoardAnnotation board = boardOf(it.key());
        r = analysis::annotatedShot(std::move(r), board, lmMeasures);
        in.shots.push_back(std::move(r));
        const int swing = it.key();
        // A shot the board annotated — a reading left out, or a club re-labelled (review CN-1) — is composed in the reduction
        // from the annotated rows above, under its effective club's usual; its kept derived data was composed under the
        // recorded club, and one shot must not be composed twice two ways (DEC-81).
        if (it->derived && !board.any()) {
            in.derived.insert(swing, it->derived->data);
            in.intents.insert(swing, it->derived->intent);
            if (!it->derived->corrections.empty()) in.corrections.insert(swing, it->derived->corrections);
        } else {
            const ShotWord w = wordOf(*m_snapshot, swing, it->review.value_or(ShotReview{}));
            in.intents.insert(swing, w.intent);
            if (!w.corrections.empty()) in.corrections.insert(swing, w.corrections);
        }
    }
    in.focusConditionId = m_focus.conditionId;
    in.focusFromShot    = m_focus.fromShot;
    in.declaredMiss     = m_miss;
    in.screens          = m_screens;
    in.previousStage      = m_stageHeld ? m_stage : analysis::Stage::Cold;
    in.reachedEstablished = m_stageHeld && m_reachedEstablished;
    in.replay             = !m_stageHeld;
    in.closed             = sessionEnded();   // the Session is the one writer of its status (J-24)
    const auto e = m_sessions->open(m_id);
    in.folderName = e ? QFileInfo(e->folder).fileName() : QString();
    in.sessionId  = in.folderName;   // today's files key a session by its folder's name (FIELD-LIST F1)
    in.day        = m_sessionDay;
    return in;
}

// THE DAY READINGS' ASK (refactor 5.5d; DEC-74), beside the reduction's input: each kept shot, strike order, with its
// delivery reading when it has one for its document; its readings at address off its rows CHECKED against the flight (the
// rows diagnostics.json holds, as write() makes them; never the board-annotated rows the reduction reads), for the pack's
// address absolutes; and the cameras when the first kept shot and its document are the ones they were read from.
DayReadingsAsk SessionDiagnostics::dayAsk() const
{
    DayReadingsAsk ask;
    ask.reader = m_doors.reader;
    ask.pack   = m_snapshot ? m_snapshot->inputs.pack : nullptr;
    const QStringList absolutes = ask.pack ? analysis::addressAbsoluteIds(ask.pack->pack()) : QStringList{};
    for (auto it = m_kept.constBegin(); it != m_kept.constEnd(); ++it) {
        DayReadingsAsk::Shot s;
        s.swing    = it.key();
        s.ref      = ShotRef(m_id, it.key());
        s.document = it->document;
        s.read     = it->deliveryRead;
        s.delivery = it->delivery;
        s.gates    = it->gates;
        ask.shots.push_back(std::move(s));
        if (!absolutes.isEmpty()) {
            const ShotRecord r = checkedOf(*it);   // the shot's flight check as held
            if (std::optional<analysis::ShotAddressReading> a = analysis::shotAddressReadingOf(r, absolutes))
                ask.address.push_back(std::move(*a));
        }
    }
    if (!ask.shots.empty() && !m_camerasKey.isEmpty() && m_camerasKey == camerasKeyOf(ask.shots.front())) {
        ask.camerasKnown = true;
        ask.cameras      = m_cameras;
        ask.camerasKey   = m_camerasKey;
    }
    return ask;
}

void SessionDiagnostics::askReduction()
{
    if (m_retired || !m_snapshot)
        return;
    if (m_archiveOut) {   // diagnosed when the archive lands (B2)
        m_reductionOwed = true;
        return;
    }
    m_reductionOwed     = false;
    m_reductionLatest   = nextWorkNumber();   // the one counter (§4.4 step 7): latest wins
    m_reductionInFlight = true;
    ++m_counts.reductionsAsked;
    auto input = std::make_shared<const analysis::SessionReductionInput>(reductionInput());   // values only
    m_askedInput = input;   // a kept reduction is the latest asked (§4.4 step 6), so this is what it was reduced from
    auto day = std::make_shared<const DayReadingsAsk>(dayAsk());
    submit(WorkAddress{ m_id.toString(), m_instance, QString(), m_reductionLatest },
           [input, day]() -> std::any {
               QElapsedTimer t;
               t.start();
               ReductionOutcome out;
               out.diagnosis = analysis::reduceSession(*input);
               out.day = dayReadingsOn(*day);
               out.ms = t.elapsed();
               return out;
           });
}

void SessionDiagnostics::deliverReduction(WorkVerdict verdict, WorkResult &&result)
{
    const bool latest = result.address.ask == m_reductionLatest;
    if (latest)
        m_reductionInFlight = false;
    if (verdict != WorkVerdict::Kept) {
        ++m_counts.reductionsSuperseded;   // a newer reduction is in the lane: its own result is applied
        return;
    }
    ReductionOutcome *r = std::any_cast<ReductionOutcome>(&result.value);
    if (!result.error.isEmpty() || !r) {
        ++m_counts.reductionFailures;
        ppWarn() << "[SessionDiagnostics] the diagnosis of" << m_id.toString() << "failed:"
                 << (result.error.isEmpty() ? QStringLiteral("the job returned nothing") : result.error);
    } else {
        m_stage              = r->diagnosis.stage;
        m_reachedEstablished = r->diagnosis.reachedEstablished;
        m_stageHeld          = true;
        m_lastReductionMs    = r->ms;
        m_diagnosis          = std::move(r->diagnosis);
        m_diagnosisInput     = m_askedInput;   // kept means latest: the input asked last (refactor 3.7-a)
        // The day readings it was asked with (refactor 5.5d): the delivery readings read on the lane kept on their shots
        // while each shot's document is still the one read; the cameras with their key.
        for (auto it = r->day.read.constBegin(); it != r->day.read.constEnd(); ++it)
            if (auto k = m_kept.find(it.key()); k != m_kept.end() && !k->deliveryRead && k->document == it->document) {
                k->deliveryRead = true;
                k->delivery     = it->delivery;
                k->gates        = it->gates;
            }
        m_dayReadings = std::move(r->day.readings);
        m_gates       = std::move(r->day.gates);
        m_cameras     = r->day.cameras;
        m_camerasKey  = r->day.camerasKey;
        m_summaryFingerprint = summaryFingerprint();   // its Session's shots as they are now (3.7-e)
        m_dirty              = true;   // the diagnosis state is written with what is kept (§4.4 step 8)
        ++m_counts.reductions;
        emit diagnosisApplied();
    }
    if (m_reductionOwed && m_outstanding == 0 && m_holdReconcile == 0)
        askReduction();   // what is kept moved while it ran
    else if (m_dirty && m_doors.persist && !m_doors.synchronous && !m_readOnly)
        m_writeTimer.start(std::max(0, m_doors.writeDebounceMs));
}

analysis::BoardAnnotation SessionDiagnostics::boardOf(int swing) const
{
    Shot *shot = m_session ? m_session->shot(ShotRef(m_id, swing)) : nullptr;
    if (!shot)
        return {};
    const ShotSummary &s = shot->summary();   // read once by the Shot, then kept (its commands update it)
    return s.ok ? analysis::BoardAnnotation{ s.lmExcluded, s.lmClubOverride } : analysis::BoardAnnotation{};
}

// WHAT THE SUMMARY WAS WORKED OUT FROM (FIELD-LIST §1.8's fingerprint; R-STAMP-1): ONE RULE with the catch-up's scan
// (session_reduction.h summaryFingerprintOf; refactor 3.7-e, round 2: J-47) — every rule version, the rule data
// (summaryRulesStamp over firingInputsOf: pack, norms, grade policy, disabled sets, flight norms), the frozen club baseline,
// the athlete record's cohort facts, and each swing with the document its kept findings were fired from. Taken when a
// diagnosis is applied, on the owner thread with its Session alive; summary() reads it.
QString SessionDiagnostics::summaryFingerprint() const
{
    std::vector<analysis::SummarySwing> swings;
    if (m_session)
        for (Shot *shot : m_session->shots()) {
            analysis::SummarySwing w;
            w.folder = QFileInfo(shot->folder()).fileName();
            // The document its kept findings were fired from; a shot with none kept, or marked unread (its last read
            // failed: review R1-m2 (b)), its document as it is now — the scan's view — so it does not stale the summary for
            // ever (its own re-ask is the load's, DEC-191).
            const KeptShot *k = kept(shot->swingNumber());
            const DocumentStamp d = k && k->document.exists && !m_unread.contains(shot->swingNumber())
                                        ? k->document
                                        : (m_doors.reader ? m_doors.reader->stamp(shot->ref()) : DocumentStamp{});
            w.exists  = d.exists;
            w.size    = d.size;
            w.mtimeMs = d.mtimeMs;
            swings.push_back(std::move(w));
        }
    // The rule data's stamp hashes the whole pack and norms (~10 ms): taken once per RuleData snapshot, never per diagnosis
    // (the bench: the checklist's "no added per-shot GUI-thread cost").
    if (m_rules != m_contentStampOf) {
        m_contentStampOf = m_rules;
        m_contentStamp   = m_rules ? analysis::summaryRulesStamp(firingInputsOf(*m_rules)) : QString();
    }
    analysis::SummaryRules rules;
    rules.rules    = m_contentStamp;
    rules.baseline = analysis::clubBaselineStamp(m_baseline);
    const AthleteRecord rec = m_session && m_session->athlete() ? m_session->athlete()->record() : AthleteRecord();
    rules.cohort   = summaryCohortFacts(rec.dob, rec.sex);
    return analysis::summaryFingerprintOf(rules, std::move(swings));
}

QString SessionDiagnostics::summaryFingerprintNow(SessionRepository &sessions, const FiringReader *reader, const SessionId &id,
                                                 const QString &rulesStamp, const QString &cohort)
{
    std::vector<analysis::SummarySwing> swings;
    for (const ShotRef &ref : sessions.shots(id)) {
        analysis::SummarySwing w;
        w.folder = QFileInfo(sessions.swingFolder(ref)).fileName();
        const DocumentStamp d = reader ? reader->stamp(ref) : DocumentStamp{};
        w.exists  = d.exists;
        w.size    = d.size;
        w.mtimeMs = d.mtimeMs;
        swings.push_back(std::move(w));
    }
    analysis::SummaryRules rules;
    rules.rules    = rulesStamp;
    rules.baseline = analysis::clubBaselineStamp(frozenClubBaselineOf(sessions.readClubBaseline(id)));
    rules.cohort   = cohort;
    return analysis::summaryFingerprintOf(rules, std::move(swings));
}

analysis::SessionResults SessionDiagnostics::summary() const
{
    if (!m_diagnosis)
        return {};
    analysis::SessionResults r = m_diagnosis->results;
    r.dayReadings = m_dayReadings;   // refactor 5.5d (DEC-74): what another session's Charts and report read of this one
    r.gates       = m_gates;         // platform refactor P-3b: what the model browser's Gates view counts of this one
    r.derivedFrom.fingerprint  = m_summaryFingerprint;
    r.derivedFrom.contentStamp = m_findingsStamp;
    r.derivedFrom.gradePolicy  = m_snapshot ? m_snapshot->inputs.gradePolicy : QString();
    r.derivedFrom.writtenAtMs  = double(QDateTime::currentMSecsSinceEpoch());
    if (r.workOns.startMs <= 0) {
        // No shot carried a clock: the Session's own start (its one writer, J-24), else the start of its day.
        const auto e = m_sessions->open(m_id);
        if (e && e->startedAtUtc.isValid())  r.workOns.startMs = e->startedAtUtc.toMSecsSinceEpoch();
        else if (m_sessionDay.isValid())     r.workOns.startMs = m_sessionDay.startOfDay().toMSecsSinceEpoch();
        r.workOns.endMs = r.workOns.startMs;
    }
    return r;
}

// THE DERIVED STAMP (DEC-135; R-STAMP-1 for layer 3): everything a shot's derived data was computed from besides its
// own findings (whose stamp it carries) — the rest of the snapshot (firingStamp with the usuals, the cohort and the
// flight table neutral), the flight check's rule and thresholds (flightCheckRuleStamp; refactor 6.3a), the intent run,
// the reduction's rule version — then per shot its club's usual, the part of the flight table its club reads (its flight
// key: a flight-table edit re-judges exactly the shots it reaches) and the older copy of its word. A shot whose review
// moved has a new document stamp and fires anyway.
QString SessionDiagnostics::derivedBaseStamp() const
{
    if (!m_snapshot)
        return QString();
    analysis::FiringInputs in = m_snapshot->inputs;
    in.usuals.clear();
    in.cohort      = analysis::Cohort{};
    in.flightNorms = analysis::FlightNorms{};   // per shot, in its flight key
    return m_findingsStamp + QLatin1Char('|') + analysis::firingStamp(in) + QLatin1Char('|')
         + analysis::flightCheckRuleStamp() + QLatin1Char('|')
         + QString::number(m_intentRun.fromShot) + QLatin1Char(':') + m_intentRun.shape + QLatin1Char('|')
         + QString::number(analysis::kSessionReductionVersion)
         // The rules the summary's per-shot facts are made under (review 2 of 3.6: a bump of either re-derives them).
         + QLatin1Char('|') + QString::number(analysis::kTendencyRuleVersion) + QLatin1Char(':')
         + QString::number(analysis::kThemeRuleVersion) + QLatin1Char(':') + QString::number(analysis::kBoardAnnotationRuleVersion)
         // The athlete's hand, which a shot that records none is worded in (golferHand; refactor 6.3b fix round, B-3).
         + QLatin1Char('|') + m_athleteHandedness;
}

QString SessionDiagnostics::derivedStampOf(const QString &base, int swing, const ClubFlightUsual &usual,
                                           const QString &flightKey) const
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(base.toUtf8());
    const auto add = [&h](double v) { h.addData(std::isnan(v) ? QByteArrayLiteral("nan") : QByteArray::number(v, 'g', 17)); h.addData("|"); };
    add(usual.launch); add(usual.smash); add(usual.attack); add(usual.carry);
    h.addData(flightKey.toUtf8());
    h.addData("|");
    h.addData(m_legacyIntents.value(swing).toUtf8());
    h.addData(QJsonDocument(analysis::shotCorrectionsToJson({ { swing, m_legacyCorrections.value(swing) } }))
                  .toJson(QJsonDocument::Compact));
    return QString::fromLatin1(h.result().toHex().left(16));
}

const KeptShot *SessionDiagnostics::kept(int swingNumber) const
{
    const auto it = m_kept.constFind(swingNumber);
    return it == m_kept.constEnd() ? nullptr : &it.value();
}

QString SessionDiagnostics::flightKeyOf(const QString &club) const
{
    const QString c = canonicalClubId(club);
    if (const auto it = m_flightKeys.constFind(c); it != m_flightKeys.constEnd())
        return *it;
    const QString key = analysis::flightNormsStampFor(m_snapshot ? m_snapshot->inputs.flightNorms : analysis::FlightNorms{}, c);
    m_flightKeys.insert(c, key);
    return key;
}

bool SessionDiagnostics::derivedCurrent(const KeptShot &k) const
{
    return k.derived && k.findingsStamp == m_findingsStamp && k.derived->generation == m_derivedGeneration
        && sameUsual(k.derived->usual, m_usuals.value(canonicalClubId(k.findings.club)))
        && k.derived->flightKey == flightKeyOf(k.findings.club);
}

// THE SHOT'S FLIGHT CHECK (DEC-122; refactor 6.3a): the one its derived data holds when that is current; else, for a shot
// whose derive is in flight or failed, the one rule now — as every reader ran it before, so nothing they read changes.
ShotRecord SessionDiagnostics::checkedOf(const KeptShot &k) const
{
    if (derivedCurrent(k))
        return k.derived->checked;
    ShotRecord r = k.findings;
    analysis::checkShotAgainstFlight(r, m_usuals, m_snapshot ? m_snapshot->inputs.flightNorms : analysis::FlightNorms{});
    return r;
}

ShotRecord SessionDiagnostics::checkedRows(int swingNumber) const
{
    const KeptShot *k = kept(swingNumber);
    return k ? checkedOf(*k) : ShotRecord{};
}

// ── ShotDiagnostics, the Shot's view of what this owner keeps for it (refactor 4-b; shot.h) ──────────────────

bool ShotDiagnostics::fired() const
{
    return m_owner && m_owner->kept(m_swing);
}

bool ShotDiagnostics::hasLaunchMonitor() const
{
    const KeptShot *k = m_owner ? m_owner->kept(m_swing) : nullptr;
    return k && k->hasLaunchMonitor;
}

const analysis::ShotRecord *ShotDiagnostics::findings() const
{
    const KeptShot *k = m_owner ? m_owner->kept(m_swing) : nullptr;
    return k ? &k->findings : nullptr;
}

const analysis::ShotFactors *ShotDiagnostics::factors() const
{
    const KeptShot *k = m_owner ? m_owner->kept(m_swing) : nullptr;
    return k && k->derived ? &k->derived->data.factors : nullptr;
}

const analysis::ShotOutcomePack *ShotDiagnostics::outcomePack() const
{
    const auto snap = m_owner ? m_owner->snapshot() : nullptr;
    return snap && snap->outcomePack ? snap->outcomePack.get() : nullptr;
}

// ── The composition root's lookup (§4.4 step 6) ────────────────────────────────────────────────────────────

WorkTarget diagnosticsWorkTarget(Session *session, const WorkAddress &address)
{
    WorkTarget t;
    SessionDiagnostics *d = session ? session->diagnostics() : nullptr;
    if (!d)
        return t;   // no owner of that id loaded: OwnerGone
    t.instance = d->instance();
    if (address.item.isEmpty()) {
        t.itemFound = true;   // the owner's own work: its diagnosis (§4.4 step 7), the latest kept
        t.latestAsk = d->latestReduction();
    } else {
        const ShotRef ref = ShotRef::fromString(address.item);
        Shot *shot = (!ref.isNull() && ref.session() == d->sessionId()) ? session->shot(ref) : nullptr;
        t.itemFound = shot != nullptr;
        t.latestAsk = shot ? shot->latestAsk() : 0;
    }
    QPointer<SessionDiagnostics> to(d);
    t.deliver = [to](WorkVerdict v, WorkResult &&r) {
        if (to) to->deliver(v, std::move(r));
    };
    return t;
}

} // namespace pinpoint
