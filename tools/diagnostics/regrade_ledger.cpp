// regrade_ledger — re-reduce a session's diagnostics.json under the CURRENT pack, headlessly.
//
// The session diagnostics panel persists per-shot ROWS (diagnostic_ledger.h) and re-reduces the
// verdicts from them on review, so a gate that moves in a later build re-grades an old session
// honestly. What it does NOT do is re-run detection on shots already in the file: activateSession()
// reconciles the ledger against the swing_* directories and back-fills only the shots it has never
// reduced. So when the CONTENT moves — a norm mirrored, a measure retired to noProducer, a signal's
// tail swapped — every ledger already on disk keeps quoting the pack that wrote it, and the only
// way to see what the new pack says about an old session is to set the file aside and let the
// model back-fill every shot again. That is the whole of this tool:
//
//   1. diagnostics.json is read through its one reader (Export/session_diagnostics_files.h). One that
//      cannot be read, or that a newer build saved, is left exactly as it is and the session is SKIPPED.
//   2. diagnostics.json is COPIED to diagnostics.json.<tag> (default tag pre-regrade; never overwritten:
//      refuses if present) — SessionDiagnosticsFiles::backupDiagnostics, through the session's files. The
//      live file stays where it is: there is no moment without one.
//   3. THE GOLFER'S INPUT IS NOT IN THE CACHE (refactor step 2.5, audit H-04). It lives in session_input.json
//      and on each shot's review, which this tool never touches. A session written before step 2.5 still has
//      its input in diagnostics.json's older keys (intent, screens, shotIntent, intentFrom, corrections): the
//      model reads them as the fallback and every write of the file carries them over unchanged, so the
//      regrade keeps them without a seed (step 2.5 deleted the seed) — the lead's call J-33; migration M3
//      moves them. The tool checks it: the older keys after must equal those before, or it WARNs and exits 1.
//   4. SessionDiagnosticsModel::activateSession(dir), synchronously, with setRebuildCache(true): the file's rows
//      and session block are ignored (the older golfer keys are still read), so every shot is re-ingested and
//      the stage starts from Cold, and a fresh diagnostics.json is written after each one. If the model writes
//      no ledger (no swing_* folders, or every write failed) the original is still in place, untouched, and
//      the copy is removed.
//   0. A SESSION IN PROGRESS IS REFUSED (D11: DEC-158; H-14): one whose session.json says inProgress — being
//      recorded here or on the other machine — is SKIPPED untouched (exit 1), unless --allow-in-progress (for one
//      a crash left open).
//   5. one line per session on stdout: shots, stage, patterns, coverage, and what was carried —
//      with a WARN, and exit 1, if the rebuilt file does not say the same as the old one did
//
// It does NOT close the session — a session's End is its Session's (EVT-09), and a sweep is a
// re-reading of evidence, not a session ending — and it does
// not touch swing.json, pose, ball or shaft. No pose re-analysis is involved; the reduction reads
// the metrics already in each document.
//
// Content comes from the same environment the test suites use, so the sweep grades against the
// checkout rather than whatever the installed app carries:
//
//   export PINPOINT_CORE_PACK=src/Resources/diagnostics/core.json   (and NORMS, CONTEXTS, SCREENS,
//                                                                    DRILLS, REFERENCES likewise)
//   ./build/tests/Analysis/regrade_ledger --tag pre-headsway-fix /mnt/swingdata/corpus/swings/*/
//
// NOT A TEST, and not registered with ctest — it writes into the swing library.
//
// --dump <out.json> — THE PARITY DUMP (refactor Step G, build/refactor/step-G/BRIEF.md §2 item 2). After
// the regrade above, one JSON file over every session named, written by parity_json.h's canonical writer
// (keys sorted, numbers "%.17g", no timestamps, folders as "<session>"/"<athlete>") so two runs diff clean.
// Read off the SAME model the regrade just ran — its ledger, its shotOutcome() and its shotFactorsOf(),
// which are the panel's and the club mix's own code paths; nothing is re-derived here. Shape:
//
//   { "tool": "regrade_ledger --dump", "dumpVersion": 1,
//     "content":  { PINPOINT_CORE_<X>: sha1 of the file graded against },
//     "sessions": [ by athlete then session folder name:
//       { "athlete", "session", "status": "regraded"|"graded"|"skipped", "skipReason",
//         "stage", "shotCount", "patternCount", "patterns": [card ids, as the panel orders them],
//         "coverageLine", "contentStamp", "shotMix": {...}, "clubMix": [...], "carryToday": [...],
//         "conditions": [ by id: { "id", "drivingMeasureId", "tier", "assessable", "fired", "wilsonLower",
//                                  "pPattern", "recurrence", "firingExcess", "medianExcess",
//                                  "corridor": corridorOf(id) } ],
//         "shots": [ by shotId: { "shotId", "club", "contextId", "dataWarning", "handedness", "swingLength",
//                                 "rows": [ by conditionId: every ConditionRow field that grades — state,
//                                           value, corridorLo/Hi, z, corridorShape, firingSide, faultAt,
//                                           pFire, overruled, notAssessableReason, readings … ],
//                                 "outcome": shotOutcome(shotId),
//                                 "factors": { "strike"|"direction"|"flight": { read, values, faults,
//                                                                               method, rawValue } } } ] } ] }
//
// --engine old|new — RUN P, THE PARALLEL RUN OVER A SET (refactor step 3.5-ii; build/refactor/step-3/BRIEF.md §4.9,
// §5.3). `new` regrades each session through the NEW owner of its diagnostics instead of the old model: an Athlete
// over the session's folder (its record — the norm cohort's sex and date of birth, D6 — from --athletes-ini, a COPY of
// the app's settings, matched by the athlete folder's or the session folder's name), its Session, and that Session's
// SessionDiagnostics run headless (§4.4 step 11) with every shot fired again (DiagnosticsDoors::rebuild), which writes
// diagnostics.json (with each shot's derived data) and work_ons.json (the typed summary, DEC-192) into the session.
// `old` is the old model, as without the flag. With either, --dump writes the PARALLEL RUN'S FORM ("P", dumpVersion 2:
// src/Gui/diagnostics/diag_parallel_run.h — ids and numbers both engines own; no words, no view state), so
// tools/parity/parity_diff.py compares an `old` run with a `new` one. `new` also prints, per session, D7's check
// (DEC-172): the club baseline from the earlier sessions' SUMMARIES against the one from their LEDGERS.
//
// Dump mode needs every PINPOINT_CORE_* the diagnostics read (parity_json.h contentVariables(): the flight
// table and the gate words too, which a plain regrade can do without), because a dump graded without the
// flight table would read no flight on any shot and say so silently.

