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
//   4. The regrade itself. By default (`--engine new`, since refactor 5.3) each session's own SessionDiagnostics,
//      headless, every shot fired again (below). `--engine old` (until step 5.4b deletes it):
//      SessionDiagnosticsModel::activateSession(dir), synchronously, with setRebuildCache(true) — the file's rows and
//      session block are ignored (the older golfer keys are still read), so every shot is re-ingested and the stage
//      starts from Cold, and a fresh diagnostics.json is written after each one; it writes no club_baseline.json and no
//      work_ons.json. Either way, a session with no swing_* folders, or whose every write failed, keeps its original
//      untouched.
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
//   rsync -a --exclude '*.mp4' --exclude '*.f32' --exclude '*.raw' --exclude '*.jpg'
//         /mnt/swingdata/corpus/swings/ /mnt/swingdata/scratch/<label>/swings/   (one line: a scratch copy without the
//         video, athletes.json with it; then each file's modification time restored from the original, as
//         tools/session-report-mock/fixtures.sh does, since the share keeps whole seconds)
//   ./build/tests/Analysis/regrade_ledger --tag pre-headsway-fix /mnt/swingdata/scratch/<label>/swings/*/
//
// (The new engine, the default: each session's athlete must be known, see below. The corpus's golfers are in
// corpus/swings/athletes.json.)
//
// NOT A TEST, and not registered with ctest — it writes into the sessions it is given. One ctest drives its refusals,
// which write nothing, and its athlete sources over a scratch folder: regrade_ledger_new_needs_ini_test (a session whose
// athlete no source names is refused, with and without --engine; 3.10 review B1, re-worded in refactor 4-c and 5.3).
//
// --dump <out.json> — THE PARITY DUMP (refactor Step G; since 3.5-ii the parallel run's form). After the regrade above,
// one JSON file over every session named, written by parity_json.h's canonical writer (keys sorted, numbers "%.17g", no
// timestamps, folders as "<session>"/"<athlete>") so two runs diff clean, in THE PARALLEL RUN'S FORM ("P",
// dumpVersion 2: src/Gui/diagnostics/diag_parallel_run.h — ids and numbers both engines own; no words, no view state),
// read off the engine that just ran. tools/parity/parity_diff.py compares two dumps (parity_baseline.py's diagnostics
// half, which runs --engine new since 3.10, and an `old` run against a `new` one: run P). Until
// 3.10 a plain regrade dumped the old engine's own form v1 (every ConditionRow field, the composed outcome's words, the
// card order, the coverage line); 3.10 deleted that form (J-59), so a v1 baseline does not compare with a dump from
// this build.
//
// --engine new|old — new (the default since refactor 5.3)[^1]: each session's own SessionDiagnostics, headless, every shot
// fired again (DiagnosticsDoors::rebuild), writing diagnostics.json (each shot's findings and derived data) and
// work_ons.json (the typed summary, DEC-192), and freezing each session's club_baseline.json at first need from ITS
// ATHLETE'S earlier sessions' summaries (DEC-46, DEC-172) — a recorded input, never rewritten once current. So `new`
// must tell the golfers apart (and know each one's sex and date of birth for the norm cohort, D6). THE ATHLETE COMES
// FROM THE LIBRARY (refactor 4-c, J-61): the session's athlete folder's athlete.json (FILE-ATH-01), read through its
// owner, AthleteRepository (AthleteStore over the folder that holds the athlete folder, as the app reads it); the
// Session is that store's Athlete's. A folder with no athlete.json — the corpus, whose sessions from several golfers sit
// in one folder — is read from athletes.json BESIDE THE SESSION (`pinpoint.athletes/1`; athleteFromGolferFile below):
// the entry whose name is in the session folder's name; such a session freezes NO club baseline (NoFreezeSessions below:
// its Athlete finds no earlier session of its own in a flat folder, and a frozen baseline is never recomputed), and the
// tool prints "club baseline not frozen: corpus run". --athletes-ini <a COPY of the app's settings> stays only as the
// last FALLBACK, for a library M1 has not migrated: its [athletes] record matched by the athlete folder's or the session
// folder's name. This machine's own settings are never read for a record. A SESSION WHOSE ATHLETE NO SOURCE NAMES IS
// REFUSED (SKIP, exit 1, nothing read past its folder, nothing written): without a record every session of a folder
// such as corpus/swings would be one athlete, and one golfer's club usuals would be frozen into another's sessions
// (3.10 review B1), so a golfer is never pooled. Until 4-c, `new` refused to run without the ini and named a session the
// ini did not know after its folder; since the 22 Nov migrations the ini holds no athletes, so that pooled every one.
// `new` also prints, per session, D7's check: the club baseline from the
// earlier sessions' SUMMARIES against the one from their LEDGERS (refactor 3.5-ii; build/refactor/step-3/BRIEF.md §4.9,
// §5.3). old: the old engine above, run P's reference until step 5.4b deletes it.
//
// [^1]: Until refactor 5.3 the default was `old` (J-59: the old engine stayed run P's reference to step 5, after M1).
// [^alias]: Refactor 5.3's review (REVIEW-5.3 R1) found that nine corpus sessions' swings record a second id for one golfer,
// `52ff45d8-…`, not the library's `b389a703-…`; refactor 5.3c recorded it as that golfer's alias, 22 Nov 2026.
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
#include "Export/swing_store.h"        // SwingStore::loadParts: the recorded athlete id (5.3c)
#include "Export/swing_store_repository.h"
#include "../../src/Export/rule_data_install.h"
#include "Export/session_diagnostics_files.h"
#include "Export/session_store.h"          // SessionStore::fileSaysInProgress (D11)
#include "Export/athlete_store.h"          // the athletes, from the library (refactor 4-c, J-61)
#include "Coaching/shot_factors.h"
#include "Core/PpMessageLog.h"
#include "parity_json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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

