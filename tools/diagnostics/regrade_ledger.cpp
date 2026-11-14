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
//   1. diagnostics.json is read and parsed. One that cannot be is left exactly as it is and the
//      session is SKIPPED: it may hold the golfer's only copy of what they said.
//   2. diagnostics.json  ->  diagnostics.json.<tag>   (default tag pre-regrade; never overwritten:
//      refuses if present)
//   3. THE GOLFER'S HALF IS CARRIED ACROSS. The file is half cache (the rows, the session meta) and
//      half the only copy of what the golfer and coach said: SessionDiagnosticsModel::golferInputKeys()
//      — the focus and declared miss (`intent`), screen answers, shot intent, a run of intent and
//      outcome corrections. When the golfer said anything at all — a value, not a key: the app
//      always writes `intent` and `screens`, empty or not — those keys, as they stood, are written
//      as a seed diagnostics.json (golfer keys, no rows) before the model reads it, so the rebuilt
//      file keeps them (audit H-04: a regrade used to drop them all, and one corpus session lost
//      its declared focus that way). When the golfer said nothing no seed is written.
//      If the model then writes no ledger (no swing_* folders, or every write failed), the
//      original is PUT BACK from the backup and the seed removed: a ledger-less file is not left
//      behind, because prepareClubBaseline reads it as an out-of-date predecessor and the
//      golfer's later sessions would then never freeze their club baseline.
//   4. SessionDiagnosticsModel::activateSession(dir), synchronously, which re-ingests every shot
//      and writes a fresh diagnostics.json after each one
//   5. one line per session on stdout: shots, stage, patterns, coverage, and what was carried —
//      with a WARN, and exit 1, if the rebuilt file does not say the same as the old one did
//
// It does NOT close the session — closeSession() writes the athlete's fault_profile.json and
// freezes the stage, and a sweep is a re-reading of evidence, not a session ending — and it does
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
// Dump mode needs every PINPOINT_CORE_* the diagnostics read (parity_json.h contentVariables(): the flight
// table and the gate words too, which a plain regrade can do without), because a dump graded without the
// flight table would read no flight on any shot and say so silently.

#include "Gui/diagnostics/session_diagnostics_model.h"
#include "Coaching/shot_factors.h"
#include "Core/PpMessageLog.h"
#include "parity_json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <cstdio>

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

// Read and parse a ledger. False, with the reason, when it is there and cannot be.
bool readLedger(const QString &path, QJsonObject *root, QString *why)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *why = f.errorString(); return false; }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        *why = QStringLiteral("%1 at byte %2").arg(err.errorString()).arg(err.offset);
        return false;
    }
    if (!doc.isObject()) { *why = QStringLiteral("not a JSON object"); return false; }
    *root = doc.object();
    return true;
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