#include "Gui/diagnostics/session_diagnostics_model.h"
#include "Gui/diagnostics/diag_parallel_run.h"
#include "Coaching/session_results.h"
#include "Core/work_pool.h"
#include "DomainModel/athlete.h"
#include "DomainModel/rule_data.h"
#include "DomainModel/session.h"
#include "DomainModel/session_diagnostics.h"
#include "Export/firing_reader_store.h"
#include "Export/recording_loader.h"
#include "Export/swing_paths.h"
#include "Export/swing_store_repository.h"
#include "../../src/Export/rule_data_install.h"
#include "Export/session_diagnostics_files.h"
#include "Export/session_store.h"          // SessionStore::fileSaysInProgress (D11)
#include "Coaching/shot_factors.h"
#include "Core/PpMessageLog.h"
#include "parity_json.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

// SessionDiagnosticsModel is a QML_ELEMENT façade in the global namespace, as the app registers it.

namespace {

// The golfer's half of an envelope, in words: what must read the same before and after the regrade.
QString golferSummary(const QJsonObject &g)
{
    QStringList parts;
    const QJsonObject intent = g.value(QStringLiteral("intent")).toObject();
    const QString focus = intent.value(QStringLiteral("focusConditionId")).toString();
    if (!focus.isEmpty())
        parts << QStringLiteral("focus %1 from row %2").arg(focus).arg(intent.value(QStringLiteral("focusFromShot")).toInt(-1));
    const QString miss = intent.value(QStringLiteral("declaredMiss")).toString();
    if (!miss.isEmpty()) parts << QStringLiteral("miss %1").arg(miss);
    if (const auto n = g.value(QStringLiteral("screens")).toObject().size(); n > 0)
        parts << QStringLiteral("%1 screen answer(s)").arg(n);
    if (const auto n = g.value(QStringLiteral("shotIntent")).toObject().size(); n > 0)
        parts << QStringLiteral("%1 shot intent(s)").arg(n);
    const QJsonObject run = g.value(QStringLiteral("intentFrom")).toObject();
    if (run.value(QStringLiteral("fromShot")).toInt(-1) >= 0 && !run.value(QStringLiteral("shape")).toString().isEmpty())
        parts << QStringLiteral("%1 from shot %2").arg(run.value(QStringLiteral("shape")).toString())
                                                 .arg(run.value(QStringLiteral("fromShot")).toInt());
    if (const auto n = g.value(QStringLiteral("corrections")).toObject().size(); n > 0)
        parts << QStringLiteral("corrections on %1 shot(s)").arg(n);
    return parts.join(QStringLiteral(" · "));
}


// ── The parity dump (--dump) ─────────────────────────────────────────────────────────────────

using namespace pinpoint::analysis;

QString tierText(Tier t)
{
    switch (t) {
    case Tier::Pattern:  return QStringLiteral("pattern");
    case Tier::Watching: return QStringLiteral("watching");
    case Tier::Clean:    break;
    }
    return QStringLiteral("clean");
}

QVariantMap corridorMap(const SpreadCorridor &c)
{
    return QVariantMap{
        { QStringLiteral("known"),    c.known },
        { QStringLiteral("fromNorm"), c.fromNorm },
        { QStringLiteral("shape"),    corridorShapeToString(c.shape) },
        { QStringLiteral("lowOpen"),  c.lowOpen },
        { QStringLiteral("highOpen"), c.highOpen },
        { QStringLiteral("mu"),       c.mu },
        { QStringLiteral("sigmaLo"),  c.sigmaLo },
        { QStringLiteral("sigmaHi"),  c.sigmaHi },
        { QStringLiteral("idealLo"),  c.idealLo },
        { QStringLiteral("idealHi"),  c.idealHi },
        { QStringLiteral("signalLo"), c.signalLo },
        { QStringLiteral("signalHi"), c.signalHi },
        { QStringLiteral("faultLo"),  c.faultLo },
        { QStringLiteral("faultHi"),  c.faultHi },
    };
}

QVariantMap rowMap(const ConditionRow &r)
{
    QVariantList readings;
    for (const MeasureRow &m : r.readings)
        readings.append(QVariantMap{
            { QStringLiteral("measureId"),     m.measureId },
            { QStringLiteral("signalId"),      m.signalId },
            { QStringLiteral("value"),         m.value },
            { QStringLiteral("corridorLo"),    m.corridorLo },
            { QStringLiteral("corridorHi"),    m.corridorHi },
            { QStringLiteral("z"),             m.z },
            { QStringLiteral("corridorShape"), corridorShapeToString(m.corridorShape) },
            { QStringLiteral("firingSide"),    firingSideToString(m.firingSide) },
            { QStringLiteral("faultAt"),       m.faultAt },
            { QStringLiteral("fired"),         m.fired },
        });
    return QVariantMap{
        { QStringLiteral("conditionId"),         r.conditionId },
        { QStringLiteral("state"),               shotStateToString(r.state) },
        { QStringLiteral("confidence"),          double(r.confidence) },
        { QStringLiteral("direction"),           r.direction },
        { QStringLiteral("drivingMeasureId"),    r.drivingMeasureId },
        { QStringLiteral("value"),               r.value },
        { QStringLiteral("corridorLo"),          r.corridorLo },
        { QStringLiteral("corridorHi"),          r.corridorHi },
        { QStringLiteral("z"),                   r.z },
        { QStringLiteral("corridorShape"),       corridorShapeToString(r.corridorShape) },
        { QStringLiteral("firingSide"),          firingSideToString(r.firingSide) },
        { QStringLiteral("faultAt"),             r.faultAt },
        { QStringLiteral("allOf"),               r.allOf },
        { QStringLiteral("contextId"),           r.contextId },
        { QStringLiteral("contextInferred"),     r.contextInferred },
        { QStringLiteral("material"),            r.material },
        { QStringLiteral("sharp"),               r.sharp },
        { QStringLiteral("notAssessableReason"), r.notAssessableReason },
        { QStringLiteral("notJudgedGate"),       r.notJudgedGate },
        { QStringLiteral("notLiveStatus"),       r.notLiveStatus },
        { QStringLiteral("pFire"),               double(r.pFire) },
        { QStringLiteral("grossRisk"),           double(r.grossRisk) },
        { QStringLiteral("quantified"),          r.quantified },
        { QStringLiteral("firedSignalCount"),    r.firedSignalCount },
        { QStringLiteral("overruled"),           r.overruled },
        { QStringLiteral("readings"),            readings },
    };
}

QVariantMap factorMap(const ShotFactor &f)
{
    return QVariantMap{
        { QStringLiteral("read"),     f.read },
        { QStringLiteral("values"),   f.values },
        { QStringLiteral("faults"),   f.faults },
        { QStringLiteral("method"),   f.method },
        { QStringLiteral("rawValue"), f.rawValue },
    };
}

// One session as the model that just regraded it holds it.
QVariantMap sessionDump(const SessionDiagnosticsModel &m)
{
    QVariantMap out;
    out.insert(QStringLiteral("stage"),        m.stage());
    out.insert(QStringLiteral("shotCount"),    m.shotCount());
    out.insert(QStringLiteral("patternCount"), m.patternCount());
    out.insert(QStringLiteral("coverageLine"), m.coverageLine());
    out.insert(QStringLiteral("contentStamp"), m.contentStamp());
    QStringList patterns;
    for (const QVariant &v : m.cards()) {
        const QVariantMap card = v.toMap();
        patterns << card.value(QStringLiteral("id"), card.value(QStringLiteral("conditionId"))).toString();
    }
    out.insert(QStringLiteral("patterns"),   patterns);
    out.insert(QStringLiteral("shotMix"),    m.shotMix());
    out.insert(QStringLiteral("clubMix"),    m.clubMix());
    out.insert(QStringLiteral("carryToday"), m.carryToday());

    std::vector<const ConditionLedger *> ledgers;
    for (const ConditionLedger &l : m.ledgerRows()) ledgers.push_back(&l);
    std::sort(ledgers.begin(), ledgers.end(), [](const ConditionLedger *a, const ConditionLedger *b) { return a->id < b->id; });
    QVariantList conditions;
    for (const ConditionLedger *l : ledgers)
        conditions.append(QVariantMap{
            { QStringLiteral("id"),               l->id },
            { QStringLiteral("drivingMeasureId"), l->drivingMeasureId },
            { QStringLiteral("tier"),             tierText(l->tier) },
            { QStringLiteral("assessable"),       l->assessable },
            { QStringLiteral("fired"),            l->fired },
            { QStringLiteral("wilsonLower"),      l->wilsonLower },
            { QStringLiteral("pPattern"),         l->pPattern },
            { QStringLiteral("recurrence"),       l->recurrence },
            { QStringLiteral("firingExcess"),     l->firingExcess },
            { QStringLiteral("medianExcess"),     l->medianExcess },
            { QStringLiteral("corridor"),         corridorMap(m.corridorOf(l->id)) },
        });
    out.insert(QStringLiteral("conditions"), conditions);

    std::vector<const ShotRecord *> shots;
    for (const ShotRecord &s : m.shotRecords()) shots.push_back(&s);
    std::stable_sort(shots.begin(), shots.end(), [](const ShotRecord *a, const ShotRecord *b) { return a->shotId < b->shotId; });
    QVariantList shotList;
    for (const ShotRecord *s : shots) {
        std::vector<const ConditionRow *> rows;
        for (const ConditionRow &r : s->rows) rows.push_back(&r);
        std::stable_sort(rows.begin(), rows.end(), [](const ConditionRow *a, const ConditionRow *b) { return a->conditionId < b->conditionId; });
        QVariantList rowList;
        for (const ConditionRow *r : rows) rowList.append(rowMap(*r));
        const ShotFactors f = m.shotFactorsOf(s->shotId);
        shotList.append(QVariantMap{
            { QStringLiteral("shotId"),      s->shotId },
            { QStringLiteral("club"),        s->club },
            { QStringLiteral("contextId"),   s->contextId },
            { QStringLiteral("dataWarning"), s->dataWarning },
            { QStringLiteral("handedness"),  s->handedness },
            { QStringLiteral("swingLength"), s->swingLength },
            { QStringLiteral("rows"),        rowList },
            { QStringLiteral("outcome"),     m.shotOutcome(s->shotId) },
            { QStringLiteral("factors"),     QVariantMap{ { shotFactorStrike(),    factorMap(f.strike) },
                                                          { shotFactorDirection(), factorMap(f.direction) },
                                                          { shotFactorFlight(),    factorMap(f.flight) } } },
        });
    }
    out.insert(QStringLiteral("shots"), shotList);
    return out;
}

// One session's entry, its folders tokenised.
struct DumpEntry { QString athlete, session; QVariantMap body; };

DumpEntry dumpEntry(const QString &dir, const QString &status, QVariantMap body, const QString &skipReason = QString())
{
    parity::PathTokens tokens;
    parity::addPathToken(tokens, dir, QStringLiteral("<session>"));
    parity::addPathToken(tokens, QFileInfo(dir).absolutePath(), QStringLiteral("<athlete>"));
    body.insert(QStringLiteral("status"), status);
    if (!skipReason.isEmpty()) body.insert(QStringLiteral("skipReason"), skipReason);
    DumpEntry e;
    e.athlete = QFileInfo(QFileInfo(dir).absolutePath()).fileName();
    e.session = QFileInfo(dir).fileName();
    body.insert(QStringLiteral("athlete"), e.athlete);
    body.insert(QStringLiteral("session"), e.session);
    e.body = parity::scrub(body, tokens).toMap();
    return e;
}

// ── --engine new: the new owner, headless, over a scratch library ──────────────────────────────────────────────

struct AthleteSeen { pinpoint::AthleteId id; pinpoint::AthleteRecord record; };

// The record for a session's athlete from a copy of the app's settings ([athletes]/<uuid>/name, dob, sex): the one
// whose sanitised name is the athlete folder's name, else the one named in the session folder's name (the corpus).
AthleteSeen athleteFor(const QString &sessionDir, const QString &ini)
{
    using namespace pinpoint;
    const QString athleteFolder = QFileInfo(QFileInfo(sessionDir).absolutePath()).fileName();
    const QString sessionName   = QFileInfo(sessionDir).fileName();
    AthleteSeen out;
    if (!ini.isEmpty()) {
        QSettings s(ini, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("athletes"));
        QString match;
        for (const QString &uuid : s.childGroups()) {
            const QString token = SwingPaths::sanitise(s.value(uuid + QStringLiteral("/name")).toString());
            if (token.isEmpty()) continue;
            if (token == athleteFolder || sessionName.contains(QLatin1Char('_') + token + QLatin1Char('_'))) {
                match = uuid;
                if (token == athleteFolder) break;
            }
        }
        if (!match.isEmpty()) {
            out.id = AthleteId::fromString(match);
            out.record.name = s.value(match + QStringLiteral("/name")).toString();
            out.record.dob  = s.value(match + QStringLiteral("/dob")).toString();
            out.record.sex  = s.value(match + QStringLiteral("/sex")).toString();
        }
        s.endGroup();
    }
    if (out.id.isNull()) {   // no record: an athlete of the folder's name, with no cohort facts
        out.id = AthleteId::fromString(QStringLiteral("00000000-0000-4000-8000-%1")
                                           .arg(QString::fromLatin1(QCryptographicHash::hash(
                                                    athleteFolder.toUtf8(), QCryptographicHash::Sha1).toHex().left(12))));
        out.record.name = athleteFolder;
    }
    return out;
}

class NewEngine
{
public:
    // `parallelReader` (refactor 3.7-0): the app's parallel-run reader (pinpoint::parallel::doors's: parse a miss,
    // write no sidecar) in place of the default one, which writes the sidecars it rebuilds.
    explicit NewEngine(QString ini, bool parallelReader = false) : m_ini(std::move(ini))
    {
        m_store.setOsTrashForTest(false);   // a scratch library: nothing goes to the real Trash
        m_recordings = std::make_unique<pinpoint::SwingStoreRecordingRepository>(m_store);
        m_reader     = std::make_shared<pinpoint::SwingStoreRecordingReader>(m_store);
        m_firing     = parallelReader
                         ? pinpoint::parallel::doors(nullptr, nullptr, nullptr, m_store, *m_recordings).reader
                         : std::shared_ptr<const pinpoint::FiringReader>(
                               std::make_shared<const pinpoint::SwingStoreFiringReader>(m_store, *m_recordings));
        m_receiver   = std::make_unique<pinpoint::WorkReceiver>([this](const pinpoint::WorkAddress &a) {
            const pinpoint::SessionId id = pinpoint::SessionId::fromString(a.owner);
            for (auto &it : m_athletes)
                if (pinpoint::Session *s = it.second->loadedSession(id)) return pinpoint::diagnosticsWorkTarget(s, a);
            return pinpoint::diagnosticsWorkTarget(nullptr, a);
        });
    }
    ~NewEngine() { m_athletes.clear(); }