// Whose a session is: the athlete's id and record, and where they were read (`from`: "athlete.json" or "ini").
// `recorded`: for a golfer file's golfer, what the session's swings record of the id when it is not the entry's (an alias
// of it, or an id the file does not list), in words; empty otherwise.
struct AthleteSeen { pinpoint::AthleteId id; pinpoint::AthleteRecord record; QString from; QString recorded; };

// THE FALLBACK (an unmigrated library): the record for a session's athlete from a copy of the app's settings
// ([athletes]/<uuid>/name, dob, sex): the one whose sanitised name is the athlete folder's name, else the one named in
// the session folder's name (the corpus). A null id: the ini names no record for it (the caller refuses the session).
AthleteSeen athleteFromIni(const QString &sessionDir, const QString &ini)
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
    return out;   // no record: a null id — never an athlete of the folder's name (a golfer is never pooled)
}

// The athlete ids a session's swings record: each swing document's `athlete.uuid`, the header its capture wrote (the
// document's root alone, no chunk read). Read only; for the golfer file's alias check below (refactor 5.3c).
QStringList recordedAthleteIds(const QString &sessionDir)
{
    using namespace pinpoint;
    QStringList out;
    for (const QString &w : SwingPaths::swingFolders(sessionDir)) {
        const AthleteId id = AthleteId::fromString(SwingStore::loadParts(QDir(sessionDir).filePath(w), {})
                                                       .value(QStringLiteral("athlete")).toObject()
                                                       .value(QStringLiteral("uuid")).toString());
        if (!id.isNull() && !out.contains(id.toString())) out << id.toString();
    }
    out.sort();
    return out;
}