void printUsage(FILE *to)
{
    std::fprintf(to,
        "usage: regrade_ledger [--tag <backup-suffix>] [--dump <out.json>] <session-dir>...\n"
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
        "  --help             this text\n");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // A headless tool echoes the app log (PpMessageLog.h): the model reports through ppWarn — an
    // unreadable club baseline, a ledger set aside — and nothing else in this process reads it.
    PpMessageLog::instance()->setEchoToStderr(true);

    QString     tag = QStringLiteral("pre-regrade");
    QString     dumpPath;
    QStringList dirs;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) { printUsage(stdout); return 0; }
        if (a == QLatin1String("--tag") && i + 1 < argc) { tag = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a == QLatin1String("--dump") && i + 1 < argc) { dumpPath = QString::fromLocal8Bit(argv[++i]); continue; }
        if (a.startsWith(QLatin1String("--"))) {
            std::fprintf(stderr, "regrade_ledger: unknown option %s\n", argv[i]);
            printUsage(stderr);
            return 2;
        }
        dirs << a;
    }
    if (dirs.isEmpty()) {
        printUsage(stderr);
        return 2;
    }
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
        const QString ledger = dir + QStringLiteral("/diagnostics.json");
        const QString backup = ledger + QLatin1Char('.') + tag;
        const QString name   = QFileInfo(dir).fileName();
        bool hadLedger = false;
        QJsonObject carried;          // the golfer's half of the old file, as it stood
        bool seeded = false;          // diagnostics.json is, for now, the carried golfer input alone
        if (QFile::exists(ledger)) {
            if (QFile::exists(backup)) {
                std::printf("%-48s SKIP  %s already exists — pick another --tag\n",
                            qPrintable(name), qPrintable(QFileInfo(backup).fileName()));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("backup already exists")));
                ++failures;
                continue;
            }
            QJsonObject old;
            QString why;
            if (!readLedger(ledger, &old, &why)) {
                std::printf("%-48s SKIP  diagnostics.json cannot be read (%s) — left as it is\n",
                            qPrintable(name), qPrintable(why));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("diagnostics.json cannot be read")));
                ++failures;
                continue;
            }
            carried = SessionDiagnosticsModel::golferInput(old);
            if (!QFile::rename(ledger, backup)) {
                std::printf("%-48s SKIP  could not set the ledger aside\n", qPrintable(name));
                dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("could not set the ledger aside")));
                ++failures;
                continue;
            }
            hadLedger = true;
            if (!golferSummary(carried).isEmpty()) {          // the golfer said something: carry it
                // The seed the model reads: the golfer's keys and no rows, so every shot back-fills and
                // each write carries them. Put back as it was if the seed cannot be written.
                QJsonObject seed = carried;
                if (old.contains(QStringLiteral("schemaVersion")))
                    seed.insert(QStringLiteral("schemaVersion"), old.value(QStringLiteral("schemaVersion")));
                QSaveFile out(ledger);
                const bool written = out.open(QIODevice::WriteOnly)
                                  && out.write(QJsonDocument(seed).toJson(QJsonDocument::Compact)) >= 0
                                  && out.commit();
                if (!written) {
                    const bool restored = QFile::rename(backup, ledger);
                    std::printf("%-48s SKIP  could not carry the golfer's input across (%s)%s\n",
                                qPrintable(name), qPrintable(out.errorString()),
                                restored ? " — ledger put back" : " — the ledger is in the backup");
                    dump.push_back(dumpEntry(dir, QStringLiteral("skipped"), {}, QStringLiteral("could not carry the golfer's input across")));
                    ++failures;
                    continue;
                }
                seeded = true;
            }
        }

        SessionDiagnosticsModel m;
        m.setSynchronous(true);
        m.setCadence(QStringLiteral("everyShot"));
        m.setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);   // a tool: no club_baseline.json
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
        // itself is asked: a seed still standing has no `ledger`.
        QJsonObject now;
        QString why;
        const bool readable = readLedger(ledger, &now, &why);
        const bool modelWrote = m.persistCount() > 0 && readable && now.contains(QStringLiteral("ledger"));
        if (!modelWrote) {
            QString what = QStringLiteral("no diagnostics.json was written (no swing_* directories?)");
            if (hadLedger) {
                // Nothing was regraded: the session keeps the file it had, not a seed and not nothing.
                const bool seedGone = !QFile::exists(ledger)
                                   || (seeded && readable && !now.contains(QStringLiteral("ledger")) && QFile::remove(ledger));
                const bool restored = seedGone && QFile::rename(backup, ledger);
                what += restored ? QStringLiteral(" — the original is put back")
                                 : QStringLiteral(" — could not put the original back; it is %1").arg(QFileInfo(backup).fileName());
                if (!restored) ++failures;
            }
            std::printf("%-48s WARN  %s\n", qPrintable(name), qPrintable(what));
        } else if (seeded) {
            // What the rebuilt file says the golfer said, against what the old one did.
            const QString before = golferSummary(carried);
            const QString after  = golferSummary(SessionDiagnosticsModel::golferInput(now));
            std::printf("%-48s   carried: %s\n", "", qPrintable(before));
            if (after != before) {
                std::printf("%-48s WARN  the golfer's input did not survive: now %s — the old file is %s\n",
                            qPrintable(name), after.isEmpty() ? "nothing" : qPrintable(after),
                            qPrintable(QFileInfo(backup).fileName()));
                ++failures;
            }
        }
        if (!dumpPath.isEmpty()) {
            QVariantMap body = sessionDump(m);
            body.insert(QStringLiteral("ledgerWritten"), modelWrote);
            dump.push_back(dumpEntry(dir, hadLedger ? QStringLiteral("regraded") : QStringLiteral("graded"), body));
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
            { QStringLiteral("dumpVersion"), 1 },
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