    pinpoint::Session *open(const QString &dir, AthleteSeen *who)
    {
        using namespace pinpoint;
        const AthleteSeen seen = athleteFor(dir, m_ini);
        if (who) *who = seen;
        const QString folder = QFileInfo(dir).absolutePath();
        const QString key = seen.id.toString() + QLatin1Char('|') + folder;
        auto it = m_athletes.find(key);
        if (it == m_athletes.end()) {
            auto a = std::make_unique<Athlete>(nullptr, &m_store, seen.id);
            a->setStored(seen.record, folder, SessionOwner{ seen.id, seen.record.name, QFileInfo(folder).absolutePath() },
                         Athlete::Source::Library, false);
            a->setShotDoors({ m_recordings.get(), m_reader });
            DiagnosticsDoors d;
            d.receiver     = m_receiver.get();
            d.ruleData     = RuleData::installed();
            d.reader       = m_firing;
            d.synchronous  = true;    // §4.4 step 11 (R-HL-2)
            d.persist      = true;    // into the session NAMED: a scratch copy (the tool's own warning)
            // with persist, work_ons.json (the typed summary, DEC-192) is written too: its one writer (step 3.6)
            d.rebuild      = true;    // every shot fired again, the ratchet replayed from Cold
            d.attach       = &SessionDiagnostics::attach;
            a->setDiagnosticsDoors(d);
            it = m_athletes.emplace(key, std::move(a)).first;
        }
        return it->second->sessionAt(dir);
    }
    void release(pinpoint::Session *s)
    {
        if (!s || !s->athlete()) return;
        s->athlete()->releaseSession(s->id());   // what it kept is written first; a sweep holds one at a time (Q-18)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    const pinpoint::SessionStore &store() const { return m_store; }

private:
    QString                                                     m_ini;
    pinpoint::SessionStore                                      m_store;
    std::unique_ptr<pinpoint::SwingStoreRecordingRepository>    m_recordings;
    std::shared_ptr<pinpoint::SwingStoreRecordingReader>        m_reader;
    std::shared_ptr<const pinpoint::FiringReader>               m_firing;
    std::unique_ptr<pinpoint::WorkReceiver>                     m_receiver;
    std::map<QString, std::unique_ptr<pinpoint::Athlete>>       m_athletes;   // destroyed before the receiver
};

// D7's check (DEC-172), per athlete: each session's earlier sessions, from their summaries and from their ledgers.
struct D7Session {
    QString athleteKey, name;
    pinpoint::analysis::SessionPlace place;
    std::vector<pinpoint::analysis::ClubWindow> windows;   // read back from the work_ons.json written
    pinpoint::analysis::ClubDistanceSession ledger;      // the rows as diagnostics.json holds them, the corrections
};

QString baselineText(const QHash<QString, pinpoint::analysis::ClubBaselineEntry> &b)
{
    QStringList keys = b.keys();
    keys.sort();
    QStringList parts;
    for (const QString &k : keys) {
        const auto &e = b.value(k);
        parts << QStringLiteral("%1 n%2 carry %3±%4 ball %5 speed %6 smash %7 p4 %8").arg(k).arg(e.n)
                     .arg(e.carryMean, 0, 'g', 12).arg(e.carrySd, 0, 'g', 12).arg(e.ballSpeedMean, 0, 'g', 12)
                     .arg(e.speedMean, 0, 'g', 12).arg(e.smashMean, 0, 'g', 12).arg(e.shaftAngleP4Mean, 0, 'g', 12);
    }
    return parts.join(QStringLiteral("; "));
}

void printUsage(FILE *to)
{
    std::fprintf(to,
        "usage: regrade_ledger [--tag <backup-suffix>] [--dump <out.json>] [--allow-in-progress] <session-dir>...\n"
        "\n"
        "Re-reduces each session's diagnostics.json under the CURRENT pack (PINPOINT_CORE_* at the checkout's\n"
        "src/Resources/diagnostics/*.json). The old file is kept as diagnostics.json.<tag> (default pre-regrade;\n"
        "refuses if that exists). WRITES INTO THE SESSION: run it on scratch copies unless a library regrade\n"
        "is what you mean.\n"
        "\n"
        "  --tag <suffix>     the backup's suffix\n"
        "  --dump <out.json>  after the regrade, write the PARITY DUMP: per session the ledger (every condition's\n"
        "                     tier and corridor, every shot's rows by condition id), and per shot the composed\n"
        "                     outcome and its three factors (strike, direction, flight), read off the model's own\n"
        "                     code paths. Canonical JSON (keys sorted, %%.17g numbers, no timestamps, folders as\n"
        "                     <session>/<athlete>), so two runs diff byte for byte. Needs every PINPOINT_CORE_*\n"
        "                     (PACK NORMS FLIGHT_NORMS CONTEXTS SCREENS DRILLS GATE_WORDS REFERENCES).\n"
        "  --allow-in-progress  also regrade a session whose session.json says it is in progress (one a crash\n"
        "                     left open); without it such a session is skipped untouched (D11)\n"
        "  --engine old|new   RUN P (refactor 3.5-ii): `new` regrades through the new owner (SessionDiagnostics,\n"
        "                     headless), writing diagnostics.json and work_ons.json; `old` the old model. With\n"
        "                     either, --dump writes the parallel run's form (dumpVersion 2) for parity_diff.py\n"
        "  --athletes-ini <f> (with --engine new) a COPY of the app's settings: each athlete's sex and date of\n"
        "                     birth for the norm cohort (D6), matched by the athlete folder's or session's name\n"
        "  --parallel-reader  (with --engine new; refactor 3.7-0) read the shots with the app's parallel-run reader\n"
        "                     (parallel::doors): a missing or stale sidecar is parsed and NO sidecar is written\n"
        "  --help             this text\n");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The packs and norms: this process's one RuleData, over the files the PINPOINT_CORE_* seams name
    // (refactor step 3.2). Made here, on the main thread, before anything grades.
    pinpoint::installProcessRuleData();
    // A headless tool echoes the app log (PpMessageLog.h): the model reports through ppWarn — an
    // unreadable club baseline, a ledger set aside — and nothing else in this process reads it.
    PpMessageLog::instance()->setEchoToStderr(true);

    QString     tag = QStringLiteral("pre-regrade");
    QString     dumpPath;
    QStringList dirs;
    bool        allowInProgress = false;
    QString     engine;          // "", "old", "new" (run P)
    QString     athletesIni;
    bool        parallelReader = false;   // 3.7-0: the app's parallel-run reader (--parallel-reader)
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--engine") && i + 1 < argc) { engine = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a.startsWith(QLatin1String("--engine="))) { engine = a.mid(9); continue; }
        if (a == QLatin1String("--athletes-ini") && i + 1 < argc) { athletesIni = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--parallel-reader")) { parallelReader = true; continue; }
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) { printUsage(stdout); return 0; }
        if (a == QLatin1String("--tag") && i + 1 < argc) { tag = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--dump") && i + 1 < argc) { dumpPath = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--allow-in-progress")) { allowInProgress = true; continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "regrade_ledger: unknown option %s\n", argv[i]);
            printUsage(stderr);
            return 2;
        }
        dirs << a;
    }
    if (dirs.isEmpty() || (!engine.isEmpty() && engine != QLatin1String("old") && engine != QLatin1String("new"))) {
        printUsage(stderr);
        return 2;
    }
    const bool newEngine = engine == QLatin1String("new");
    std::unique_ptr<NewEngine> ng;
    if (newEngine) ng = std::make_unique<NewEngine>(athletesIni, parallelReader);
    std::vector<D7Session> d7;
    if (!dumpPath.isEmpty()) {
        if (const QStringList missing = parity::missingContent(); !missing.isEmpty()) {
            std::fprintf(stderr, "regrade_ledger: --dump needs every content file; unset or unreadable: %s\n",
                         qPrintable(missing.join(QLatin1Char(' '))));
            return 2;
        }
    }
    std::vector<DumpEntry> dump;
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::fprintf(stderr, "regrade_ledger: PINPOINT_CORE_PACK is unset — export PINPOINT_CORE_{PACK,NORMS,"
                             "CONTEXTS,SCREENS,DRILLS,REFERENCES} at src/Resources/diagnostics/*.json so the "
                             "sweep grades against the checkout, not an empty pack.\n");
        return 2;
    }

    int failures = 0;
    for (const QString &raw : dirs) {
        const QString dir = QDir(raw).absolutePath();
        if (!QDir(dir).exists()) {
            std::printf("%-48s SKIP  no such directory\n", qPrintable(QFileInfo(dir).fileName()));
            dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("no such directory")));
            ++failures;
            continue;
        }
        using pinpoint::DiagnosticsRead;
        using pinpoint::SessionDiagnosticsFiles;
        const QString name = QFileInfo(dir).fileName();
        if (!allowInProgress && pinpoint::SessionStore::fileSaysInProgress(dir)) {
            std::printf("%-48s SKIP  %s\n", qPrintable(name), qPrintable(pinpoint::SessionStore::inProgressRefusal(dir)));
            dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("in progress (D11)")));
            ++failures;
            continue;
        }
        bool hadLedger = false;
        QString backup;
        QJsonObject carried;          // the older golfer keys of the file, as they stood (J-33)
        const DiagnosticsRead before = SessionDiagnosticsFiles::readDiagnostics(dir);
        if (before.exists()) {
            if (before.state != DiagnosticsRead::State::Read) {
                const QString why = before.state == DiagnosticsRead::State::Newer
                                        ? QStringLiteral("diagnostics.json was %1").arg(before.error)
                                        : QStringLiteral("diagnostics.json cannot be read (%1)").arg(before.error);
                std::printf("%-48s SKIP  %s — left as it is\n", qPrintable(name), qPrintable(why));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {},
                                         before.state == DiagnosticsRead::State::Newer
                                             ? QStringLiteral("diagnostics.json is a newer build's")
                                             : QStringLiteral("diagnostics.json cannot be read")));
                ++failures;
                continue;
            }
            const pinpoint::WriteResult b = SessionDiagnosticsFiles::backupDiagnostics(dir, tag, &backup);
            if (!b.ok()) {
                const bool exists = QFileInfo::exists(backup);
                std::printf("%-48s SKIP  %s\n", qPrintable(name),
                            exists ? qPrintable(QStringLiteral("%1 already exists — pick another --tag").arg(QFileInfo(backup).fileName()))
                                   : qPrintable(QStringLiteral("could not back the ledger up (%1)").arg(b.error)));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {},
                                         exists ? QStringLiteral("backup already exists")
                                                : QStringLiteral("could not back the ledger up")));
                ++failures;
                continue;
            }
            hadLedger = true;
            carried = before.envelope.legacyGolferInput;
        }

        if (newEngine) {
            // ── The new owner (run P) ──
            AthleteSeen who;
            pinpoint::Session *s = ng->open(dir, &who);
            pinpoint::SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
            if (!d) {
                std::printf("%-48s SKIP  the new owner could not open the session\n", qPrintable(name));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("the new owner could not open it")));
                ++failures;
                continue;
            }
            d->flush();
            const pinpoint::analysis::SessionDiagnosis *dg = d->diagnosis();
            std::printf("%-48s %s  shots %2d  stage %-11s patterns %2d  (new owner: %llu asks, %llu reductions, %lld ms, "
                        "cohort %s %s)\n",
                        qPrintable(name), hadLedger ? "REGRADED" : "GRADED  ", int(d->keptShots().size()),
                        dg ? qPrintable(pinpoint::parallel::formOf(*d).value(QStringLiteral("stage")).toString()) : "-",
                        dg ? int(dg->patterns.size()) : 0, (unsigned long long)d->asks().size(),
                        (unsigned long long)d->counts().reductions, (long long)d->lastReductionMs(),
                        who.record.sex.isEmpty() ? "-" : qPrintable(who.record.sex),
                        who.record.dob.isEmpty() ? "-" : qPrintable(who.record.dob));
            if (dg && !dg->patterns.isEmpty())
                std::printf("%-48s   patterns: %s\n", "", qPrintable(dg->patterns.join(QLatin1Char(' '))));
            const DiagnosticsRead now = SessionDiagnosticsFiles::readDiagnostics(dir);
            const bool wrote = d->counts().writes > 0 && now.parsed() && !now.envelope.ledger.isEmpty();
            if (!wrote) {
                std::printf("%-48s WARN  no diagnostics.json was written by the new owner\n", qPrintable(name));
                ++failures;
            } else if (!carried.isEmpty() && now.envelope.legacyGolferInput != carried) {
                std::printf("%-48s WARN  the golfer's input did not survive the new owner's write\n", qPrintable(name));
                ++failures;
            }
            // D7: what the summary written holds, read back through its one reader, and the ledger it sits beside.
            {
                D7Session e;
                e.athleteKey = who.id.toString() + QLatin1Char('|') + QFileInfo(dir).absolutePath();
                e.name = name;
                const auto entry = ng->store().open(s->id());
                e.place = pinpoint::analysis::SessionPlace{ name, entry ? entry->identity.localDay() : QDate(), 0 };
                bool ok = false;
                const pinpoint::analysis::SessionResults back = pinpoint::analysis::sessionResultsFromJson(
                    SessionDiagnosticsFiles::readWorkOns(dir).root, pinpoint::analysis::SessionResultsRead::Strict, &ok);
                if (!ok || !back.hasGrowth)
                    std::printf("%-48s WARN  work_ons.json does not read back with its DEC-192 keys\n", qPrintable(name));
                e.windows = back.clubWindows;
                e.ledger.folder = name;
                e.ledger.day    = e.place.day;
                e.ledger.shots  = now.parsed() ? pinpoint::analysis::fromJson(now.envelope.ledger)
                                               : std::vector<pinpoint::analysis::ShotRecord>{};
                for (const int swing : d->keptShots())
                    if (const pinpoint::KeptShot *k = d->kept(swing); k && k->derived && !k->derived->corrections.empty())
                        e.ledger.corrections.insert(swing, k->derived->corrections);
                d7.push_back(std::move(e));
            }
            if (!dumpPath.isEmpty()) {
                QVariantMap body = pinpoint::parallel::formOf(*d).toVariantMap();
                body.insert(QStringLiteral("ledgerWritten"), wrote);
                dump.push_back(dumpEntry(dir, hadLedger ? QStringLiteral("regraded") : QStringLiteral("graded"), body));
            }
            ng->release(s);
            continue;
        }

        SessionDiagnosticsModel m;
        m.setSynchronous(true);
        m.setCadence(QStringLiteral("everyShot"));
        m.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);   // a tool: no club_baseline.json
        m.setRebuildCache(true);      // every shot graded again; the older golfer keys still read and carried
        m.activateSession(dir);

        std::printf("%-48s %s  shots %2d  stage %-11s patterns %2d  %s\n",
                    qPrintable(QFileInfo(dir).fileName()),
                    hadLedger ? "REGRADED" : "GRADED  ",
                    m.shotCount(), qPrintable(m.stage()), m.patternCount(),
                    qPrintable(m.coverageLine()));
        QStringList patterns;
        for (const QVariant &v : m.cards()) {
            const QVariantMap card = v.toMap();
            const QString id = card.value(QStringLiteral("id"),
                                          card.value(QStringLiteral("conditionId"))).toString();
            if (!id.isEmpty()) patterns << id;
        }
        if (!patterns.isEmpty())
            std::printf("%-48s   patterns: %s\n", "", qPrintable(patterns.join(QLatin1Char(' '))));
        // Did the model write a ledger? Its write attempts are counted, not its successes, so the file
        // itself is asked.
        const DiagnosticsRead now = SessionDiagnosticsFiles::readDiagnostics(dir);
        const bool modelWrote = m.persistCount() > 0 && now.parsed() && !now.envelope.ledger.isEmpty()
                             && (!hadLedger || now.envelope.session != before.envelope.session);
        if (!modelWrote) {
            QString what = QStringLiteral("no diagnostics.json was written (no swing_* directories?)");
            if (hadLedger) {
                // Nothing was regraded: the session keeps the file it had, which never moved; the copy goes.
                what += QFile::remove(backup) ? QStringLiteral(" — the original is kept as it was")
                                              : QStringLiteral(" — the original is kept as it was; its copy %1 is left")
                                                    .arg(QFileInfo(backup).fileName());
            }
            std::printf("%-48s WARN  %s\n", qPrintable(name), qPrintable(what));
        } else if (!carried.isEmpty()) {
            // The older golfer keys the rebuilt file carries, against what the old one held: they must be the same.
            const QString summary = golferSummary(carried);
            if (!summary.isEmpty()) std::printf("%-48s   carried: %s\n", "", qPrintable(summary));
            if (now.envelope.legacyGolferInput != carried) {
                const QString after = golferSummary(now.envelope.legacyGolferInput);
                std::printf("%-48s WARN  the golfer's input did not survive: now %s — the old file is %s\n",
                            qPrintable(name), after.isEmpty() ? "nothing" : qPrintable(after),
                            qPrintable(QFileInfo(backup).fileName()));
                ++failures;
            }
        }
        if (!dumpPath.isEmpty()) {
            QVariantMap body = engine.isEmpty() ? sessionDump(m) : pinpoint::parallel::formOf(m).toVariantMap();
            body.insert(QStringLiteral("ledgerWritten"), modelWrote);
            dump.push_back(dumpEntry(dir, hadLedger ? QStringLiteral("regraded") : QStringLiteral("graded"), body));
        }
    }

    // D7's check (DEC-172; run P case 7): for every session, the club baseline as of its day from the earlier
    // sessions' summaries, against the one from their ledgers (clubBaselineAsOf, today's freeze path).
    if (newEngine) {
        const pinpoint::analysis::FlightNorms flight = pinpoint::RuleData::installed() && pinpoint::RuleData::installed()->snapshot()->flightNorms
                                                           ? *pinpoint::RuleData::installed()->snapshot()->flightNorms
                                                           : pinpoint::analysis::FlightNorms{};
        for (const D7Session &me : d7) {
            std::vector<pinpoint::analysis::SummaryClubWindows> summaries;
            std::vector<pinpoint::analysis::ClubDistanceSession> ledgers;
            for (const D7Session &o : d7) {
                if (o.athleteKey != me.athleteKey) continue;
                summaries.push_back(pinpoint::analysis::SummaryClubWindows{ o.place, o.windows });
                ledgers.push_back(o.ledger);
            }
            const auto fromSummaries = pinpoint::analysis::clubBaselineFromSummaries(summaries, me.place.day);
            const auto fromLedgers   = pinpoint::analysis::clubBaselineAsOf(ledgers, me.place.day, flight);
            const QString a = baselineText(fromSummaries), b = baselineText(fromLedgers);
            if (a == b) std::printf("D7  %-44s baseline from summaries == from ledgers (%d club(s))\n", qPrintable(me.name), int(fromSummaries.size()));
            else {
                std::printf("D7  %-44s DIFFERS\n      summaries: %s\n      ledgers:   %s\n", qPrintable(me.name), qPrintable(a), qPrintable(b));
                ++failures;
            }
        }
    }

    if (!dumpPath.isEmpty()) {
        // By athlete then session folder: the order the dirs were named in is the caller's, not the data's.
        std::stable_sort(dump.begin(), dump.end(), [](const DumpEntry &a, const DumpEntry &b) {
            return a.athlete != b.athlete ? a.athlete < b.athlete : a.session < b.session;
        });
        QVariantList sessions;
        for (const DumpEntry &e : dump) sessions.append(e.body);
        const QVariantMap root{
            { QStringLiteral("tool"),        QStringLiteral("regrade_ledger --dump") },
            { QStringLiteral("dumpVersion"), engine.isEmpty() ? 1 : 2 },
            { QStringLiteral("form"),        engine.isEmpty() ? QStringLiteral("v1") : QStringLiteral("P") },
            { QStringLiteral("engine"),      engine.isEmpty() ? QStringLiteral("old") : engine },
            { QStringLiteral("content"),     parity::contentHashes() },
            { QStringLiteral("sessions"),    sessions },
        };
        QString why;
        if (!parity::writeFile(dumpPath, parity::toCanonicalJson(root), &why)) {
            std::fprintf(stderr, "regrade_ledger: could not write the dump %s (%s)\n", qPrintable(dumpPath), qPrintable(why));
            return 1;
        }
        std::printf("dump: %d session(s) -> %s\n", int(dump.size()), qPrintable(dumpPath));
    }
    return failures == 0 ? 0 : 1;
}