// THE GOLFER FILE BESIDE THE SESSION (refactor 5.3): `athletes.json` in the folder that holds the session, for a folder
// of several golfers' sessions that has no athlete.json (the corpus, `corpus/swings/`). Schema `pinpoint.athletes/1`:
//
//   { "schema": "pinpoint.athletes/1",
//     "athletes": [ { "athleteId": "<uuid>", "name": "Nick Henn", "sex": "male", "dob": "",
//                     "aliases": [ "<uuid>", … ] (optional) }, … ] }
//
// `aliases` (refactor 5.3c; REVIEW-5.3 R1)[^alias]: other ids the golfer's swings record, captured under another machine's
// record of the same golfer. The entry is still matched by name and the session graded as `athleteId`; an alias only
// tells the tool that swings recording it are the same golfer, and the graded line says so. A recorded id that is
// neither the entry's nor an alias is said too (not refused: the name decides, as before).
//
// An entry names a session when its sanitised name (SwingPaths::sanitise: "Nick-Henn") is the athlete folder's name, or
// sits between underscores in the session folder's name (2026-10-20_Nick-Henn_Wrist_01): the ini fallback's rule. A null
// id when the file is absent, unreadable, a newer schema's, names no entry for the session, or names more than one — the
// caller then refuses the session. Read only.
AthleteSeen athleteFromGolferFile(const QString &sessionDir, QString *problem = nullptr)
{
    using namespace pinpoint;
    const QString athleteDir    = QFileInfo(sessionDir).absolutePath();
    const QString athleteFolder = QFileInfo(athleteDir).fileName();
    const QString sessionName   = QFileInfo(sessionDir).fileName();
    const QString path          = QDir(athleteDir).filePath(QStringLiteral("athletes.json"));
    AthleteSeen out;
    QFile f(path);
    if (!f.exists()) return out;
    QJsonParseError pe;
    const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &pe) : QJsonDocument();
    const QJsonObject root = doc.object();
    const QString schema = root.value(QStringLiteral("schema")).toString();
    if (!doc.isObject() || schema != QLatin1String("pinpoint.athletes/1")) {
        if (problem)
            *problem = schema.startsWith(QLatin1String("pinpoint.athletes/"))
                ? QStringLiteral("athletes.json is %1, a newer build's").arg(schema)
                : QStringLiteral("athletes.json cannot be read");
        return out;
    }
    QList<QJsonObject> matches;
    for (const QJsonValue &v : root.value(QStringLiteral("athletes")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString token = SwingPaths::sanitise(e.value(QStringLiteral("name")).toString());
        if (token.isEmpty() || AthleteId::fromString(e.value(QStringLiteral("athleteId")).toString()).isNull()) continue;
        if (token == athleteFolder || sessionName.contains(QLatin1Char('_') + token + QLatin1Char('_')))
            matches << e;
    }
    if (matches.size() > 1) {
        if (problem) *problem = QStringLiteral("athletes.json names %1 golfers for it").arg(matches.size());
        return out;
    }
    if (matches.size() == 1) {
        const QJsonObject &e = matches.first();
        out.id          = AthleteId::fromString(e.value(QStringLiteral("athleteId")).toString());
        out.record.name = e.value(QStringLiteral("name")).toString();
        out.record.sex  = e.value(QStringLiteral("sex")).toString();
        out.record.dob  = e.value(QStringLiteral("dob")).toString();
        out.from        = QStringLiteral("athletes.json");
        QStringList aliases;
        for (const QJsonValue &v : e.value(QStringLiteral("aliases")).toArray())
            if (const AthleteId a = AthleteId::fromString(v.toString()); !a.isNull()) aliases << a.toString();
        QStringList viaAlias, unlisted;
        for (const QString &r : recordedAthleteIds(sessionDir))
            if (r != out.id.toString()) (aliases.contains(r) ? viaAlias : unlisted) << r;
        QStringList words;
        if (!viaAlias.isEmpty())
            words << QStringLiteral("its swings record %1, an alias of %2 in athletes.json: the same golfer")
                         .arg(viaAlias.join(QStringLiteral(", ")), out.record.name);
        if (!unlisted.isEmpty())
            words << QStringLiteral("its swings record %1, which athletes.json does not list for %2 (matched by name)")
                         .arg(unlisted.join(QStringLiteral(", ")), out.record.name);
        out.recorded = words.join(QStringLiteral("; "));
    }
    return out;
}

// A CORPUS RUN FREEZES NO CLUB BASELINE (refactor 5.3, review fix). A session whose athlete came from athletes.json sits in
// a folder of several golfers' sessions (corpus/swings), where its Athlete finds no earlier session of its own: its
// index is its name's folder under the library (SessionStore::athleteFolder), which a flat folder is not. A baseline
// frozen there would be empty, and a frozen baseline is never computed again (DEC-46), so the session's usual numbers
// would be lost for good. So such a session's repository is the store with its club baseline closed: an existing
// club_baseline.json is read as it is; a missing one reads as present but not usable (SessionDiagnostics then grades
// with no baseline, as a session without one, and freezes nothing); a write is refused. Everything else is the store.
class NoFreezeSessions final : public pinpoint::SessionRepository
{
public:
    explicit NoFreezeSessions(pinpoint::SessionStore &s) : m(s) {}
    using Id = pinpoint::SessionId;
    pinpoint::SessionStartResult start(const pinpoint::SessionOwner &o, const QString &t, const QString &n) override { return m.start(o, t, n); }
    Id todaysSession(const pinpoint::SessionOwner &o, const QString &t) override { return m.todaysSession(o, t); }
    pinpoint::WriteResult resume(const Id &id, const QString &t) override { return m.resume(id, t); }
    pinpoint::WriteResult end(const Id &id) override { return m.end(id); }
    QList<pinpoint::SessionIndexEntry> enumerate(const pinpoint::SessionOwner &o) override { return m.enumerate(o); }
    std::optional<pinpoint::SessionIndexEntry> open(const Id &id) const override { return m.open(id); }
    Id openFolder(const QString &f, const pinpoint::AthleteId &a = {}) override { return m.openFolder(f, a); }
    pinpoint::ShotRef openSwingFolder(const QString &f, const pinpoint::AthleteId &a = {}) override { return m.openSwingFolder(f, a); }
    pinpoint::SwingAllocation allocateSwing(const Id &id) override { return m.allocateSwing(id); }
    QList<pinpoint::ShotRef> shots(const Id &id) const override { return m.shots(id); }
    QString swingFolder(const pinpoint::ShotRef &r) const override { return m.swingFolder(r); }
    bool folderPresent(const Id &id) const override { return m.folderPresent(id); }
    pinpoint::SessionArchiveResult archive(const Id &id, const pinpoint::SessionArchiveOptions &o,
                                           const pinpoint::SessionArchiveProgress &p = {}) override { return m.archive(id, o, p); }
    pinpoint::SessionArchiveResult restore(const Id &id, const pinpoint::SessionArchiveProgress &p = {}) override { return m.restore(id, p); }
    pinpoint::SessionArchiveJob archiveJob(const Id &id, const pinpoint::SessionArchiveOptions &o) override { return m.archiveJob(id, o); }
    void archiveLanded(const Id &id) override { m.archiveLanded(id); }
    pinpoint::WriteResult trash(const Id &id, QString *w = nullptr) override { return m.trash(id, w); }
    pinpoint::WriteResult moveToAthlete(const Id &id, const pinpoint::SessionOwner &to, QString *f = nullptr) override { return m.moveToAthlete(id, to, f); }
    pinpoint::WriteResult trashShot(const pinpoint::ShotRef &r, QString *w = nullptr) override { return m.trashShot(r, w); }
    pinpoint::DiagnosticsRead readDiagnostics(const Id &id) override { return m.readDiagnostics(id); }
    pinpoint::WriteResult writeDiagnostics(const Id &id, const QJsonObject &s, const QJsonObject &l, const QJsonObject &f,
                                           pinpoint::DiagnosticsRead::State *st = nullptr) override { return m.writeDiagnostics(id, s, l, f, st); }
    pinpoint::WriteResult backupDiagnostics(const Id &id, const QString &t, QString *b = nullptr) override { return m.backupDiagnostics(id, t, b); }
    pinpoint::WriteResult setAsideDiagnostics(const Id &id, QString *a = nullptr) override { return m.setAsideDiagnostics(id, a); }
    pinpoint::SessionInputRead readSessionInput(const Id &id) override { return m.readSessionInput(id); }
    pinpoint::WriteResult writeSessionInput(const Id &id, const pinpoint::SessionInput &i) override { return m.writeSessionInput(id, i); }
    pinpoint::JsonFileRead readClubBaseline(const Id &id) override
    {
        pinpoint::JsonFileRead r = m.readClubBaseline(id);
        if (!r.exists) {   // none frozen: read as there but not usable, so nothing is computed or frozen in its place
            r.exists = true;
            r.ok     = false;
            r.error  = QStringLiteral("club baseline not frozen: corpus run");
        }
        return r;
    }
    pinpoint::WriteResult writeClubBaseline(const Id &, const QJsonObject &) override
    {
        return { pinpoint::WriteResult::Status::Failed, QStringLiteral("club baseline not frozen: corpus run") };
    }
    pinpoint::JsonFileRead readWorkOns(const Id &id) override { return m.readWorkOns(id); }
    pinpoint::WriteResult writeWorkOns(const Id &id, const QJsonObject &r) override { return m.writeWorkOns(id, r); }
    std::function<pinpoint::JsonFileRead()> deferredReadWorkOns(const Id &id) override { return m.deferredReadWorkOns(id); }

private:
    pinpoint::SessionStore &m;
};

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
            for (auto &it : m_libraries)
                if (pinpoint::Session *s = it.second->loadedSession(id)) return pinpoint::diagnosticsWorkTarget(s, a);
            for (auto &it : m_athletes)
                if (pinpoint::Session *s = it.second->loadedSession(id)) return pinpoint::diagnosticsWorkTarget(s, a);
            return pinpoint::diagnosticsWorkTarget(nullptr, a);
        });
    }
    ~NewEngine() { m_athletes.clear(); m_libraries.clear(); }

    // Whose `dir` is: the athlete folder's athlete.json, through the athlete repository over the folder that holds it
    // (refactor 4-c, J-61); else athletes.json beside the session (refactor 5.3); else the ini's record (the fallback for
    // an unmigrated library). Null — and `why` says so — when none names one. Reads only: list() and load() write
    // nothing (AthleteRepository's rule).
    pinpoint::Athlete *libraryAthlete(const QString &dir)
    {
        using namespace pinpoint;
        const QString athleteDir = QDir::cleanPath(QFileInfo(dir).absolutePath());
        const QString root       = QDir::cleanPath(QFileInfo(athleteDir).absolutePath());
        auto it = m_libraries.find(root);
        if (it == m_libraries.end()) {
            auto store = std::make_unique<AthleteStore>([root] { return root; }, &m_store);
            store->setOsTrashForTest(false);
            store->setShotDoors({ m_recordings.get(), m_reader });
            store->setDiagnosticsDoors(doors());
            it = m_libraries.emplace(root, std::move(store)).first;
        }
        for (const AthleteId &id : it->second->list()) {
            Athlete *a = it->second->load(id);
            // athlete.json only: a settings record (an unmigrated athlete) is this machine's, never the tool's to read.
            if (a && a->source() == Athlete::Source::Library && QDir::cleanPath(a->folder()) == athleteDir)
                return a;
        }
        return nullptr;
    }
    bool resolve(const QString &dir, AthleteSeen *who, QString *why)
    {
        if (pinpoint::Athlete *a = libraryAthlete(dir)) {
            if (who) *who = AthleteSeen{ a->id(), a->record(), QStringLiteral("athlete.json") };
            return true;
        }
        QString fileProblem;
        if (const AthleteSeen file = athleteFromGolferFile(dir, &fileProblem); !file.id.isNull()) {
            if (who) *who = file;
            return true;
        }
        const AthleteSeen seen = athleteFromIni(dir, m_ini);
        if (!seen.id.isNull()) {
            if (who) { *who = seen; who->from = QStringLiteral("ini"); }
            return true;
        }
        if (why) {
            const QString folder = QFileInfo(QFileInfo(dir).absolutePath()).fileName();
            *why = m_ini.isEmpty()
                ? QStringLiteral("no athlete.json in %1, no athletes.json beside it and no --athletes-ini names its athlete: "
                                 "refused, so no golfer is pooled").arg(folder)
                : QStringLiteral("neither %1/athlete.json, athletes.json beside it nor --athletes-ini names its athlete: "
                                 "refused, so no golfer is pooled").arg(folder);
            if (!fileProblem.isEmpty()) *why += QStringLiteral(" (%1)").arg(fileProblem);
        }
        return false;
    }

    pinpoint::Session *open(const QString &dir, AthleteSeen *who)
    {
        using namespace pinpoint;
        AthleteSeen seen;
        if (!resolve(dir, &seen, nullptr)) return nullptr;
        if (who) *who = seen;
        if (Athlete *a = libraryAthlete(dir)) return a->sessionAt(dir);   // the library's own Athlete (FILE-ATH-01)
        // The fallbacks: an Athlete made here from athletes.json's record (5.3) or the ini's (as before 4-c). Its index is
        // its own token's folder, which a flat folder of several golfers is not: so no earlier session is pooled into its
        // club baseline (3.10 review B1). An athletes.json golfer's repository freezes none (NoFreezeSessions): graded
        // with no baseline, as under the old engine, and no empty one frozen for good.
        const QString folder = QFileInfo(dir).absolutePath();
        const QString key = seen.id.toString() + QLatin1Char('|') + folder;
        auto it = m_athletes.find(key);
        if (it == m_athletes.end()) {
            // A corpus golfer (athletes.json): the store with its club baseline closed (NoFreezeSessions, above).
            SessionRepository *repo = seen.from == QLatin1String("athletes.json")
                                          ? static_cast<SessionRepository *>(&m_noFreeze) : &m_store;
            auto a = std::make_unique<Athlete>(nullptr, repo, seen.id);
            a->setStored(seen.record, folder, SessionOwner{ seen.id, seen.record.name, QFileInfo(folder).absolutePath() },
                         Athlete::Source::Library, false);
            a->setShotDoors({ m_recordings.get(), m_reader });
            a->setDiagnosticsDoors(doors());
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
    pinpoint::DiagnosticsDoors doors()
    {
        using namespace pinpoint;
        DiagnosticsDoors d;
        d.receiver     = m_receiver.get();
        d.ruleData     = RuleData::installed();
        d.reader       = m_firing;
        d.synchronous  = true;    // §4.4 step 11 (R-HL-2)
        d.persist      = true;    // into the session NAMED: a scratch copy (the tool's own warning)
        // with persist, work_ons.json (the typed summary, DEC-192) is written too: its one writer (step 3.6)
        d.rebuild      = true;    // every shot fired again, the ratchet replayed from Cold
        d.attach       = &SessionDiagnostics::attach;
        return d;
    }

    QString                                                     m_ini;
    pinpoint::SessionStore                                      m_store;
    NoFreezeSessions                                            m_noFreeze{ m_store };   // athletes.json's sessions
    std::unique_ptr<pinpoint::SwingStoreRecordingRepository>    m_recordings;
    std::shared_ptr<pinpoint::SwingStoreRecordingReader>        m_reader;
    std::shared_ptr<const pinpoint::FiringReader>               m_firing;
    std::unique_ptr<pinpoint::WorkReceiver>                     m_receiver;
    std::map<QString, std::unique_ptr<pinpoint::Athlete>>       m_athletes;    // athletes.json's and the ini's; destroyed before the receiver
    std::map<QString, std::unique_ptr<pinpoint::AthleteStore>>  m_libraries;   // by library root; likewise
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
        "usage: regrade_ledger [--engine new|old] [--athletes-ini <f>] [--tag <backup-suffix>] [--dump <out.json>]\n"
        "                     [--allow-in-progress] [--parallel-reader] <session-dir>...\n"
        "\n"
        "Re-reduces each session's diagnostics.json under the CURRENT pack (PINPOINT_CORE_* at the checkout's\n"
        "src/Resources/diagnostics/*.json). The old file is kept as diagnostics.json.<tag> (default pre-regrade;\n"
        "refuses if that exists). WRITES INTO THE SESSION: run it on scratch copies unless a library regrade\n"
        "is what you mean.\n"
        "\n"
        "  --tag <suffix>     the backup's suffix\n"
        "  --dump <out.json>  after the regrade, write the PARITY DUMP in the parallel run's form (P, dumpVersion\n"
        "                     2): per session the stage, patterns and ledger figures, per shot its rows, outcome\n"
        "                     facts and three factors, read off the engine that ran. Canonical JSON (keys\n"
        "                     sorted, %%.17g numbers, no timestamps, folders as <session>/<athlete>), so two\n"
        "                     runs diff byte for byte. Needs every PINPOINT_CORE_*\n"
        "                     (PACK NORMS FLIGHT_NORMS CONTEXTS SCREENS DRILLS GATE_WORDS REFERENCES).\n"
        "  --allow-in-progress  also regrade a session whose session.json says it is in progress (one a crash\n"
        "                     left open); without it such a session is skipped untouched (D11)\n"
        "  --engine new|old   new (the default): each session's own SessionDiagnostics, headless, writing\n"
        "                     diagnostics.json, work_ons.json and a missing club_baseline.json; old (until\n"
        "                     step 5.4b): the old engine, run P's reference, writing diagnostics.json only\n"
        "                     The new engine reads each session's athlete (who it is, sex and date of birth\n"
        "                     for the norm cohort, D6) from its athlete folder's athlete.json, else from\n"
        "                     athletes.json beside the session (pinpoint.athletes/1, the corpus: the entry whose\n"
        "                     name is in the session folder's name; swings recording one of its `aliases` are\n"
        "                     said to be the same golfer); a session whose athlete no source names\n"
        "                     is SKIPPED untouched, so no golfer is pooled into another's club baseline\n"
        "  --athletes-ini <f> (new engine; the last FALLBACK, for a library not yet migrated) a COPY of the\n"
        "                     app's settings: its [athletes] record matched by the athlete folder's or the\n"
        "                     session folder's name\n"
        "  --parallel-reader  (new engine; refactor 3.7-0) read the shots with the app's parallel-run reader\n"
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
    QString     engine = QStringLiteral("new");   // the default (refactor 5.3): each session's own SessionDiagnostics; "old" until 5.4b
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
    if (dirs.isEmpty() || (engine != QLatin1String("old") && engine != QLatin1String("new"))) {
        printUsage(stderr);
        return 2;
    }
    const bool newEngine = engine == QLatin1String("new");
    // THE NEW ENGINE NEEDS THE ATHLETES (3.10 review B1): it freezes each session's club baseline from its athlete's
    // earlier sessions, and without a record every session of one folder is one athlete — one golfer's usuals frozen
    // into another's sessions, for good. Since refactor 4-c (J-61) they come from the library's athlete.json files, then
    // (5.3) athletes.json beside the session, the ini only as the last fallback; a session none names is refused below,
    // before its folder is written. An ini named but missing is refused here, before any folder is read or written.
    if (newEngine && !athletesIni.isEmpty() && !QFileInfo(athletesIni).isFile()) {
        std::fprintf(stderr, "regrade_ledger: --athletes-ini %s is not a file. Nothing was read or written.\n",
                     qPrintable(athletesIni));
        return 2;
    }
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
        // THE GOLFER MUST BE KNOWN (3.10 review B1; refactor 4-c, 5.3): a session whose athlete neither the library's
        // athlete.json, athletes.json beside it nor --athletes-ini names is refused here, before its ledger is backed up
        // or anything is written.
        if (newEngine) {
            QString why;
            if (!ng->resolve(dir, nullptr, &why)) {
                std::printf("%-48s SKIP  %s\n", qPrintable(name), qPrintable(why));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("its athlete is not known")));
                ++failures;
                continue;
            }
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
                        "cohort %s %s, athlete from %s)\n",
                        qPrintable(name), hadLedger ? "REGRADED" : "GRADED  ", int(d->keptShots().size()),
                        dg ? qPrintable(pinpoint::parallel::formOf(*d).value(QStringLiteral("stage")).toString()) : "-",
                        dg ? int(dg->patterns.size()) : 0, (unsigned long long)d->asks().size(),
                        (unsigned long long)d->counts().reductions, (long long)d->lastReductionMs(),
                        who.record.sex.isEmpty() ? "-" : qPrintable(who.record.sex),
                        who.record.dob.isEmpty() ? "-" : qPrintable(who.record.dob), qPrintable(who.from));
            if (dg && !dg->patterns.isEmpty())
                std::printf("%-48s   patterns: %s\n", "", qPrintable(dg->patterns.join(QLatin1Char(' '))));
            if (!who.recorded.isEmpty())
                std::printf("%-48s   %s\n", "", qPrintable(who.recorded));
            if (who.from == QLatin1String("athletes.json")
                && !QFileInfo::exists(QDir(dir).filePath(QStringLiteral("club_baseline.json"))))
                std::printf("%-48s   club baseline not frozen: corpus run (graded with no baseline)\n", "");
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
            QVariantMap body = pinpoint::parallel::formOf(m).toVariantMap();
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
            { QStringLiteral("dumpVersion"), 2 },
            { QStringLiteral("form"),        QStringLiteral("P") },
            { QStringLiteral("engine"),      engine },
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
